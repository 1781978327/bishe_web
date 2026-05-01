// HTTP/WebSocket 控制服务 - 多线程推理版本
// 编译: cmake . && make

#include "http_ctrl_globals.h"
#include "http_ctrl_utils.h"
#include "http_ctrl_usage_monitor.h"
#include "http_ctrl_mediamtx.h"
#include "http_ctrl_v4l2_auto.h"
#include "http_ctrl_recording.h"
#include "http_ctrl_model.h"
#include "http_ctrl_video.h"
#include "http_ctrl_rtsp.h"
#include "http_ctrl_alerts.h"
#include "http_ctrl_tracker_draw.h"
#include "http_ctrl_routes.h"
#include "http_ctrl_camera_io.h"
#include "http_ctrl_web_utils.h"
#include "rknnPool.hpp"
#include "postprocess.h"
#include "rk_common.h"
#include "v4l2_dmabuf_capture.h"

#ifdef USE_RTSP_MPP
#include "http_ctrl_raw_video_rtsp.h"
#include "rtsp_mpp_sender.h"
#include "ffmpeg_rkmpp_reader.h"
#endif

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <libdrm/drm_fourcc.h>

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
#include <linux/videodev2.h>
#include <limits.h>
#include <glob.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <memory>
#include <cctype>
#include <cstdint>

void print_banner() {
    printf("\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║        YOLOv8 RKNN HTTP 控制服务 (多线程推理版)         ║\n");
    printf("║        OrangePi 5B / RK3588                               ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  HTTP 端口: %d                                            ║\n", g_http_port);
    printf("║  模型加载: 首次调用 /api/inference/on 时自动加载          ║\n");
    printf("║  每摄像头 slot: %d                                         ║\n", SLOTS_PER_CAM);
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    bool cam0_source_user_set = false;
    bool cam1_source_user_set = false;

    // 读取 RTSP 环境变量（可选）
    const char* env_rtsp_host = getenv("RTSP_HOST");
    const char* env_rtsp_port = getenv("RTSP_PORT");
    const char* env_rtsp_prebind_dma = getenv("RTSP_PREBIND_DMA");
    const char* env_mediamtx_bin = getenv("MEDIAMTX_BIN");
    const char* env_mediamtx_log = getenv("MEDIAMTX_LOG");
    const char* env_mediamtx_auto = getenv("MEDIAMTX_AUTO_START");
    const char* env_ffmpeg_bin = getenv("FFMPEG_BIN");
    const char* env_record_output_dir = getenv("RECORD_OUTPUT_DIR");
    const char* env_cam0_source = getenv("CAM0_SOURCE");
    const char* env_cam1_source = getenv("CAM1_SOURCE");
    const char* env_tracker_backend = getenv("TRACKER_BACKEND");
    const char* env_tracker_reid = getenv("TRACKER_REID_MODEL");
    const char* env_report_host = getenv("REPORT_SERVER_HOST");
    const char* env_report_port = getenv("REPORT_SERVER_PORT");
    const char* env_report_path = getenv("REPORT_SERVER_PATH");
    const char* env_report_cam0_id = getenv("REPORT_CAM0_ID");
    const char* env_report_cam1_id = getenv("REPORT_CAM1_ID");
    const char* env_box_alert_cooldown = getenv("BOX_ALERT_COOLDOWN_MS");
    const char* env_forbidden_area_path = getenv("FORBIDDEN_AREA_PATH");
    const char* env_forbidden_area_sync_ms = getenv("FORBIDDEN_AREA_SYNC_MS");
    const char* env_forbidden_area_timeout_ms = getenv("FORBIDDEN_AREA_TIMEOUT_MS");
    const char* env_intrusion_alert_cooldown = getenv("INTRUSION_ALERT_COOLDOWN_MS");
    bool rtsp_prebind_dma = false;
    if (env_rtsp_host && *env_rtsp_host) {
        g_rtsp_host = env_rtsp_host;
    }
    if (env_rtsp_port && *env_rtsp_port) {
        int p = atoi(env_rtsp_port);
        if (p > 0 && p <= 65535) g_rtsp_port = p;
    }
    if (env_rtsp_prebind_dma && *env_rtsp_prebind_dma) {
        std::string v = env_rtsp_prebind_dma;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        rtsp_prebind_dma = (v == "1" || v == "true" || v == "on" || v == "yes");
    }
    if (env_mediamtx_bin && *env_mediamtx_bin) {
        g_mediamtx_bin = env_mediamtx_bin;
    }
    if (env_mediamtx_log && *env_mediamtx_log) {
        g_mediamtx_log = env_mediamtx_log;
    }
    if (env_mediamtx_auto && *env_mediamtx_auto) {
        g_mediamtx_auto_start = parse_bool_flag_text(env_mediamtx_auto, true);
    }
    if (env_ffmpeg_bin && *env_ffmpeg_bin) {
        g_ffmpeg_bin = env_ffmpeg_bin;
    }
    if (env_record_output_dir && *env_record_output_dir) {
        g_record_output_dir = env_record_output_dir;
    }
    if (env_cam0_source && *env_cam0_source) {
        g_input_source_cam0 = env_cam0_source;
        cam0_source_user_set = true;
    }
    if (env_cam1_source && *env_cam1_source) {
        g_input_source_cam1 = env_cam1_source;
        cam1_source_user_set = true;
    }
    if (env_tracker_backend && *env_tracker_backend) {
        if (!rknn_lite::set_tracker_backend(env_tracker_backend)) {
            printf("[Tracker] 警告: 无效的 TRACKER_BACKEND=%s，继续使用默认 bytetrack\n", env_tracker_backend);
        }
    }
    if (env_tracker_reid && *env_tracker_reid) {
        rknn_lite::set_tracker_reid_model_override(env_tracker_reid);
    }
    if (env_report_host && *env_report_host) {
        g_report_server_host = env_report_host;
    }
    if (env_report_port && *env_report_port) {
        int p = atoi(env_report_port);
        if (p > 0 && p <= 65535) g_report_server_port = p;
    }
    if (env_report_path && *env_report_path) {
        g_report_server_path = env_report_path;
        if (!g_report_server_path.empty() && g_report_server_path[0] != '/') {
            g_report_server_path = "/" + g_report_server_path;
        }
    }
    if (env_report_cam0_id && *env_report_cam0_id) {
        g_report_camera_id_cam0 = atoi(env_report_cam0_id);
    }
    if (env_report_cam1_id && *env_report_cam1_id) {
        g_report_camera_id_cam1 = atoi(env_report_cam1_id);
    }
    if (env_box_alert_cooldown && *env_box_alert_cooldown) {
        int cooldown = atoi(env_box_alert_cooldown);
        if (cooldown >= 0) g_box_count_alert_cooldown_ms.store(cooldown);
    }
    if (env_forbidden_area_path && *env_forbidden_area_path) {
        g_forbidden_area_path = env_forbidden_area_path;
        if (!g_forbidden_area_path.empty() && g_forbidden_area_path[0] != '/') {
            g_forbidden_area_path = "/" + g_forbidden_area_path;
        }
    }
    if (env_forbidden_area_sync_ms && *env_forbidden_area_sync_ms) {
        int v = atoi(env_forbidden_area_sync_ms);
        if (v > 0) g_forbidden_area_fetch_interval_ms.store(v);
    }
    if (env_forbidden_area_timeout_ms && *env_forbidden_area_timeout_ms) {
        int v = atoi(env_forbidden_area_timeout_ms);
        if (v > 0) g_forbidden_area_fetch_timeout_ms.store(v);
    }
    if (env_intrusion_alert_cooldown && *env_intrusion_alert_cooldown) {
        int v = atoi(env_intrusion_alert_cooldown);
        if (v >= 0) g_intrusion_alert_cooldown_ms.store(v);
    }

    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            g_http_port = std::stoi(argv[++i]);
        } else if (arg == "--rtsp-host" && i + 1 < argc) {
            g_rtsp_host = argv[++i];
        } else if (arg == "--rtsp-port" && i + 1 < argc) {
            int p = std::stoi(argv[++i]);
            if (p > 0 && p <= 65535) {
                g_rtsp_port = p;
            }
        } else if (arg == "--mediamtx-bin" && i + 1 < argc) {
            g_mediamtx_bin = argv[++i];
        } else if (arg == "--mediamtx-log" && i + 1 < argc) {
            g_mediamtx_log = argv[++i];
        } else if (arg == "--mediamtx-auto-start" && i + 1 < argc) {
            g_mediamtx_auto_start = parse_bool_flag_text(argv[++i], true);
        } else if (arg == "--ffmpeg-bin" && i + 1 < argc) {
            g_ffmpeg_bin = argv[++i];
        } else if (arg == "--record-output-dir" && i + 1 < argc) {
            g_record_output_dir = argv[++i];
        } else if (arg == "--cam0-source" && i + 1 < argc) {
            g_input_source_cam0 = argv[++i];
            cam0_source_user_set = true;
        } else if (arg == "--cam1-source" && i + 1 < argc) {
            g_input_source_cam1 = argv[++i];
            cam1_source_user_set = true;
        } else if (arg == "--tracker-backend" && i + 1 < argc) {
            if (!rknn_lite::set_tracker_backend(argv[++i])) {
                printf("[Tracker] 警告: 无效的 --tracker-backend，继续使用默认 bytetrack\n");
            }
        } else if (arg == "--reid-model" && i + 1 < argc) {
            rknn_lite::set_tracker_reid_model_override(argv[++i]);
        }
    }

    maybe_auto_assign_camera_sources(cam0_source_user_set, cam1_source_user_set);
    if (g_mediamtx_bin.empty()) {
        g_mediamtx_bin = default_mediamtx_binary_path();
    }
    if (g_record_output_dir.empty()) {
        g_record_output_dir = default_record_output_dir();
    }
    std::string resolved_ffmpeg_bin = resolve_ffmpeg_binary_path();
    if (!resolved_ffmpeg_bin.empty()) {
        g_ffmpeg_bin = resolved_ffmpeg_bin;
    }

    refresh_rtsp_urls();
    print_banner();
    printf("[RTSP] 推流目标: %s | %s | %s | %s\n",
           g_rtsp_url_0.c_str(), g_rtsp_url_1.c_str(),
           g_rtsp_url_mosaic.c_str(), g_rtsp_url_video.c_str());
    printf("[RTSP] 预绑定 DMA: %s (RTSP_PREBIND_DMA=%s)\n",
           rtsp_prebind_dma ? "开启" : "关闭",
           rtsp_prebind_dma ? "1" : "0");
    printf("[RTSP] mediamtx 自动启动: %s | bin=%s | log=%s\n",
           g_mediamtx_auto_start ? "开启" : "关闭",
           g_mediamtx_bin.c_str(),
           g_mediamtx_log.c_str());
    printf("[Record] 输出目录: %s | ffmpeg=%s\n",
           g_record_output_dir.c_str(),
           g_ffmpeg_bin.c_str());
    printf("[Input] Cam0 输入源: %s\n", g_input_source_cam0.c_str());
    printf("[Input] Cam1 输入源: %s\n", g_input_source_cam1.c_str());
    printf("[Tracker] 默认算法: %s | ReID: %s\n",
           rknn_lite::get_tracker_backend_name().c_str(),
           rknn_lite::resolve_tracker_reid_model().empty() ? "(none)" : rknn_lite::resolve_tracker_reid_model().c_str());
    printf("[AlertReport] 目标: http://%s:%d%s | cam0->%d cam1->%d | cooldown=%dms\n",
           g_report_server_host.c_str(), g_report_server_port, g_report_server_path.c_str(),
           g_report_camera_id_cam0, g_report_camera_id_cam1, g_box_count_alert_cooldown_ms.load());
    printf("[ForbiddenArea] 拉取: http://%s:%d%s | interval=%dms timeout=%dms | intrusion_cooldown=%dms\n",
           g_report_server_host.c_str(), g_report_server_port, g_forbidden_area_path.c_str(),
           g_forbidden_area_fetch_interval_ms.load(),
           g_forbidden_area_fetch_timeout_ms.load(),
           g_intrusion_alert_cooldown_ms.load());

    if (g_mediamtx_auto_start) {
        std::string mediamtx_bootstrap_detail;
        (void)start_mediamtx_if_needed("service startup", &mediamtx_bootstrap_detail);
    }

    // 初始化 slot 容器，模型在 /api/inference/on 时懒加载
    int total_slots = SLOTS_PER_CAM * 2;
    {
        std::lock_guard<std::mutex> lock(g_model_mutex);
        g_rkpool.assign(total_slots, nullptr);
    }
    auto& rkpool = g_rkpool;

    // 打开输入源：支持 /dev/video*、数字索引、RTSP/文件流
    v4l2_dmabuf::CaptureContext dmabuf_cap0;
    v4l2_dmabuf::CaptureContext dmabuf_cap1;
    cv::VideoCapture cap0;
    cv::VideoCapture cap1;
    std::string cap0_norm;
    std::string cap1_norm;
    std::string cap0_err;
    std::string cap1_err;

    bool cap0_ok = open_camera_input_source(g_input_source_cam0, 0, 640, 480, 30,
                                            &dmabuf_cap0, &cap0,
                                            &g_input_source_cam0_dmabuf, &cap0_norm, &cap0_err);
    bool cap1_ok = open_camera_input_source(g_input_source_cam1, 2, 640, 480, 30,
                                            &dmabuf_cap1, &cap1,
                                            &g_input_source_cam1_dmabuf, &cap1_norm, &cap1_err);
    if (!cap0_norm.empty()) g_input_source_cam0 = cap0_norm;
    if (!cap1_norm.empty()) g_input_source_cam1 = cap1_norm;

    if (cap0_ok) {
        if (g_input_source_cam0_dmabuf) {
            printf("[Capture] Cam0 输入已打开(DMABUF): %s (%dx%d @ %dfps)\n",
                   g_input_source_cam0.c_str(), dmabuf_cap0.width, dmabuf_cap0.height, 30);
        } else {
            int w = (int)cap0.get(cv::CAP_PROP_FRAME_WIDTH);
            int h = (int)cap0.get(cv::CAP_PROP_FRAME_HEIGHT);
            double fps = cap0.get(cv::CAP_PROP_FPS);
            printf("[Capture] Cam0 输入已打开(OpenCV): %s (%dx%d @ %.1ffps)\n",
                   g_input_source_cam0.c_str(), w, h, fps);
        }
    } else {
        printf("[Capture] 警告: Cam0 输入打开失败: source=%s, err=%s\n",
               g_input_source_cam0.c_str(), cap0_err.c_str());
    }

    if (cap1_ok) {
        if (g_input_source_cam1_dmabuf) {
            printf("[Capture] Cam1 输入已打开(DMABUF): %s (%dx%d @ %dfps)\n",
                   g_input_source_cam1.c_str(), dmabuf_cap1.width, dmabuf_cap1.height, 30);
        } else {
            int w = (int)cap1.get(cv::CAP_PROP_FRAME_WIDTH);
            int h = (int)cap1.get(cv::CAP_PROP_FRAME_HEIGHT);
            double fps = cap1.get(cv::CAP_PROP_FPS);
            printf("[Capture] Cam1 输入已打开(OpenCV): %s (%dx%d @ %.1ffps)\n",
                   g_input_source_cam1.c_str(), w, h, fps);
        }
    } else {
        printf("[Capture] 警告: Cam1 输入打开失败: source=%s, err=%s\n",
               g_input_source_cam1.c_str(), cap1_err.c_str());
    }

    // 初始化 slot 状态
    struct SlotState {
        std::atomic<bool> busy{false};
        std::atomic<bool> ready{false};
        std::thread::id thread_id;
    };

    std::vector<SlotState> slot_states(total_slots);
#ifdef USE_RTSP_MPP
    std::vector<SlotBgrDmabuf> slot_output_buffers(total_slots);
#endif

    // 初始化跟踪结果向量
    g_tracker_results.resize(total_slots);

    // FPS 统计
    int frames0 = 0, frames1 = 0;
    int push0 = 0, push1 = 0, push_mosaic = 0;
    bool video_raw_pace_initialized = false;
    auto video_raw_next_deadline = std::chrono::steady_clock::now();
    long long last_source_reopen_ms[2] = {0, 0};
    const int source_reopen_interval_ms = 2000;
    struct timeval time_start, time_now;
    gettimeofday(&time_start, nullptr);
    long last_fps_time_ms = time_start.tv_sec * 1000 + time_start.tv_usec / 1000;
    // 启动 HTTP 服务器
    std::thread http_thread(http_server_thread);
    // 启动 CPU/NPU 使用率监控（每 10s 打印均值）
    std::thread usage_thread(usage_monitor_loop);

    refresh_forbidden_area_cache_if_needed(0);
    refresh_forbidden_area_cache_if_needed(1);

    printf("[Main] 流水线已启动\n");

    // 主循环
    while (g_running) {
        if (g_model_switching.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (!g_model_loaded.load()) {
#ifdef USE_RTSP_MPP
            // 未加载模型时，仍允许 RTSP 裸流（不做推理）
            if (g_rtsp_streaming.load()) {
                if (g_video_mode.load()) {
                    if (!g_video_rtsp_raw_running.load()) {
                        start_video_rtsp_raw_thread_if_needed();
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                    continue;
                }

                video_raw_pace_initialized = false;

                bool got_frame = false;

                {
                    cv::Mat raw0;
                    if (read_camera_frame(&dmabuf_cap0, &cap0, &raw0) && !raw0.empty()) {
                        got_frame = true;
                        frames0++;
                        update_latest_frame_global(raw0, 0);
                        update_latest_frame_for_cam(0, raw0, 0);
                        {
                            std::lock_guard<std::mutex> rtsp_lock(g_rtsp_mutex);
                            if (g_rtsp_sender0 && g_rtsp_sender0->inited()) {
                                draw_rtsp_fps_overlay(raw0, g_rtsp_cam0_fps.load());
                                if (g_rtsp_sender0->push(raw0)) {
                                    push0++;
                                    push_mosaic_rtsp_if_needed(0, raw0, push_mosaic);
                                } else {
                                    printf("[RTSP] Cam0 裸流写包失败，已自动停止\n");
                                    g_rtsp_streaming = false;
                                }
                            }
                        }
                    }
                }

                {
                    cv::Mat raw1;
                    if (read_camera_frame(&dmabuf_cap1, &cap1, &raw1) && !raw1.empty()) {
                        got_frame = true;
                        frames1++;
                        update_latest_frame_global(raw1, SLOTS_PER_CAM);
                        update_latest_frame_for_cam(1, raw1, SLOTS_PER_CAM);
                        {
                            std::lock_guard<std::mutex> rtsp_lock(g_rtsp_mutex);
                            if (g_rtsp_sender1 && g_rtsp_sender1->inited()) {
                                draw_rtsp_fps_overlay(raw1, g_rtsp_cam1_fps.load());
                                if (g_rtsp_sender1->push(raw1)) {
                                    push1++;
                                    push_mosaic_rtsp_if_needed(1, raw1, push_mosaic);
                                } else {
                                    printf("[RTSP] Cam1 裸流写包失败，已自动停止\n");
                                    g_rtsp_streaming = false;
                                }
                            }
                        }
                    }
                }

                if (!got_frame) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                gettimeofday(&time_now, nullptr);
                long now_ms = time_now.tv_sec * 1000 + time_now.tv_usec / 1000;
                if (now_ms - last_fps_time_ms >= 1000) {
                    float elapsed = (now_ms - last_fps_time_ms) / 1000.0f;
                    g_cam0_fps.store((int)(frames0 / elapsed));
                    g_cam1_fps.store((int)(frames1 / elapsed));
                    g_rtsp_cam0_fps.store((int)(push0 / elapsed));
                    g_rtsp_cam1_fps.store((int)(push1 / elapsed));
                    g_rtsp_mosaic_fps.store((int)(push_mosaic / elapsed));
                    g_rtsp_video_fps.store(0);
                    if (push0 > 0 || push1 > 0 || push_mosaic > 0) {
                        printf("[FPS-RAW] Cam0: %d FPS | Cam1: %d FPS | RTSP: %d | %d | Mosaic: %d\n",
                               g_cam0_fps.load(), g_cam1_fps.load(), push0, push1, push_mosaic);
                    }
                    frames0 = 0;
                    frames1 = 0;
                    push0 = 0;
                    push1 = 0;
                    push_mosaic = 0;
                    last_fps_time_ms = now_ms;
                }
                continue;
            }
#endif
            g_cam0_fps.store(0);
            g_cam1_fps.store(0);
            g_rtsp_cam0_fps.store(0);
            g_rtsp_cam1_fps.store(0);
            g_rtsp_mosaic_fps.store(0);
            g_rtsp_video_fps.store(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        bool any_done = false;

        // 检查并处理完成的 slot
        for (int i = 0; i < total_slots; i++) {
            if (g_model_switching.load()) break;
            int cam = (i < SLOTS_PER_CAM) ? 0 : 1;

            if (slot_states[i].ready.load()) {
                any_done = true;
                cv::Mat worker_frame;
                std::vector<DetectionResultItem> frame_detections;
                {
                    std::lock_guard<std::mutex> lock(g_model_mutex);
                    if (i < (int)rkpool.size() && rkpool[i]) {
                        worker_frame = rkpool[i]->ori_img;  // shallow copy: avoid clone in hot path
                        if (g_inference_enabled.load()) {
                            frame_detections = rkpool[i]->get_last_detections();
                            if (frame_detections.empty()) {
                                auto tracks = rkpool[i]->get_last_tracks();
                                if (!tracks.empty()) {
                                    frame_detections = build_detections_from_tracks(tracks);
                                }
                            }
                        }
                    }
                }
                if (worker_frame.empty()) {
                    slot_states[i].ready.store(false);
                    continue;
                }

                draw_forbidden_area_overlay_if_available(cam, worker_frame);
                draw_intrusion_boxes_overlay_if_needed(cam, worker_frame, frame_detections);

                // 更新最新帧（供 HTTP API 获取）
                bool should_update_frame = false;
                if (g_video_mode.load()) {
                    should_update_frame = (i == 0);
                } else {
                    should_update_frame = true;
                }

                if (should_update_frame) {
                    update_latest_frame_global(worker_frame, i);
                    if (!g_video_mode.load() && cam >= 0 && cam <= 1) {
                        update_latest_frame_for_cam(cam, worker_frame, i);
                    }
                }

                // RTSP 推流（绘制跟踪框）
#ifdef USE_RTSP_MPP
                if (g_rtsp_streaming.load()) {
                    cv::Mat frame_for_rtsp = worker_frame;  // shallow copy: avoid clone in hot path
                    int rtsp_fps_text = g_video_mode.load()
                        ? g_rtsp_video_fps.load()
                        : ((cam == 0) ? g_rtsp_cam0_fps.load() : g_rtsp_cam1_fps.load());
                    draw_rtsp_fps_overlay(frame_for_rtsp, rtsp_fps_text);

                    bool use_tracker = g_tracker_enabled.load();
                    if (use_tracker) {
                        static int rtsp_skip_redraw_log_counter = 0;
                        if ((rtsp_skip_redraw_log_counter++ % 1000) == 0) {
                            printf("[RTSP] 跳过二次绘制（避免双框）\n");
                        }
                    }

                    {
                        std::lock_guard<std::mutex> rtsp_lock(g_rtsp_mutex);
                        if (g_video_mode.load() && g_rtsp_sender_video) {
                            bool pushed_ok = false;
                            if (i >= 0 && i < (int)slot_output_buffers.size()) {
                                SlotBgrDmabuf& out = slot_output_buffers[i];
                                if (out.fd >= 0 &&
                                    out.width == frame_for_rtsp.cols &&
                                    out.height == frame_for_rtsp.rows &&
                                    out.mat.data == frame_for_rtsp.data) {
                                    pushed_ok = g_rtsp_sender_video->push_bgr_dmabuf(
                                        out.fd, out.width, out.height, out.wstride, out.hstride);
                                }
                            }
                            if (!pushed_ok) {
                                pushed_ok = g_rtsp_sender_video->push(frame_for_rtsp);
                            }
                            if (pushed_ok) {
                                push0++;
                            } else {
                                printf("[RTSP] 视频推流写包失败，已自动停止，请重新调用 /api/rtsp/video/start\n");
                                g_rtsp_streaming = false;
                            }
                        } else if (!g_video_mode.load()) {
                            if (cam == 0 && g_rtsp_sender0) {
                                bool pushed_ok = false;
                                if (i >= 0 && i < (int)slot_output_buffers.size()) {
                                    SlotBgrDmabuf& out = slot_output_buffers[i];
                                    if (out.fd >= 0 &&
                                        out.width == frame_for_rtsp.cols &&
                                        out.height == frame_for_rtsp.rows &&
                                        out.mat.data == frame_for_rtsp.data) {
                                        pushed_ok = g_rtsp_sender0->push_bgr_dmabuf(
                                            out.fd, out.width, out.height, out.wstride, out.hstride);
                                    }
                                }
                                if (!pushed_ok) {
                                    pushed_ok = g_rtsp_sender0->push(frame_for_rtsp);
                                }
                                if (pushed_ok) {
                                    push0++;
                                    push_mosaic_rtsp_if_needed(0, frame_for_rtsp, push_mosaic);
                                } else {
                                    printf("[RTSP] Cam0 推流写包失败，已自动停止，请重新调用 /api/rtsp/start\n");
                                    g_rtsp_streaming = false;
                                }
                            }
                            if (cam == 1 && g_rtsp_sender1) {
                                bool pushed_ok = false;
                                if (i >= 0 && i < (int)slot_output_buffers.size()) {
                                    SlotBgrDmabuf& out = slot_output_buffers[i];
                                    if (out.fd >= 0 &&
                                        out.width == frame_for_rtsp.cols &&
                                        out.height == frame_for_rtsp.rows &&
                                        out.mat.data == frame_for_rtsp.data) {
                                        pushed_ok = g_rtsp_sender1->push_bgr_dmabuf(
                                            out.fd, out.width, out.height, out.wstride, out.hstride);
                                    }
                                }
                                if (!pushed_ok) {
                                    pushed_ok = g_rtsp_sender1->push(frame_for_rtsp);
                                }
                                if (pushed_ok) {
                                    push1++;
                                    push_mosaic_rtsp_if_needed(1, frame_for_rtsp, push_mosaic);
                                } else {
                                    printf("[RTSP] Cam1 推流写包失败，已自动停止，请重新调用 /api/rtsp/start\n");
                                    g_rtsp_streaming = false;
                                }
                            }
                        }
                    }
                }
#endif

                if (cam == 0) frames0++;
                else frames1++;

                slot_states[i].ready.store(false);
            }
        }

        // 给空闲的 slot 分配新任务
        for (int i = 0; i < total_slots; i++) {
            if (g_model_switching.load()) break;
            if (slot_states[i].busy.load() || slot_states[i].ready.load()) continue;

            int cam = (i < SLOTS_PER_CAM) ? 0 : 1;
            rknn_lite* worker = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_model_mutex);
                if (i < (int)rkpool.size()) {
                    worker = rkpool[i];
                }
            }
            if (!worker) continue;

            cv::Mat frame;
            bool frame_ok = false;
            bool do_inference = g_inference_enabled.load();
            CameraDmabufFrameInfo camera_frame_info;
            cv::VideoCapture* cv_cap_for_reopen = nullptr;
            bool should_release_cv_on_fail = false;
#ifdef USE_RTSP_MPP
            SlotBgrDmabuf* out_slot = nullptr;
            if (rtsp_prebind_dma && g_rtsp_streaming.load() && i >= 0 && i < (int)slot_output_buffers.size()) {
                out_slot = &slot_output_buffers[i];
            }
#endif

            // 检查是否是视频文件模式
            bool video_mode = g_video_mode.load();
            if (video_mode) {
                std::lock_guard<std::mutex> lock(g_video_mutex);
                VideoFrameInfo video_frame_info;
                if (!ensure_video_source_open_locked()) {
                        printf("[Video] 无法打开视频: %s\n", g_video_path.c_str());
                        g_video_running = false;
                        g_video_mode = false;
                }
                if (g_video_running.load() && g_video_mode.load()) {
#ifdef USE_RTSP_MPP
                    if (out_slot) {
                        int vw = g_video_width.load();
                        int vh = g_video_height.load();
                        if (vw > 0 && vh > 0 && ensure_slot_bgr_dmabuf(out_slot, vw, vh)) {
                            frame = out_slot->mat;
                        }
                    }
#endif
                    frame_ok = read_video_frame_locked(frame, &video_frame_info);
                }
                if (!frame_ok) {
                    close_video_source_locked();
                    g_video_running = false;
                    g_video_mode = false;
                    printf("[Video] 视频播放完毕\n");
                } else if (video_frame_info.valid) {
                    if (i >= 0 && i < (int)rkpool.size() && rkpool[i]) {
                        rkpool[i]->set_video_dmabuf_frame(video_frame_info.fd, video_frame_info.size,
                                                          video_frame_info.width, video_frame_info.height,
                                                          video_frame_info.wstride, video_frame_info.hstride,
                                                          video_frame_info.drm_format);
                        video_frame_info.fd = -1;
                    }
                } else if (video_frame_info.nv12_valid) {
                    if (i >= 0 && i < (int)rkpool.size() && rkpool[i]) {
                        rkpool[i]->set_video_nv12_frame(video_frame_info.nv12_packed,
                                                        video_frame_info.width, video_frame_info.height,
                                                        video_frame_info.wstride, video_frame_info.hstride);
                    }
                }
            } else {
                // 使用摄像头
                v4l2_dmabuf::CaptureContext* active_cap = (cam == 0) ? &dmabuf_cap0 : &dmabuf_cap1;
                cv::VideoCapture* active_cv_cap = (cam == 0) ? &cap0 : &cap1;
                cv_cap_for_reopen = active_cv_cap;
                should_release_cv_on_fail = !((cam == 0) ? g_input_source_cam0_dmabuf : g_input_source_cam1_dmabuf);
                bool cap_available =
                    ((active_cap && active_cap->fd >= 0 && active_cap->streaming) ||
                     (active_cv_cap && active_cv_cap->isOpened()));
                if (!cap_available) {
                    long long now_ms = monotonic_now_ms();
                    if (now_ms - last_source_reopen_ms[cam] >= source_reopen_interval_ms) {
                        last_source_reopen_ms[cam] = now_ms;
                        bool using_dmabuf = false;
                        std::string normalized_source;
                        std::string reopen_err;
                        const std::string src = (cam == 0) ? g_input_source_cam0 : g_input_source_cam1;
                        int default_index = (cam == 0) ? 0 : 2;
                        bool reopened = open_camera_input_source(src, default_index, 640, 480, 30,
                                                                 active_cap, active_cv_cap,
                                                                 &using_dmabuf, &normalized_source, &reopen_err);
                        if (reopened) {
                            if (cam == 0) {
                                g_input_source_cam0_dmabuf = using_dmabuf;
                                if (!normalized_source.empty()) g_input_source_cam0 = normalized_source;
                            } else {
                                g_input_source_cam1_dmabuf = using_dmabuf;
                                if (!normalized_source.empty()) g_input_source_cam1 = normalized_source;
                            }
                            printf("[Capture] Cam%d 输入重连成功: source=%s mode=%s\n",
                                   cam, normalized_source.c_str(), using_dmabuf ? "dmabuf" : "opencv");
                        } else {
                            printf("[Capture] Cam%d 输入重连失败: source=%s err=%s\n",
                                   cam, src.c_str(), reopen_err.c_str());
                        }
                    }
                }
#ifdef USE_RTSP_MPP
                if (out_slot) {
                    int cw = 0;
                    int ch = 0;
                    if (active_cap && active_cap->fd >= 0 && active_cap->streaming) {
                        cw = active_cap->width;
                        ch = active_cap->height;
                    } else if (active_cv_cap && active_cv_cap->isOpened()) {
                        cw = (int)active_cv_cap->get(cv::CAP_PROP_FRAME_WIDTH);
                        ch = (int)active_cv_cap->get(cv::CAP_PROP_FRAME_HEIGHT);
                    }
                    if (cw > 0 && ch > 0 && ensure_slot_bgr_dmabuf(out_slot, cw, ch)) {
                        frame = out_slot->mat;
                    }
                }
#endif
                frame_ok = acquire_camera_frame(active_cap, active_cv_cap, &frame, &camera_frame_info);
                if (frame_ok && do_inference && camera_frame_info.valid) {
                    (void)worker->prepare_camera_dmabuf_input(camera_frame_info.fd,
                                                              camera_frame_info.width,
                                                              camera_frame_info.height,
                                                              camera_frame_info.wstride,
                                                              camera_frame_info.hstride,
                                                              camera_frame_info.rga_format);
                }
                release_camera_dmabuf_frame(active_cap, &camera_frame_info);
            }

            if (!frame_ok) {
                if (should_release_cv_on_fail && cv_cap_for_reopen && cv_cap_for_reopen->isOpened()) {
                    cv_cap_for_reopen->release();
                }
                continue;
            }

            g_active_jobs.fetch_add(1);
#ifdef USE_RTSP_MPP
            bool bound_dma_output = false;
            if (g_rtsp_streaming.load() && i >= 0 && i < (int)slot_output_buffers.size()) {
                SlotBgrDmabuf& out = slot_output_buffers[i];
                if (rtsp_prebind_dma) {
                    bool same_dma_mat = (out.fd >= 0 &&
                                         out.width == frame.cols &&
                                         out.height == frame.rows &&
                                         out.mat.data == frame.data);
                    if (same_dma_mat) {
                        worker->ori_img = frame;
                        bound_dma_output = true;
                    } else if (ensure_slot_bgr_dmabuf(&out, frame.cols, frame.rows)) {
                        if (out.mat.data != frame.data) {
                            frame.copyTo(out.mat);
                        }
                        worker->ori_img = out.mat;
                        bound_dma_output = true;
                    }
                } else {
                    if (ensure_slot_bgr_dmabuf(&out, frame.cols, frame.rows)) {
                        frame.copyTo(out.mat);
                        worker->ori_img = out.mat;
                        bound_dma_output = true;
                    }
                }
            }
            if (!bound_dma_output)
#endif
            {
                frame.copyTo(worker->ori_img);
            }
            slot_states[i].busy.store(true);
            slot_states[i].ready.store(false);
            slot_states[i].thread_id = std::this_thread::get_id();

            cv::Size img_size = frame.size();

            bool use_tracker = do_inference ? g_tracker_enabled.load() : false;
            int cam_id = (i < 3) ? 0 : 1;
            int tracker_stream_id = g_video_mode.load() ? 2 : cam_id;
            try {
                std::thread([i, &slot_states, worker, do_inference, use_tracker, cam_id, tracker_stream_id, img_size]() {
                worker->set_tracker_stream_id(tracker_stream_id);
                worker->set_use_tracker(use_tracker);
                int ret = 0;
                if (do_inference) {
                    ret = worker->interf_detect_only();
                }
                if (ret == 0) {
                    g_current_img_size = img_size;

                    if (do_inference) {
                        int detection_count = worker->get_last_detection_count();
                        if (cam_id == 0) {
                            g_cam0_detection_count = detection_count;
                        } else {
                            g_cam1_detection_count = detection_count;
                        }

                        maybe_trigger_detection_count_alert(cam_id, detection_count, worker->ori_img);

                        auto tracks = worker->get_last_tracks();
                        auto detections = worker->get_last_detections();
                        if (detections.empty() && !tracks.empty()) {
                            detections = build_detections_from_tracks(tracks);
                        }
                        maybe_trigger_forbidden_area_intrusion_alert(cam_id, detections, worker->ori_img);

                        if (use_tracker) {
                            std::lock_guard<std::mutex> lock(g_tracker_mutex);
                            if (i < (int)g_tracker_results.size()) {
                                g_tracker_results[i] = tracks;
                            }
                        }
                    } else {
                        if (cam_id == 0) g_cam0_detection_count = 0;
                        else g_cam1_detection_count = 0;
                    }

                    slot_states[i].ready.store(true);
                }
                if (!do_inference) {
                    slot_states[i].ready.store(true);
                }
                slot_states[i].busy.store(false);
                g_active_jobs.fetch_sub(1);
                }).detach();
            } catch (...) {
                slot_states[i].busy.store(false);
                slot_states[i].ready.store(false);
                g_active_jobs.fetch_sub(1);
            }
        }

        if (!any_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // FPS 统计
        gettimeofday(&time_now, nullptr);
        long now_ms = time_now.tv_sec * 1000 + time_now.tv_usec / 1000;
        if (now_ms - last_fps_time_ms >= 1000) {
            float elapsed = (now_ms - last_fps_time_ms) / 1000.0f;
            g_cam0_fps.store((int)(frames0 / elapsed));
            g_cam1_fps.store((int)(frames1 / elapsed));
            if (g_video_mode.load()) {
                g_rtsp_video_fps.store((int)(push0 / elapsed));
                g_rtsp_cam0_fps.store(0);
                g_rtsp_cam1_fps.store(0);
                g_rtsp_mosaic_fps.store(0);
            } else {
                g_rtsp_cam0_fps.store((int)(push0 / elapsed));
                g_rtsp_cam1_fps.store((int)(push1 / elapsed));
                g_rtsp_mosaic_fps.store((int)(push_mosaic / elapsed));
                g_rtsp_video_fps.store(0);
            }

#ifdef USE_RTSP_MPP
            if (g_rtsp_streaming.load() && (push0 > 0 || push1 > 0 || push_mosaic > 0)) {
                printf("[FPS] Cam0: %d FPS | Cam1: %d FPS | RTSP: %d | %d | Mosaic: %d\n",
                       g_cam0_fps.load(), g_cam1_fps.load(), push0, push1, push_mosaic);
            }
#else
            if (frames0 > 0 || frames1 > 0) {
                printf("[FPS] Cam0: %d FPS | Cam1: %d FPS\n",
                       g_cam0_fps.load(), g_cam1_fps.load());
            }
#endif

            frames0 = 0;
            frames1 = 0;
            push0 = 0;
            push1 = 0;
            push_mosaic = 0;
            last_fps_time_ms = now_ms;
        }
    }

    printf("[Main] 等待退出...\n");
    http_thread.join();
    if (usage_thread.joinable()) {
        usage_thread.join();
    }

    stop_all_recordings();

    std::string unload_msg;
    if (g_model_loaded.load() || g_active_jobs.load() > 0) {
        if (!unload_model_runtime(&unload_msg)) {
            printf("[Main] 警告: %s\n", unload_msg.c_str());
        } else {
            printf("[Main] 模型资源已清理\n");
        }
    }

    if (cap0.isOpened()) cap0.release();
    if (cap1.isOpened()) cap1.release();
    v4l2_dmabuf::close_capture(&dmabuf_cap0);
    v4l2_dmabuf::close_capture(&dmabuf_cap1);

#ifdef USE_RTSP_MPP
    {
        std::lock_guard<std::mutex> lock(g_rtsp_mutex);
        g_video_rtsp_raw_stop = true;
        stop_rtsp_senders_locked();
    }
    for (auto& out : slot_output_buffers) {
        release_slot_bgr_dmabuf(&out);
    }
#endif

    stop_video_reader();

    printf("[Main] 服务已退出\n");
    return 0;
}
