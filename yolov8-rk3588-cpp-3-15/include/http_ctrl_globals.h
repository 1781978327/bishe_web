#ifndef HTTP_CTRL_GLOBALS_H
#define HTTP_CTRL_GLOBALS_H

// Global variables shared across the vision (http_ctrl) service modules.
// Definitions live in http_ctrl_globals.cpp; other translation units include
// this header and access them via extern declarations.

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <cstdint>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "postprocess.h"          // DetectionResultItem, TrackerResultItem
class rknn_lite;                  // forward declaration — avoid including header-only rknnPool.hpp

#ifdef USE_RTSP_MPP
#include <rockchip/mpp_buffer.h>
#include "rtsp_mpp_sender.h"
#include "ffmpeg_rkmpp_reader.h"
#endif

// ---------------------- #define constants ----------------------
#define DEFAULT_HTTP_PORT  8091
#define DEFAULT_RTSP_HOST "127.0.0.1"
#define DEFAULT_RTSP_PORT 8554
#define DEFAULT_MODEL_PATH "../model/RK3588/best-coco-person-moto.rknn"
#define DEFAULT_LABEL_REL_PATH "model/RK3588/best-coco-person-moto.txt"
#define DEFAULT_MEDIAMTX_REL_PATH "src/mediamtx"
#define DEFAULT_MEDIAMTX_LOG "/tmp/mediamtx_auto.log"
#define DEFAULT_RECORD_OUTPUT_REL_PATH "recordings/camera"
#define DEFAULT_FFMPEG_BIN "/usr/local/ffmpeg/bin/ffmpeg"
#define DEFAULT_WIDTH     640
#define DEFAULT_HEIGHT    640
#define SLOTS_PER_CAM     3  // per-camera slot count

// ---------------------- struct definitions ----------------------

struct CameraRecordingState {
    pid_t pid = -1;
    bool active = false;
    bool stop_requested = false;
    std::string output_path;
    std::string log_path;
    std::string started_at;
    std::string last_error;
};

struct VideoFrameInfo {
    bool valid = false;
    int fd = -1;
    bool nv12_valid = false;
    cv::Mat nv12_packed;
    int size = 0;
    int width = 0;
    int height = 0;
    int wstride = 0;
    int hstride = 0;
    uint32_t drm_format = 0;
};

struct ForbiddenAreaCache {
    bool valid = false;
    int image_width = 0;
    int image_height = 0;
    std::vector<cv::Point2f> points;
    long long last_fetch_ms = 0;
};

struct CpuTimes {
    uint64_t user = 0;
    uint64_t nice = 0;
    uint64_t system = 0;
    uint64_t idle = 0;
    uint64_t iowait = 0;
    uint64_t irq = 0;
    uint64_t softirq = 0;
    uint64_t steal = 0;
};

#ifdef USE_RTSP_MPP
struct SlotBgrDmabuf {
    MppBuffer buffer = nullptr;
    int fd = -1;
    void* ptr = nullptr;
    int size = 0;
    int width = 0;
    int height = 0;
    int wstride = 0;
    int hstride = 0;
    cv::Mat mat;
};
#endif

// ---------------------- extern declarations ----------------------

// -- Configuration --
extern int g_http_port;
extern int g_server_fd;
extern std::string g_rtsp_host;
extern int g_rtsp_port;
extern std::string g_rtsp_url_0;
extern std::string g_rtsp_url_1;
extern std::string g_rtsp_url_mosaic;
extern std::string g_rtsp_url_video;
extern std::string g_input_source_cam0;
extern std::string g_input_source_cam1;
extern bool g_input_source_cam0_dmabuf;
extern bool g_input_source_cam1_dmabuf;
extern std::string g_mediamtx_bin;
extern std::string g_mediamtx_log;
extern bool g_mediamtx_auto_start;
extern std::string g_record_output_dir;
extern std::string g_ffmpeg_bin;

// -- Recording --
extern std::mutex g_record_mutex;
extern CameraRecordingState g_record_states[2];

// -- Global state --
extern std::atomic<bool> g_running;
extern std::atomic<bool> g_inference_enabled;
extern std::atomic<bool> g_model_loaded;
extern std::atomic<bool> g_model_loading;
extern std::atomic<int>  g_current_cam;
extern std::atomic<int>  g_cam0_fps;
extern std::atomic<int>  g_cam1_fps;
extern std::atomic<int>  g_rtsp_cam0_fps;
extern std::atomic<int>  g_rtsp_cam1_fps;
extern std::atomic<int>  g_rtsp_mosaic_fps;
extern std::atomic<int>  g_rtsp_video_fps;
extern std::mutex g_model_mutex;
extern std::string g_model_path;
extern std::string g_label_path;
extern std::string g_model_error;
extern std::vector<rknn_lite*> g_rkpool;
extern std::atomic<int> g_active_jobs;
extern std::atomic<bool> g_model_switching;

// -- Latest frame (HTTP API) --
extern cv::Mat g_latest_frame;
extern std::mutex g_latest_frame_mutex;
extern std::atomic<bool> g_frame_available;
extern std::atomic<int> g_latest_frame_slot;
extern cv::Mat g_latest_frame_cam[2];
extern std::mutex g_latest_frame_cam_mutex[2];
extern bool g_frame_available_cam[2];
extern int g_latest_frame_slot_cam[2];

// -- Tracker --
extern std::atomic<bool> g_tracker_enabled;
extern std::mutex g_tracker_mutex;
extern std::vector<std::vector<TrackerResultItem>> g_tracker_results;

// -- Video source --
extern std::atomic<bool> g_video_mode;
extern std::atomic<bool> g_video_loop;
extern std::string g_video_path;
extern cv::VideoCapture g_video_cap;
extern std::mutex g_video_mutex;
extern std::queue<cv::Mat> g_video_frames;
extern std::condition_variable g_video_cv;
extern std::thread g_video_thread;
extern std::atomic<bool> g_video_running;
extern std::atomic<int> g_video_width;
extern std::atomic<int> g_video_height;
extern std::atomic<int> g_video_fps;

// -- Detection / alert --
extern std::atomic<float> g_confidence_threshold;
extern std::atomic<int> g_cam0_detection_count;
extern std::atomic<int> g_cam1_detection_count;
extern std::atomic<int> g_box_count_alert_threshold;
extern std::atomic<int> g_box_count_alert_cooldown_ms;
extern std::mutex g_box_alert_state_mutex;
extern bool g_box_count_over_state[2];
extern long long g_last_box_alert_ms[2];

// -- Report server --
extern std::string g_report_server_host;
extern int g_report_server_port;
extern std::string g_report_server_path;
extern int g_report_camera_id_cam0;
extern int g_report_camera_id_cam1;
extern std::string g_forbidden_area_path;
extern std::atomic<int> g_forbidden_area_fetch_interval_ms;
extern std::atomic<int> g_forbidden_area_fetch_timeout_ms;
extern std::atomic<int> g_intrusion_alert_cooldown_ms;

// -- Forbidden area / intrusion --
extern std::mutex g_forbidden_area_mutex;
extern ForbiddenAreaCache g_forbidden_area_cache[2];
extern bool g_forbidden_area_fetching[2];
extern std::mutex g_intrusion_alert_state_mutex;
extern bool g_intrusion_over_state[2];
extern long long g_last_intrusion_alert_ms[2];

// -- Image / model dimensions --
extern cv::Size g_current_img_size;
extern int g_model_width;
extern int g_model_height;

// -- RTSP streaming (USE_RTSP_MPP only) --
#ifdef USE_RTSP_MPP
extern std::atomic<bool> g_rtsp_streaming;
extern RtspMppSender* g_rtsp_sender0;
extern RtspMppSender* g_rtsp_sender1;
extern RtspMppSender* g_rtsp_sender_mosaic;
extern RtspMppSender* g_rtsp_sender_video;
extern std::thread g_video_rtsp_raw_thread;
extern std::atomic<bool> g_video_rtsp_raw_running;
extern std::atomic<bool> g_video_rtsp_raw_stop;
extern std::atomic<long long> g_rtsp_mosaic_last_push_ms;
extern std::mutex g_rtsp_mutex;

extern std::unique_ptr<FFmpegRkmppReader> g_video_hw_reader;
extern std::atomic<bool> g_video_hw_enabled;
#endif

#endif // HTTP_CTRL_GLOBALS_H
