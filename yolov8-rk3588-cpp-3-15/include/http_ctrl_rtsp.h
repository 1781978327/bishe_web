#ifndef HTTP_CTRL_RTSP_H
#define HTTP_CTRL_RTSP_H

// RTSP streaming, mosaic composition, and raw-video thread.
// All functions in this module are only compiled when USE_RTSP_MPP is defined.
// Extracted from main_http_ctrl.cc during refactoring.

#ifdef USE_RTSP_MPP

#include <string>
#include <opencv2/core/core.hpp>
#include "http_ctrl_globals.h"   // for SlotBgrDmabuf (under USE_RTSP_MPP guard)

// -- SlotBgrDmabuf helpers --
void release_slot_bgr_dmabuf(SlotBgrDmabuf* slot);
bool ensure_slot_bgr_dmabuf(SlotBgrDmabuf* slot, int width, int height);

// -- FPS overlay --
void draw_rtsp_fps_overlay(cv::Mat& frame, int fps_value);

// -- RTSP sender management --
void destroy_rtsp_sender(RtspMppSender*& sender);
void stop_rtsp_senders_locked();

// -- Mosaic composition --
void draw_mosaic_placeholder(cv::Mat& tile, const std::string& label);
void prepare_mosaic_tile(const cv::Mat& src, const std::string& placeholder, cv::Mat& tile);
bool build_mosaic_frame(int updated_cam, const cv::Mat& updated_frame, cv::Mat& mosaic_frame);
bool should_push_mosaic_frame();
void push_mosaic_rtsp_if_needed(int updated_cam, const cv::Mat& updated_frame, int& push_mosaic_counter);

// -- Raw video RTSP thread --
void video_rtsp_raw_loop();
void start_video_rtsp_raw_thread_if_needed();
void stop_video_rtsp_raw_thread();

#endif // USE_RTSP_MPP

#endif // HTTP_CTRL_RTSP_H
