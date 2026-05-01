#ifndef HTTP_CTRL_VIDEO_H
#define HTTP_CTRL_VIDEO_H

// Video file source reading and management.
// Extracted from main_http_ctrl.cc during refactoring.

#include <string>
#include <opencv2/core/core.hpp>
#include "http_ctrl_globals.h"  // for VideoFrameInfo

// Clear the video frame queue (caller must hold g_video_mutex).
void clear_video_queue_locked();

// Open the video source (hw accel if USE_RTSP_MPP, else OpenCV fallback).
// Caller must hold g_video_mutex.
bool open_video_source_locked();

// Read a single frame from the video source. Caller must hold g_video_mutex.
bool read_video_frame_locked(cv::Mat& frame, VideoFrameInfo* frame_info = nullptr);

// Ensure the video source is open; opens it if needed. Caller must hold g_video_mutex.
bool ensure_video_source_open_locked();

// Close the video source. Caller must hold g_video_mutex.
void close_video_source_locked();

// Main loop for the video reader thread.
void video_reader_loop();

// Stop the video reader thread and clean up.
void stop_video_reader();

// Start the video reader thread for a given file path.
void start_video_reader(const std::string& path, bool loop);

#endif // HTTP_CTRL_VIDEO_H
