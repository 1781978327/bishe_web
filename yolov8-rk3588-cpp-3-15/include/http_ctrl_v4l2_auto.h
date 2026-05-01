#ifndef HTTP_CTRL_V4L2_AUTO_H
#define HTTP_CTRL_V4L2_AUTO_H

// V4L2 auto-detection and camera source assignment.
// Extracted from main_http_ctrl.cc during refactoring.

#include <string>
#include <vector>

#include <opencv2/core/core.hpp>
#include <opencv2/videoio.hpp>

#include "v4l2_dmabuf_capture.h"

struct AutoCameraCandidate {
    std::string path;
    std::string card;
    std::string bus_info;
    int index = -1;
};

// Parse a V4L2 source string ("0", "/dev/video0", etc.)
bool parse_v4l2_source(const std::string& source, std::string* device_path, int* device_index);

// Extract the numeric index from "/dev/videoN"; returns -1 on failure.
int parse_video_device_index(const std::string& path);

// Query a single V4L2 device for capture capability.
bool query_v4l2_capture_candidate(const std::string& path, AutoCameraCandidate* out);

// List all V4L2 capture-capable devices on the system.
std::vector<AutoCameraCandidate> list_v4l2_capture_candidates();

// Check if a source string means "auto-assign".
bool is_auto_source_text(const std::string& source_text);

// Auto-assign g_input_source_cam0 / g_input_source_cam1 when they are "auto".
void maybe_auto_assign_camera_sources(bool cam0_source_user_set, bool cam1_source_user_set);

// Open a camera input source (V4L2 dmabuf, V4L2 OpenCV fallback, or network stream).
bool open_camera_input_source(const std::string& requested_source,
                              int default_index,
                              int target_width,
                              int target_height,
                              int target_fps,
                              v4l2_dmabuf::CaptureContext* dmabuf_cap,
                              cv::VideoCapture* cv_cap,
                              bool* using_dmabuf,
                              std::string* normalized_source,
                              std::string* err_msg);

#endif // HTTP_CTRL_V4L2_AUTO_H
