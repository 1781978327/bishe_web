// Model loading, unloading, and status functions.
// Extracted from main_http_ctrl.cc during refactoring.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <sstream>

#include "http_ctrl_model.h"
#include "http_ctrl_globals.h"
#include "http_ctrl_web_utils.h"
#include "rknnPool.hpp"

#ifdef USE_RTSP_MPP
#include "rtsp_mpp_sender.h"
#endif

// -- Forward declarations for utilities still in main_http_ctrl.cc --
extern bool file_exists(const std::string& path);
extern std::string detect_project_root();
extern std::string path_join(const std::string& base, const std::string& leaf);
extern bool is_tcp_service_ready(const std::string& host, int port, int timeout_ms);

void update_latest_frame_global(const cv::Mat& frame, int slot) {
    if (frame.empty()) return;
    std::lock_guard<std::mutex> lock(g_latest_frame_mutex);
    g_latest_frame = frame.clone();
    g_frame_available = !g_latest_frame.empty();
    g_latest_frame_slot = slot;
}

void update_latest_frame_for_cam(int cam, const cv::Mat& frame, int slot) {
    if (cam < 0 || cam > 1) return;
    if (frame.empty()) return;
    std::lock_guard<std::mutex> lock(g_latest_frame_cam_mutex[cam]);
    g_latest_frame_cam[cam] = frame.clone();
    g_frame_available_cam[cam] = !g_latest_frame_cam[cam].empty();
    g_latest_frame_slot_cam[cam] = slot;
}

std::string detect_model_path_locked() {
    // 若外部已指定模型路径，优先使用该路径
    if (file_exists(g_model_path)) {
        return g_model_path;
    }


    std::vector<std::string> candidates = {
        "../model/RK3588/best-coco-person-moto.rknn",
        DEFAULT_MODEL_PATH,
        "../model/RK3588/yolov8s.rknn",
        "../model/RK3588/yolov8n.rknn",
        "../model/RK3588/yolov8m.rknn"
    };
    {
        std::string root = detect_project_root();
        if (!root.empty()) {
            candidates.push_back(path_join(root, "model/RK3588/person_2700_i8.rknn"));
            candidates.push_back(path_join(root, "model/RK3588/yolov8s.rknn"));
            candidates.push_back(path_join(root, "model/RK3588/yolov8n.rknn"));
            candidates.push_back(path_join(root, "model/RK3588/yolov8m.rknn"));
            candidates.push_back(path_join(root, "model/RK3588/best-coco-person-moto.rknn"));
        }
    }

    glob_t glob_result;
    memset(&glob_result, 0, sizeof(glob_result));
    if (glob("../model/RK3588/*.rknn", 0, nullptr, &glob_result) == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            candidates.emplace_back(glob_result.gl_pathv[i]);
        }
    }
    globfree(&glob_result);
    {
        std::string root = detect_project_root();
        if (!root.empty()) {
            std::string pattern = path_join(root, "model/RK3588/*.rknn");
            memset(&glob_result, 0, sizeof(glob_result));
            if (glob(pattern.c_str(), 0, nullptr, &glob_result) == 0) {
                for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
                    candidates.emplace_back(glob_result.gl_pathv[i]);
                }
            }
            globfree(&glob_result);
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    // 让 best-coco-person-moto 作为默认模型，person_2700_i8 继续作为回退首选
    std::stable_sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b) {
        bool a_best = a.find("best-coco-person-moto") != std::string::npos;
        bool b_best = b.find("best-coco-person-moto") != std::string::npos;
        if (a_best != b_best) return a_best;
        bool a_person = a.find("person_2700_i8") != std::string::npos;
        bool b_person = b.find("person_2700_i8") != std::string::npos;
        if (a_person != b_person) return a_person;
        bool a_s = a.find("yolov8s") != std::string::npos;
        bool b_s = b.find("yolov8s") != std::string::npos;
        if (a_s != b_s) return a_s;
        return a < b;
    });

    for (const auto& path : candidates) {
        if (file_exists(path)) return path;
    }
    return "";
}

std::string build_status_response() {
    std::ostringstream oss;
    oss << "{";
    oss << "\"running\":" << (g_running ? "true" : "false") << ",";
    oss << "\"inference_enabled\":" << (g_inference_enabled ? "true" : "false") << ",";
    oss << "\"current_camera\":" << g_current_cam.load() << ",";
    oss << "\"fps\":" << (g_cam0_fps.load() + g_cam1_fps.load()) << ",";
    oss << "\"fps_cam0\":" << g_cam0_fps.load() << ",";
    oss << "\"fps_cam1\":" << g_cam1_fps.load();
#ifdef USE_RTSP_MPP
    oss << ",\"rtsp_streaming\":" << (g_rtsp_streaming ? "true" : "false");
#else
    oss << ",\"rtsp_streaming\":false";
#endif
    oss << ",\"input_source_cam0\":\"" << json_escape(g_input_source_cam0) << "\"";
    oss << ",\"input_source_cam1\":\"" << json_escape(g_input_source_cam1) << "\"";
    oss << ",\"input_source_cam0_dmabuf\":" << (g_input_source_cam0_dmabuf ? "true" : "false");
    oss << ",\"input_source_cam1_dmabuf\":" << (g_input_source_cam1_dmabuf ? "true" : "false");
    oss << ",\"mediamtx_auto_start\":" << (g_mediamtx_auto_start ? "true" : "false");
    oss << ",\"mediamtx_bin\":\"" << json_escape(g_mediamtx_bin) << "\"";
    oss << ",\"mediamtx_running\":" << (is_tcp_service_ready(g_rtsp_host, g_rtsp_port, 150) ? "true" : "false");
    oss << "}";
    return oss.str();
}

void release_model_workers_locked() {
    for (auto*& ptr : g_rkpool) {
        delete ptr;
        ptr = nullptr;
    }
}

bool ensure_model_loaded(std::string* msg_out) {
    std::lock_guard<std::mutex> lock(g_model_mutex);
    if (g_model_loaded.load()) {
        if (msg_out) *msg_out = "模型已就绪";
        return true;
    }
    if (g_model_loading.load()) {
        if (msg_out) *msg_out = "模型正在加载中";
        return false;
    }

    g_model_loading = true;
    g_model_error.clear();

    std::string model_to_use = detect_model_path_locked();
    if (model_to_use.empty()) {
        g_model_loading = false;
        g_model_loaded = false;
        g_model_error = "未找到可用模型(.rknn)，请检查 ../model/RK3588/";
        if (msg_out) *msg_out = g_model_error;
        return false;
    }

    printf("[Model] 开始加载模型: %s\n", model_to_use.c_str());
    if (g_rkpool.empty()) {
        g_rkpool.resize(SLOTS_PER_CAM * 2, nullptr);
    }

    for (size_t i = 0; i < g_rkpool.size(); ++i) {
        if (!g_rkpool[i]) {
            int core_id = (int)(i % 3);
            g_rkpool[i] = new rknn_lite(const_cast<char*>(model_to_use.c_str()), core_id);
            printf("[Model] Worker %zu 初始化完成 (核心 %d)\n", i, core_id);
        }
    }

    g_model_path = model_to_use;
    g_model_loaded = true;
    g_model_loading = false;
    if (msg_out) *msg_out = "模型加载成功";
    printf("[Model] 加载完成: %s\n", g_model_path.c_str());
    return true;
}

bool unload_model_runtime(std::string* msg_out) {
    g_inference_enabled = false;
    g_tracker_enabled = false;
    g_model_switching = true;

    // 给主循环一个时间片停止继续派发任务
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (g_active_jobs.load() > 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (g_active_jobs.load() > 0) {
        g_model_switching = false;
        if (msg_out) *msg_out = "模型卸载超时：仍有推理任务在运行";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_model_mutex);
        release_model_workers_locked();
        g_model_loaded = false;
        g_model_loading = false;
        g_model_error.clear();
    }
    rknn_lite::reset_shared_deepsort_trackers();
    rknn_lite::reset_shared_bytetrack_trackers();
    {
        std::lock_guard<std::mutex> lock(g_tracker_mutex);
        for (auto& tracks : g_tracker_results) {
            tracks.clear();
        }
    }
    g_cam0_detection_count = 0;
    g_cam1_detection_count = 0;
    g_model_switching = false;

    if (msg_out) *msg_out = "模型已卸载";
    printf("[Model] 已卸载\n");
    return true;
}
