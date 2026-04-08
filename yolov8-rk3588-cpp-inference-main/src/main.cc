// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <sys/time.h>
#include <thread>
#include <queue>
#include <vector>
#include <string>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#define _BASETSD_H

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "rknnPool.hpp"
#include "ThreadPool.hpp"
#include "im2d.h"
#include "rga.h"
#include "RgaUtils.h"
#include "bytetrack/include/BYTETracker.h"
#ifdef USE_RTSP_MPP
#include "rtsp_mpp_sender.h"
#endif

using std::queue;
using std::time;
using std::time_t;
using std::vector;

// rknnPool.hpp 中定义的全局可见性
extern std::vector<bool> class_visible;
extern const char** coco_labels;
extern int coco_class_num;


int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <rknn model> [video_path] [output_path] [reid_model] [--tracker deepsort|bytetrack] [--skip N]\n", argv[0]);
        printf("  双摄: video_path 用逗号分隔；双路 RTSP: output_path 写两个地址逗号分隔\n");
        printf("  例: %s model.rknn /dev/video0,/dev/video2 rtsp://ip:8554/cam0,rtsp://ip:8554/cam1\n", argv[0]);
        printf("  跟踪器: --tracker deepsort (默认,需要ReID) 或 --tracker bytetrack (纯IoU,无需ReID)\n");
        printf("  隔帧跟踪: --skip N (DeepSort专有,跳过N帧不做跟踪,可大幅提升帧率)\n");
        printf("    --skip 0  每帧都跟踪 (默认)\n");
        printf("    --skip 1  每2帧跟踪1次\n");
        printf("    --skip 2  每3帧跟踪1次\n");
        return -1;
    }

    char* model_name = argv[1];

    // video_path
    std::string video_path = (argc >= 3) ? argv[2] : "";
    std::string video_path0, video_path1;
    bool dual_cam = false;
    size_t comma = video_path.find(',');
    if (comma == std::string::npos) comma = video_path.find('，');
    if (comma != std::string::npos) {
        video_path0 = video_path.substr(0, comma);
        video_path1 = video_path.substr(comma + 1);
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t");
            if (a == std::string::npos) s.clear();
            else { size_t b = s.find_last_not_of(" \t"); s = s.substr(a, b - a + 1); }
        };
        trim(video_path0); trim(video_path1);
        dual_cam = true;
    } else {
        video_path0 = video_path;
    }

    // output_path
    std::string output_path = (argc >= 4) ? argv[3] : "";
    std::string output_path0, output_path1;
    if (dual_cam && output_path.find(',') != std::string::npos) {
        size_t co = output_path.find(',');
        output_path0 = output_path.substr(0, co);
        output_path1 = output_path.substr(co + 1);
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t");
            if (a == std::string::npos) s.clear();
            else { size_t b = s.find_last_not_of(" \t"); s = s.substr(a, b - a + 1); }
        };
        trim(output_path0); trim(output_path1);
    }

    // tracker 选择
    std::string reid_model_path;
    std::string tracker_type = "deepsort";
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--tracker" && i + 1 < argc) {
            tracker_type = argv[i + 1];
        } else if (std::string(argv[i]) == "--skip" && i + 1 < argc) {
            g_ds_skip_frames = std::stoi(argv[i + 1]);
            printf("DeepSort 隔帧数: %d (每 %d 帧执行一次跟踪)\n", g_ds_skip_frames, g_ds_skip_frames + 1);
        } else if (i == 4 && std::string(argv[i]) != "--tracker" && std::string(argv[i]) != "--skip") {
            reid_model_path = argv[i];
        }
    }
    bool use_bytetrack = (tracker_type == "bytetrack");
    bool use_deepsort  = (tracker_type == "deepsort");

#ifdef USE_RTSP_MPP
    bool use_rtsp_mpp  = !dual_cam && (output_path.size() >= 7 && output_path.substr(0, 7) == "rtsp://");
    bool use_rtsp_dual = dual_cam && output_path0.size() >= 7 && output_path0.substr(0, 7) == "rtsp://"
                      && output_path1.size() >= 7 && output_path1.substr(0, 7) == "rtsp://";
#else
    bool use_rtsp_mpp = false, use_rtsp_dual = false;
#endif

    auto open_cap = [](const std::string& path) -> cv::VideoCapture {
        cv::VideoCapture c;
        bool is_cam = path.empty() || path == "0" ||
                      (path.size() >= 10 && path.compare(0, 10, "/dev/video") == 0);
        if (is_cam) {
            if (path.empty() || path == "0") {
                c.open(0, cv::CAP_V4L2);
            } else {
                // /dev/videoN -> extract N and open V4L2 with device index
                int idx = path[10] - '0';
                c.open(idx, cv::CAP_V4L2);
            }
        } else {
            if (!c.open(path, cv::CAP_FFMPEG) && !c.open(path, cv::CAP_ANY)) return c;
        }
        return c;
    };

    cv::VideoCapture cap, cap1;
    if (dual_cam) {
        cap = open_cap(video_path0); cap1 = open_cap(video_path1);
        if (!cap.isOpened())  { printf("无法打开视频源 0: %s\n", video_path0.empty() ? "0" : video_path0.c_str()); return -1; }
        if (!cap1.isOpened()) { printf("无法打开视频源 1: %s\n", video_path1.empty() ? "1" : video_path1.c_str()); return -1; }
        printf("双摄模式: 源0=%s 源1=%s\n",
               video_path0.empty() ? "0" : video_path0.c_str(),
               video_path1.empty() ? "1" : video_path1.c_str());
    } else {
        cap = open_cap(video_path0);
        if (!cap.isOpened()) {
            printf("无法打开视频源: %s\n", video_path0.empty() ? "camera 0" : video_path0.c_str());
            return -1;
        }
    }

    int width = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int height = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    if (width <= 0 || height <= 0) { width = 1920; height = 1080; }
    double input_fps = cap.get(cv::CAP_PROP_FPS);
    if (input_fps <= 0 || input_fps > 120) input_fps = 25.0;
    int width1 = width, height1 = height; double fps1 = input_fps;
    if (dual_cam) {
        width1 = (int)cap1.get(cv::CAP_PROP_FRAME_WIDTH);
        height1 = (int)cap1.get(cv::CAP_PROP_FRAME_HEIGHT);
        if (width1 <= 0 || height1 <= 0) { width1 = 1920; height1 = 1080; }
        fps1 = cap1.get(cv::CAP_PROP_FPS);
        if (fps1 <= 0 || fps1 > 120) fps1 = 25.0;
    }

#ifdef USE_RTSP_MPP
    RtspMppSender* rtsp_sender = nullptr, *rtsp_sender1 = nullptr;
    if (use_rtsp_mpp) {
        rtsp_sender = new RtspMppSender();
        if (!rtsp_sender->init(output_path.c_str(), width, height, (int)input_fps)) {
            printf("RTSP+MPP 初始化失败: %s\n", output_path.c_str());
            delete rtsp_sender; rtsp_sender = nullptr;
        } else {
            printf("RTSP+MPP 硬件推流: %s, %dx%d, FPS: %.1f\n", output_path.c_str(), width, height, input_fps);
        }
    } else if (use_rtsp_dual) {
        rtsp_sender = new RtspMppSender(); rtsp_sender1 = new RtspMppSender();
        if (!rtsp_sender->init(output_path0.c_str(), width, height, (int)input_fps)) {
            printf("RTSP+MPP Cam0 初始化失败\n"); delete rtsp_sender; delete rtsp_sender1;
            rtsp_sender = nullptr; rtsp_sender1 = nullptr;
        } else if (!rtsp_sender1->init(output_path1.c_str(), width1, height1, (int)fps1)) {
            printf("RTSP+MPP Cam1 初始化失败\n");
            rtsp_sender->destroy(); delete rtsp_sender; delete rtsp_sender1;
            rtsp_sender = nullptr; rtsp_sender1 = nullptr;
        } else {
            printf("RTSP+MPP 双路推流已启动\n");
        }
    }
#endif

    cv::VideoWriter writer;
    if (!output_path.empty() && !use_rtsp_mpp && !use_rtsp_dual) {
        writer.open(output_path, cv::VideoWriter::fourcc('M','J','P','G'), input_fps, cv::Size(width, height));
        if (!writer.isOpened())
            printf("警告: 无法打开输出视频: %s\n", output_path.c_str());
        else
            printf("输出视频: %s, %dx%d, FPS: %.1f\n", output_path.c_str(), width, height, input_fps);
    }

    // DeepSort 跟踪器
    DeepSort* tracker = nullptr;
    if (use_deepsort && !reid_model_path.empty()) {
        tracker = new DeepSort(reid_model_path, 1, 512, 6, RKNN_NPU_CORE_2);
        printf("DeepSort 跟踪器已启用 (Re-ID: %s)\n", reid_model_path.c_str());
    } else if (!reid_model_path.empty() && !use_bytetrack) {
        printf("DeepSort 跟踪器未启用（请通过第5个参数传入 Re-ID 模型路径）\n");
    }

    // ByteTrack 跟踪器（共享单实例，保证帧序连续）
    BYTETracker* byte_tracker = nullptr;
    if (use_bytetrack) {
        int bt_fps = (int)(input_fps > 0 ? input_fps : 25);
        byte_tracker = new BYTETracker(bt_fps, 30);
        printf("ByteTrack 跟踪器已启用 (FPS=%d, buffer=30帧)\n", bt_fps);
    }

    const int n = 6;
    printf("线程数:\t%d\n", n);
    vector<rknn_lite*> rkpool;
    dpool::ThreadPool pool(n);

    struct timeval time;
    gettimeofday(&time, nullptr);
    long initTime = time.tv_sec * 1000 + time.tv_usec / 1000;
    long lopTime = initTime, tmpTime;

    if (dual_cam) {
        cv::namedWindow("Cam0", cv::WINDOW_NORMAL);
        cv::namedWindow("Cam1", cv::WINDOW_NORMAL);
        cv::resizeWindow("Cam0", 1280, 720);
        cv::resizeWindow("Cam1", 1280, 720);
        int bt_fps = (int)(input_fps > 0 ? input_fps : 25);
        for (int i = 0; i < n; i++) {
            rknn_lite* ptr = new rknn_lite(model_name, i % 3,
                                           use_deepsort ? tracker : nullptr,
                                           byte_tracker, i, bt_fps);
            rkpool.push_back(ptr);
        }
        std::future<int> slots[6];
        for (int i = 0; i < 3; i++) {
            cv::Mat frame;
            if (!cap.read(frame)) { printf("Cam0 无法读取初始帧\n"); return -1; }
            frame.copyTo(rkpool[i]->ori_img);
            slots[i] = pool.submit(&rknn_lite::interf, rkpool[i]);
        }
        for (int i = 3; i < 6; i++) {
            cv::Mat frame;
            if (!cap1.read(frame)) { printf("Cam1 无法读取初始帧\n"); return -1; }
            frame.copyTo(rkpool[i]->ori_img);
            slots[i] = pool.submit(&rknn_lite::interf, rkpool[i]);
        }
        int frames[2]  = {3, 3};
        long lopTime[2] = { time.tv_sec * 1000 + time.tv_usec / 1000,
                            time.tv_sec * 1000 + time.tv_usec / 1000 };
        long fpsClock[2] = {0, 0};
        while (true) {
            bool any = false;
            for (int i = 0; i < 6; i++) {
                if (slots[i].valid() && slots[i].wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    if (slots[i].get() != 0) goto dual_done;
                    any = true;
                    int cam = (i < 3) ? 0 : 1;

                    cv::imshow(cam == 0 ? "Cam0" : "Cam1", rkpool[i]->ori_img);
#ifdef USE_RTSP_MPP
                    if (cam == 0 && rtsp_sender)  rtsp_sender->push(rkpool[i]->ori_img);
                    if (cam == 1 && rtsp_sender1) rtsp_sender1->push(rkpool[i]->ori_img);
#endif
                    cv::VideoCapture& c = (cam == 0) ? cap : cap1;
                    cv::Mat frame;
                    if (!c.read(frame)) { printf("视频流结束 cam%d\n", cam); goto dual_done; }
                    frame.copyTo(rkpool[i]->ori_img);
                    slots[i] = pool.submit(&rknn_lite::interf, rkpool[i]);
                    frames[cam]++; fpsClock[cam]++;
                    if (fpsClock[cam] >= 30) {
                        gettimeofday(&time, nullptr);
                        long now = time.tv_sec * 1000 + time.tv_usec / 1000;
                        printf("Cam%d FPS: %.1f (30帧耗时 %.1fms)\n", cam, 30000.0f / (now - lopTime[cam]), (float)(now - lopTime[cam]));
                        lopTime[cam] = now; fpsClock[cam] = 0;
                    }
                }
            }
            if (!any) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (cv::waitKey(1) == 'q') break;
        }
dual_done:
        gettimeofday(&time, nullptr);
        printf("\n平均帧率 Cam0: %.1f  Cam1: %.1f\n",
               float(frames[0]) / (float)(time.tv_sec * 1000 + time.tv_usec / 1000 - initTime + 0.0001) * 1000.0,
               float(frames[1]) / (float)(time.tv_sec * 1000 + time.tv_usec / 1000 - initTime + 0.0001) * 1000.0);
        for (int i = 0; i < 6; i++) if (slots[i].valid()) slots[i].wait();
        cap1.release();
    } else {
        cv::namedWindow("Video Stream FPS");
        queue<std::future<int>> futs;
        int frames = 0;
        int bt_fps = (int)(input_fps > 0 ? input_fps : 25);

        for (int i = 0; i < n; i++) {
            rknn_lite* ptr = new rknn_lite(model_name, i % 3,
                                           use_deepsort ? tracker : nullptr,
                                           byte_tracker, i, bt_fps);
            rkpool.push_back(ptr);
            cv::Mat frame;
            if (!cap.read(frame)) { printf("无法从视频读取初始帧\n"); return -1; }
            frame.copyTo(ptr->ori_img);
            futs.push(pool.submit(&rknn_lite::interf, ptr));
        }

        while (true) {
            if (futs.front().get() != 0) break;
            futs.pop();

            cv::Mat& show_img = rkpool[frames % n]->ori_img;
            cv::imshow("Video Stream FPS", show_img);
#ifdef USE_RTSP_MPP
            if (rtsp_sender) rtsp_sender->push(show_img);
            else
#endif
            if (writer.isOpened()) writer.write(show_img);

            if (cv::waitKey(1) == 'q') break;

            cv::Mat frame;
            if (!cap.read(frame)) { printf("视频流结束\n"); break; }
            frame.copyTo(rkpool[frames % n]->ori_img);
            futs.push(pool.submit(&rknn_lite::interf, rkpool[frames % n]));
            frames++;

            if (frames % 30 == 0) {
                gettimeofday(&time, nullptr);
                tmpTime = time.tv_sec * 1000 + time.tv_usec / 1000;
                printf("FPS: %.1f (30帧耗时 %.1fms)\n", 30000.0f / (tmpTime - lopTime), (float)(tmpTime - lopTime));
                lopTime = tmpTime;
            }
        }
        gettimeofday(&time, nullptr);
        printf("\n平均帧率:\t%f帧\n", float(frames) / (float)(time.tv_sec * 1000 + time.tv_usec / 1000 - initTime + 0.0001) * 1000.0);
        while (!futs.empty()) { if (futs.front().get()) break; futs.pop(); }
    }

    for (int i = 0; i < n; i++) delete rkpool[i];
    if (tracker) delete tracker;
    if (byte_tracker) delete byte_tracker;
    cap.release();
#ifdef USE_RTSP_MPP
    if (rtsp_sender)  { rtsp_sender->destroy();  delete rtsp_sender; }
    if (rtsp_sender1) { rtsp_sender1->destroy(); delete rtsp_sender1; }
#endif
    if (writer.isOpened()) writer.release();
    cv::destroyAllWindows();
    return 0;
}
