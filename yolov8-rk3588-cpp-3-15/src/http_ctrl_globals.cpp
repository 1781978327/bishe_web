// Global variable definitions for the vision (http_ctrl) service.
// Corresponding extern declarations live in http_ctrl_globals.h.

#include "http_ctrl_globals.h"

// -- Configuration --
int g_http_port = DEFAULT_HTTP_PORT;
int g_server_fd = -1;
std::string g_rtsp_host = DEFAULT_RTSP_HOST;
int g_rtsp_port = DEFAULT_RTSP_PORT;
std::string g_rtsp_url_0 = "rtsp://127.0.0.1:8554/cam0";
std::string g_rtsp_url_1 = "rtsp://127.0.0.1:8554/cam1";
std::string g_rtsp_url_mosaic = "rtsp://127.0.0.1:8554/cam2";
std::string g_rtsp_url_video = "rtsp://127.0.0.1:8554/cam3";
std::string g_input_source_cam0 = "auto";
std::string g_input_source_cam1 = "auto";
bool g_input_source_cam0_dmabuf = false;
bool g_input_source_cam1_dmabuf = false;
std::string g_mediamtx_bin;
std::string g_mediamtx_log = DEFAULT_MEDIAMTX_LOG;
bool g_mediamtx_auto_start = true;
std::string g_record_output_dir;
std::string g_ffmpeg_bin = DEFAULT_FFMPEG_BIN;

// -- Recording --
std::mutex g_record_mutex;
CameraRecordingState g_record_states[2];

// -- Global state --
std::atomic<bool> g_running(true);
std::atomic<bool> g_inference_enabled(false);  // default inference off
std::atomic<bool> g_model_loaded(false);
std::atomic<bool> g_model_loading(false);
std::atomic<int>  g_current_cam(0);
std::atomic<int>  g_cam0_fps(0);
std::atomic<int>  g_cam1_fps(0);
std::atomic<int>  g_rtsp_cam0_fps(0);
std::atomic<int>  g_rtsp_cam1_fps(0);
std::atomic<int>  g_rtsp_mosaic_fps(0);
std::atomic<int>  g_rtsp_video_fps(0);
std::mutex g_model_mutex;
std::string g_model_path = DEFAULT_MODEL_PATH;
std::string g_label_path = "";
std::string g_model_error;
std::vector<rknn_lite*> g_rkpool;
std::atomic<int> g_active_jobs(0);
std::atomic<bool> g_model_switching(false);

// -- Latest frame (HTTP API) --
cv::Mat g_latest_frame;
std::mutex g_latest_frame_mutex;
std::atomic<bool> g_frame_available(false);
std::atomic<int> g_latest_frame_slot(-1);
cv::Mat g_latest_frame_cam[2];
std::mutex g_latest_frame_cam_mutex[2];
bool g_frame_available_cam[2] = {false, false};
int g_latest_frame_slot_cam[2] = {-1, -1};

// -- Tracker --
std::atomic<bool> g_tracker_enabled(false);
std::mutex g_tracker_mutex;
std::vector<std::vector<TrackerResultItem>> g_tracker_results;

// -- Video source --
std::atomic<bool> g_video_mode(false);
std::atomic<bool> g_video_loop(false);
std::string g_video_path;
cv::VideoCapture g_video_cap;
std::mutex g_video_mutex;
std::queue<cv::Mat> g_video_frames;
std::condition_variable g_video_cv;
std::thread g_video_thread;
std::atomic<bool> g_video_running(false);
std::atomic<int> g_video_width(1920);
std::atomic<int> g_video_height(1080);
std::atomic<int> g_video_fps(25);

// -- Detection / alert --
std::atomic<float> g_confidence_threshold(0.5f);
std::atomic<int> g_cam0_detection_count(0);
std::atomic<int> g_cam1_detection_count(0);
std::atomic<int> g_box_count_alert_threshold(0);
std::atomic<int> g_box_count_alert_cooldown_ms(8000);
std::mutex g_box_alert_state_mutex;
bool g_box_count_over_state[2] = {false, false};
long long g_last_box_alert_ms[2] = {0, 0};

// -- Report server --
std::string g_report_server_host = "127.0.0.1";
int g_report_server_port = 8080;
std::string g_report_server_path = "/api/detection/record/rknn/report";
int g_report_camera_id_cam0 = 1;
int g_report_camera_id_cam1 = 2;
std::string g_forbidden_area_path = "/api/rknn/forbidden-area";
std::atomic<int> g_forbidden_area_fetch_interval_ms(2000);
std::atomic<int> g_forbidden_area_fetch_timeout_ms(600);
std::atomic<int> g_intrusion_alert_cooldown_ms(8000);

// -- Forbidden area / intrusion --
std::mutex g_forbidden_area_mutex;
ForbiddenAreaCache g_forbidden_area_cache[2];
bool g_forbidden_area_fetching[2] = {false, false};
std::mutex g_intrusion_alert_state_mutex;
bool g_intrusion_over_state[2] = {false, false};
long long g_last_intrusion_alert_ms[2] = {0, 0};

// -- Image / model dimensions --
cv::Size g_current_img_size(1280, 720);
int g_model_width = 640;
int g_model_height = 640;

// -- RTSP streaming (USE_RTSP_MPP only) --
#ifdef USE_RTSP_MPP
std::atomic<bool> g_rtsp_streaming(false);
RtspMppSender* g_rtsp_sender0 = nullptr;
RtspMppSender* g_rtsp_sender1 = nullptr;
RtspMppSender* g_rtsp_sender_mosaic = nullptr;
RtspMppSender* g_rtsp_sender_video = nullptr;
std::thread g_video_rtsp_raw_thread;
std::atomic<bool> g_video_rtsp_raw_running(false);
std::atomic<bool> g_video_rtsp_raw_stop(false);
std::atomic<long long> g_rtsp_mosaic_last_push_ms(0);
std::mutex g_rtsp_mutex;

std::unique_ptr<FFmpegRkmppReader> g_video_hw_reader;
std::atomic<bool> g_video_hw_enabled(false);
#endif
