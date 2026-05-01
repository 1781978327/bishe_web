// Camera recording management (start/stop/status).
// Extracted from main_http_ctrl.cc during refactoring.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>

#include "http_ctrl_recording.h"
#include "http_ctrl_globals.h"

#ifdef USE_RTSP_MPP
#include "rtsp_mpp_sender.h"
#endif

// -- Forward declarations for utilities still in main_http_ctrl.cc --
extern std::string parse_query_param(const std::string& path, const std::string& key);
extern std::string sanitize_filename_component(const std::string& text);
extern std::string trim_copy(const std::string& input);
extern std::string compact_timestamp_for_filename();
extern std::string local_time_iso8601();
extern bool ensure_directory_tree(const std::string& path);
extern std::string path_join(const std::string& base, const std::string& leaf);
extern std::string resolve_ffmpeg_binary_path();
extern std::string default_record_output_dir();
extern bool start_mediamtx_if_needed(const char* reason, std::string* detail);

#ifdef USE_RTSP_MPP
// Forward declarations from rtsp module
extern void destroy_rtsp_sender(RtspMppSender*& sender);
extern void stop_rtsp_senders_locked();
#endif

std::string record_camera_name(int cam) {
    return cam == 0 ? "cam0" : "cam1";
}

int parse_record_camera_index(const std::string& path) {
    std::string cam_text = parse_query_param(path, "cam");
    if (cam_text.empty()) cam_text = parse_query_param(path, "camera");
    if (!cam_text.empty()) {
        if (cam_text == "0" || cam_text == "cam0") return 0;
        if (cam_text == "1" || cam_text == "cam1") return 1;
    }

    std::string camera_id_text = parse_query_param(path, "cameraId");
    if (!camera_id_text.empty()) {
        char* endptr = nullptr;
        long camera_id = strtol(camera_id_text.c_str(), &endptr, 10);
        if (endptr != camera_id_text.c_str() && *endptr == '\0') {
            if (camera_id == 1) return 0;
            if (camera_id == 2) return 1;
        }
    }
    return -1;
}

std::string recording_rtsp_url_for_camera(int cam) {
    return cam == 0 ? g_rtsp_url_0 : g_rtsp_url_1;
}

void refresh_recording_state_locked(int cam) {
    if (cam < 0 || cam > 1) return;
    CameraRecordingState& state = g_record_states[cam];
    if (state.pid <= 0) {
        state.active = false;
        state.pid = -1;
        return;
    }

    int status = 0;
    pid_t rc = waitpid(state.pid, &status, WNOHANG);
    if (rc == 0) {
        state.active = true;
        return;
    }

    if (rc == state.pid) {
        state.active = false;
        state.pid = -1;
        if (state.stop_requested) {
            state.last_error.clear();
        } else if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code == 0) {
                state.last_error.clear();
            } else {
                state.last_error = "ffmpeg 退出码 " + std::to_string(code);
            }
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            if (sig == SIGINT || sig == SIGTERM) {
                state.last_error.clear();
            } else {
                state.last_error = "ffmpeg 被信号终止: " + std::to_string(sig);
            }
        } else {
            state.last_error = "ffmpeg 已结束";
        }
        state.stop_requested = false;
        return;
    }

    if (rc < 0 && errno == ECHILD) {
        state.active = false;
        state.pid = -1;
        if (state.stop_requested) {
            state.last_error.clear();
        } else if (state.last_error.empty()) {
            state.last_error = "录像进程已结束";
        }
        state.stop_requested = false;
    }
}

void refresh_all_recording_states() {
    std::lock_guard<std::mutex> lock(g_record_mutex);
    refresh_recording_state_locked(0);
    refresh_recording_state_locked(1);
}

std::string build_recording_filename(int cam, const std::string& requested_name) {
    std::string base = sanitize_filename_component(trim_copy(requested_name));
    if (base.empty()) {
        base = record_camera_name(cam) + "_" + compact_timestamp_for_filename();
    }
    const std::string suffix = ".mp4";
    if (base.size() < suffix.size() ||
        base.substr(base.size() - suffix.size()) != suffix) {
        base += suffix;
    }
    return base;
}

bool ensure_camera_rtsp_ready_for_recording(int cam, std::string* detail) {
#ifdef USE_RTSP_MPP
    if (detail) detail->clear();
    if (cam < 0 || cam > 1) {
        if (detail) *detail = "无效的摄像头编号";
        return false;
    }

    std::string mediamtx_detail;
    (void)start_mediamtx_if_needed("/api/record/start", &mediamtx_detail);

    std::lock_guard<std::mutex> lock(g_rtsp_mutex);
    if (g_video_mode.load()) {
        if (detail) *detail = "当前处于视频文件模式，无法录制摄像头";
        return false;
    }

    bool cam0_ok = (g_rtsp_sender0 != nullptr && g_rtsp_sender0->inited());
    bool cam1_ok = (g_rtsp_sender1 != nullptr && g_rtsp_sender1->inited());
    if (!cam0_ok || !cam1_ok) {
        if (!g_rtsp_streaming.load()) {
            stop_rtsp_senders_locked();
        }
        if (!cam0_ok) {
            if (g_rtsp_sender0) {
                delete g_rtsp_sender0;
                g_rtsp_sender0 = nullptr;
            }
            g_rtsp_sender0 = new RtspMppSender();
            if (!g_rtsp_sender0->init(g_rtsp_url_0.c_str(), 640, 480, 30)) {
                delete g_rtsp_sender0;
                g_rtsp_sender0 = nullptr;
                printf("[Record] Cam0 RTSP 启动失败: %s\n", g_rtsp_url_0.c_str());
            } else {
                cam0_ok = true;
                printf("[Record] Cam0 RTSP 已为录像启动\n");
            }
        }
        if (!cam1_ok) {
            if (g_rtsp_sender1) {
                delete g_rtsp_sender1;
                g_rtsp_sender1 = nullptr;
            }
            g_rtsp_sender1 = new RtspMppSender();
            if (!g_rtsp_sender1->init(g_rtsp_url_1.c_str(), 640, 480, 30)) {
                delete g_rtsp_sender1;
                g_rtsp_sender1 = nullptr;
                printf("[Record] Cam1 RTSP 启动失败: %s\n", g_rtsp_url_1.c_str());
            } else {
                cam1_ok = true;
                printf("[Record] Cam1 RTSP 已为录像启动\n");
            }
        }
        if (!cam0_ok && !cam1_ok) {
            g_rtsp_streaming = false;
            if (detail) *detail = "无法为录像启动 RTSP 推流";
            return false;
        }
    }

    g_rtsp_streaming = true;
    bool requested_ok = (cam == 0) ? cam0_ok : cam1_ok;
    if (!requested_ok) {
        if (detail) *detail = "目标摄像头 RTSP 未就绪";
        return false;
    }
    if (detail) *detail = "ready";
    return true;
#else
    (void)cam;
    if (detail) *detail = "当前构建未启用 USE_RTSP_MPP，无法录像";
    return false;
#endif
}

bool start_camera_recording(int cam, const std::string& requested_name,
                            std::string* out_message, int* out_status_code) {
    if (out_message) out_message->clear();
    if (out_status_code) *out_status_code = 200;

    if (cam < 0 || cam > 1) {
        if (out_message) *out_message = "无效的 cam 参数，支持 0 或 1";
        if (out_status_code) *out_status_code = 400;
        return false;
    }

    std::string rtsp_detail;
    if (!ensure_camera_rtsp_ready_for_recording(cam, &rtsp_detail)) {
        if (out_message) *out_message = rtsp_detail.empty() ? "录像前 RTSP 未就绪" : rtsp_detail;
        if (out_status_code) *out_status_code = 409;
        return false;
    }

    std::string ffmpeg_bin = resolve_ffmpeg_binary_path();
    if (ffmpeg_bin.empty()) {
        if (out_message) *out_message = "未找到可执行的 ffmpeg";
        if (out_status_code) *out_status_code = 500;
        return false;
    }

    std::string output_dir = g_record_output_dir.empty() ? default_record_output_dir() : g_record_output_dir;
    if (!ensure_directory_tree(output_dir)) {
        if (out_message) *out_message = "无法创建录像输出目录: " + output_dir;
        if (out_status_code) *out_status_code = 500;
        return false;
    }

    std::string filename = build_recording_filename(cam, requested_name);
    std::string output_path = path_join(output_dir, filename);
    std::string log_path = output_path + ".log";
    std::string rtsp_url = recording_rtsp_url_for_camera(cam);

    {
        std::lock_guard<std::mutex> lock(g_record_mutex);
        refresh_recording_state_locked(cam);
        if (g_record_states[cam].active) {
            if (out_message) {
                *out_message = record_camera_name(cam) + " 已在录制: " + g_record_states[cam].output_path;
            }
            if (out_status_code) *out_status_code = 409;
            return false;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (out_message) *out_message = std::string("fork 失败: ") + strerror(errno);
        if (out_status_code) *out_status_code = 500;
        return false;
    }

    if (pid == 0) {
        int stdin_fd = open("/dev/null", O_RDONLY);
        if (stdin_fd >= 0) {
            dup2(stdin_fd, STDIN_FILENO);
            close(stdin_fd);
        }

        int log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        execlp(ffmpeg_bin.c_str(),
               ffmpeg_bin.c_str(),
               "-nostdin",
               "-y",
               "-rtsp_transport", "tcp",
               "-i", rtsp_url.c_str(),
               "-c", "copy",
               "-movflags", "+faststart",
               output_path.c_str(),
               (char*)nullptr);
        _exit(127);
    }

    {
        std::lock_guard<std::mutex> lock(g_record_mutex);
        CameraRecordingState& state = g_record_states[cam];
        state.pid = pid;
        state.active = true;
        state.stop_requested = false;
        state.output_path = output_path;
        state.log_path = log_path;
        state.started_at = local_time_iso8601();
        state.last_error.clear();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard<std::mutex> lock(g_record_mutex);
        refresh_recording_state_locked(cam);
        if (!g_record_states[cam].active) {
            if (out_message) {
                *out_message = g_record_states[cam].last_error.empty()
                                   ? ("录像启动失败，请查看日志: " + log_path)
                                   : (g_record_states[cam].last_error + "，日志: " + log_path);
            }
            if (out_status_code) *out_status_code = 500;
            return false;
        }
    }

    if (out_message) {
        *out_message = record_camera_name(cam) + " 开始录像: " + output_path;
    }
    return true;
}

bool stop_camera_recording(int cam, std::string* out_message, int* out_status_code) {
    if (out_message) out_message->clear();
    if (out_status_code) *out_status_code = 200;

    if (cam < 0 || cam > 1) {
        if (out_message) *out_message = "无效的 cam 参数，支持 0 或 1";
        if (out_status_code) *out_status_code = 400;
        return false;
    }

    pid_t pid = -1;
    {
        std::lock_guard<std::mutex> lock(g_record_mutex);
        refresh_recording_state_locked(cam);
        CameraRecordingState& state = g_record_states[cam];
        if (!state.active || state.pid <= 0) {
            if (out_message) *out_message = record_camera_name(cam) + " 当前未在录制";
            return true;
        }
        state.stop_requested = true;
        pid = state.pid;
    }

    if (kill(pid, SIGINT) != 0 && errno != ESRCH) {
        if (out_message) *out_message = std::string("停止录像失败: ") + strerror(errno);
        if (out_status_code) *out_status_code = 500;
        return false;
    }

    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard<std::mutex> lock(g_record_mutex);
        refresh_recording_state_locked(cam);
        if (!g_record_states[cam].active) {
            if (out_message) *out_message = record_camera_name(cam) + " 已停止录像";
            return true;
        }
    }

    if (kill(pid, SIGTERM) == 0 || errno == ESRCH) {
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::lock_guard<std::mutex> lock(g_record_mutex);
            refresh_recording_state_locked(cam);
            if (!g_record_states[cam].active) {
                if (out_message) *out_message = record_camera_name(cam) + " 已停止录像";
                return true;
            }
        }
    }

    if (kill(pid, SIGKILL) == 0 || errno == ESRCH) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    {
        std::lock_guard<std::mutex> lock(g_record_mutex);
        refresh_recording_state_locked(cam);
        if (!g_record_states[cam].active) {
            if (out_message) *out_message = record_camera_name(cam) + " 已强制停止录像";
            return true;
        }
    }

    if (out_message) *out_message = record_camera_name(cam) + " 停止录像超时";
    if (out_status_code) *out_status_code = 500;
    return false;
}

void stop_all_recordings() {
    for (int cam = 0; cam < 2; ++cam) {
        std::string ignore_msg;
        int ignore_status = 200;
        (void)stop_camera_recording(cam, &ignore_msg, &ignore_status);
    }
}
