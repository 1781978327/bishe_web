// Video file source reading and management.
// Extracted from main_http_ctrl.cc during refactoring.

#include <stdio.h>
#include <chrono>
#include <thread>

#include "http_ctrl_video.h"
#include "http_ctrl_globals.h"

#ifdef USE_RTSP_MPP
#include "ffmpeg_rkmpp_reader.h"
#include "http_ctrl_rtsp.h"  // for stop_video_rtsp_raw_thread
#endif

void clear_video_queue_locked() {
    while (!g_video_frames.empty()) {
        g_video_frames.pop();
    }
}

bool open_video_source_locked() {
#ifdef USE_RTSP_MPP
    if (!g_video_hw_reader) {
        g_video_hw_reader.reset(new FFmpegRkmppReader());
    }
    if (g_video_hw_reader->Open(g_video_path)) {
        if (g_video_hw_reader->Width() > 0) g_video_width = g_video_hw_reader->Width();
        if (g_video_hw_reader->Height() > 0) g_video_height = g_video_hw_reader->Height();
        if (g_video_hw_reader->Fps() >= 1.0 && g_video_hw_reader->Fps() <= 120.0) {
            g_video_fps = (int)(g_video_hw_reader->Fps() + 0.5);
        }
        g_video_hw_enabled = true;
        printf("[Video] FFmpeg DRM_PRIME 已打开: %dx%d @ %.1f fps (decoder=%s)\n",
               g_video_width.load(), g_video_height.load(), g_video_hw_reader->Fps(),
               g_video_hw_reader->DecoderName().c_str());
        return true;
    }
    if (g_video_hw_reader) {
        g_video_hw_reader->Close();
    }
    g_video_hw_enabled = false;
#endif

    g_video_cap.open(g_video_path, cv::CAP_FFMPEG);
    if (!g_video_cap.isOpened()) {
        return false;
    }

    int vw = (int)g_video_cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int vh = (int)g_video_cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    double vf = g_video_cap.get(cv::CAP_PROP_FPS);
    if (vw > 0) g_video_width = vw;
    if (vh > 0) g_video_height = vh;
    if (vf >= 1.0 && vf <= 120.0) g_video_fps = (int)(vf + 0.5);
    printf("[Video] OpenCV/FFmpeg 回退已打开: %dx%d @ %.1f fps\n", g_video_width.load(), g_video_height.load(), vf);
    return true;
}

bool read_video_frame_locked(cv::Mat& frame, VideoFrameInfo* frame_info) {
    if (frame_info) {
        *frame_info = VideoFrameInfo{};
    }
#ifdef USE_RTSP_MPP
    if (g_video_hw_enabled.load() && g_video_hw_reader) {
        FFmpegRkmppReader::FrameInfo reader_info;
        FFmpegRkmppReader::FrameInfo* reader_info_ptr = frame_info ? &reader_info : nullptr;
        auto copy_reader_info = [&]() {
            if (!frame_info || !reader_info_ptr) return;
            frame_info->nv12_valid = reader_info_ptr->nv12_valid;
            frame_info->nv12_packed = reader_info_ptr->nv12_packed;
            frame_info->valid = reader_info_ptr->valid;
            frame_info->fd = reader_info_ptr->fd;
            frame_info->size = reader_info_ptr->size;
            frame_info->width = reader_info_ptr->width;
            frame_info->height = reader_info_ptr->height;
            frame_info->wstride = reader_info_ptr->wstride;
            frame_info->hstride = reader_info_ptr->hstride;
            frame_info->drm_format = reader_info_ptr->drm_format;
        };

        if (g_video_hw_reader->ReadFrame(frame, reader_info_ptr) && !frame.empty()) {
            copy_reader_info();
            return true;
        }
        if (g_video_loop.load()) {
            g_video_hw_reader->Close();
            if (g_video_hw_reader->Open(g_video_path) && g_video_hw_reader->ReadFrame(frame, reader_info_ptr) && !frame.empty()) {
                if (g_video_hw_reader->Width() > 0) g_video_width = g_video_hw_reader->Width();
                if (g_video_hw_reader->Height() > 0) g_video_height = g_video_hw_reader->Height();
                if (g_video_hw_reader->Fps() >= 1.0 && g_video_hw_reader->Fps() <= 120.0) {
                    g_video_fps = (int)(g_video_hw_reader->Fps() + 0.5);
                }
                copy_reader_info();
                return true;
            }
        }
        g_video_hw_reader->Close();
        g_video_hw_enabled = false;
        return false;
    }
#endif

    if (!g_video_cap.isOpened()) {
        return false;
    }
    if (g_video_cap.read(frame) && !frame.empty()) {
        return true;
    }
    if (g_video_loop.load()) {
        g_video_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
        return g_video_cap.read(frame) && !frame.empty();
    }
    return false;
}

bool ensure_video_source_open_locked() {
    bool video_opened =
#ifdef USE_RTSP_MPP
        (g_video_hw_enabled.load() && g_video_hw_reader && g_video_hw_reader->IsOpen()) ||
#endif
        g_video_cap.isOpened();
    if (video_opened) {
        return true;
    }
    return open_video_source_locked();
}

void close_video_source_locked() {
#ifdef USE_RTSP_MPP
    if (g_video_hw_reader) {
        g_video_hw_reader->Close();
    }
    g_video_hw_enabled = false;
#endif
    if (g_video_cap.isOpened()) {
        g_video_cap.release();
    }
}

void video_reader_loop() {
    const size_t kMaxQueuedFrames = 8;

    while (g_running.load()) {
        cv::Mat frame;
        bool frame_ok = false;

        {
            std::unique_lock<std::mutex> lock(g_video_mutex);
            if (!g_video_running.load()) {
                break;
            }

            bool video_opened =
#ifdef USE_RTSP_MPP
                (g_video_hw_enabled.load() && g_video_hw_reader && g_video_hw_reader->IsOpen()) ||
#endif
                g_video_cap.isOpened();
            if (!video_opened) {
                if (!ensure_video_source_open_locked()) {
                    printf("[Video] 无法打开视频: %s\n", g_video_path.c_str());
                    g_video_running = false;
                    g_video_mode = false;
                    clear_video_queue_locked();
                    break;
                }
            }

            frame_ok = read_video_frame_locked(frame);

            if (!frame_ok) {
                close_video_source_locked();
                g_video_running = false;
                g_video_mode = false;
                clear_video_queue_locked();
                printf("[Video] 视频播放完毕\n");
                break;
            }

            if (g_video_frames.size() >= kMaxQueuedFrames) {
                g_video_frames.pop();
            }
            g_video_frames.push(frame.clone());
        }

        g_video_cv.notify_one();
    }

    g_video_cv.notify_all();
}

void stop_video_reader() {
#ifdef USE_RTSP_MPP
    stop_video_rtsp_raw_thread();
#endif
    {
        std::lock_guard<std::mutex> lock(g_video_mutex);
        g_video_running = false;
        g_video_mode = false;
        clear_video_queue_locked();
        close_video_source_locked();
    }
    g_video_cv.notify_all();
    if (g_video_thread.joinable()) {
        g_video_thread.join();
    }
}

void start_video_reader(const std::string& path, bool loop) {
    stop_video_reader();
    {
        std::lock_guard<std::mutex> lock(g_video_mutex);
        g_video_path = path;
        g_video_loop = loop;
        g_video_mode = true;
        g_video_running = true;
        g_video_width = 1920;
        g_video_height = 1080;
        g_video_fps = 25;
        clear_video_queue_locked();
    }
}
