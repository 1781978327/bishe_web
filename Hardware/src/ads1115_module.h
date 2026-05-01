#pragma once

#include "dht11_module.h"

#include <cstdint>
#include <ctime>

// ADS1115 constants
static constexpr uint8_t REG_POINTER_CONVERT = 0x00;
static constexpr uint8_t REG_POINTER_CONFIG = 0x01;
static constexpr uint16_t OS_SINGLE = 0x8000;
static constexpr uint16_t MUX_SINGLE_0 = 0x4000;
static constexpr uint16_t MUX_SINGLE_1 = 0x5000;
static constexpr uint16_t PGA_4_096V = 0x0200;
static constexpr uint16_t MODE_SINGLE = 0x0100;
static constexpr uint16_t DR_128SPS = 0x0080;
static constexpr uint16_t COMP_QUE_NONE = 0x0003;

static constexpr float VCC = 5.0f;
static constexpr float RL = 10000.0f;
static constexpr float ADC_FS_VOLTS = 4.096f;
static constexpr float ADC_SCALE = ADC_FS_VOLTS / 32768.0f;
static constexpr int ADS_MEDIAN_SAMPLE_COUNT = 5;

struct SensorSnapshot {
    float temperature = -1.0f;
    float humidity = -1.0f;
    float smoke = -1.0f;
    float light = -1.0f;
    std::time_t timestamp = 0;
    bool dht_ok = false;
    bool ads_ok = false;
};

SensorSnapshot readAllSensors();
SensorSnapshot getSnapshotCopy();
void updateSnapshot(const SensorSnapshot& snapshot);
