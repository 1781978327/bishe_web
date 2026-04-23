#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <wiringPi.h>

typedef unsigned char uint8;
typedef unsigned int uint16;
typedef unsigned long uint32;

#define START_LOW_MS 25
#define START_RELEASE_US 30
#define HIGH_TIME_US 32
#define RESPONSE_TIMEOUT_US 1000
#define BIT_TIMEOUT_US 200
#define POWER_STABILIZE_MS 1000
#define DEFAULT_INTERVAL_SEC 3

static uint32 databuf = 0;

static int parse_int(const char* text, int* out) {
    char* end = NULL;
    long value = 0;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || text[0] == '\0' || end == NULL || *end != '\0') {
        return 0;
    }

    *out = (int)value;
    return 1;
}

static int wait_for_state(int gpio_pin, int expected_state, unsigned int timeout_us) {
    unsigned int start = micros();
    while (digitalRead(gpio_pin) != expected_state) {
        if ((unsigned int)(micros() - start) > timeout_us) {
            return 0;
        }
    }
    return 1;
}

static int wait_while_state(int gpio_pin, int expected_state, unsigned int timeout_us) {
    unsigned int start = micros();
    while (digitalRead(gpio_pin) == expected_state) {
        if ((unsigned int)(micros() - start) > timeout_us) {
            return 0;
        }
    }
    return 1;
}

static void gpio_init(int gpio_pin) {
    pinMode(gpio_pin, OUTPUT);
    digitalWrite(gpio_pin, HIGH);
    delay(POWER_STABILIZE_MS);
}

static void dht11_start(int gpio_pin) {
    pinMode(gpio_pin, OUTPUT);
    digitalWrite(gpio_pin, HIGH);
    delayMicroseconds(4);
    digitalWrite(gpio_pin, LOW);
    delay(START_LOW_MS);
    digitalWrite(gpio_pin, HIGH);
    pinMode(gpio_pin, INPUT);
    pullUpDnControl(gpio_pin, PUD_UP);
    delayMicroseconds(START_RELEASE_US);
}

static int dht11_read_frame(int gpio_pin, uint8* crc_out) {
    uint8 crc = 0;
    int i = 0;

    databuf = 0;

    if (!wait_for_state(gpio_pin, LOW, RESPONSE_TIMEOUT_US)) {
        return 0;
    }
    if (!wait_while_state(gpio_pin, LOW, RESPONSE_TIMEOUT_US)) {
        return 0;
    }
    if (!wait_while_state(gpio_pin, HIGH, RESPONSE_TIMEOUT_US)) {
        return 0;
    }

    for (i = 0; i < 32; ++i) {
        if (!wait_for_state(gpio_pin, LOW, BIT_TIMEOUT_US)) {
            return 0;
        }
        if (!wait_while_state(gpio_pin, LOW, BIT_TIMEOUT_US)) {
            return 0;
        }

        delayMicroseconds(HIGH_TIME_US);
        databuf <<= 1;
        if (digitalRead(gpio_pin) == HIGH) {
            databuf |= 1;
            if (!wait_while_state(gpio_pin, HIGH, BIT_TIMEOUT_US)) {
                return 0;
            }
        }
    }

    for (i = 0; i < 8; ++i) {
        if (!wait_for_state(gpio_pin, LOW, BIT_TIMEOUT_US)) {
            return 0;
        }
        if (!wait_while_state(gpio_pin, LOW, BIT_TIMEOUT_US)) {
            return 0;
        }

        delayMicroseconds(HIGH_TIME_US);
        crc <<= 1;
        if (digitalRead(gpio_pin) == HIGH) {
            crc |= 1;
            if (!wait_while_state(gpio_pin, HIGH, BIT_TIMEOUT_US)) {
                return 0;
            }
        }
    }

    *crc_out = crc;
    return 1;
}

static int decode_frame(uint8 crc,
                        uint8* humidity_int,
                        uint8* humidity_dec,
                        uint8* temperature_int,
                        uint8* temperature_dec) {
    uint8 checksum = 0;

    *humidity_int = (uint8)((databuf >> 24) & 0xFF);
    *humidity_dec = (uint8)((databuf >> 16) & 0xFF);
    *temperature_int = (uint8)((databuf >> 8) & 0xFF);
    *temperature_dec = (uint8)(databuf & 0xFF);

    checksum = (uint8)(*humidity_int + *humidity_dec + *temperature_int + *temperature_dec);
    return checksum == crc;
}

static void print_usage(const char* argv0) {
    fprintf(stderr, "Usage: sudo %s <wiringPi_pin> [interval_seconds] [count]\n", argv0);
    fprintf(stderr, "  interval_seconds defaults to %d\n", DEFAULT_INTERVAL_SEC);
    fprintf(stderr, "  count defaults to 0, which means loop forever\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  sudo %s 2\n", argv0);
    fprintf(stderr, "  sudo %s 2 3 1\n", argv0);
}

int main(int argc, char* argv[]) {
    int gpio_pin = 0;
    int interval_seconds = DEFAULT_INTERVAL_SEC;
    int count = 0;
    int iteration = 0;

    if (argc < 2 || argc > 4) {
        print_usage(argv[0]);
        return 1;
    }

    if (!parse_int(argv[1], &gpio_pin) || gpio_pin < 0) {
        fprintf(stderr, "Invalid wiringPi pin: %s\n", argv[1]);
        return 1;
    }
    if (argc >= 3 && (!parse_int(argv[2], &interval_seconds) || interval_seconds < 0)) {
        fprintf(stderr, "Invalid interval seconds: %s\n", argv[2]);
        return 1;
    }
    if (argc >= 4 && (!parse_int(argv[3], &count) || count < 0)) {
        fprintf(stderr, "Invalid count: %s\n", argv[3]);
        return 1;
    }

    if (wiringPiSetup() == -1) {
        fprintf(stderr, "wiringPiSetup failed\n");
        return 1;
    }

    gpio_init(gpio_pin);
    printf("DHT11 basic test started: pin=%d interval=%d count=%d\n",
           gpio_pin,
           interval_seconds,
           count);

    while (count == 0 || iteration < count) {
        uint8 crc = 0;
        uint8 humidity_int = 0;
        uint8 humidity_dec = 0;
        uint8 temperature_int = 0;
        uint8 temperature_dec = 0;

        ++iteration;
        dht11_start(gpio_pin);

        if (!dht11_read_frame(gpio_pin, &crc)) {
            printf("[%d] Sensor did not answer or frame timed out\n", iteration);
        } else if (!decode_frame(crc,
                                 &humidity_int,
                                 &humidity_dec,
                                 &temperature_int,
                                 &temperature_dec)) {
            printf("[%d] Checksum mismatch\n", iteration);
        } else {
            printf("[%d] RH:%u.%u TMP:%u.%u checksum:%u\n",
                   iteration,
                   humidity_int,
                   humidity_dec,
                   temperature_int,
                   temperature_dec,
                   crc);
        }

        pinMode(gpio_pin, OUTPUT);
        digitalWrite(gpio_pin, HIGH);

        if (count == 0 || iteration < count) {
            sleep((unsigned int)interval_seconds);
        }
    }

    return 0;
}
