// Local DHT11 timing test for Orange Pi / wiringPi.
// Default mode follows the MCU-style fixed-delay sampling logic:
// wait for the data bit to go high, delay ~30us, then sample 0/1.
//
// Build:
//   g++ -std=c++17 -O2 -Wall dht11_timing_test.cpp -o dht11_timing_test -lwiringPi
//
// Run:
//   sudo ./dht11_timing_test
//   sudo ./dht11_timing_test --mode pulse --debug

#include <wiringPi.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kDefaultPin = 2;  // wiringPi pin 2, physical pin 7
constexpr int kStartLowMs = 20;
constexpr int kStartReleaseUs = 30;
constexpr int kResponseTimeoutUs = 1000;
constexpr int kBitTimeoutUs = 200;
constexpr int kDefaultSampleDelayUs = 30;
constexpr int kDefaultThresholdUs = 45;
constexpr int kDefaultIntervalMs = 2000;

volatile sig_atomic_t g_stop = 0;

enum class ReadMode {
    kSample,
    kPulse,
};

struct Options {
    int pin = kDefaultPin;
    int intervalMs = kDefaultIntervalMs;
    int sampleDelayUs = kDefaultSampleDelayUs;
    int thresholdUs = kDefaultThresholdUs;
    int count = 0;  // 0 means run until Ctrl+C
    bool debug = false;
    ReadMode mode = ReadMode::kSample;
};

struct Frame {
    uint8_t bytes[5] = {0, 0, 0, 0, 0};
    int highUs[40] = {0};
    bool checksumOk = false;
    const char* error = nullptr;
};

int64_t nowMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void onSignal(int) {
    g_stop = 1;
}

bool waitForState(int pin, int state, int timeoutUs) {
    const int64_t start = nowMicros();
    while (digitalRead(pin) != state) {
        if (nowMicros() - start > timeoutUs) {
            return false;
        }
    }
    return true;
}

bool waitWhileState(int pin, int state, int timeoutUs, int* durationUs) {
    const int64_t start = nowMicros();
    while (digitalRead(pin) == state) {
        if (nowMicros() - start > timeoutUs) {
            return false;
        }
    }

    if (durationUs) {
        *durationUs = static_cast<int>(nowMicros() - start);
    }
    return true;
}

void primeLineHigh(int pin) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, 1);
}

bool sendStartSignal(int pin, Frame* frame) {
    primeLineHigh(pin);
    delayMicroseconds(4);
    digitalWrite(pin, 0);
    delay(kStartLowMs);
    digitalWrite(pin, 1);
    delayMicroseconds(kStartReleaseUs);
    pinMode(pin, INPUT);
    pullUpDnControl(pin, PUD_UP);

    if (!waitForState(pin, 0, kResponseTimeoutUs)) {
        frame->error = "timeout waiting for response low";
        return false;
    }
    if (!waitWhileState(pin, 0, kResponseTimeoutUs, nullptr)) {
        frame->error = "timeout waiting for response low end";
        return false;
    }
    if (!waitWhileState(pin, 1, kResponseTimeoutUs, nullptr)) {
        frame->error = "timeout waiting for response high end";
        return false;
    }
    return true;
}

bool captureFrameSample(const Options& options, Frame* frame) {
    *frame = {};
    if (!sendStartSignal(options.pin, frame)) {
        return false;
    }

    for (int i = 0; i < 40; ++i) {
        if (!waitForState(options.pin, 0, kBitTimeoutUs)) {
            frame->error = "timeout waiting for bit low";
            return false;
        }
        if (!waitWhileState(options.pin, 0, kBitTimeoutUs, nullptr)) {
            frame->error = "timeout waiting for bit high";
            return false;
        }

        const int64_t highStart = nowMicros();
        delayMicroseconds(options.sampleDelayUs);
        const int bit = digitalRead(options.pin);
        if (bit == 1 && !waitForState(options.pin, 0, kBitTimeoutUs)) {
            frame->error = "timeout waiting for sampled high to end";
            return false;
        }

        frame->highUs[i] = static_cast<int>(nowMicros() - highStart);
        frame->bytes[i / 8] <<= 1;
        if (bit == 1) {
            frame->bytes[i / 8] |= 1;
        }
    }

    const uint8_t checksum =
        static_cast<uint8_t>(frame->bytes[0] + frame->bytes[1] + frame->bytes[2] + frame->bytes[3]);
    frame->checksumOk = (checksum == frame->bytes[4]);
    return true;
}

bool captureFramePulse(const Options& options, Frame* frame) {
    *frame = {};
    if (!sendStartSignal(options.pin, frame)) {
        return false;
    }

    for (int i = 0; i < 40; ++i) {
        if (!waitForState(options.pin, 0, kBitTimeoutUs)) {
            frame->error = "timeout waiting for bit low";
            return false;
        }
        if (!waitWhileState(options.pin, 0, kBitTimeoutUs, nullptr)) {
            frame->error = "timeout waiting for bit high";
            return false;
        }

        int highUs = 0;
        if (!waitWhileState(options.pin, 1, kBitTimeoutUs, &highUs)) {
            frame->error = "timeout measuring pulse width";
            return false;
        }

        frame->highUs[i] = highUs;
        frame->bytes[i / 8] <<= 1;
        if (highUs >= options.thresholdUs) {
            frame->bytes[i / 8] |= 1;
        }
    }

    const uint8_t checksum =
        static_cast<uint8_t>(frame->bytes[0] + frame->bytes[1] + frame->bytes[2] + frame->bytes[3]);
    frame->checksumOk = (checksum == frame->bytes[4]);
    return true;
}

void printUsage(const char* argv0) {
    std::printf("Usage: %s [options]\n", argv0);
    std::printf("  --mode sample|pulse     sample: MCU-style fixed-delay sampling (default)\n");
    std::printf("  --sample-delay-us N     sample mode delay after rising edge (default: %d)\n",
                kDefaultSampleDelayUs);
    std::printf("  --threshold-us N        pulse mode threshold for 1-bit detection (default: %d)\n",
                kDefaultThresholdUs);
    std::printf("  --interval-ms N         delay between reads (default: %d)\n", kDefaultIntervalMs);
    std::printf("  --count N               stop after N reads (default: run until Ctrl+C)\n");
    std::printf("  --pin N                 wiringPi pin number (default: %d)\n", kDefaultPin);
    std::printf("  --debug                 print per-bit pulse widths\n");
    std::printf("  --help                  show this help\n");
}

bool parseInt(const char* text, int* out) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!text[0] || !end || *end != '\0') {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool parseArgs(int argc, char** argv, Options* options) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "sample") == 0) {
                options->mode = ReadMode::kSample;
            } else if (std::strcmp(argv[i], "pulse") == 0) {
                options->mode = ReadMode::kPulse;
            } else {
                std::fprintf(stderr, "Unknown mode: %s\n", argv[i]);
                return false;
            }
        } else if (std::strcmp(argv[i], "--sample-delay-us") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], &options->sampleDelayUs)) return false;
        } else if (std::strcmp(argv[i], "--threshold-us") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], &options->thresholdUs)) return false;
        } else if (std::strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], &options->intervalMs)) return false;
        } else if (std::strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], &options->count)) return false;
        } else if (std::strcmp(argv[i], "--pin") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], &options->pin)) return false;
        } else if (std::strcmp(argv[i], "--debug") == 0) {
            options->debug = true;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

void printFrame(const Options& options, int index, const Frame& frame) {
    const int checksumCalc =
        (frame.bytes[0] + frame.bytes[1] + frame.bytes[2] + frame.bytes[3]) & 0xFF;
    const float humidity = frame.bytes[0] + frame.bytes[1] * 0.1f;
    const float temperature = frame.bytes[2] + frame.bytes[3] * 0.1f;
    const char* mode = options.mode == ReadMode::kSample ? "sample" : "pulse";

    std::printf("[%d] mode=%s pin=%d bytes=[%u,%u,%u,%u,%u] checksum=%s(calc=%d) RH=%.1f T=%.1f\n",
                index,
                mode,
                options.pin,
                frame.bytes[0],
                frame.bytes[1],
                frame.bytes[2],
                frame.bytes[3],
                frame.bytes[4],
                frame.checksumOk ? "ok" : "bad",
                checksumCalc,
                humidity,
                temperature);

    if (options.debug) {
        std::printf("     high_us:");
        for (int i = 0; i < 40; ++i) {
            if (i % 8 == 0) {
                std::printf(" |");
            }
            std::printf(" %d", frame.highUs[i]);
        }
        std::printf("\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, &options)) {
        printUsage(argv[0]);
        return 1;
    }

    if (wiringPiSetup() == -1) {
        std::fprintf(stderr, "wiringPiSetup failed\n");
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::printf("DHT11 timing test started\n");
    std::printf("  pin=%d mode=%s interval=%dms",
                options.pin,
                options.mode == ReadMode::kSample ? "sample" : "pulse",
                options.intervalMs);
    if (options.mode == ReadMode::kSample) {
        std::printf(" sample_delay=%dus\n", options.sampleDelayUs);
    } else {
        std::printf(" threshold=%dus\n", options.thresholdUs);
    }
    std::printf("Press Ctrl+C to stop\n");

    primeLineHigh(options.pin);

    int readIndex = 0;
    while (!g_stop && (options.count == 0 || readIndex < options.count)) {
        Frame frame;
        bool ok = false;
        if (options.mode == ReadMode::kSample) {
            ok = captureFrameSample(options, &frame);
        } else {
            ok = captureFramePulse(options, &frame);
        }

        ++readIndex;
        if (ok) {
            printFrame(options, readIndex, frame);
        } else {
            std::printf("[%d] read failed: %s\n", readIndex, frame.error ? frame.error : "unknown");
        }

        if (g_stop || (options.count != 0 && readIndex >= options.count)) {
            break;
        }
        delay(options.intervalMs);
    }

    return 0;
}
