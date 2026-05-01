// RTSP streaming, mosaic composition, and raw-video thread.
// All code in this file is guarded by USE_RTSP_MPP.
// Extracted from main_http_ctrl.cc during refactoring.

#ifdef USE_RTSP_MPP

#include <stdio.h>
#include <string.h>
#include <chrono>
#include <thread>
#include <algorithm>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "http_ctrl_rtsp.h"
#include "http_ctrl_globals.h"
#include "http_ctrl_raw_video_rtsp.h"

// ---------------------------------------------------------------------------
// SlotBgrDmabuf helpers
// ---------------------------------------------------------------------------

void release_slot_bgr_dmabuf(SlotBgrDmabuf* slot) {
    if (!slot) return;
    slot->mat.release();
    if (slot->buffer) {
        mpp_buffer_put(slot->buffer);
    }
    *slot = SlotBgrDmabuf{};
}

bool ensure_slot_bgr_dmabuf(SlotBgrDmabuf* slot, int width, int height) {
    if (!slot || width <= 0 || height <= 0) return false;
    if (slot->buffer && slot->ptr && slot->fd >= 0 &&
        slot->width == width && slot->height == height) {
        return true;
    }

    release_slot_bgr_dmabuf(slot);

    const int size = width * height * 3;
    MPP_RET ret = mpp_buffer_get(nullptr, &slot->buffer, size);
    if (ret != MPP_OK || !slot->buffer) {
        release_slot_bgr_dmabuf(slot);
        return false;
    }

    slot->fd = mpp_buffer_get_fd(slot->buffer);
    slot->ptr = mpp_buffer_get_ptr(slot->buffer);
    if (slot->fd < 0 || slot->ptr == nullptr) {
        release_slot_bgr_dmabuf(slot);
        return false;
    }

    slot->size = size;
    slot->width = width;
    slot->height = height;
    slot->wstride = width;
    slot->hstride = height;
    slot->mat = cv::Mat(height, width, CV_8UC3, slot->ptr, (size_t)width * 3);
    return !slot->mat.empty();
}

// ---------------------------------------------------------------------------
// FPS overlay
// ---------------------------------------------------------------------------

void draw_rtsp_fps_overlay(cv::Mat& frame, int fps_value) {
    if (frame.empty()) return;
    if (fps_value < 0) fps_value = 0;
    char text[64];
    snprintf(text, sizeof(text), "FPS: %d", fps_value);

    int baseline = 0;
    const double font_scale = 0.8;
    const int thickness = 2;
    cv::Size text_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline);
    const int pad = 6;
    const int tx = 10;
    const int ty = 10 + text_size.height;
    int bx = std::max(0, tx - pad);
    int by = std::max(0, ty - text_size.height - pad);
    int bw = text_size.width + pad * 2;
    int bh = text_size.height + baseline + pad * 2;
    if (bx + bw > frame.cols) bw = std::max(1, frame.cols - bx);
    if (by + bh > frame.rows) bh = std::max(1, frame.rows - by);

    cv::rectangle(frame, cv::Rect(bx, by, bw, bh), cv::Scalar(0, 0, 0), -1);
    cv::putText(frame, text, cv::Point(tx, ty),
                cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(0, 255, 0), thickness);
}

// ---------------------------------------------------------------------------
// RTSP sender management
// ---------------------------------------------------------------------------

void destroy_rtsp_sender(RtspMppSender*& sender) {
    if (!sender) return;
    sender->destroy();
    delete sender;
    sender = nullptr;
}

void stop_rtsp_senders_locked() {
    destroy_rtsp_sender(g_rtsp_sender0);
    destroy_rtsp_sender(g_rtsp_sender1);
    destroy_rtsp_sender(g_rtsp_sender_mosaic);
    destroy_rtsp_sender(g_rtsp_sender_video);
    g_rtsp_mosaic_last_push_ms.store(0);
}

// ---------------------------------------------------------------------------
// Mosaic composition
// ---------------------------------------------------------------------------

constexpr int MOSAIC_TILE_WIDTH = 640;
constexpr int MOSAIC_TILE_HEIGHT = 480;
constexpr int MOSAIC_OUTPUT_WIDTH = MOSAIC_TILE_WIDTH * 2;
constexpr int MOSAIC_OUTPUT_HEIGHT = MOSAIC_TILE_HEIGHT;
constexpr int MOSAIC_TARGET_FPS = 30;

void draw_mosaic_placeholder(cv::Mat& tile, const std::string& label) {
    tile = cv::Mat::zeros(MOSAIC_TILE_HEIGHT, MOSAIC_TILE_WIDTH, CV_8UC3);
    cv::putText(tile, label,
                cv::Point(40, MOSAIC_TILE_HEIGHT / 2),
                cv::FONT_HERSHEY_SIMPLEX, 0.9,
                cv::Scalar(180, 180, 180), 2, cv::LINE_AA);
}

void prepare_mosaic_tile(const cv::Mat& src, const std::string& placeholder, cv::Mat& tile) {
    if (src.empty()) {
        draw_mosaic_placeholder(tile, placeholder);
        return;
    }

    if (src.cols == MOSAIC_TILE_WIDTH && src.rows == MOSAIC_TILE_HEIGHT) {
        tile = src.clone();
    } else {
        cv::resize(src, tile, cv::Size(MOSAIC_TILE_WIDTH, MOSAIC_TILE_HEIGHT));
    }
}

bool build_mosaic_frame(int updated_cam, const cv::Mat& updated_frame, cv::Mat& mosaic_frame) {
    cv::Mat cam0_frame;
    cv::Mat cam1_frame;

    if (updated_cam == 0 && !updated_frame.empty()) {
        cam0_frame = updated_frame;
    } else {
        std::lock_guard<std::mutex> lock(g_latest_frame_cam_mutex[0]);
        if (!g_latest_frame_cam[0].empty()) {
            cam0_frame = g_latest_frame_cam[0].clone();
        }
    }

    if (updated_cam == 1 && !updated_frame.empty()) {
        cam1_frame = updated_frame;
    } else {
        std::lock_guard<std::mutex> lock(g_latest_frame_cam_mutex[1]);
        if (!g_latest_frame_cam[1].empty()) {
            cam1_frame = g_latest_frame_cam[1].clone();
        }
    }

    if (cam0_frame.empty() && cam1_frame.empty()) {
        return false;
    }

    cv::Mat left_tile;
    cv::Mat right_tile;
    prepare_mosaic_tile(cam0_frame, "cam0 unavailable", left_tile);
    prepare_mosaic_tile(cam1_frame, "cam1 unavailable", right_tile);

    mosaic_frame = cv::Mat::zeros(MOSAIC_OUTPUT_HEIGHT, MOSAIC_OUTPUT_WIDTH, CV_8UC3);
    left_tile.copyTo(mosaic_frame(cv::Rect(0, 0, MOSAIC_TILE_WIDTH, MOSAIC_TILE_HEIGHT)));
    right_tile.copyTo(mosaic_frame(cv::Rect(MOSAIC_TILE_WIDTH, 0, MOSAIC_TILE_WIDTH, MOSAIC_TILE_HEIGHT)));

    cv::line(mosaic_frame,
             cv::Point(MOSAIC_TILE_WIDTH, 0),
             cv::Point(MOSAIC_TILE_WIDTH, MOSAIC_OUTPUT_HEIGHT),
             cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
    cv::putText(mosaic_frame, "cam0",
                cv::Point(16, 34), cv::FONT_HERSHEY_SIMPLEX, 0.9,
                cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    cv::putText(mosaic_frame, "cam1",
                cv::Point(MOSAIC_TILE_WIDTH + 16, 34), cv::FONT_HERSHEY_SIMPLEX, 0.9,
                cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    return true;
}

bool should_push_mosaic_frame() {
    long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    long long last_ms = g_rtsp_mosaic_last_push_ms.load();
    long long min_interval_ms = 1000 / MOSAIC_TARGET_FPS;
    if (last_ms > 0 && (now_ms - last_ms) < min_interval_ms) {
        return false;
    }
    g_rtsp_mosaic_last_push_ms.store(now_ms);
    return true;
}

void push_mosaic_rtsp_if_needed(int updated_cam, const cv::Mat& updated_frame, int& push_mosaic_counter) {
    if (updated_cam < 0 || updated_cam > 1) return;
    if (!g_rtsp_sender_mosaic || !g_rtsp_sender_mosaic->inited()) return;
    if (!should_push_mosaic_frame()) return;

    cv::Mat mosaic_frame;
    if (!build_mosaic_frame(updated_cam, updated_frame, mosaic_frame) || mosaic_frame.empty()) {
        return;
    }

    if (g_rtsp_sender_mosaic->push(mosaic_frame)) {
        push_mosaic_counter++;
    } else {
        printf("[RTSP] Mosaic 推流写包失败，已自动停止，请重新调用 /api/rtsp/start\n");
        destroy_rtsp_sender(g_rtsp_sender_mosaic);
    }
}

// ---------------------------------------------------------------------------
// Raw video RTSP thread
// ---------------------------------------------------------------------------

void video_rtsp_raw_loop() {
    g_video_rtsp_raw_running = true;
    g_video_rtsp_raw_stop = false;

    RawVideoRtspDecoderContext decoder;
    if (!OpenRawVideoRtspDecoder(g_video_path, &decoder)) {
        printf("[RTSP-RAW-VIDEO] 无法打开视频: %s\n", g_video_path.c_str());
        g_video_rtsp_raw_running = false;
        return;
    }

    double fps = decoder.fps;
    if (fps <= 0.0) fps = 25.0;
    g_rtsp_video_fps.store((int)(fps + 0.5f));
    auto frame_interval = std::chrono::microseconds((int)(1000000.0 / fps));
    auto next_deadline = std::chrono::steady_clock::now();
    int pushed = 0;

    while (!g_video_rtsp_raw_stop.load() && g_running.load()) {
        if (!g_rtsp_streaming.load() || !g_video_mode.load() || g_model_loaded.load()) {
            break;
        }

        cv::Mat frame;
        RawVideoDmabufFrameInfo frame_info;
        if (!ReadRawVideoRtspFrame(&decoder, &frame, &frame_info) ||
            (!frame_info.valid && frame.empty())) {
            ClearRawVideoDmabufFrameInfo(&frame_info);
            if (g_video_loop.load()) {
                CloseRawVideoRtspDecoder(&decoder);
                if (!OpenRawVideoRtspDecoder(g_video_path, &decoder)) {
                    printf("[RTSP-RAW-VIDEO] 循环重开失败: %s\n", g_video_path.c_str());
                    break;
                }
                fps = decoder.fps;
                if (fps <= 0.0) fps = 25.0;
                frame_interval = std::chrono::microseconds((int)(1000000.0 / fps));
                next_deadline = std::chrono::steady_clock::now();
                continue;
            }
            printf("[RTSP-RAW-VIDEO] 视频播放完毕\n");
            break;
        }

        bool pushed_ok = false;
        {
            std::lock_guard<std::mutex> lock(g_rtsp_mutex);
            if (g_rtsp_sender_video && g_rtsp_sender_video->inited()) {
                if (!frame.empty()) {
                    draw_rtsp_fps_overlay(frame, g_rtsp_video_fps.load());
                    pushed_ok = g_rtsp_sender_video->push(frame);
                } else if (frame_info.valid) {
                    pushed_ok = g_rtsp_sender_video->push_dmabuf(
                        frame_info.fd,
                        frame_info.size,
                        frame_info.width,
                        frame_info.height,
                        frame_info.wstride,
                        frame_info.hstride,
                        frame_info.drm_format);
                }
            }
        }
        ClearRawVideoDmabufFrameInfo(&frame_info);
        if (!pushed_ok) {
            printf("[RTSP-RAW-VIDEO] 推流写包失败，停止裸流线程\n");
            break;
        }

        ++pushed;
        if ((pushed % 120) == 0) {
            printf("[RTSP-RAW-VIDEO] pushed=%d fps=%.1f\n", pushed, fps);
        }

        next_deadline += frame_interval;
        std::this_thread::sleep_until(next_deadline);
    }

    CloseRawVideoRtspDecoder(&decoder);
    g_rtsp_video_fps.store(0);
    g_video_rtsp_raw_running = false;
}

void start_video_rtsp_raw_thread_if_needed() {
    if (g_video_rtsp_raw_running.load()) {
        return;
    }
    // 线程结束后对象仍可能是 joinable，重新赋值前先回收，避免 std::terminate。
    if (g_video_rtsp_raw_thread.joinable()) {
        g_video_rtsp_raw_thread.join();
    }
    g_video_rtsp_raw_thread = std::thread(video_rtsp_raw_loop);
}

void stop_video_rtsp_raw_thread() {
    g_video_rtsp_raw_stop = true;
    if (g_video_rtsp_raw_thread.joinable()) {
        g_video_rtsp_raw_thread.join();
    }
    g_video_rtsp_raw_running = false;
}

#endif // USE_RTSP_MPP
