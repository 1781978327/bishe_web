#include "sensor_http_server.h"

#include <microhttpd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

std::atomic<bool> g_running{true};

static SensorSnapshot g_snapshot;
static pthread_mutex_t g_snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;

void updateSnapshot(const SensorSnapshot& snapshot) {
    pthread_mutex_lock(&g_snapshot_mutex);
    g_snapshot = snapshot;
    pthread_mutex_unlock(&g_snapshot_mutex);
}

SensorSnapshot getSnapshotCopy() {
    pthread_mutex_lock(&g_snapshot_mutex);
    SensorSnapshot copy = g_snapshot;
    pthread_mutex_unlock(&g_snapshot_mutex);
    return copy;
}

void* pollingThreadMain(void*) {
    while (g_running.load()) {
        updateSnapshot(readAllSensors());
        for (int i = 0; i < kPollIntervalMs / 100; ++i) {
            if (!g_running.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return nullptr;
}

std::string buildServiceInfoJson() {
    return "{\"service\": \"sensor-reader\", \"version\": \"1.0\", \"endpoints\": [\"/health\", \"/sensor\"]}";
}

std::string buildHealthJson() {
    const SensorSnapshot snapshot = getSnapshotCopy();
    std::ostringstream oss;
    oss << "{\"status\": \"ok\", \"timestamp\": " << static_cast<long long>(snapshot.timestamp) << "}";
    return oss.str();
}

std::string buildSensorJson(const SensorSnapshot& snapshot) {
    char buffer[256];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "{\"temperature\": %.1f, \"humidity\": %.1f, \"smoke\": %.0f, \"light\": %.0f}",
                  snapshot.temperature,
                  snapshot.humidity,
                  snapshot.smoke,
                  snapshot.light);
    return std::string(buffer);
}

int queueResponse(struct MHD_Connection* connection,
                  unsigned int status_code,
                  const std::string& body,
                  const char* content_type) {
    struct MHD_Response* response =
        MHD_create_response_from_buffer(body.size(),
                                        const_cast<char*>(body.data()),
                                        MHD_RESPMEM_MUST_COPY);
    if (!response) {
        return MHD_NO;
    }
    MHD_add_response_header(response, "Content-Type", content_type);
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    int ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

int requestHandler(void*,
                   struct MHD_Connection* connection,
                   const char* url,
                   const char* method,
                   const char*,
                   const char*,
                   size_t*,
                   void**) {
    if (std::strcmp(method, MHD_HTTP_METHOD_GET) != 0) {
        return queueResponse(connection,
                             MHD_HTTP_METHOD_NOT_ALLOWED,
                             "{\"error\": \"method not allowed\"}");
    }

    if (std::strcmp(url, "/") == 0) {
        return queueResponse(connection, MHD_HTTP_OK, buildServiceInfoJson());
    }
    if (std::strcmp(url, "/health") == 0) {
        return queueResponse(connection, MHD_HTTP_OK, buildHealthJson());
    }
    if (std::strcmp(url, "/sensor") == 0) {
        SensorSnapshot snapshot = readAllSensors();
        updateSnapshot(snapshot);
        return queueResponse(connection, MHD_HTTP_OK, buildSensorJson(snapshot));
    }

    return queueResponse(connection, MHD_HTTP_NOT_FOUND, "{\"error\": \"not found\"}");
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) std::exit(1);
    if (pid > 0) std::exit(0);

    if (setsid() < 0) std::exit(1);

    pid = fork();
    if (pid < 0) std::exit(1);
    if (pid > 0) std::exit(0);

    umask(0);
    if (chdir("/") != 0) {
        std::exit(1);
    }

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}
