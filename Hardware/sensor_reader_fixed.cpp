/**
 * 整合传感器读取程序（修复版）
 * 读取: DHT11(温度/湿度), MQ-2(烟雾), 光照传感器
 * 输出JSON格式供后端解析
 * 
 * 编译:
 *   g++ -std=c++17 -O2 -Wall sensor_reader_fixed.cpp -o sensor_reader_fixed -lwiringPi
 * 
 * 运行:
 *   sudo ./sensor_reader_fixed
 */

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <chrono>

// ============== DHT11 部分 =================
#include <wiringPi.h>

typedef unsigned char uint8;
typedef unsigned int  uint16;
typedef unsigned long uint32;

#define DHT_PIN 2  // 物理7脚 → wPi编号 = 2
#define HIGH_TIME 32
#define TIMEOUT_US 100000  // 100ms超时
uint32 dht_databuf;

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
        // 湿度 = 高8位整数.低8位小数， 温度 = 高8位整数.低8位小数
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
    return voltage * 200.0f;  // 假设0-5V对应0-1000lux
}

int main() {
    if (wiringPiSetup() == -1) {
        printf("{\"error\": \"DHT11初始化失败\"}\n");
        return 1;
    }
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, 1);
    
    constexpr const char* I2C_DEV = "/dev/i2c-1";
    constexpr int ADC_ADDR = 0x48;
    
    int fd = open(I2C_DEV, O_RDWR);
    if (fd < 0) {
        printf("{\"error\": \"I2C设备打开失败\"}\n");
        return 1;
    }
    if (ioctl(fd, I2C_SLAVE, ADC_ADDR) < 0) {
        printf("{\"error\": \"I2C地址设置失败\"}\n");
        close(fd);
        return 1;
    }
    
    float temperature = 0, humidity = 0;
    float smoke = 0, light = 0;
    
    // 读取DHT11 (带重试)
    if (!readDHT11WithRetry(temperature, humidity)) {
        temperature = -1;
        humidity = -1;
    }
    
    // 读取MQ-2 (烟雾 - AIN0)
    int16_t raw_mq2;
    if (readAds1115Channel(fd, 0, raw_mq2)) {
        float voltage_mq2 = raw_mq2 * ADC_SCALE;
        smoke = calculateSmokePPM(voltage_mq2);
    } else {
        smoke = -1;
    }
    
    // 读取光照传感器 (AIN1)
    int16_t raw_light;
    if (readAds1115Channel(fd, 1, raw_light)) {
        float voltage_light = raw_light * ADC_SCALE;
        light = calculateLightLux(voltage_light);
    } else {
        light = -1;
    }
    
    close(fd);
    
    printf("{\"temperature\": %.1f, \"humidity\": %.1f, \"smoke\": %.0f, \"light\": %.0f}\n",
           temperature, humidity, smoke, light);
    
    return 0;
}
