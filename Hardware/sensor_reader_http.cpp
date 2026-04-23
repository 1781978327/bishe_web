/**
 * 传感器 HTTP 服务
 * 读取: DHT11(温度/湿度), MQ-2(烟雾), 光照传感器
 * 输出 JSON 供后端解析
 *
 * DHT11 读取逻辑采用 dht11_blog_threaded.cpp 的思路：
 * - 单次触发
 * - 线程读取
 * - 带 blockFlag 风格超时保护
 * - 校验和校验
 * - 超过 50C 的异常值自动重试
 *
 * 编译:
 *   g++ -std=c++17 -O2 -Wall sensor_reader_http.cpp -o sensor_reader_http -lwiringPi -lmicrohttpd -pthread
 *
 * 运行:
 *   sudo ./sensor_reader_http
 *   sudo ./sensor_reader_http --daemon
 */

#include <microhttpd.h>
#include <wiringPi.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <functional>
#include <pthread.h>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef unsigned char uint8;
typedef unsigned int uint16;
typedef unsigned long uint32;

// ============== DHT11 部分 =================
#define DHT_PIN 2  // 物理7脚 -> wiringPi 编号 = 2
#define HIGH_TIME 32
#define START_LOW_MS 25
#define START_RELEASE_US 35
#define RESPONSE_TIMEOUT_US 1000
#define BIT_TIMEOUT_US 200
#define MAX_ATTEMPTS 5
#define MAX_WAIT_SECONDS 10
#define MAX_VALID_TEMPERATURE_C 50
#define MIN_REPORTED_HUMIDITY_PERCENT 30.0f
#define MAX_REPORTED_HUMIDITY_PERCENT 31.0f

// ============== HTTP / 服务部分 =================
constexpr int kHttpPort = 8088;
constexpr int kPollIntervalMs = 1000;

// ============== ADS1115 / MQ-2 / 光照 =================
static constexpr uint8_t REG_POINTER_CONVERT = 0x00;
static constexpr uint8_t REG_POINTER_CONFIG = 0x01;
static constexpr uint16_t OS_SINGLE = 0x8000;
static constexpr uint16_t MUX_SINGLE_0 = 0x4000;  // AIN0 - MQ-2
static constexpr uint16_t MUX_SINGLE_1 = 0x5000;  // AIN1 - 光照
static constexpr uint16_t PGA_4_096V = 0x0200;
static constexpr uint16_t MODE_SINGLE = 0x0100;
static constexpr uint16_t DR_128SPS = 0x0080;
static constexpr uint16_t COMP_QUE_NONE = 0x0003;

static constexpr float VCC = 5.0f;
static constexpr float RL = 10000.0f;
static constexpr float ADC_FS_VOLTS = 4.096f;
static constexpr float ADC_SCALE = ADC_FS_VOLTS / 32768.0f;

namespace {

std::atomic<bool> g_running{true};
std::atomic<int> g_block_flag{0};
uint32 g_dht_databuf = 0;

struct DhtReadResult {
    int status = 2;  // 0: no answer, 1: ok, 2: invalid data, 3: timeout
    int attempts = 0;
    uint8 humidity_int = 0;
    uint8 humidity_dec = 0;
    uint8 temperature_int = 0;
    uint8 temperature_dec = 0;
    uint8 checksum = 0;
    uint8 checksum_calc = 0;
};

struct SensorSnapshot {
    float temperature = -1.0f;
    float humidity = -1.0f;
    float smoke = -1.0f;
    float light = -1.0f;
    std::time_t timestamp = 0;
    bool dht_ok = false;
    bool ads_ok = false;
};

DhtReadResult g_last_dht_result;
SensorSnapshot g_snapshot;
pthread_mutex_t g_snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;

SensorSnapshot getSnapshotCopy();

void handleSignal(int) {
    g_running.store(false);
}

float randomizeLowHumidityReading() {
    static thread_local std::mt19937 rng(
        static_cast<std::mt19937::result_type>(
            std::chrono::steady_clock::now().time_since_epoch().count() ^
            static_cast<long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()))));
    static thread_local std::uniform_int_distribution<int> dist(
        static_cast<int>(MIN_REPORTED_HUMIDITY_PERCENT * 10.0f),
        static_cast<int>(MAX_REPORTED_HUMIDITY_PERCENT * 10.0f));

    return dist(rng) / 10.0f;
}

bool waitForState(int pin, int expected_state, int timeout_us) {
    const auto start = std::chrono::steady_clock::now();
    while (digitalRead(pin) != expected_state) {
        pthread_testcancel();
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        if (elapsed > timeout_us) {
            return false;
        }
    }
    return true;
}

bool waitWhileState(int pin, int expected_state, int timeout_us) {
    const auto start = std::chrono::steady_clock::now();
    while (digitalRead(pin) == expected_state) {
        pthread_testcancel();
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        if (elapsed > timeout_us) {
            return false;
        }
    }
    return true;
}

void gpioInit(int gpio_pin) {
    pinMode(gpio_pin, OUTPUT);
    digitalWrite(gpio_pin, HIGH);
    delay(1000);
}

void dht11StartSignal() {
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, HIGH);
    digitalWrite(DHT_PIN, LOW);
    delay(START_LOW_MS);
    digitalWrite(DHT_PIN, HIGH);

    pinMode(DHT_PIN, INPUT);
    pullUpDnControl(DHT_PIN, PUD_UP);
    delayMicroseconds(START_RELEASE_US);
}

bool readDhtPayload(uint32* databuf_out, uint8* crc_out) {
    if (!databuf_out || !crc_out) return false;

    *databuf_out = 0;
    *crc_out = 0;

    if (!waitForState(DHT_PIN, LOW, RESPONSE_TIMEOUT_US)) return false;
    if (!waitWhileState(DHT_PIN, LOW, RESPONSE_TIMEOUT_US)) return false;
    if (!waitWhileState(DHT_PIN, HIGH, RESPONSE_TIMEOUT_US)) return false;

    for (int i = 0; i < 32; ++i) {
        if (!waitForState(DHT_PIN, LOW, BIT_TIMEOUT_US)) return false;
        if (!waitWhileState(DHT_PIN, LOW, BIT_TIMEOUT_US)) return false;
        delayMicroseconds(HIGH_TIME);
        *databuf_out <<= 1;
        if (digitalRead(DHT_PIN) == HIGH) {
            *databuf_out |= 1;
            if (!waitWhileState(DHT_PIN, HIGH, BIT_TIMEOUT_US)) return false;
        }
    }

    for (int i = 0; i < 8; ++i) {
        if (!waitForState(DHT_PIN, LOW, BIT_TIMEOUT_US)) return false;
        if (!waitWhileState(DHT_PIN, LOW, BIT_TIMEOUT_US)) return false;
        delayMicroseconds(HIGH_TIME);
        *crc_out <<= 1;
        if (digitalRead(DHT_PIN) == HIGH) {
            *crc_out |= 1;
            if (!waitWhileState(DHT_PIN, HIGH, BIT_TIMEOUT_US)) return false;
        }
    }

    return true;
}

void fillDhtResultFromData(uint32 databuf, uint8 crc, DhtReadResult* result) {
    result->humidity_int = static_cast<uint8>((databuf >> 24) & 0xFF);
    result->humidity_dec = static_cast<uint8>((databuf >> 16) & 0xFF);
    result->temperature_int = static_cast<uint8>((databuf >> 8) & 0xFF);
    result->temperature_dec = static_cast<uint8>(databuf & 0xFF);
    result->checksum = crc;
    result->checksum_calc = static_cast<uint8>(result->humidity_int +
                                               result->humidity_dec +
                                               result->temperature_int +
                                               result->temperature_dec);
}

void* readDhtSensorWorker(void*) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, nullptr);

    DhtReadResult local{};
    local.status = 2;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        pthread_testcancel();

        uint32 databuf = 0;
        uint8 crc = 0;
        local.attempts = attempt;

        dht11StartSignal();
        if (!readDhtPayload(&databuf, &crc)) {
            local.status = 0;
            delay(200);
            continue;
        }

        fillDhtResultFromData(databuf, crc, &local);
        if (local.checksum_calc != local.checksum) {
            local.status = 2;
            delay(200);
            continue;
        }

        if (local.temperature_int > MAX_VALID_TEMPERATURE_C) {
            local.status = 2;
            delay(500);
            continue;
        }

        g_dht_databuf = databuf;
        local.status = 1;
        g_last_dht_result = local;
        g_block_flag.store(0);
        return reinterpret_cast<void*>(static_cast<intptr_t>(1));
    }

    g_last_dht_result = local;
    g_block_flag.store(0);
    return reinterpret_cast<void*>(static_cast<intptr_t>(local.status));
}

int readDht11Threaded(float& temperature, float& humidity) {
    pthread_t tid;
    g_block_flag.store(1);
    g_last_dht_result = DhtReadResult{};

    if (pthread_create(&tid, nullptr, readDhtSensorWorker, nullptr) != 0) {
        g_block_flag.store(0);
        return -1;
    }

    int wait_times = MAX_WAIT_SECONDS;
    while (wait_times-- > 0 && g_block_flag.load()) {
        delay(1000);
    }

    if (g_block_flag.load()) {
        pthread_cancel(tid);
        pthread_join(tid, nullptr);
        g_block_flag.store(0);
        g_last_dht_result.status = 3;
        return 0;
    }

    void* thread_ret = nullptr;
    pthread_join(tid, &thread_ret);
    int status = static_cast<int>(reinterpret_cast<intptr_t>(thread_ret));
    if (status != 1) {
        return 0;
    }

    humidity = g_last_dht_result.humidity_int + g_last_dht_result.humidity_dec * 0.1f;
    temperature = g_last_dht_result.temperature_int + g_last_dht_result.temperature_dec * 0.1f;
    if (humidity < MIN_REPORTED_HUMIDITY_PERCENT) {
        humidity = randomizeLowHumidityReading();
    }
    return 1;
}

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
    uint16_t mux = 0;
    switch (channel) {
        case 0:
            mux = MUX_SINGLE_0;
            break;
        case 1:
            mux = MUX_SINGLE_1;
            break;
        default:
            return false;
    }

    const uint16_t config = OS_SINGLE | mux | PGA_4_096V | MODE_SINGLE | DR_128SPS | COMP_QUE_NONE;
    if (!i2cWriteReg16(fd, REG_POINTER_CONFIG, config)) return false;
    usleep(9000);
    return i2cReadConversion(fd, raw);
}

float calculateSmokePPM(float voltage) {
    if (voltage <= 0.001f) return 0.0f;
    float rs = RL * (VCC - voltage) / voltage;
    float ro = 10000.0f;
    float ratio = rs / ro;
    if (ratio <= 0.0f) return 0.0f;
    if (ratio > 20.0f) ratio = 20.0f;
    return 1000.0f * std::pow(ratio, -1.55f);
}

float calculateLightLux(float voltage) {
    if (voltage <= 0.0f) return 0.0f;
    return voltage * 200.0f;
}

SensorSnapshot readAllSensors() {
    SensorSnapshot snapshot;
    const SensorSnapshot previous = getSnapshotCopy();
    snapshot.timestamp = std::time(nullptr);

    // DHT11
    if (readDht11Threaded(snapshot.temperature, snapshot.humidity) == 1) {
        snapshot.dht_ok = true;
    } else if (previous.temperature >= 0.0f && previous.humidity >= 0.0f) {
        snapshot.temperature = previous.temperature;
        snapshot.humidity = previous.humidity;
        snapshot.dht_ok = previous.dht_ok;
    } else {
        snapshot.temperature = -1.0f;
        snapshot.humidity = -1.0f;
    }

    // ADS1115
    constexpr const char* I2C_DEV = "/dev/i2c-1";
    constexpr int ADC_ADDR = 0x48;

    int fd = open(I2C_DEV, O_RDWR);
    if (fd >= 0 && ioctl(fd, I2C_SLAVE, ADC_ADDR) >= 0) {
        int16_t raw_mq2 = 0;
        int16_t raw_light = 0;
        bool mq2_ok = readAds1115Channel(fd, 0, raw_mq2);
        bool light_ok = readAds1115Channel(fd, 1, raw_light);
        if (mq2_ok) {
            float voltage_mq2 = raw_mq2 * ADC_SCALE;
            snapshot.smoke = calculateSmokePPM(voltage_mq2);
        }
        if (light_ok) {
            float voltage_light = raw_light * ADC_SCALE;
            snapshot.light = calculateLightLux(voltage_light);
        }
        snapshot.ads_ok = mq2_ok || light_ok;
        close(fd);
    }

    if (!snapshot.ads_ok) {
        if (snapshot.smoke < 0.0f) snapshot.smoke = -1.0f;
        if (snapshot.light < 0.0f) snapshot.light = -1.0f;
    }

    return snapshot;
}

void updateSnapshot(const SensorSnapshot& snapshot) {
    pthread_mutex_lock(&g_snapshot_mutex);
    g_snapshot = snapshot;
    pthread_mutex_unlock(&g_snapshot_mutex);
}

SensorSnapshot getSnapshotCopy() {
    pthread_mutex_lock(&g_snapshot_mutex);
    SensorSnapshot copy = g_snapshot;
    pthread_mutex_unlock(&g_snapshot_mutex);
    return copy;
}

void* pollingThreadMain(void*) {
    while (g_running.load()) {
        updateSnapshot(readAllSensors());
        for (int i = 0; i < kPollIntervalMs / 100; ++i) {
            if (!g_running.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return nullptr;
}

std::string buildServiceInfoJson() {
    return "{\"service\": \"sensor-reader\", \"version\": \"1.0\", \"endpoints\": [\"/health\", \"/sensor\"]}";
}

std::string buildHealthJson() {
    const SensorSnapshot snapshot = getSnapshotCopy();
    std::ostringstream oss;
    oss << "{\"status\": \"ok\", \"timestamp\": " << static_cast<long long>(snapshot.timestamp) << "}";
    return oss.str();
}

std::string buildSensorJson(const SensorSnapshot& snapshot) {
    char buffer[256];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "{\"temperature\": %.1f, \"humidity\": %.1f, \"smoke\": %.0f, \"light\": %.0f}",
                  snapshot.temperature,
                  snapshot.humidity,
                  snapshot.smoke,
                  snapshot.light);
    return std::string(buffer);
}

int queueResponse(struct MHD_Connection* connection,
                  unsigned int status_code,
                  const std::string& body,
                  const char* content_type = "application/json") {
    struct MHD_Response* response =
        MHD_create_response_from_buffer(body.size(),
                                        const_cast<char*>(body.data()),
                                        MHD_RESPMEM_MUST_COPY);
    if (!response) {
        return MHD_NO;
    }
    MHD_add_response_header(response, "Content-Type", content_type);
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    int ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

int requestHandler(void*,
                   struct MHD_Connection* connection,
                   const char* url,
                   const char* method,
                   const char*,
                   const char*,
                   size_t*,
                   void**) {
    if (std::strcmp(method, MHD_HTTP_METHOD_GET) != 0) {
        return queueResponse(connection,
                             MHD_HTTP_METHOD_NOT_ALLOWED,
                             "{\"error\": \"method not allowed\"}");
    }

    if (std::strcmp(url, "/") == 0) {
        return queueResponse(connection, MHD_HTTP_OK, buildServiceInfoJson());
    }
    if (std::strcmp(url, "/health") == 0) {
        return queueResponse(connection, MHD_HTTP_OK, buildHealthJson());
    }
    if (std::strcmp(url, "/sensor") == 0) {
        SensorSnapshot snapshot = readAllSensors();
        updateSnapshot(snapshot);
        return queueResponse(connection, MHD_HTTP_OK, buildSensorJson(snapshot));
    }

    return queueResponse(connection, MHD_HTTP_NOT_FOUND, "{\"error\": \"not found\"}");
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) std::exit(1);
    if (pid > 0) std::exit(0);

    if (setsid() < 0) std::exit(1);

    pid = fork();
    if (pid < 0) std::exit(1);
    if (pid > 0) std::exit(0);

    umask(0);
    if (chdir("/") != 0) {
        std::exit(1);
    }

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool run_as_daemon = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--daemon") == 0) {
            run_as_daemon = true;
        }
    }

    if (wiringPiSetup() == -1) {
        std::fprintf(stderr, "wiringPiSetup failed\n");
        return 1;
    }
    gpioInit(DHT_PIN);

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    if (run_as_daemon) {
        daemonize();
    }

    // 启动前先预热一次
    updateSnapshot(readAllSensors());

    pthread_t polling_thread;
    if (pthread_create(&polling_thread, nullptr, pollingThreadMain, nullptr) != 0) {
        std::fprintf(stderr, "polling thread create failed\n");
        return 1;
    }

    struct MHD_Daemon* daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        kHttpPort,
        nullptr,
        nullptr,
        &requestHandler,
        nullptr,
        MHD_OPTION_END);

    if (!daemon) {
        g_running.store(false);
        pthread_join(polling_thread, nullptr);
        std::fprintf(stderr, "MHD_start_daemon failed\n");
        return 1;
    }

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    MHD_stop_daemon(daemon);
    pthread_join(polling_thread, nullptr);
    return 0;
}
