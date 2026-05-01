#include "http_ctrl_mediamtx.h"
#include "http_ctrl_globals.h"
#include "http_ctrl_utils.h"

#include <stdio.h>
#include <unistd.h>

#include <chrono>
#include <mutex>
#include <vector>

// ---------------------- Resolve mediamtx binary path ----------------------

std::string resolve_mediamtx_binary_path() {
    std::vector<std::string> candidates;
    candidates.reserve(8);
    if (!g_mediamtx_bin.empty()) candidates.push_back(g_mediamtx_bin);
    candidates.push_back(default_mediamtx_binary_path());
    {
        std::string root = detect_project_root();
        if (!root.empty()) {
            candidates.push_back(path_join(root, "mediamtx"));
        }
    }
    candidates.push_back("./mediamtx");
    candidates.push_back("../src/mediamtx");
    candidates.push_back("../mediamtx");
    for (const auto& p : candidates) {
        if (is_executable_file(p)) return p;
    }
    return "";
}

// ---------------------- Start mediamtx if needed ----------------------

bool start_mediamtx_if_needed(const char* reason, std::string* detail) {
    if (detail) detail->clear();
    if (!g_mediamtx_auto_start) {
        if (detail) *detail = "auto-start disabled";
        return false;
    }
    if (!is_local_rtsp_host(g_rtsp_host)) {
        if (detail) *detail = "rtsp host is remote";
        return false;
    }
    if (is_tcp_service_ready(g_rtsp_host, g_rtsp_port, 250)) {
        if (detail) *detail = "already ready";
        return true;
    }

    static std::mutex s_mtx;
    static long long s_last_attempt_ms = 0;
    const int retry_interval_ms = 3000;

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(s_mtx);
        if (now_ms - s_last_attempt_ms < retry_interval_ms) {
            if (detail) *detail = "recently attempted";
            return is_tcp_service_ready(g_rtsp_host, g_rtsp_port, 250);
        }
        s_last_attempt_ms = now_ms;
    }

    std::string bin_path = resolve_mediamtx_binary_path();
    if (bin_path.empty()) {
        if (detail) *detail = "mediamtx binary not found";
        printf("[RTSP] mediamtx 自动启动失败: 未找到可执行文件\n");
        return false;
    }

    if (g_mediamtx_log.empty()) {
        g_mediamtx_log = DEFAULT_MEDIAMTX_LOG;
    }
    std::string command = "MTX_RTMP=no MTX_HLS=no MTX_WEBRTC=yes MTX_SRT=no \"" +
                          bin_path + "\" >\"" + g_mediamtx_log + "\" 2>&1 &";
    int rc = system(command.c_str());
    if (rc != 0) {
        if (detail) *detail = "launch command failed";
        printf("[RTSP] mediamtx 自动启动失败: rc=%d cmd=%s\n", rc, command.c_str());
        return false;
    }

    for (int i = 0; i < 20; ++i) {
        if (is_tcp_service_ready(g_rtsp_host, g_rtsp_port, 250)) {
            if (detail) *detail = "started";
            printf("[RTSP] mediamtx 已自动启动 (%s), reason=%s\n", bin_path.c_str(),
                   reason ? reason : "unknown");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (detail) *detail = "started but port still unavailable";
    printf("[RTSP] mediamtx 启动命令已执行，但端口 %d 仍不可用，日志: %s\n",
           g_rtsp_port, g_mediamtx_log.c_str());
    return false;
}
