/**
 * 传感器 HTTP 服务
 * 读取: DHT11(温度/湿度), MQ-2(烟雾), 光照传感器
 * 输出 JSON 供后端解析
 *
 * 编译 (CMake):
 *   mkdir build && cd build && cmake .. && make -j$(nproc)
 *
 * 运行:
 *   sudo ./sensor_reader_http
 *   sudo ./sensor_reader_http --daemon
 */

#include "src/dht11_module.h"
#include "src/sensor_http_server.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <microhttpd.h>
#include <pthread.h>
#include <thread>
#include <wiringPi.h>

static void handleSignal(int) {
    g_running.store(false);
}

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
