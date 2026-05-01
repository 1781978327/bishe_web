#pragma once

#include <cstdint>

typedef unsigned char uint8;
typedef unsigned int uint16;
typedef unsigned long uint32;

// DHT11 constants
#define DHT_PIN 2
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

struct DhtReadResult {
    int status = 2;
    int attempts = 0;
    uint8 humidity_int = 0;
    uint8 humidity_dec = 0;
    uint8 temperature_int = 0;
    uint8 temperature_dec = 0;
    uint8 checksum = 0;
    uint8 checksum_calc = 0;
};

void gpioInit(int gpio_pin);
int readDht11Threaded(float& temperature, float& humidity);
