// Global variable definitions extracted from rknnPool.hpp
// This single TU provides the canonical storage for all shared globals.
#include "rknnPool.hpp"

// --- Label state ---
std::vector<std::string> coco_labels;
std::string g_label_file_override;
std::mutex g_label_file_mutex;
std::string g_label_cache_key;

// --- Tracker configuration ---
TrackerBackend g_tracker_backend_override = TrackerBackend::ByteTrack;
std::string g_tracker_reid_model_override;
int g_deepsort_skip_frames = 0;
std::mutex g_tracker_config_mutex;

// --- Shared DeepSort trackers ---
DeepSort* g_shared_deepsort_trackers[3] = {nullptr, nullptr, nullptr};
std::mutex g_shared_deepsort_init_mutex;
std::mutex g_shared_deepsort_runtime_mutex[3];
std::string g_shared_deepsort_model_path;
std::atomic<long long> g_shared_deepsort_frame_counter[3] = {};
std::vector<DetectBox> g_shared_deepsort_last_detections[3];

// --- Shared ByteTrack trackers ---
BYTETracker* g_shared_bytetrack_trackers[3] = {nullptr, nullptr, nullptr};
std::mutex g_shared_bytetrack_init_mutex;
std::mutex g_shared_bytetrack_runtime_mutex[3];
std::atomic<long long> g_shared_bytetrack_frame_counter[3] = {};

// --- FPS ---
int fps_frame_count = 0;
double fps_last_time = 0.0;
double current_fps = 0.0;

// --- Preprocess counters ---
std::atomic<int> g_preprocess_rgb_iomem_log_counter(0);
std::atomic<int> g_preprocess_camera_iomem_log_counter(0);
std::atomic<int> g_preprocess_video_iomem_log_counter(0);
std::atomic<int> g_preprocess_legacy_log_counter(0);
std::atomic<int> g_preprocess_video_iomem_fail_counter(0);
std::atomic<int> g_preprocess_video_iomem_fallback_counter(0);
std::atomic<bool> g_preprocess_video_iomem_disabled(false);

// --- OpenCV draw timing ---
std::atomic<long long> g_opencv_draw_total_us(0);
std::atomic<long long> g_opencv_draw_sample_count(0);

// --- Static class members ---
std::atomic<float> rknn_lite::detection_threshold(0.5f);
std::atomic<int> rknn_lite::last_detection_count(0);
