#ifndef HTTP_CTRL_TRACKER_DRAW_H
#define HTTP_CTRL_TRACKER_DRAW_H

// Tracker / detection drawing functions extracted from main_http_ctrl.cc.

#include <string>
#include <vector>

#include <opencv2/core/core.hpp>

#include "rknnPool.hpp"  // TrackerResultItem, DetectionResultItem

std::vector<DetectionResultItem> build_detections_from_tracks(const std::vector<TrackerResultItem>& tracks);

std::string label_name_for_detection(const DetectionResultItem& det);

void draw_tracker_boxes(cv::Mat& img, const std::vector<TrackerResultItem>& tracks, int slot_idx);

#endif // HTTP_CTRL_TRACKER_DRAW_H
