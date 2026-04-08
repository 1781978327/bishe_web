/**
 * RTSP 推流器 - 基于管道 + ffmpeg 子进程
 * 架构: BGR 帧 -> MPP H.264 编码 -> ffmpeg stdin -> RTSP 服务器
 * 参考: /home/orangepi/Desktop/rkmpp编码案例/main.cpp
 */
#include "rtsp_mpp_sender.h"
#include "command_mpp.h"
#include <opencv2/imgproc.hpp>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#ifndef MPP_ALIGN
#define MPP_ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#endif

using namespace cv;

static const int RTSP_QUEUE_MAX = 8;
static const int RTSP_PKT_BUF_SIZE = 256 * 1024;

struct PktBuf {
    std::vector<uint8_t> buf;
    size_t size = 0;
    PktBuf() : buf(RTSP_PKT_BUF_SIZE), size(0) {}
};

struct RtspPipeContext {
    int width = 0, height = 0, fps = 0;
    FILE* pipe = nullptr;
    std::thread writer_thread;
    std::queue<PktBuf> queue;
    std::queue<PktBuf> free_list;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> stop{false};
    int write_count = 0;

    // MPP encoder
    MppCtx mppCtx = nullptr;
    MppApi* mppApi = nullptr;
    MppBufferGroup group = nullptr;
    MppEncCfg cfg = nullptr;
    MppTask task = nullptr;
    int hor_stride = 0, ver_stride = 0;
    size_t image_size = 0;
    int frame_count = 0;
    int mpp_inited = 0;
};

static void rtsp_writer_thread(FILE* pipe, std::queue<PktBuf>& q,
                                std::queue<PktBuf>& free_list,
                                std::mutex& m, std::condition_variable& cv,
                                std::atomic<bool>& stop, int& write_count) {
    while (true) {
        PktBuf pkt;
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&] { return stop.load() || !q.empty(); });
            if (stop.load() && q.empty()) break;
            if (q.empty()) continue;
            pkt = std::move(q.front());
            q.pop();
        }
        if (pkt.size == 0) break;
        size_t written = fwrite(pkt.buf.data(), 1, pkt.size, pipe);
        if (written != pkt.size) {
            std::cerr << "pipe write error: " << written << "/" << pkt.size << std::endl;
        }
        write_count++;
        if (write_count % 60 == 0) fflush(pipe);
        {
            std::lock_guard<std::mutex> lock(m);
            free_list.push(std::move(pkt));
            cv.notify_one();
        }
    }
    fflush(pipe);
    std::cout << "RTSP writer thread exit, wrote " << write_count << " frames" << std::endl;
}

static MPP_RET init_mpp_encoder(RtspPipeContext* ctx) {
    MPP_RET ret = mpp_create(&ctx->mppCtx, &ctx->mppApi);
    if (ret != MPP_OK) { std::cerr << "mpp_create failed" << std::endl; return ret; }
    ret = mpp_init(ctx->mppCtx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) { std::cerr << "mpp_init failed" << std::endl; return ret; }
    ret = mpp_enc_cfg_init(&ctx->cfg);
    if (ret != MPP_OK) { std::cerr << "mpp_enc_cfg_init failed" << std::endl; return ret; }
    ret = ctx->mppApi->control(ctx->mppCtx, MPP_ENC_GET_CFG, ctx->cfg);
    if (ret != MPP_OK) { std::cerr << "MPP_ENC_GET_CFG failed" << std::endl; return ret; }

    // prep
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:width", ctx->width);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:height", ctx->height);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:hor_stride", ctx->hor_stride);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:ver_stride", ctx->ver_stride);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:format", MPP_FMT_YUV420P);

    // rate control - best quality VBR
    int pixels = ctx->width * ctx->height;
    int bps = (pixels <= 640 * 480) ? 1000000 : 2000000;
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:mode", MPP_ENC_RC_MODE_VBR);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:bps_max", bps * 3 / 2);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:bps_min", bps / 2);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:quality", MPP_ENC_RC_QUALITY_BEST);

    // fps
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_in_num", ctx->fps);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_out_num", ctx->fps);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_out_denorm", 1);

    // profile
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:profile", 100);  // High profile
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:level", 42);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:entropy", 1);  // CABAC
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:qp_init", 26);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:qp_step", 4);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:qp_min", 10);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:qp_max", 51);

    ret = ctx->mppApi->control(ctx->mppCtx, MPP_ENC_SET_CFG, ctx->cfg);
    if (ret != MPP_OK) { std::cerr << "MPP_ENC_SET_CFG failed" << std::endl; return ret; }

    // DRM buffer group for hardware encoding
    ret = mpp_buffer_group_get_external(&ctx->group, MPP_BUFFER_TYPE_DRM);
    if (ret != MPP_OK) { std::cerr << "mpp_buffer_group_get_external failed" << std::endl; return ret; }

    ctx->mpp_inited = 1;
    std::cout << "MPP encoder inited: " << ctx->width << "x" << ctx->height
              << " @" << ctx->fps << " fps, bps=" << bps << std::endl;
    return MPP_OK;
}

static int encode_and_send(RtspPipeContext* ctx, cv::Mat& bgr_frame) {
    if (!ctx->mpp_inited || !ctx->pipe) return -1;

    // BGR -> YUV420P
    cv::Mat yuv;
    cv::cvtColor(bgr_frame, yuv, cv::COLOR_BGR2YUV_YV12);

    // 填充 DRM buffer
    MppBuffer buffer = nullptr;
    MPP_RET ret = mpp_buffer_get(nullptr, &buffer, ctx->image_size);
    if (ret != MPP_OK) return -1;

    uint8_t* ptr = (uint8_t*)mpp_buffer_get_ptr(buffer);
    int rs = ctx->hor_stride;
    int vs = ctx->ver_stride;
    const uint8_t* yuv_ptr = yuv.datastart;
    // Y plane
    for (int r = 0; r < ctx->height; r++)
        memcpy(ptr + r * rs, yuv_ptr + r * ctx->width, ctx->width);
    // V plane (Y之后, Y_size = height*width)
    for (int r = 0; r < ctx->height / 2; r++)
        memcpy(ptr + vs * rs + r * (rs / 2),
               yuv_ptr + ctx->height * ctx->width + r * (ctx->width / 2),
               ctx->width / 2);
    // U plane (V之后, Y_size + V_size = height*width*5/4)
    for (int r = 0; r < ctx->height / 2; r++)
        memcpy(ptr + vs * rs * 5 / 4 + r * (rs / 2),
               yuv_ptr + ctx->height * ctx->width * 5 / 4 + r * (ctx->width / 2),
               ctx->width / 2);

    MppBufferInfo info = {};
    info.fd = mpp_buffer_get_fd(buffer);
    info.ptr = ptr;
    info.index = ctx->frame_count;
    info.size = ctx->image_size;
    info.type = MPP_BUFFER_TYPE_DRM;
    mpp_buffer_commit(ctx->group, &info);
    mpp_buffer_get(ctx->group, &buffer, ctx->image_size);

    MppFrame mframe = nullptr;
    mpp_frame_init(&mframe);
    mpp_frame_set_width(mframe, ctx->width);
    mpp_frame_set_height(mframe, ctx->height);
    mpp_frame_set_hor_stride(mframe, ctx->hor_stride);
    mpp_frame_set_ver_stride(mframe, ctx->ver_stride);
    mpp_frame_set_buf_size(mframe, ctx->image_size);
    mpp_frame_set_buffer(mframe, buffer);
    mpp_frame_set_fmt(mframe, MPP_FMT_YUV420P);
    mpp_frame_set_eos(mframe, 0);

    MppPacket mpp_pkt = nullptr;
    mpp_packet_init_with_buffer(&mpp_pkt, buffer);
    mpp_packet_set_length(mpp_pkt, 0);

    ctx->mppApi->poll(ctx->mppCtx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    ctx->mppApi->dequeue(ctx->mppCtx, MPP_PORT_INPUT, &ctx->task);
    mpp_task_meta_set_packet(ctx->task, KEY_OUTPUT_PACKET, mpp_pkt);
    mpp_task_meta_set_frame(ctx->task, KEY_INPUT_FRAME, mframe);
    ctx->mppApi->enqueue(ctx->mppCtx, MPP_PORT_INPUT, ctx->task);

    ctx->mppApi->poll(ctx->mppCtx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    ctx->mppApi->dequeue(ctx->mppCtx, MPP_PORT_OUTPUT, &ctx->task);
    mpp_task_meta_get_packet(ctx->task, KEY_OUTPUT_PACKET, &mpp_pkt);
    ctx->mppApi->enqueue(ctx->mppCtx, MPP_PORT_OUTPUT, ctx->task);

    uint8_t* enc_data = (uint8_t*)mpp_packet_get_data(mpp_pkt);
    size_t enc_size = mpp_packet_get_length(mpp_pkt);

    if (enc_data && enc_size > 0) {
        // Push encoded H.264 data to pipe writer thread
        // ffmpeg receives raw NAL units via pipe and handles RTSP streaming
        PktBuf pkt;
        {
            std::lock_guard<std::mutex> lock(ctx->mtx);
            if (ctx->free_list.empty()) {
                if ((int)ctx->queue.size() >= RTSP_QUEUE_MAX) {
                    ctx->queue.pop();  // drop oldest
                } else {
                    pkt = PktBuf();
                }
            } else {
                pkt = std::move(ctx->free_list.front());
                ctx->free_list.pop();
            }
        }

        if (pkt.buf.capacity() == 0) pkt.buf.resize(RTSP_PKT_BUF_SIZE);

        size_t copy_size = enc_size;
        if (copy_size > RTSP_PKT_BUF_SIZE) copy_size = RTSP_PKT_BUF_SIZE;
        memcpy(pkt.buf.data(), enc_data, copy_size);
        pkt.size = copy_size;

        {
            std::lock_guard<std::mutex> lock(ctx->mtx);
            ctx->queue.push(std::move(pkt));
            ctx->cv.notify_one();
        }
    }

    ctx->frame_count++;
    if (mpp_pkt) { mpp_packet_deinit(&mpp_pkt); }
    if (mframe) { mpp_frame_deinit(&mframe); }
    if (buffer) { mpp_buffer_put(buffer); }
    mpp_buffer_group_clear(ctx->group);

    return 0;
}

static void context_destroy(RtspPipeContext* ctx) {
    if (!ctx) return;
    ctx->stop = true;
    ctx->cv.notify_one();
    if (ctx->writer_thread.joinable()) ctx->writer_thread.join();
    if (ctx->pipe) { fflush(ctx->pipe); pclose(ctx->pipe); ctx->pipe = nullptr; }
    ctx->mpp_inited = 0;
    if (ctx->mppCtx) {
        if (ctx->group) { mpp_buffer_group_put(ctx->group); ctx->group = nullptr; }
        if (ctx->cfg) { mpp_enc_cfg_deinit(ctx->cfg); ctx->cfg = nullptr; }
        mpp_destroy(ctx->mppCtx); ctx->mppCtx = nullptr; ctx->mppApi = nullptr;
    }
}

// 导出函数
bool RtspMppSender::init(const char* rtsp_url, int w, int h, int fps) {
    if (inited_) return true;
    RtspPipeContext* ctx = new RtspPipeContext();
    ctx_ = ctx;

    // 对齐到 16
    ctx->width = MPP_ALIGN(w, 16);
    ctx->height = MPP_ALIGN(h, 16);
    ctx->fps = fps > 0 ? fps : 25;
    ctx->hor_stride = ctx->width;
    ctx->ver_stride = ctx->height;
    ctx->image_size = (size_t)ctx->hor_stride * ctx->ver_stride * 3 / 2;

    // 预分配缓冲池
    for (int i = 0; i < RTSP_QUEUE_MAX + 2; i++) ctx->free_list.push(PktBuf());

    // 启动 ffmpeg 子进程
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -y -fflags +genpts -f h264 -r %d -i pipe:0 -c copy -f rtsp -rtsp_transport tcp \"%s\"",
             ctx->fps, rtsp_url);
    ctx->pipe = popen(cmd, "w");
    if (!ctx->pipe) {
        std::cerr << "Failed to start ffmpeg for RTSP push: " << cmd << std::endl;
        delete ctx; ctx_ = nullptr; return false;
    }
    // 1MB pipe buffer
    setvbuf(ctx->pipe, NULL, _IOFBF, 512 * 1024);
    int pipe_fd = fileno(ctx->pipe);
    if (pipe_fd >= 0) fcntl(pipe_fd, F_SETPIPE_SZ, 1024 * 1024);

    std::cout << "RTSP pipe started: " << rtsp_url << std::endl;

    // 启动写线程
    ctx->writer_thread = std::thread(rtsp_writer_thread, ctx->pipe,
                                      std::ref(ctx->queue), std::ref(ctx->free_list),
                                      std::ref(ctx->mtx), std::ref(ctx->cv),
                                      std::ref(ctx->stop), std::ref(ctx->write_count));

    // 初始化 MPP 编码器
    if (init_mpp_encoder(ctx) != MPP_OK) {
        context_destroy(ctx); delete ctx; ctx_ = nullptr; return false;
    }

    std::cout << "RTSP+MPP sender ready: " << ctx->width << "x" << ctx->height
              << " @" << ctx->fps << " fps -> " << rtsp_url << std::endl;
    inited_ = true;
    return true;
}

bool RtspMppSender::push(cv::Mat& bgr_frame) {
    if (bgr_frame.empty() || bgr_frame.type() != CV_8UC3) return false;
    if (!inited_ || !ctx_) return false;
    if (!bgr_frame.cols || !bgr_frame.rows) return false;

    RtspPipeContext* ctx = (RtspPipeContext*)ctx_;

    // 缩放到对齐后的分辨率（如果需要）
    cv::Mat frame_to_encode;
    if (bgr_frame.cols != ctx->width || bgr_frame.rows != ctx->height) {
        cv::resize(bgr_frame, frame_to_encode, cv::Size(ctx->width, ctx->height));
    } else {
        frame_to_encode = bgr_frame;
    }

    return encode_and_send(ctx, frame_to_encode) == 0;
}

void RtspMppSender::destroy() {
    if (!inited_) return;
    inited_ = false;
    if (ctx_) {
        context_destroy((RtspPipeContext*)ctx_);
        delete (RtspPipeContext*)ctx_;
        ctx_ = nullptr;
    }
}
