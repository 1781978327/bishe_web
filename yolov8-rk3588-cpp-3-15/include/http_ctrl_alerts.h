#ifndef HTTP_CTRL_ALERTS_H
#define HTTP_CTRL_ALERTS_H

// Alert and forbidden-area intrusion detection module.
// Extracted from the monolithic main_http_ctrl.cc for the vision service refactoring.

#include <string>
#include <vector>
#include <opencv2/core/core.hpp>

#include "rknnPool.hpp"          // DetectionResultItem, TrackerResultItem
#include "http_ctrl_globals.h"   // ForbiddenAreaCache

// ---- Detection count alert ----

void report_detection_count_alert_worker(int cam_id, int detection_count, int threshold, cv::Mat frame_bgr);
void report_detection_count_alert_async(int cam_id, int detection_count, int threshold, const cv::Mat& frame_bgr);
void maybe_trigger_detection_count_alert(int cam_id, int detection_count, const cv::Mat& frame_bgr);

// ---- Forbidden area ----

int forbidden_area_camera_id_from_cam(int cam_id);
bool fetch_forbidden_area_from_server(int cam_id, ForbiddenAreaCache* cache_out, std::string* err_msg);
void refresh_forbidden_area_cache_if_needed(int cam_id);
bool get_forbidden_area_polygon_for_frame(int cam_id, const cv::Size& frame_size, std::vector<cv::Point2f>* polygon_out);

// ---- Intrusion detection ----

bool collect_intrusion_hits_for_frame(
    int cam_id,
    const cv::Size& frame_size,
    const std::vector<DetectionResultItem>& detections,
    std::vector<cv::Point2f>* polygon_out,
    std::vector<DetectionResultItem>* hit_detections_out);

void draw_forbidden_area_overlay_if_available(int cam_id, cv::Mat& frame_bgr);
void draw_intrusion_boxes_overlay_if_needed(
    int cam_id,
    cv::Mat& frame_bgr,
    const std::vector<DetectionResultItem>& detections);

// build_detections_from_tracks and label_name_for_detection are declared in http_ctrl_tracker_draw.h

// ---- Intrusion alert ----

void report_forbidden_area_intrusion_alert_worker(
    int cam_id,
    std::vector<DetectionResultItem> hit_detections,
    std::vector<cv::Point2f> polygon,
    cv::Mat frame_bgr);

void report_forbidden_area_intrusion_alert_async(
    int cam_id,
    const std::vector<DetectionResultItem>& hit_detections,
    const std::vector<cv::Point2f>& polygon,
    const cv::Mat& frame_bgr);

void maybe_trigger_forbidden_area_intrusion_alert(
    int cam_id,
    const std::vector<DetectionResultItem>& detections,
    const cv::Mat& frame_bgr);

#endif // HTTP_CTRL_ALERTS_H
