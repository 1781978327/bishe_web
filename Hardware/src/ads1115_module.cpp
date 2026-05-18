#include "ads1115_module.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>

static bool i2cWriteReg16(int fd, uint8_t reg, uint16_t value) {
    uint8_t buf[3] = {reg, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
    return write(fd, buf, 3) == 3;
}

static bool i2cReadConversion(int fd, int16_t& out) {
    uint8_t ptr = REG_POINTER_CONVERT;
    if (write(fd, &ptr, 1) != 1) return false;
    uint8_t data[2];
    if (read(fd, data, 2) != 2) return false;
    out = static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
    return true;
}

static bool readAds1115Channel(int fd, uint8_t channel, int16_t& raw) {
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

static bool readAds1115ChannelMedian(int fd, uint8_t channel, int16_t& raw) {
    std::array<int16_t, ADS_MEDIAN_SAMPLE_COUNT> samples{};
    int count = 0;

    for (int i = 0; i < ADS_MEDIAN_SAMPLE_COUNT; ++i) {
        int16_t sample = 0;
        if (!readAds1115Channel(fd, channel, sample)) {
            return false;
        }
        samples[count++] = sample;
        usleep(1000);
    }

    std::sort(samples.begin(), samples.begin() + count);
    raw = samples[count / 2];
    return true;
}

static float calculateSmokePPM(float voltage) {
    if (voltage <= 0.001f) return 0.0f;
    if (voltage >= VCC - 0.001f) return 9999.0f;

    float rs = RL * (VCC - voltage) / voltage;
    constexpr float R0 = 106000.0f;
    if (rs <= 0.0f) return 0.0f;

    return std::pow(21.72f * R0 / rs, 2.1101f);
}

static float calculateLightLux(float voltage) {
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
        bool mq2_ok = readAds1115ChannelMedian(fd, 0, raw_mq2);
        bool light_ok = readAds1115ChannelMedian(fd, 1, raw_light);
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
