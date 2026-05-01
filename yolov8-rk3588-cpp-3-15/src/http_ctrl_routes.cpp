// HTTP route handlers and dispatcher for the vision (http_ctrl) service.
// Extracted from main_http_ctrl.cc for the vision service refactoring.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <limits.h>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cctype>
#include <cstdint>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#ifdef USE_RTSP_MPP
#include <rockchip/mpp_buffer.h>
#include "rtsp_mpp_sender.h"
#include "ffmpeg_rkmpp_reader.h"
#endif

#include "rknnPool.hpp"
#include "postprocess.h"

#include "http_ctrl_globals.h"
#include "http_ctrl_web_utils.h"
#include "http_ctrl_utils.h"
#include "http_ctrl_alerts.h"
#include "http_ctrl_routes.h"

// ---- Forward declarations for functions not yet in dedicated headers ----
// TODO: extract these into proper headers when their modules are refactored.

// From http_ctrl_model (not yet extracted)
bool ensure_model_loaded(std::string* msg_out = nullptr);
bool unload_model_runtime(std::string* msg_out = nullptr);

// From http_ctrl_recording (not yet extracted)
void refresh_recording_state_locked(int cam);
bool start_camera_recording(int cam, const std::string& requested_name, std::string* out_message, int* out_status_code);
bool stop_camera_recording(int cam, std::string* out_message, int* out_status_code);
int parse_record_camera_index(const std::string& path);

// From http_ctrl_video (not yet extracted)
void start_video_reader(const std::string& path, bool loop);
void stop_video_reader();

// From http_ctrl_rtsp (not yet extracted)
#ifdef USE_RTSP_MPP
void stop_rtsp_senders_locked();
#endif

// From http_ctrl_mediamtx (not yet extracted)
bool start_mediamtx_if_needed(const char* reason, std::string* detail = nullptr);

// From http_ctrl_tracker_draw (not yet extracted)
void draw_tracker_boxes(cv::Mat& img, const std::vector<TrackerResultItem>& tracks, int slot_idx);

// From http_ctrl_raw_video_rtsp (USE_RTSP_MPP)
#ifdef USE_RTSP_MPP
void stop_video_rtsp_raw_thread();
void start_video_rtsp_raw_thread_if_needed();
bool ensure_video_source_open_locked();
#endif

// =====================================================================
// Route: GET /api/status
// =====================================================================

void handle_route_status(int client_fd) {
    std::string model_path;
    std::string label_path;
    std::string model_error;
    bool mediamtx_running = is_tcp_service_ready(g_rtsp_host, g_rtsp_port, 150);
    std::string tracker_backend = rknn_lite::get_tracker_backend_name();
    std::string tracker_reid_model = rknn_lite::resolve_tracker_reid_model();
    int tracker_skip_frames = rknn_lite::get_deepsort_skip_frames();
    bool forbidden_cam0_loaded = false;
    bool forbidden_cam1_loaded = false;
    CameraRecordingState record_cam0;
    CameraRecordingState record_cam1;
    {
        std::lock_guard<std::mutex> lock(g_model_mutex);
        model_path = g_model_path;
        label_path = g_label_path;
        model_error = g_model_error;
    }
    {
        std::lock_guard<std::mutex> lock(g_forbidden_area_mutex);
        forbidden_cam0_loaded = g_forbidden_area_cache[0].valid;
        forbidden_cam1_loaded = g_forbidden_area_cache[1].valid;
    }
    {
        std::lock_guard<std::mutex> lock(g_record_mutex);
        refresh_recording_state_locked(0);
        refresh_recording_state_locked(1);
        record_cam0 = g_record_states[0];
        record_cam1 = g_record_states[1];
    }
    std::string active_label_path = label_path.empty() ? default_label_path() : label_path;
    std::ostringstream oss;
    oss << "{";
    oss << "\"running\":" << (g_running ? "true" : "false") << ",";
    oss << "\"inference_enabled\":" << (g_inference_enabled ? "true" : "false") << ",";
    oss << "\"model_loaded\":" << (g_model_loaded.load() ? "true" : "false") << ",";
    oss << "\"model_loading\":" << (g_model_loading.load() ? "true" : "false") << ",";
    oss << "\"model_switching\":" << (g_model_switching.load() ? "true" : "false") << ",";
    oss << "\"active_jobs\":" << g_active_jobs.load() << ",";
    oss << "\"model_path\":\"" << json_escape(model_path) << "\",";
    oss << "\"label_path\":\"" << json_escape(active_label_path) << "\",";
    oss << "\"rtsp_url_cam0\":\"" << json_escape(g_rtsp_url_0) << "\",";
    oss << "\"rtsp_url_cam1\":\"" << json_escape(g_rtsp_url_1) << "\",";
    oss << "\"rtsp_url_mosaic\":\"" << json_escape(g_rtsp_url_mosaic) << "\",";
    oss << "\"rtsp_url_video\":\"" << json_escape(g_rtsp_url_video) << "\",";
    oss << "\"input_source_cam0\":\"" << json_escape(g_input_source_cam0) << "\",";
    oss << "\"input_source_cam1\":\"" << json_escape(g_input_source_cam1) << "\",";
    oss << "\"input_source_cam0_dmabuf\":" << (g_input_source_cam0_dmabuf ? "true" : "false") << ",";
    oss << "\"input_source_cam1_dmabuf\":" << (g_input_source_cam1_dmabuf ? "true" : "false") << ",";
    oss << "\"mediamtx_auto_start\":" << (g_mediamtx_auto_start ? "true" : "false") << ",";
    oss << "\"mediamtx_bin\":\"" << json_escape(g_mediamtx_bin) << "\",";
    oss << "\"mediamtx_log\":\"" << json_escape(g_mediamtx_log) << "\",";
    oss << "\"mediamtx_running\":" << (mediamtx_running ? "true" : "false") << ",";
    oss << "\"record_output_dir\":\"" << json_escape(g_record_output_dir) << "\",";
    oss << "\"record_ffmpeg_bin\":\"" << json_escape(g_ffmpeg_bin) << "\",";
    oss << "\"recording_cam0\":" << (record_cam0.active ? "true" : "false") << ",";
    oss << "\"recording_cam1\":" << (record_cam1.active ? "true" : "false") << ",";
    oss << "\"recording_cam0_file\":\"" << json_escape(record_cam0.output_path) << "\",";
    oss << "\"recording_cam1_file\":\"" << json_escape(record_cam1.output_path) << "\",";
    oss << "\"recording_cam0_log\":\"" << json_escape(record_cam0.log_path) << "\",";
    oss << "\"recording_cam1_log\":\"" << json_escape(record_cam1.log_path) << "\",";
    oss << "\"recording_cam0_started_at\":\"" << json_escape(record_cam0.started_at) << "\",";
    oss << "\"recording_cam1_started_at\":\"" << json_escape(record_cam1.started_at) << "\",";
    oss << "\"recording_cam0_error\":\"" << json_escape(record_cam0.last_error) << "\",";
    oss << "\"recording_cam1_error\":\"" << json_escape(record_cam1.last_error) << "\",";
    oss << "\"model_error\":\"" << json_escape(model_error) << "\",";
    oss << "\"current_camera\":" << g_current_cam.load() << ",";
    oss << "\"fps\":" << (g_cam0_fps.load() + g_cam1_fps.load()) << ",";
    oss << "\"fps_cam0\":" << g_cam0_fps.load() << ",";
    oss << "\"fps_cam1\":" << g_cam1_fps.load() << ",";
    oss << "\"fps_rtsp_mosaic\":" << g_rtsp_mosaic_fps.load() << ",";
    oss << "\"tracker_enabled\":" << (g_tracker_enabled ? "true" : "false") << ",";
    oss << "\"tracker_backend\":\"" << json_escape(tracker_backend) << "\",";
    oss << "\"tracker_reid_model\":\"" << json_escape(tracker_reid_model) << "\",";
    oss << "\"tracker_skip_frames\":" << tracker_skip_frames << ",";
    oss << "\"detection_count_cam0\":" << g_cam0_detection_count.load() << ",";
    oss << "\"detection_count_cam1\":" << g_cam1_detection_count.load() << ",";
    oss << "\"threshold\":" << rknn_lite::get_detection_threshold() << ",";
    oss << "\"box_count_alert_threshold\":" << g_box_count_alert_threshold.load() << ",";
    oss << "\"box_count_alert_cooldown_ms\":" << g_box_count_alert_cooldown_ms.load() << ",";
    oss << "\"forbidden_area_path\":\"" << json_escape(g_forbidden_area_path) << "\",";
    oss << "\"forbidden_area_fetch_interval_ms\":" << g_forbidden_area_fetch_interval_ms.load() << ",";
    oss << "\"forbidden_area_fetch_timeout_ms\":" << g_forbidden_area_fetch_timeout_ms.load() << ",";
    oss << "\"intrusion_alert_cooldown_ms\":" << g_intrusion_alert_cooldown_ms.load() << ",";
    oss << "\"forbidden_area_cam0_loaded\":" << (forbidden_cam0_loaded ? "true" : "false") << ",";
    oss << "\"forbidden_area_cam1_loaded\":" << (forbidden_cam1_loaded ? "true" : "false") << ",";
    oss << "\"video_mode\":" << (g_video_mode ? "true" : "false");
#ifdef USE_RTSP_MPP
    oss << ",\"rtsp_streaming\":" << (g_rtsp_streaming ? "true" : "false");
#else
    oss << ",\"rtsp_streaming\":false";
#endif
    oss << "}";
    send_response(client_fd, oss.str(), "application/json");
}

// =====================================================================
// Route: POST /api/threshold/set
// =====================================================================

void handle_route_threshold_set(int client_fd, const std::string& path) {
    // 设置检测阈值: /api/threshold/set?value=0.6&box_count=3
    float value = rknn_lite::get_detection_threshold();
    std::string value_text = parse_query_param(path, "value");
    if (!value_text.empty()) {
        try {
            value = std::stof(value_text);
        } catch (...) {
            send_response(client_fd,
                build_json_response("error", "无效的 value 参数，必须是 0~1 浮点数"),
                "application/json", 400);
            return;
        }
    }
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    g_confidence_threshold.store(value);
    rknn_lite::set_detection_threshold(value);

    bool has_box_count_param =
        (path.find("box_count=") != std::string::npos) ||
        (path.find("boxCount=") != std::string::npos);
    if (has_box_count_param) {
        std::string box_count_text = parse_query_param(path, "box_count");
        if (box_count_text.empty()) {
            box_count_text = parse_query_param(path, "boxCount");
        }
        char* endptr = nullptr;
        long parsed = strtol(box_count_text.c_str(), &endptr, 10);
        if (box_count_text.empty() || endptr == box_count_text.c_str() || *endptr != '\0' || parsed < 0) {
            send_response(client_fd,
                build_json_response("error", "无效的 box_count 参数，必须是大于等于 0 的整数"),
                "application/json", 400);
            return;
        }
        if (parsed > 10000) parsed = 10000;
        g_box_count_alert_threshold.store((int)parsed);
    }

    std::ostringstream msg;
    msg << "阈值已设置为: " << value;
    if (has_box_count_param) {
        msg << ", 框数量阈值: " << g_box_count_alert_threshold.load();
    }
    send_response(client_fd, build_json_response("success", msg.str()), "application/json");
}

// =====================================================================
// Route: GET /api/threshold/get
// =====================================================================

void handle_route_threshold_get(int client_fd) {
    // 获取当前阈值（置信度 + 框数量告警阈值）
    std::ostringstream oss;
    oss << "{";
    oss << "\"threshold\":" << rknn_lite::get_detection_threshold() << ",";
    oss << "\"box_count_threshold\":" << g_box_count_alert_threshold.load();
    oss << "}";
    send_response(client_fd, oss.str(), "application/json");
}

// =====================================================================
// Route: GET /api/detection/count
// =====================================================================

void handle_route_detection_count(int client_fd, const std::string& path) {
    // 获取指定摄像头的检测数量: /api/detection/count?cam=0
    int cam = 0;
    size_t pos = path.find("cam=");
    if (pos != std::string::npos) {
        cam = std::stoi(path.substr(pos + 4));
    }
    if (cam < 0) cam = 0;
    if (cam >= 2) cam = 0;  // 只支持 0 和 1
    int count = (cam == 0) ? g_cam0_detection_count.load() : g_cam1_detection_count.load();
    std::ostringstream oss;
    oss << "{\"cam\":" << cam << ",\"count\":" << count << "}";
    send_response(client_fd, oss.str(), "application/json");
}

// =====================================================================
// Route: GET /api/frame
// =====================================================================

void handle_route_frame(int client_fd, const std::string& path) {
    // 解析参数: /api/frame?track=1&redraw=1&cam=0
    bool want_tracker = g_tracker_enabled.load();
    if (path.find("track=1") != std::string::npos || path.find("track=ON") != std::string::npos) {
        want_tracker = true;
    } else if (path.find("track=0") != std::string::npos || path.find("track=OFF") != std::string::npos) {
        want_tracker = false;
    }
    // 默认不二次绘制：推理线程已经在 ori_img 上画过框，二次叠加会出现"一人双框"
    bool force_redraw = (path.find("redraw=1") != std::string::npos || path.find("overlay=1") != std::string::npos);
    int requested_cam = -1;
    std::string cam_text = parse_query_param(path, "cam");
    if (!cam_text.empty()) {
        char* endptr = nullptr;
        long parsed = strtol(cam_text.c_str(), &endptr, 10);
        if (endptr != cam_text.c_str() && *endptr == '\0' && parsed >= 0 && parsed <= 1) {
            requested_cam = (int)parsed;
        }
    }
    if (requested_cam < 0) {
        std::string camera_id_text = parse_query_param(path, "cameraId");
        if (!camera_id_text.empty()) {
            char* endptr = nullptr;
            long camera_id = strtol(camera_id_text.c_str(), &endptr, 10);
            if (endptr != camera_id_text.c_str() && *endptr == '\0') {
                if (camera_id == 1) requested_cam = 0;
                else if (camera_id == 2) requested_cam = 1;
            }
        }
    }

    cv::Mat frame_copy;
    bool available = false;
    int frame_slot = -1;
    if (requested_cam >= 0 && requested_cam <= 1) {
        std::lock_guard<std::mutex> lock(g_latest_frame_cam_mutex[requested_cam]);
        if (!g_latest_frame_cam[requested_cam].empty()) {
            frame_copy = g_latest_frame_cam[requested_cam].clone();
            available = true;
            frame_slot = g_latest_frame_slot_cam[requested_cam];
        }
    }
    if (!available) {
        std::lock_guard<std::mutex> lock(g_latest_frame_mutex);
        if (!g_latest_frame.empty()) {
            frame_copy = g_latest_frame.clone();
            available = true;
            frame_slot = g_latest_frame_slot.load();
        }
    }

    if (available && !frame_copy.empty()) {
        // 如果启用了跟踪，可选二次绘制（仅用于调试）。默认关闭，避免叠加双框。
        if (want_tracker && force_redraw) {
            std::vector<TrackerResultItem> tracks_for_frame;
            {
                std::lock_guard<std::mutex> lock(g_tracker_mutex);
                if (frame_slot >= 0 && frame_slot < (int)g_tracker_results.size()) {
                    tracks_for_frame = g_tracker_results[frame_slot];
                }
            }
            if (!tracks_for_frame.empty()) {
                draw_tracker_boxes(frame_copy, tracks_for_frame, frame_slot);
            }

            static int redraw_log_counter = 0;
            if ((redraw_log_counter++ % 120) == 0) {
                printf("[FrameAPI] redraw=1, slot=%d, tracks=%zu (注意：可能与推理线程已有框叠加)\n",
                       frame_slot, tracks_for_frame.size());
            }
        } else if (want_tracker && !force_redraw) {
            static int skip_redraw_log_counter = 0;
            if ((skip_redraw_log_counter++ % 600) == 0) {
                printf("[FrameAPI] 跳过二次绘制（避免双框）。如需调试叠加，请使用 /api/frame?track=1&redraw=1\n");
            }
        }

        std::vector<uchar> buf;
        cv::imencode(".jpg", frame_copy, buf, {cv::IMWRITE_JPEG_QUALITY, 85});
        std::string content(buf.begin(), buf.end());
        send_response(client_fd, content, "image/jpeg");
    } else {
        send_response(client_fd, build_json_response("error", "暂无帧数据"), "application/json", 503);
    }
}

// =====================================================================
// Route: GET /api/tracker  and  POST /api/tracker/{0|1}
// =====================================================================

void handle_route_tracker(int client_fd, const std::string& route, const std::string& method) {
    if (method == "GET") {
        // 获取/设置跟踪状态
        send_response(client_fd, build_json_response("success",
            std::string("跟踪状态: ") + (g_tracker_enabled ? "开启" : "关闭") +
            ", 算法: " + rknn_lite::get_tracker_backend_name()), "application/json");
    } else if (method == "POST" && route.find("/api/tracker/") == 0) {
        // /api/tracker/0 关闭跟踪 /api/tracker/1 开启跟踪
        std::string val = route.substr(13);
        bool enable = (val == "1" || val == "on" || val == "ON");
        g_tracker_enabled = enable;
        printf("[Tracker] 跟踪已%s\n", enable ? "开启" : "关闭");
        send_response(client_fd, build_json_response("success",
            std::string("跟踪已") + (enable ? "开启" : "关闭")), "application/json");
    }
}

// =====================================================================
// Route: POST /api/inference/on
// =====================================================================

void handle_route_inference_on(int client_fd, const std::string& path) {
    // /api/inference/on?track=0 或 /api/inference/on?track=1
    // 可选:
    //   /api/inference/on?model=../model/RK3588/yolov8s.rknn
    //   /api/inference/on?labels=/path/to/labels.txt
    if (g_model_switching.load()) {
        send_response(client_fd, build_json_response("error", "模型切换中，请稍后重试"), "application/json", 409);
        return;
    }

    // 默认开启跟踪，除非明确指定 track=0
    bool enable_tracker = true;
    if (path.find("track=0") != std::string::npos || path.find("track=OFF") != std::string::npos) {
        enable_tracker = false;
    }

    std::string requested_model = parse_query_param(path, "model");
    bool has_labels_param = path.find("labels=") != std::string::npos;
    std::string requested_labels = parse_query_param(path, "labels");
    bool has_tracker_param = path.find("tracker=") != std::string::npos;
    std::string requested_tracker = has_tracker_param
        ? parse_query_param(path, "tracker")
        : rknn_lite::get_tracker_backend_name();
    TrackerBackend requested_backend;
    if (!parse_tracker_backend_name(requested_tracker, &requested_backend)) {
        send_response(client_fd,
            build_json_response("error", "无效的 tracker 参数，支持 bytetrack 或 deepsort"),
            "application/json", 400);
        return;
    }
    bool has_reid_param = (path.find("reid_model=") != std::string::npos) ||
                          (path.find("reid=") != std::string::npos);
    std::string requested_reid = parse_query_param(path, "reid_model");
    if (requested_reid.empty() && path.find("reid=") != std::string::npos) {
        requested_reid = parse_query_param(path, "reid");
    }
    bool has_tracker_skip_param = (path.find("tracker_skip=") != std::string::npos) ||
                                  (path.find("deepsort_skip=") != std::string::npos);
    std::string requested_skip_text = parse_query_param(path, "tracker_skip");
    if (requested_skip_text.empty() && path.find("deepsort_skip=") != std::string::npos) {
        requested_skip_text = parse_query_param(path, "deepsort_skip");
    }
    int requested_skip_frames = rknn_lite::get_deepsort_skip_frames();
    if (has_tracker_skip_param) {
        char* endptr = nullptr;
        long parsed = strtol(requested_skip_text.c_str(), &endptr, 10);
        if (requested_skip_text.empty() || endptr == requested_skip_text.c_str() || *endptr != '\0' || parsed < 0) {
            send_response(client_fd,
                build_json_response("error", "无效的 tracker_skip 参数，必须是大于等于 0 的整数"),
                "application/json", 400);
            return;
        }
        requested_skip_frames = (int)parsed;
    }
    if (requested_labels == "default" || requested_labels == "DEFAULT") {
        requested_labels.clear();
    }
    if (requested_reid == "default" || requested_reid == "DEFAULT") {
        requested_reid.clear();
    }
    if (!requested_model.empty() && !file_exists(requested_model)) {
        send_response(client_fd,
            build_json_response("error", "模型文件不存在或不可读: " + requested_model),
            "application/json", 400);
        return;
    }
    if (has_labels_param && !requested_labels.empty() && !file_exists(requested_labels)) {
        send_response(client_fd,
            build_json_response("error", "标签文件不存在或不可读: " + requested_labels),
            "application/json", 400);
        return;
    }
    if (has_reid_param && !requested_reid.empty() && !file_exists(requested_reid)) {
        send_response(client_fd,
            build_json_response("error", "DeepSORT ReID 模型不存在或不可读: " + requested_reid),
            "application/json", 400);
        return;
    }
    if (requested_backend == TrackerBackend::DeepSort) {
        std::string resolved_reid = has_reid_param ? requested_reid : rknn_lite::resolve_tracker_reid_model();
        if (resolved_reid.empty()) {
            send_response(client_fd,
                build_json_response("error", "未找到可用的 DeepSORT ReID 模型，请传入 reid_model 参数"),
                "application/json", 400);
            return;
        }
        if (!file_exists(resolved_reid)) {
            send_response(client_fd,
                build_json_response("error", "DeepSORT ReID 模型不存在或不可读: " + resolved_reid),
                "application/json", 400);
            return;
        }
    }

    if (!requested_model.empty() || has_labels_param || has_tracker_param || has_reid_param || has_tracker_skip_param) {
        std::string current_model;
        std::string current_labels;
        std::string current_tracker;
        std::string current_reid;
        bool loaded = false;
        {
            std::lock_guard<std::mutex> lock(g_model_mutex);
            loaded = g_model_loaded.load();
            current_model = g_model_path;
            current_labels = g_label_path;
        }
        current_tracker = rknn_lite::get_tracker_backend_name();
        current_reid = rknn_lite::get_tracker_reid_model_override();
        int current_skip = rknn_lite::get_deepsort_skip_frames();

        bool model_changed = (!requested_model.empty() && requested_model != current_model);
        bool labels_changed = (has_labels_param && requested_labels != current_labels);
        bool tracker_changed = (has_tracker_param && requested_tracker != current_tracker);
        bool reid_changed = (has_reid_param && requested_reid != current_reid);
        bool skip_changed = (has_tracker_skip_param && requested_skip_frames != current_skip);

        if (loaded && (model_changed || labels_changed || tracker_changed || reid_changed)) {
            if (g_inference_enabled.load()) {
                send_response(client_fd,
                    build_json_response("error", "请先调用 /api/inference/off 关闭推理后再切换模型/标签/跟踪算法"),
                    "application/json", 409);
                return;
            }
            std::string unload_msg;
            if (!unload_model_runtime(&unload_msg)) {
                send_response(client_fd, build_json_response("error", unload_msg), "application/json", 500);
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_model_mutex);
            if (!requested_model.empty()) {
                g_model_path = requested_model;
            }
            if (has_labels_param) {
                g_label_path = requested_labels;
            }
            g_model_error.clear();
        }
        if (tracker_changed) {
            rknn_lite::set_tracker_backend(requested_tracker);
        }
        if (reid_changed) {
            rknn_lite::set_tracker_reid_model_override(requested_reid);
        }
        if (skip_changed) {
            rknn_lite::set_deepsort_skip_frames(requested_skip_frames);
        }
    }

    std::string label_override;
    {
        std::lock_guard<std::mutex> lock(g_model_mutex);
        label_override = g_label_path;
    }
    rknn_lite::set_label_file_override(label_override);

    std::string load_msg;
    if (!g_model_loaded.load()) {
#ifdef USE_RTSP_MPP
        stop_video_rtsp_raw_thread();
#endif
        if (!ensure_model_loaded(&load_msg)) {
            g_inference_enabled = false;
            g_tracker_enabled = false;
            send_response(client_fd, build_json_response("error", load_msg), "application/json", 500);
            return;
        }
    }

    std::string active_model;
    std::string active_labels;
    std::string active_tracker = rknn_lite::get_tracker_backend_name();
    std::string active_reid = rknn_lite::resolve_tracker_reid_model();
    int active_skip_frames = rknn_lite::get_deepsort_skip_frames();
    {
        std::lock_guard<std::mutex> lock(g_model_mutex);
        active_model = g_model_path;
        active_labels = g_label_path.empty() ? default_label_path() : g_label_path;
    }

    g_inference_enabled = true;
    g_tracker_enabled = enable_tracker;
    printf("[Inference] 推理已开启, 跟踪: %s, 算法: %s, skip=%d, 模型: %s, 标签: %s, ReID: %s\n",
           enable_tracker ? "开启" : "关闭",
           active_tracker.c_str(),
           active_skip_frames,
           active_model.c_str(),
           active_labels.c_str(),
           active_reid.empty() ? "(none)" : active_reid.c_str());
    send_response(client_fd, build_json_response("success",
        std::string("推理已开启, 跟踪: ") + (enable_tracker ? "开启" : "关闭") +
        ", 算法: " + active_tracker +
        ", skip: " + std::to_string(active_skip_frames) +
        ", 模型: " + active_model +
        ", 标签: " + active_labels +
        (active_tracker == "deepsort" ? ", ReID: " + active_reid : "")), "application/json");
}

// =====================================================================
// Route: POST /api/inference/off
// =====================================================================

void handle_route_inference_off(int client_fd, const std::string& path) {
    bool unload_requested =
        (path.find("unload=1") != std::string::npos) ||
        (path.find("unload=true") != std::string::npos) ||
        (path.find("unload=ON") != std::string::npos);
    std::string requested_model = parse_query_param(path, "model");
    bool has_labels_param = path.find("labels=") != std::string::npos;
    std::string requested_labels = parse_query_param(path, "labels");
    if (requested_labels == "default" || requested_labels == "DEFAULT") {
        requested_labels.clear();
    }

    if (!requested_model.empty() && !file_exists(requested_model)) {
        send_response(client_fd,
            build_json_response("error", "模型文件不存在或不可读: " + requested_model),
            "application/json", 400);
        return;
    }
    if (has_labels_param && !requested_labels.empty() && !file_exists(requested_labels)) {
        send_response(client_fd,
            build_json_response("error", "标签文件不存在或不可读: " + requested_labels),
            "application/json", 400);
        return;
    }

    g_inference_enabled = false;
    g_tracker_enabled = false;
    // 清空跟踪结果，停止显示检测框
    {
        std::lock_guard<std::mutex> lock(g_tracker_mutex);
        for (auto& tracks : g_tracker_results) {
            tracks.clear();
        }
    }

    std::string message = "推理已关闭";
    if (unload_requested || !requested_model.empty() || has_labels_param) {
        std::string unload_msg;
        if (!unload_model_runtime(&unload_msg)) {
            send_response(client_fd, build_json_response("error", unload_msg), "application/json", 500);
            return;
        }
        message = "推理已关闭，模型已卸载";
    }

    if (!requested_model.empty()) {
        {
            std::lock_guard<std::mutex> lock(g_model_mutex);
            g_model_path = requested_model;
            g_model_error.clear();
        }
        message += "，下次将加载: " + requested_model;
    }
    if (has_labels_param) {
        std::string active_label;
        {
            std::lock_guard<std::mutex> lock(g_model_mutex);
            g_label_path = requested_labels;
            g_model_error.clear();
            active_label = g_label_path.empty() ? default_label_path() : g_label_path;
        }
        rknn_lite::set_label_file_override(requested_labels);
        message += "，标签: " + active_label;
    }

    printf("[Inference] 推理已关闭\n");
    send_response(client_fd, build_json_response("success", message), "application/json");
}

// =====================================================================
// Route: POST /api/camera/{id}
// =====================================================================

void handle_route_camera_switch(int client_fd, int camera_id) {
    if (camera_id >= 0 && camera_id <= 2) {
        g_current_cam = camera_id;
        send_response(client_fd, build_json_response("success", "已切换到摄像头 " + std::to_string(camera_id)), "application/json");
    } else {
        send_response(client_fd, build_json_response("error", "无效的摄像头ID"), "application/json", 400);
    }
}

// =====================================================================
// Routes: RTSP (USE_RTSP_MPP)
// =====================================================================

#ifdef USE_RTSP_MPP

// Mosaic constants used by RTSP routes
constexpr int MOSAIC_TILE_WIDTH = 640;
constexpr int MOSAIC_TILE_HEIGHT = 480;
constexpr int MOSAIC_OUTPUT_WIDTH = MOSAIC_TILE_WIDTH * 2;
constexpr int MOSAIC_OUTPUT_HEIGHT = MOSAIC_TILE_HEIGHT;
constexpr int MOSAIC_TARGET_FPS = 30;

void handle_route_rtsp_start(int client_fd) {
    std::string mediamtx_detail;
    (void)start_mediamtx_if_needed("/api/rtsp/start", &mediamtx_detail);
    std::lock_guard<std::mutex> lock(g_rtsp_mutex);
    printf("[RTSP] /api/rtsp/start 请求: streaming=%d video_mode=%d sender0=%p sender1=%p senderM=%p senderV=%p rss=%ldKB\n",
           g_rtsp_streaming.load() ? 1 : 0,
           g_video_mode.load() ? 1 : 0,
           (void*)g_rtsp_sender0, (void*)g_rtsp_sender1, (void*)g_rtsp_sender_mosaic, (void*)g_rtsp_sender_video,
           get_process_rss_kb());
    if (g_video_mode.load() && g_rtsp_streaming.load() &&
        g_rtsp_sender_video && g_rtsp_sender_video->inited()) {
        send_response(client_fd, build_json_response("success", "视频 RTSP 已在运行"), "application/json");
        return;
    }
    if (!g_video_mode.load() && g_rtsp_streaming.load() &&
        g_rtsp_sender0 && g_rtsp_sender0->inited() &&
        g_rtsp_sender1 && g_rtsp_sender1->inited() &&
        g_rtsp_sender_mosaic && g_rtsp_sender_mosaic->inited()) {
        send_response(client_fd, build_json_response("success", "摄像头 RTSP 已在运行"), "application/json");
        return;
    }

    // 每次从停止态重新开启时，先重建推流器，避免复用已断开的 socket
    if (!g_rtsp_streaming.load()) {
        printf("[RTSP] 重新启动前清理旧 sender\n");
        stop_rtsp_senders_locked();
    }

    if (g_video_mode.load()) {
        // 视频模式：创建视频推流器
        int vw = g_video_width.load();
        int vh = g_video_height.load();
        int vf = g_video_fps.load();
        {
            std::lock_guard<std::mutex> video_lock(g_video_mutex);
            if (g_video_running.load() && g_video_mode.load()) {
                (void)ensure_video_source_open_locked();
            }
            if (g_video_hw_enabled.load() && g_video_hw_reader && g_video_hw_reader->IsOpen()) {
                if (g_video_hw_reader->Width() > 0) vw = g_video_hw_reader->Width();
                if (g_video_hw_reader->Height() > 0) vh = g_video_hw_reader->Height();
                if (g_video_hw_reader->Fps() > 0) vf = (int)(g_video_hw_reader->Fps() + 0.5);
            } else
            if (g_video_cap.isOpened()) {
                int cap_w = (int)g_video_cap.get(cv::CAP_PROP_FRAME_WIDTH);
                int cap_h = (int)g_video_cap.get(cv::CAP_PROP_FRAME_HEIGHT);
                int cap_fps = (int)(g_video_cap.get(cv::CAP_PROP_FPS) + 0.5);
                if (cap_w > 0) vw = cap_w;
                if (cap_h > 0) vh = cap_h;
                if (cap_fps > 0) vf = cap_fps;
            }
        }
        if (!g_rtsp_sender_video || !g_rtsp_sender_video->inited()) {
            if (g_rtsp_sender_video) {
                delete g_rtsp_sender_video;
                g_rtsp_sender_video = nullptr;
            }
            g_rtsp_sender_video = new RtspMppSender();
            if (!g_rtsp_sender_video->init(g_rtsp_url_video.c_str(), vw, vh, vf > 0 ? vf : 25)) {
                delete g_rtsp_sender_video;
                g_rtsp_sender_video = nullptr;
                printf("[RTSP] 视频推流启动失败\n");
                send_response(client_fd, build_json_response("error", "视频 RTSP 启动失败"), "application/json", 500);
                return;
            }
            printf("[RTSP] 视频推流已启动 (%dx%d @ %dfps) -> %s\n", vw, vh, vf > 0 ? vf : 25, g_rtsp_url_video.c_str());
        }
        g_rtsp_streaming = true;
        send_response(client_fd, build_json_response("success", "视频 RTSP 推流已启动"), "application/json");
        return;
    } else {
        // 摄像头模式：创建摄像头推流器
        // 摄像头采集端设置为 30fps，这里保持一致，避免推送时基与实际送帧速率不一致导致播放器卡顿感。
        int cam0_w = 640, cam0_h = 480, cam0_fps = 30;
        int cam1_w = 640, cam1_h = 480, cam1_fps = 30;
        bool cam0_ok = (g_rtsp_sender0 != nullptr && g_rtsp_sender0->inited());
        bool cam1_ok = (g_rtsp_sender1 != nullptr && g_rtsp_sender1->inited());
        if (!cam0_ok) {
            if (g_rtsp_sender0) {
                delete g_rtsp_sender0;
                g_rtsp_sender0 = nullptr;
            }
            g_rtsp_sender0 = new RtspMppSender();
            if (!g_rtsp_sender0->init(g_rtsp_url_0.c_str(), cam0_w, cam0_h, cam0_fps)) {
                delete g_rtsp_sender0;
                g_rtsp_sender0 = nullptr;
                printf("[RTSP] Cam0 启动失败: %s\n", g_rtsp_url_0.c_str());
            } else {
                printf("[RTSP] Cam0 已启动 (%dx%d @ %dfps)\n", cam0_w, cam0_h, cam0_fps);
                cam0_ok = true;
            }
        }
        if (!cam1_ok) {
            if (g_rtsp_sender1) {
                delete g_rtsp_sender1;
                g_rtsp_sender1 = nullptr;
            }
            g_rtsp_sender1 = new RtspMppSender();
            if (!g_rtsp_sender1->init(g_rtsp_url_1.c_str(), cam1_w, cam1_h, cam1_fps)) {
                delete g_rtsp_sender1;
                g_rtsp_sender1 = nullptr;
                printf("[RTSP] Cam1 启动失败: %s\n", g_rtsp_url_1.c_str());
            } else {
                printf("[RTSP] Cam1 已启动 (%dx%d @ %dfps)\n", cam1_w, cam1_h, cam1_fps);
                cam1_ok = true;
            }
        }
        bool mosaic_ok = (g_rtsp_sender_mosaic != nullptr && g_rtsp_sender_mosaic->inited());
        if (!mosaic_ok) {
            if (g_rtsp_sender_mosaic) {
                delete g_rtsp_sender_mosaic;
                g_rtsp_sender_mosaic = nullptr;
            }
            g_rtsp_sender_mosaic = new RtspMppSender();
            if (!g_rtsp_sender_mosaic->init(g_rtsp_url_mosaic.c_str(), MOSAIC_OUTPUT_WIDTH, MOSAIC_OUTPUT_HEIGHT, MOSAIC_TARGET_FPS)) {
                delete g_rtsp_sender_mosaic;
                g_rtsp_sender_mosaic = nullptr;
                printf("[RTSP] Mosaic 启动失败: %s\n", g_rtsp_url_mosaic.c_str());
            } else {
                g_rtsp_mosaic_last_push_ms.store(0);
                printf("[RTSP] Mosaic 已启动 (%dx%d @ %dfps)\n", MOSAIC_OUTPUT_WIDTH, MOSAIC_OUTPUT_HEIGHT, MOSAIC_TARGET_FPS);
                mosaic_ok = true;
            }
        }
        if (!cam0_ok && !cam1_ok) {
            g_rtsp_streaming = false;
            send_response(client_fd,
                build_json_response("error", "RTSP 推流启动失败（cam0/cam1 均未连接）"),
                "application/json", 500);
            return;
        }
        g_rtsp_streaming = true;
        if (g_model_loaded.load()) {
            printf("[RTSP] /api/rtsp/start 成功: cam0=%d cam1=%d mosaic=%d rss=%ldKB\n",
                   cam0_ok ? 1 : 0, cam1_ok ? 1 : 0, mosaic_ok ? 1 : 0, get_process_rss_kb());
            send_response(client_fd,
                build_json_response("success", "RTSP 推流已启动"),
                "application/json");
        } else {
            printf("[RTSP] /api/rtsp/start 成功(裸流): cam0=%d cam1=%d mosaic=%d rss=%ldKB\n",
                   cam0_ok ? 1 : 0, cam1_ok ? 1 : 0, mosaic_ok ? 1 : 0, get_process_rss_kb());
            send_response(client_fd,
                build_json_response("success", "RTSP 裸流已启动（未开启推理）"),
                "application/json");
        }
        return;
    }
}

void handle_route_rtsp_video_start(int client_fd) {
    // 视频模式专用推流，使用原始视频分辨率
    std::string mediamtx_detail;
    (void)start_mediamtx_if_needed("/api/rtsp/video/start", &mediamtx_detail);
    std::lock_guard<std::mutex> lock(g_rtsp_mutex);
    if (g_rtsp_streaming.load() && g_rtsp_sender_video && g_rtsp_sender_video->inited()) {
        send_response(client_fd, build_json_response("success", "视频 RTSP 已在运行"), "application/json");
        return;
    }
    if (!g_rtsp_streaming.load()) {
        stop_rtsp_senders_locked();
    }
    int vw = g_video_width.load();
    int vh = g_video_height.load();
    int vf = g_video_fps.load();
    {
        std::lock_guard<std::mutex> video_lock(g_video_mutex);
        if (g_video_running.load() && g_video_mode.load()) {
            (void)ensure_video_source_open_locked();
        }
        if (g_video_hw_enabled.load() && g_video_hw_reader && g_video_hw_reader->IsOpen()) {
            if (g_video_hw_reader->Width() > 0) vw = g_video_hw_reader->Width();
            if (g_video_hw_reader->Height() > 0) vh = g_video_hw_reader->Height();
            if (g_video_hw_reader->Fps() > 0) vf = (int)(g_video_hw_reader->Fps() + 0.5);
        } else
        if (g_video_cap.isOpened()) {
            int cap_w = (int)g_video_cap.get(cv::CAP_PROP_FRAME_WIDTH);
            int cap_h = (int)g_video_cap.get(cv::CAP_PROP_FRAME_HEIGHT);
            int cap_fps = (int)(g_video_cap.get(cv::CAP_PROP_FPS) + 0.5);
            if (cap_w > 0) vw = cap_w;
            if (cap_h > 0) vh = cap_h;
            if (cap_fps > 0) vf = cap_fps;
        }
    }
    if (!g_rtsp_sender_video || !g_rtsp_sender_video->inited()) {
        if (g_rtsp_sender_video) {
            delete g_rtsp_sender_video;
            g_rtsp_sender_video = nullptr;
        }
        g_rtsp_sender_video = new RtspMppSender();
        if (!g_rtsp_sender_video->init(g_rtsp_url_video.c_str(), vw, vh, vf > 0 ? vf : 25)) {
            delete g_rtsp_sender_video;
            g_rtsp_sender_video = nullptr;
            printf("[RTSP] 视频推流启动失败\n");
            send_response(client_fd, build_json_response("error", "视频 RTSP 启动失败"), "application/json", 500);
            return;
        }
        printf("[RTSP] 视频推流已启动 (%dx%d @ %dfps)\n", vw, vh, vf > 0 ? vf : 25);
    }
    g_rtsp_streaming = true;
    if (!g_model_loaded.load()) {
        start_video_rtsp_raw_thread_if_needed();
    }
    send_response(client_fd, build_json_response("success",
        "视频推流已启动 (" + std::to_string(vw) + "x" + std::to_string(vh) + ")"), "application/json");
}

void handle_route_rtsp_stop(int client_fd) {
    g_video_rtsp_raw_stop = true;
    {
        std::lock_guard<std::mutex> lock(g_rtsp_mutex);
        bool was_streaming = g_rtsp_streaming.load();
        g_rtsp_streaming = false;
        stop_rtsp_senders_locked();
        printf("[RTSP] /api/rtsp/stop: was_streaming=%d, sender 已销毁, rss=%ldKB\n",
               was_streaming ? 1 : 0, get_process_rss_kb());
    }
    stop_video_rtsp_raw_thread();
    send_response(client_fd, build_json_response("success", "RTSP 推流已停止"), "application/json");
}

#endif // USE_RTSP_MPP

// =====================================================================
// Route: GET /api/record/status
// =====================================================================

void handle_route_record_status(int client_fd) {
    CameraRecordingState record_cam0;
    CameraRecordingState record_cam1;
    {
        std::lock_guard<std::mutex> lock(g_record_mutex);
        refresh_recording_state_locked(0);
        refresh_recording_state_locked(1);
        record_cam0 = g_record_states[0];
        record_cam1 = g_record_states[1];
    }
    std::ostringstream oss;
    oss << "{";
    oss << "\"record_output_dir\":\"" << json_escape(g_record_output_dir) << "\",";
    oss << "\"record_ffmpeg_bin\":\"" << json_escape(g_ffmpeg_bin) << "\",";
    oss << "\"cam0\":{";
    oss << "\"recording\":" << (record_cam0.active ? "true" : "false") << ",";
    oss << "\"file\":\"" << json_escape(record_cam0.output_path) << "\",";
    oss << "\"log\":\"" << json_escape(record_cam0.log_path) << "\",";
    oss << "\"started_at\":\"" << json_escape(record_cam0.started_at) << "\",";
    oss << "\"error\":\"" << json_escape(record_cam0.last_error) << "\"";
    oss << "},";
    oss << "\"cam1\":{";
    oss << "\"recording\":" << (record_cam1.active ? "true" : "false") << ",";
    oss << "\"file\":\"" << json_escape(record_cam1.output_path) << "\",";
    oss << "\"log\":\"" << json_escape(record_cam1.log_path) << "\",";
    oss << "\"started_at\":\"" << json_escape(record_cam1.started_at) << "\",";
    oss << "\"error\":\"" << json_escape(record_cam1.last_error) << "\"";
    oss << "}";
    oss << "}";
    send_response(client_fd, oss.str(), "application/json");
}

// =====================================================================
// Route: POST /api/record/start
// =====================================================================

void handle_route_record_start(int client_fd, const std::string& path) {
    int cam = parse_record_camera_index(path);
    if (cam < 0) {
        send_response(client_fd,
                      build_json_response("error", "缺少或无效的 cam 参数，请使用 /api/record/start?cam=0 或 cam=1"),
                      "application/json", 400);
        return;
    }
    std::string requested_name = parse_query_param(path, "name");
    std::string message;
    int status_code = 200;
    if (!start_camera_recording(cam, requested_name, &message, &status_code)) {
        send_response(client_fd, build_json_response("error", message), "application/json", status_code);
        return;
    }
    send_response(client_fd, build_json_response("success", message), "application/json");
}

// =====================================================================
// Route: POST /api/record/stop
// =====================================================================

void handle_route_record_stop(int client_fd, const std::string& path) {
    int cam = parse_record_camera_index(path);
    if (cam < 0) {
        std::string msg0;
        std::string msg1;
        int status0 = 200;
        int status1 = 200;
        bool ok0 = stop_camera_recording(0, &msg0, &status0);
        bool ok1 = stop_camera_recording(1, &msg1, &status1);
        if (!ok0 || !ok1) {
            std::string combined = "停止录像结果: cam0=" + msg0 + ", cam1=" + msg1;
            send_response(client_fd, build_json_response("error", combined), "application/json", 500);
            return;
        }
        send_response(client_fd,
                      build_json_response("success", "已停止所有摄像头录像"),
                      "application/json");
        return;
    }
    std::string message;
    int status_code = 200;
    if (!stop_camera_recording(cam, &message, &status_code)) {
        send_response(client_fd, build_json_response("error", message), "application/json", status_code);
        return;
    }
    send_response(client_fd, build_json_response("success", message), "application/json");
}

// =====================================================================
// Route: GET /api/video/status
// =====================================================================

void handle_route_video_status(int client_fd) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"video_mode\":" << (g_video_mode ? "true" : "false") << ",";
    oss << "\"video_path\":\"" << g_video_path << "\",";
    oss << "\"video_loop\":" << (g_video_loop ? "true" : "false");
    oss << "}";
    send_response(client_fd, oss.str(), "application/json");
}

// =====================================================================
// Route: POST /api/video/start
// =====================================================================

void handle_route_video_start(int client_fd, const std::string& post_body) {
    // 读取 POST body
    std::string path;
    bool loop = false;
    parse_json_body(post_body, path, loop);

    if (path.empty()) {
        send_response(client_fd, build_json_response("error", "缺少 path 参数"), "application/json", 400);
    } else {
        // 防抖：相同视频重复 start 时不重启，避免频繁 reset 导致不稳定
        bool already_running_same_video = false;
        {
            std::lock_guard<std::mutex> lock(g_video_mutex);
            already_running_same_video =
                g_video_mode.load() &&
                g_video_running.load() &&
                (g_video_path == path) &&
                (g_video_loop.load() == loop);
        }
        if (already_running_same_video) {
            send_response(client_fd, build_json_response("success",
                "视频已在播放: " + path + (loop ? " (循环)" : "")), "application/json");
            return;
        }

        start_video_reader(path, loop);
        printf("[Video] 开始播放视频: %s (loop=%s)\n", path.c_str(), loop ? "true" : "false");
        send_response(client_fd, build_json_response("success",
            "视频已启动: " + path + (loop ? " (循环)" : "")), "application/json");
    }
}

// =====================================================================
// Route: POST /api/video/stop
// =====================================================================

void handle_route_video_stop(int client_fd) {
    stop_video_reader();
    printf("[Video] 已停止视频，恢复摄像头\n");
    send_response(client_fd, build_json_response("success", "已停止视频，恢复摄像头"), "application/json");
}

// =====================================================================
// Route: GET / or /index.html
// =====================================================================

void handle_route_index(int client_fd) {
    send_response(client_fd, build_html_page(), "text/html");
}

// =====================================================================
// Dispatcher: handle_client
// =====================================================================

void handle_client(int client_fd) {
    char buffer[16384] = {0};
    int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }

    std::string request(buffer);
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;

    std::string route = path;
    size_t query_start = path.find('?');
    if (query_start != std::string::npos) {
        route = path.substr(0, query_start);
    }

    printf("[HTTP] %s %s\n", method.c_str(), route.c_str());

    // 解析 POST body (Content-Length 方式)
    std::string post_body;
    if (method == "POST") {
        size_t header_end = request.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            size_t content_len_start = request.find("Content-Length:");
            if (content_len_start != std::string::npos && content_len_start < header_end) {
                size_t colon = request.find(":", content_len_start);
                if (colon != std::string::npos) {
                    std::string len_str = request.substr(colon + 1, header_end - colon - 1);
                    int content_len = atoi(len_str.c_str());
                    post_body = request.substr(header_end + 4);
                    // 如果 body 不完整，尝试继续读取
                    while ((int)post_body.size() < content_len) {
                        char extra[4096] = {0};
                        int extra_n = recv(client_fd, extra, sizeof(extra) - 1, 0);
                        if (extra_n <= 0) break;
                        post_body.append(extra, extra_n);
                    }
                }
            }
        }
    }

    // ---- Route dispatch ----

    if (route == "/api/status" && method == "GET") {
        handle_route_status(client_fd);
    }
    else if (route == "/api/threshold/set" && method == "POST") {
        handle_route_threshold_set(client_fd, path);
    }
    else if (route == "/api/threshold/get" && method == "GET") {
        handle_route_threshold_get(client_fd);
    }
    else if (route == "/api/detection/count" && method == "GET") {
        handle_route_detection_count(client_fd, path);
    }
    else if (route == "/api/frame" && method == "GET") {
        handle_route_frame(client_fd, path);
    }
    else if (route == "/api/tracker" && method == "GET") {
        handle_route_tracker(client_fd, route, method);
    }
    else if (route.find("/api/tracker/") == 0 && method == "POST") {
        handle_route_tracker(client_fd, route, method);
    }
    else if (route == "/api/inference/on" && method == "POST") {
        handle_route_inference_on(client_fd, path);
    }
    else if (route == "/api/inference/off" && method == "POST") {
        handle_route_inference_off(client_fd, path);
    }
    else if (route.find("/api/camera/") == 0 && method == "POST") {
        int cam = std::stoi(route.substr(12));
        handle_route_camera_switch(client_fd, cam);
    }
#ifdef USE_RTSP_MPP
    else if (route == "/api/rtsp/start" && method == "POST") {
        handle_route_rtsp_start(client_fd);
    }
    else if (route == "/api/rtsp/video/start" && method == "POST") {
        handle_route_rtsp_video_start(client_fd);
    }
    else if (route == "/api/rtsp/stop" && method == "POST") {
        handle_route_rtsp_stop(client_fd);
    }
#endif
    else if (route == "/api/record/status" && method == "GET") {
        handle_route_record_status(client_fd);
    }
    else if (route == "/api/record/start" && method == "POST") {
        handle_route_record_start(client_fd, path);
    }
    else if (route == "/api/record/stop" && method == "POST") {
        handle_route_record_stop(client_fd, path);
    }
    else if (route == "/api/video/status" && method == "GET") {
        handle_route_video_status(client_fd);
    }
    else if (route == "/api/video/start" && method == "POST") {
        handle_route_video_start(client_fd, post_body);
    }
    else if (route == "/api/video/stop" && method == "POST") {
        handle_route_video_stop(client_fd);
    }
    else if (route == "/" || route == "/index.html") {
        handle_route_index(client_fd);
    }
    else {
        send_response(client_fd, build_json_response("error", "Unknown endpoint"), "application/json", 404);
    }

    close(client_fd);
}

// =====================================================================
// HTTP server thread
// =====================================================================

void http_server_thread() {
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        printf("[HTTP] socket 失败\n");
        return;
    }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_http_port);

    if (bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("[HTTP] bind 端口 %d 失败\n", g_http_port);
        close(g_server_fd);
        g_server_fd = -1;
        return;
    }

    struct timeval tv = {0, 100000};
    setsockopt(g_server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    listen(g_server_fd, 10);
    printf("[HTTP] HTTP 服务器已启动: http://0.0.0.0:%d\n", g_http_port);

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            std::thread(handle_client, client_fd).detach();
        }
    }

    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
    printf("[HTTP] HTTP 服务器已停止\n");
}

// =====================================================================
// Signal handler
// =====================================================================

void signal_handler(int sig) {
    printf("\n[Main] 收到信号 %d, 正在停止...\n", sig);
    g_running = false;
    g_inference_enabled = false;
    g_tracker_enabled = false;
    g_model_switching = true;

    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }

#ifdef USE_RTSP_MPP
    g_rtsp_streaming = false;
    g_video_rtsp_raw_stop = true;
#endif

    g_video_mode = false;
    g_video_running = false;
}
