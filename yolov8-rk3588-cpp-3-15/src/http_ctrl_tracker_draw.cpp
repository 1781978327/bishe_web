#include "http_ctrl_tracker_draw.h"

#include <stdio.h>
#include <algorithm>

#include <opencv2/imgproc/imgproc.hpp>

#include "rknnPool.hpp"  // coco_labels, tracker_color_for_item, get_track_draw_points_limit

// ---------------------- Build detections from tracks ----------------------

std::vector<DetectionResultItem> build_detections_from_tracks(const std::vector<TrackerResultItem>& tracks) {
    std::vector<DetectionResultItem> detections;
    detections.reserve(tracks.size());
    for (const auto& t : tracks) {
        if (!t.active) continue;
        DetectionResultItem d;
        d.label = t.label;
        d.score = t.score;
        d.x1 = t.x1;
        d.y1 = t.y1;
        d.x2 = t.x2;
        d.y2 = t.y2;
        detections.push_back(d);
    }
    return detections;
}

// ---------------------- Label name for detection ----------------------

std::string label_name_for_detection(const DetectionResultItem& det) {
    if (det.label >= 0 && det.label < (int)coco_labels.size()) {
        return coco_labels[det.label];
    }
    return "unknown";
}

// ---------------------- Draw tracker boxes ----------------------

// 绘制带跟踪的检测框（需要外部持有 g_tracker_mutex）
void draw_tracker_boxes(cv::Mat& img, const std::vector<TrackerResultItem>& tracks, int slot_idx) {
    static int debug_counter = 0;
    bool print_debug = (debug_counter++ % 300 == 0);
    if (print_debug) {
        printf("[DEBUG draw] 收到 %zu 个跟踪结果, 图像尺寸: %dx%d, slot=%d\n",
               tracks.size(), img.cols, img.rows, slot_idx);
    }

    char text[256];
    int max_boxes = 20;
    int box_count = 0;
    for (const auto& track : tracks) {
        if (!track.active || track.track_id < 0) continue;

        box_count++;
        if (box_count > max_boxes) break;

        int x1 = std::max(0, std::min((int)track.x1, img.cols - 1));
        int y1 = std::max(0, std::min((int)track.y1, img.rows - 1));
        int x2 = std::max(0, std::min((int)track.x2, img.cols - 1));
        int y2 = std::max(0, std::min((int)track.y2, img.rows - 1));
        if (x2 <= x1 || y2 <= y1) continue;

        const char* label = (track.label >= 0 && track.label < (int)coco_labels.size())
                            ? coco_labels[track.label].c_str() : "unknown";
        if (print_debug) {
            printf("[DEBUG draw]   绘制 ID=%d %s 框: (%d,%d)-(%d,%d)\n",
                   track.track_id, label, x1, y1, x2, y2);
        }

        cv::Scalar color = tracker_color_for_item(track.label, track.track_id);
        cv::rectangle(img, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);

        snprintf(text, sizeof(text), "ID:%d %s %.1f%%", track.track_id, label, track.score * 100.0f);
        int baseLine = 0;
        cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);

        int tx = x1;
        int ty = y1 - label_size.height - baseLine;
        if (ty < 0) ty = 0;
        if (tx + label_size.width > img.cols) tx = img.cols - label_size.width;

        cv::rectangle(img, cv::Rect(cv::Point(tx, ty),
                      cv::Size(label_size.width, label_size.height + baseLine)),
                      color, -1);
        cv::putText(img, text, cv::Point(tx, ty + label_size.height),
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);

        const auto& traj = track.trajectory;
        const int draw_trail_points = get_track_draw_points_limit();
        int start_idx = (traj.size() > (size_t)draw_trail_points) ? ((int)traj.size() - draw_trail_points) : 0;
        for (size_t j = (size_t)start_idx + 1; j < traj.size(); j++) {
            int px1 = std::max(0, std::min((int)traj[j - 1].first, img.cols - 1));
            int py1 = std::max(0, std::min((int)traj[j - 1].second, img.rows - 1));
            int px2 = std::max(0, std::min((int)traj[j].first, img.cols - 1));
            int py2 = std::max(0, std::min((int)traj[j].second, img.rows - 1));
            cv::line(img, cv::Point(px1, py1), cv::Point(px2, py2), color, 1);
        }
    }
}
