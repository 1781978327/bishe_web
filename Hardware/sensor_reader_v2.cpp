/**
 * 整合传感器读取程序 (使用 libgpiod 替代 wiringPi)
 * 读取: DHT11(温度/湿度), MQ-2(烟雾), 光照传感器
 * 输出JSON格式供后端解析
 * 
 * 依赖安装:
 *   sudo apt install libgpiod-dev
 * 
 * 编译:
 *   g++ -std=c++17 -O2 -Wall sensor_reader_v2.cpp -o sensor_reader -lgpiod
 * 
 * 运行:
 *   ./sensor_reader
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
#include <gpiod.h>

// ============== DHT11 部分 (使用 libgpiod) =================
#define DHT_PIN 2  // GPIO2 (对应物理7脚)

// DHT11 数据读取
int readDHT11(float& temperature, float& humidity) {
    const char* chip_path = "/dev/gpiochip0";
    struct gpiod_chip* chip;
    struct gpiod_line* line;
    int ret;
    
    // 打开GPIO chip
    chip = gpiod_chip_open(chip_path);
    if (!chip) {
        fprintf(stderr, "无法打开 GPIO chip: %s\n", chip_path);
        return 0;
    }
    
    // 获取GPIO2
    line = gpiod_chip_get_line(chip, DHT_PIN);
    if (!line) {
        fprintf(stderr, "无法获取 GPIO%d\n", DHT_PIN);
        gpiod_chip_close(chip);
        return 0;
    }
    
    // 设置为输出并拉高
    ret = gpiod_line_request_output(line, "dht11", 1);
    if (ret < 0) {
        fprintf(stderr, "无法设置 GPIO%d 为输出\n", DHT_PIN);
        gpiod_line_release(line);
        gpiod_chip_close(chip);
        return 0;
    }
    
    // 发送起始信号
    gpiod_line_set_value(line, 0);  // 拉低
    usleep(20000);  // 20ms
    gpiod_line_set_value(line, 1);  // 拉高
    usleep(40);     // 40us
    
    // 切换为输入
    gpiod_line_request_input(line, "dht11");
    
    // 等待DHT11响应 (低电平约80us)
    int timeout = 1000;
    while (gpiod_line_get_value(line) == 1 && timeout > 0) {
        usleep(1);
        timeout--;
    }
    if (timeout == 0) {
        gpiod_line_release(line);
        gpiod_chip_close(chip);
        return 0;
    }
    
    // 等待响应结束 (低电平约80us)
    timeout = 1000;
    while (gpiod_line_get_value(line) == 0 && timeout > 0) {
        usleep(1);
        timeout--;
    }
    
    // 读取40位数据
    uint8_t data[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 40; i++) {
        // 等待数据位开始 (高电平约50us)
        timeout = 1000;
        while (gpiod_line_get_value(line) == 0 && timeout > 0) {
            usleep(1);
            timeout--;
        }
        
        // 等待50us后读取
        usleep(50);
        uint8_t value = gpiod_line_get_value(line);
        
        // 数据位 = 1 如果高电平持续 > 28us
        usleep(30);
        if (gpiod_line_get_value(line) == 1) {
            value = 1;
        }
        
        data[i / 8] <<= 1;
        if (value) {
            data[i / 8] |= 1;
        }
    }
    
    gpiod_line_release(line);
    gpiod_chip_close(chip);
    
    // 校验
    if (data[4] == (data[0] + data[1] + data[2] + data[3]) % 256) {
        humidity = data[0] + data[1] * 0.1f;
        temperature = data[2] + data[3] * 0.1f;
        return 1;
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
    return voltage * 200.0f;  // 线性映射
}

int main() {
    // 初始化I2C
    const char* i2c_dev = "/dev/i2c-1";
    int adc_addr = 0x48;
    
    int fd = open(i2c_dev, O_RDWR);
    if (fd < 0) {
        printf("{\"error\": \"I2C设备打开失败\"}\n");
        return 1;
    }
    if (ioctl(fd, I2C_SLAVE, adc_addr) < 0) {
        printf("{\"error\": \"I2C地址设置失败\"}\n");
        close(fd);
        return 1;
    }
    
    // 读取传感器数据
    float temperature = 0, humidity = 0;
    float smoke = 0, light = 0;
    
    // 读取DHT11 (温度/湿度)
    if (readDHT11(temperature, humidity)) {
        // 成功
    } else {
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
    
    // 输出JSON格式
    printf("{\"temperature\": %.1f, \"humidity\": %.1f, \"smoke\": %.0f, \"light\": %.0f}\n",
           temperature, humidity, smoke, light);
    
    return 0;
}
