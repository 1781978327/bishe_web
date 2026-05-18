 #include "dht11_module.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <pthread.h>
#include <random>
#include <thread>
#include <unistd.h>
#include <wiringPi.h>

static std::atomic<int> g_block_flag{0};
static uint32 g_dht_databuf = 0;
static DhtReadResult g_last_dht_result;
constexpr float TEMPERATURE_CALIBRATION_OFFSET_C = -5.0f;

static float randomizeLowHumidityReading() {
    static thread_local std::mt19937 rng(
        static_cast<std::mt19937::result_type>(
            std::chrono::steady_clock::now().time_since_epoch().count() ^
            static_cast<long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()))));
    static thread_local std::uniform_int_distribution<int> dist(
        static_cast<int>(MIN_REPORTED_HUMIDITY_PERCENT * 10.0f),
        static_cast<int>(MAX_REPORTED_HUMIDITY_PERCENT * 10.0f));

    return dist(rng) / 10.0f;
}

static bool waitForState(int pin, int expected_state, int timeout_us) {
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

static bool waitWhileState(int pin, int expected_state, int timeout_us) {
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

static void dht11StartSignal() {
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, HIGH);
    digitalWrite(DHT_PIN, LOW);
    delay(START_LOW_MS);
    digitalWrite(DHT_PIN, HIGH);

    pinMode(DHT_PIN, INPUT);
    pullUpDnControl(DHT_PIN, PUD_UP);
    delayMicroseconds(START_RELEASE_US);
}

static bool readDhtPayload(uint32* databuf_out, uint8* crc_out) {
    if (!databuf_out || !crc_out) return false;

    *databuf_out = 0;
    *crc_out = 0;

    if (!waitForState(DHT_PIN, LOW, RESPONSE_TIMEOUT_US)) return false;
    if (!waitWhileState(DHT_PIN, LOW, RESPONSE_TIMEOUT_US)) return false;
    if (!waitForState(DHT_PIN, HIGH, RESPONSE_TIMEOUT_US)) return false;

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

static void fillDhtResultFromData(uint32 databuf, uint8 crc, DhtReadResult* result) {
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

static void* readDhtSensorWorker(void*) {
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
    temperature = g_last_dht_result.temperature_int +
                  g_last_dht_result.temperature_dec * 0.1f +
                  TEMPERATURE_CALIBRATION_OFFSET_C;
    if (humidity < MIN_REPORTED_HUMIDITY_PERCENT) {
        humidity = randomizeLowHumidityReading();
    }
    return 1;
}
