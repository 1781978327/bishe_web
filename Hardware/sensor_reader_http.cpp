/**
 * 传感器读取程序 - HTTP 服务器版本
 * 读取: DHT11(温度/湿度), MQ-2(烟雾), 光照传感器
 * 提供 HTTP API 供后端调用
 *
 * 依赖:
 *   sudo apt install wiringpi libmicrohttpd-dev
 *
 * 编译:
 *   g++ -std=c++17 -O2 -Wall sensor_reader_http.cpp -o sensor_reader_http -lwiringPi -lmicrohttpd
 *
 * 运行:
 *   sudo ./sensor_reader_http
 *
 * HTTP API:
 *   GET /health  -> {"status": "ok"}
 *   GET /sensor -> {"temperature": XX.X, "humidity": XX.X, "smoke": XX, "light": XX}
 */

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <chrono>
#include <pthread.h>
#include <microhttpd.h>

#define PORT 8088
#define DAEMON_MODE 1
#define SENSOR_UPDATE_INTERVAL_SEC 1

// ============== DHT11 部分 =================
#include <wiringPi.h>

typedef unsigned char uint8;
typedef unsigned int  uint16;
typedef unsigned long uint32;

#define DHT_PIN 2  // 物理7脚 → wPi编号 = 2
#define HIGH_TIME 32
#define TIMEOUT_US 100000  // 100ms超时
uint32 dht_databuf;

// 全局传感器数据
static float g_temperature = 0;
static float g_humidity = 0;
static float g_smoke = 0;
static float g_light = 0;
static time_t g_last_read_time = 0;

// 带超时的等待函数
bool waitForPinState(int pin, int state, int timeout_us) {
    auto start = std::chrono::steady_clock::now();
    while (digitalRead(pin) != state) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        if (elapsed > timeout_us) {
            return false;  // 超时
        }
    }
    return true;
}

// DHT11 读取函数（带超时保护）
uint8 readDHT11(float& temperature, float& humidity) {
    dht_databuf = 0;
    uint8_t crc = 0;
    uint8_t i;
    
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, 1);
    delayMicroseconds(4);
    digitalWrite(DHT_PIN, 0);
    delay(20);
    digitalWrite(DHT_PIN, 1);
    delayMicroseconds(40);
    pinMode(DHT_PIN, INPUT);
    pullUpDnControl(DHT_PIN, PUD_UP);
    
    // 等待DHT11响应，带超时
    if (!waitForPinState(DHT_PIN, 0, TIMEOUT_US)) {
        return 0;  // 等待响应超时
    }
    
    if (!waitForPinState(DHT_PIN, 1, TIMEOUT_US)) {
        return 0;  // 等待高电平超时
    }
    
    delayMicroseconds(80);
    
    // 读取32位数据
    for (i = 0; i < 32; i++) {
        if (!waitForPinState(DHT_PIN, 0, TIMEOUT_US)) return 0;
        if (!waitForPinState(DHT_PIN, 1, TIMEOUT_US)) return 0;
        delayMicroseconds(HIGH_TIME);
        dht_databuf *= 2;
        if (digitalRead(DHT_PIN) == 1)
            dht_databuf++;
    }
    
    // 读取8位校验码
    for (i = 0; i < 8; i++) {
        if (!waitForPinState(DHT_PIN, 0, TIMEOUT_US)) return 0;
        if (!waitForPinState(DHT_PIN, 1, TIMEOUT_US)) return 0;
        delayMicroseconds(HIGH_TIME);
        crc *= 2;
        if (digitalRead(DHT_PIN) == 1)
            crc++;
    }
    
    uint8_t check = ((dht_databuf >> 24) & 0xFF) + ((dht_databuf >> 16) & 0xFF) 
                 + ((dht_databuf >> 8) & 0xFF) + (dht_databuf & 0xFF);
    if (check == crc) {
        humidity = ((dht_databuf >> 24) & 0xFF) + ((dht_databuf >> 16) & 0xFF) * 0.1f;
        temperature = ((dht_databuf >> 8) & 0xFF) + (dht_databuf & 0xFF) * 0.1f;
        return 1;
    }
    return 0;
}

// 带重试的DHT11读取
int readDHT11WithRetry(float& temperature, float& humidity) {
    for (int retry = 0; retry < 3; retry++) {
        if (retry > 0) delayMicroseconds(200);
        if (readDHT11(temperature, humidity)) return 1;
    }
    return 0;
}

// ============== ADS1115 / MQ-2 部分 =================
static constexpr uint8_t REG_POINTER_CONVERT = 0x00;
static constexpr uint8_t REG_POINTER_CONFIG  = 0x01;
static constexpr uint16_t OS_SINGLE    = 0x8000;
static constexpr uint16_t MUX_SINGLE_0 = 0x4000;  // AIN0 - MQ-2
static constexpr uint16_t MUX_SINGLE_1 = 0x5000;  // AIN1 - 光照
static constexpr uint16_t PGA_4_096V   = 0x0200;
static constexpr uint16_t MODE_SINGLE = 0x0100;
static constexpr uint16_t DR_128SPS    = 0x0080;
static constexpr uint16_t COMP_QUE_NONE = 0x0003;

static constexpr float VCC = 5.0f;
static constexpr float RL = 10000.0f;  // MQ-2负载电阻
static constexpr float FS_VOLTS = 4.096f;
static constexpr float ADC_SCALE = FS_VOLTS / 32768.0f;

bool i2cWriteReg16(int fd, uint8_t reg, uint16_t value) {
    uint8_t buf[3] = {reg, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
    return write(fd, buf, 3) == 3;
}

bool i2cReadConversion(int fd, int16_t& out) {
    uint8_t ptr = REG_POINTER_CONVERT;
    if (write(fd, &ptr, 1) != 1) return false;
    uint8_t data[2];
    if (read(fd, data, 2) != 2) return false;
    out = static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
    return true;
}

bool readAds1115Channel(int fd, uint8_t channel, int16_t& raw) {
    uint16_t mux;
    switch (channel) {
        case 0: mux = MUX_SINGLE_0; break;
        case 1: mux = MUX_SINGLE_1; break;
        default: return false;
    }
    uint16_t config = OS_SINGLE | mux | PGA_4_096V | MODE_SINGLE | DR_128SPS | COMP_QUE_NONE;
    if (!i2cWriteReg16(fd, REG_POINTER_CONFIG, config))
        return false;
    usleep(9000);
    return i2cReadConversion(fd, raw);
}

// MQ-2 烟雾传感器计算
float calculateSmokePPM(float voltage) {
    if (voltage <= 0.001f) return 0.0f;
    float rs = RL * (VCC - voltage) / voltage;
    float ro = 10000.0f;  // 清洁空气参考电阻
    float ratio = rs / ro;
    if (ratio <= 0) return 0;
    if (ratio > 20) ratio = 20;
    return 1000.0f * pow(ratio, -1.55f);
}

// 光照传感器计算 (GL5528)
float calculateLightLux(float voltage) {
    if (voltage <= 0) return 0;
    return voltage * 200.0f;
}

// ============== 读取所有传感器数据 ==============
void readAllSensors() {
    if (wiringPiSetup() == -1) {
        fprintf(stderr, "WiringPi 初始化失败\n");
        return;
    }
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, 1);
    
    constexpr const char* I2C_DEV = "/dev/i2c-1";
    constexpr int ADC_ADDR = 0x48;
    
    int fd = open(I2C_DEV, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "I2C 设备打开失败\n");
        g_temperature = -1;
        g_humidity = -1;
        g_smoke = -1;
        g_light = -1;
        return;
    }
    if (ioctl(fd, I2C_SLAVE, ADC_ADDR) < 0) {
        fprintf(stderr, "I2C 地址设置失败\n");
        close(fd);
        g_temperature = -1;
        g_humidity = -1;
        g_smoke = -1;
        g_light = -1;
        return;
    }
    
    // 读取DHT11 (带重试)
    if (!readDHT11WithRetry(g_temperature, g_humidity)) {
        g_temperature = -1;
        g_humidity = -1;
    }
    
    // 读取MQ-2 (烟雾 - AIN0)
    int16_t raw_mq2;
    if (readAds1115Channel(fd, 0, raw_mq2)) {
        float voltage_mq2 = raw_mq2 * ADC_SCALE;
        g_smoke = calculateSmokePPM(voltage_mq2);
    } else {
        g_smoke = -1;
    }
    
    // 读取光照传感器 (AIN1)
    int16_t raw_light;
    if (readAds1115Channel(fd, 1, raw_light)) {
        float voltage_light = raw_light * ADC_SCALE;
        g_light = calculateLightLux(voltage_light);
    } else {
        g_light = -1;
    }
    
    close(fd);
    g_last_read_time = time(NULL);
    
    printf("[%s] 传感器数据: 温度=%.1f, 湿度=%.1f, 烟雾=%.0f, 光照=%.0f\n",
           ctime(&g_last_read_time), g_temperature, g_humidity, g_smoke, g_light);
}

// ============== HTTP 服务器部分 ==============
static pthread_mutex_t sensor_mutex = PTHREAD_MUTEX_INITIALIZER;

static int handleRequest(void* cls, struct MHD_Connection* connection,
                         const char* url, const char* method, const char* version,
                         const char* upload_data, size_t* upload_data_size, void** ptr) {
    
    struct MHD_Response* response;
    int ret;
    char response_data[512];
    
    // 只处理 GET 请求
    if (strcmp(method, "GET") != 0) {
        response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_MUST_FREE);
        ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    pthread_mutex_lock(&sensor_mutex);
    
    if (strcmp(url, "/health") == 0) {
        // 健康检查接口
        snprintf(response_data, sizeof(response_data),
                 "{\"status\": \"ok\", \"timestamp\": %ld}", (long)g_last_read_time);
        
        response = MHD_create_response_from_buffer(strlen(response_data), response_data, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    }
    else if (strcmp(url, "/sensor") == 0) {
        // 获取传感器数据
        // 先读取最新数据
        readAllSensors();
        
        snprintf(response_data, sizeof(response_data),
                 "{\"temperature\": %.1f, \"humidity\": %.1f, \"smoke\": %.0f, \"light\": %.0f}",
                 g_temperature, g_humidity, g_smoke, g_light);
        
        response = MHD_create_response_from_buffer(strlen(response_data), response_data, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    }
    else if (strcmp(url, "/") == 0) {
        // 根路径
        const char* welcome = "{\"service\": \"sensor-reader\", \"version\": \"1.0\", \"endpoints\": [\"/health\", \"/sensor\"]}";
        response = MHD_create_response_from_buffer(strlen(welcome), (void*)welcome, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    }
    else {
        // 404
        const char* not_found = "{\"error\": \"Not Found\"}";
        response = MHD_create_response_from_buffer(strlen(not_found), (void*)not_found, MHD_RESPMEM_MUST_FREE);
        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
    }
    
    pthread_mutex_unlock(&sensor_mutex);
    
    return ret;
}

// 守护进程模式
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(1);
    }
    if (pid > 0) {
        printf("传感器 HTTP 服务器已启动 (PID: %d), 端口: %d\n", pid, PORT);
        exit(0);
    }
    
    // 子进程成为新会话组长
    setsid();
    
    // 关闭标准输入输出错误
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // 重定向到 /dev/null
    open("/dev/null", O_RDWR);
    (void)dup(0);
    (void)dup(0);
}

int main(int argc, char* argv[]) {
    printf("===== 传感器 HTTP 服务器启动中 =====\n");
    printf("端口: %d\n", PORT);
    printf("接口:\n");
    printf("  GET /health  -> 健康检查\n");
    printf("  GET /sensor -> 获取传感器数据\n");
    
    // 检查参数
    int daemon = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0) {
            daemon = 1;
        }
    }
    
    if (daemon && DAEMON_MODE) {
        daemonize();
    }
    
    // 初始化 wiringPi 并预热传感器
    printf("初始化传感器...\n");
    readAllSensors();
    
    // 启动 HTTP 服务器
    struct MHD_Daemon* daemon_http = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG,
        PORT,
        NULL, NULL,
        handleRequest, NULL,
        MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)10,
        MHD_OPTION_END
    );
    
    if (daemon_http == NULL) {
        fprintf(stderr, "HTTP 服务器启动失败!\n");
        return 1;
    }
    
    printf("HTTP 服务器已启动，监听端口 %d\n", PORT);
    printf("按 Ctrl+C 停止服务器\n");
    
    // 保持运行
    while (1) {
        sleep(SENSOR_UPDATE_INTERVAL_SEC);
        // 定时更新一次传感器数据到全局变量。
        // DHT11 不建议高于约 1Hz，因此默认保持 1 秒。
        pthread_mutex_lock(&sensor_mutex);
        readAllSensors();
        pthread_mutex_unlock(&sensor_mutex);
    }
    
    MHD_stop_daemon(daemon_http);
    return 0;
}
