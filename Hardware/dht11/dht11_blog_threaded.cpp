// DHT11 threaded test for Orange Pi / wiringPi.
// Based on the blog-style single-shot trigger + worker-thread read flow,
// with a few safety guards added for timeouts and checksum validation.
//
// Build:
//   g++ -std=c++17 -O2 -Wall dht11_blog_threaded.cpp -o dht11_blog_threaded -lwiringPi -pthread
//
// Run once:
//   sudo ./dht11_blog_threaded --once
//
// Interactive mode:
//   sudo ./dht11_blog_threaded

#include <wiringPi.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <unistd.h>

typedef unsigned char uint8;
typedef unsigned int uint16;
typedef unsigned long uint32;

#define DHT_PIN 2  // 物理7脚 -> wPi编号 = 2
#define HIGH_TIME 32
#define START_LOW_MS 25
#define START_RELEASE_US 35
#define RESPONSE_TIMEOUT_US 1000
#define BIT_TIMEOUT_US 200
#define MAX_ATTEMPTS 5
#define MAX_WAIT_SECONDS 10
#define MAX_VALID_TEMPERATURE_C 50

namespace {

std::atomic<int> g_block_flag{0};
uint32 g_databuf = 0;

struct ReadResult {
    int status = 2;  // 0: no answer, 1: ok, 2: invalid data after retries, 3: timeout
    int attempts = 0;
    uint8 humidity_int = 0;
    uint8 humidity_dec = 0;
    uint8 temperature_int = 0;
    uint8 temperature_dec = 0;
    uint8 checksum = 0;
    uint8 checksum_calc = 0;
};

ReadResult g_last_result;

bool wait_for_state(int pin, int expected_state, int timeout_us) {
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

bool wait_while_state(int pin, int expected_state, int timeout_us) {
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

void gpio_init(int gpio_pin) {
    pinMode(gpio_pin, OUTPUT);
    digitalWrite(gpio_pin, HIGH);
    delay(1000);
}

void dht11_start_signal() {
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, HIGH);
    digitalWrite(DHT_PIN, LOW);
    delay(START_LOW_MS);
    digitalWrite(DHT_PIN, HIGH);

    pinMode(DHT_PIN, INPUT);
    pullUpDnControl(DHT_PIN, PUD_UP);
    delayMicroseconds(START_RELEASE_US);
}

bool read_payload(uint32* databuf_out, uint8* crc_out) {
    if (!databuf_out || !crc_out) return false;

    *databuf_out = 0;
    *crc_out = 0;

    if (!wait_for_state(DHT_PIN, LOW, RESPONSE_TIMEOUT_US)) {
        return false;
    }
    if (!wait_while_state(DHT_PIN, LOW, RESPONSE_TIMEOUT_US)) {
        return false;
    }
    if (!wait_while_state(DHT_PIN, HIGH, RESPONSE_TIMEOUT_US)) {
        return false;
    }

    for (int i = 0; i < 32; ++i) {
        if (!wait_for_state(DHT_PIN, LOW, BIT_TIMEOUT_US)) return false;
        if (!wait_while_state(DHT_PIN, LOW, BIT_TIMEOUT_US)) return false;
        delayMicroseconds(HIGH_TIME);
        *databuf_out <<= 1;
        if (digitalRead(DHT_PIN) == HIGH) {
            *databuf_out |= 1;
            if (!wait_while_state(DHT_PIN, HIGH, BIT_TIMEOUT_US)) return false;
        }
    }

    for (int i = 0; i < 8; ++i) {
        if (!wait_for_state(DHT_PIN, LOW, BIT_TIMEOUT_US)) return false;
        if (!wait_while_state(DHT_PIN, LOW, BIT_TIMEOUT_US)) return false;
        delayMicroseconds(HIGH_TIME);
        *crc_out <<= 1;
        if (digitalRead(DHT_PIN) == HIGH) {
            *crc_out |= 1;
            if (!wait_while_state(DHT_PIN, HIGH, BIT_TIMEOUT_US)) return false;
        }
    }

    return true;
}

void fill_result_from_data(uint32 databuf, uint8 crc, ReadResult* result) {
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

void* read_sensor_data(void*) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, nullptr);

    ReadResult local{};
    local.status = 2;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        pthread_testcancel();

        uint32 databuf = 0;
        uint8 crc = 0;
        local.attempts = attempt;

        dht11_start_signal();
        if (!read_payload(&databuf, &crc)) {
            local.status = 0;
            delay(200);
            continue;
        }

        fill_result_from_data(databuf, crc, &local);
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

        g_databuf = databuf;
        local.status = 1;
        g_last_result = local;
        g_block_flag.store(0);
        return reinterpret_cast<void*>(static_cast<intptr_t>(1));
    }

    g_last_result = local;
    g_block_flag.store(0);
    return reinterpret_cast<void*>(static_cast<intptr_t>(local.status));
}

int run_single_read() {
    pthread_t tid;
    g_block_flag.store(1);
    g_last_result = ReadResult{};

    if (pthread_create(&tid, nullptr, read_sensor_data, nullptr) != 0) {
        std::fprintf(stderr, "thread create fail!\n");
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
        g_last_result.status = 3;
        std::printf("线程超时退出.....\n");
        return 3;
    }

    void* thread_ret = nullptr;
    pthread_join(tid, &thread_ret);
    return static_cast<int>(reinterpret_cast<intptr_t>(thread_ret));
}

void print_result() {
    if (g_last_result.status == 1) {
        std::printf("Congratulations! Sensor data read ok!\n");
        std::printf("RH:%u.%u\n", g_last_result.humidity_int, g_last_result.humidity_dec);
        std::printf("TMP:%u.%u\n", g_last_result.temperature_int, g_last_result.temperature_dec);
        std::printf("checksum: recv=%u calc=%u\n",
                    g_last_result.checksum,
                    g_last_result.checksum_calc);
        return;
    }

    if (g_last_result.status == 0) {
        std::printf("Sorry! Sensor doesn't answer!\n");
        return;
    }

    if (g_last_result.status == 3) {
        std::printf("thread timeout, no valid data returned\n");
        return;
    }

    std::printf("get data fail after %d attempt(s)\n", g_last_result.attempts);
}

}  // namespace

int main(int argc, char** argv) {
    const bool run_once = (argc > 1 && std::strcmp(argv[1], "--once") == 0);

    if (wiringPiSetup() == -1) {
        std::fprintf(stderr, "wiringPiSetup failed\n");
        return 1;
    }

    gpio_init(DHT_PIN);

    if (run_once) {
        run_single_read();
        print_result();
        return g_last_result.status == 1 ? 0 : 1;
    }

    char cmd[16] = {'\0'};
    std::printf("Enter OS-------\n");
    while (true) {
        std::memset(cmd, 0, sizeof(cmd));
        delay(1000);
        std::printf("input y\n");
        if (std::scanf("%15s", cmd) != 1) {
            std::printf("input ended\n");
            break;
        }
        getchar();

        if (std::strcmp(cmd, "y") == 0) {
            run_single_read();
            print_result();
        } else {
            std::printf("go on\n");
        }
    }

    return 0;
}
