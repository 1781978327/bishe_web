// Alert and forbidden-area intrusion detection module.
// Extracted from main_http_ctrl.cc for the vision service refactoring.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cctype>
#include <cstdint>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "rknnPool.hpp"
#include "http_ctrl_globals.h"
#include "http_ctrl_alerts.h"
#include "http_ctrl_tracker_draw.h"
#include "http_ctrl_utils.h"
#include "http_ctrl_web_utils.h"

// Forward declarations for report-server HTTP functions still in main_http_ctrl.cc.
// TODO: move these into a dedicated header (e.g. http_ctrl_report.h) when that module is extracted.
bool http_request_to_report_server(
    const std::string& method,
    const std::string& request_path,
    const std::string& content_type,
    const std::string& body,
    int timeout_ms,
    std::string* response_head,
    std::string* response_body,
    int* status_code_out);

bool post_json_to_report_server(const std::string& body, std::string* response_head);

// =====================================================================
// JSON parsing helpers (internal / static to this translation unit)
// =====================================================================

static bool json_extract_int_after_key(const std::string& text, size_t key_pos, int* out) {
    if (!out || key_pos == std::string::npos) return false;
    size_t colon = text.find(':', key_pos);
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
    bool neg = false;
    if (i < text.size() && text[i] == '-') {
        neg = true;
        ++i;
    }
    if (i >= text.size() || !std::isdigit((unsigned char)text[i])) return false;
    long value = 0;
    while (i < text.size() && std::isdigit((unsigned char)text[i])) {
        value = value * 10 + (text[i] - '0');
        ++i;
    }
    if (neg) value = -value;
    *out = (int)value;
    return true;
}

static bool json_extract_int_field(const std::string& text, const std::string& key, int* out) {
    std::string token = "\"" + key + "\"";
    size_t key_pos = text.find(token);
    if (key_pos == std::string::npos) return false;
    return json_extract_int_after_key(text, key_pos, out);
}

static bool json_extract_bool_field(const std::string& text, const std::string& key, bool* out) {
    if (!out) return false;
    std::string token = "\"" + key + "\"";
    size_t key_pos = text.find(token);
    if (key_pos == std::string::npos) return false;
    size_t colon = text.find(':', key_pos);
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
    if (text.compare(i, 4, "true") == 0) {
        *out = true;
        return true;
    }
    if (text.compare(i, 5, "false") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_points_from_forbidden_area_body(const std::string& body, std::vector<cv::Point2f>* points_out) {
    if (!points_out) return false;
    points_out->clear();

    size_t key_pos = body.find("\"points\"");
    if (key_pos == std::string::npos) return false;
    size_t arr_begin = body.find('[', key_pos);
    if (arr_begin == std::string::npos) return false;

    size_t depth = 0;
    size_t arr_end = std::string::npos;
    for (size_t i = arr_begin; i < body.size(); ++i) {
        if (body[i] == '[') ++depth;
        else if (body[i] == ']') {
            if (depth == 0) return false;
            --depth;
            if (depth == 0) {
                arr_end = i;
                break;
            }
        }
    }
    if (arr_end == std::string::npos) return false;

    std::string arr_text = body.substr(arr_begin + 1, arr_end - arr_begin - 1);
    size_t cursor = 0;
    while (cursor < arr_text.size()) {
        size_t x_pos = arr_text.find("\"x\"", cursor);
        if (x_pos == std::string::npos) break;
        int x = 0;
        if (!json_extract_int_after_key(arr_text, x_pos, &x)) {
            cursor = x_pos + 3;
            continue;
        }
        size_t y_pos = arr_text.find("\"y\"", x_pos);
        if (y_pos == std::string::npos) break;
        int y = 0;
        if (!json_extract_int_after_key(arr_text, y_pos, &y)) {
            cursor = y_pos + 3;
            continue;
        }
        points_out->push_back(cv::Point2f((float)x, (float)y));
        cursor = y_pos + 3;
    }
    return !points_out->empty();
}

// =====================================================================
// Detection count alert
// =====================================================================

void report_detection_count_alert_worker(int cam_id, int detection_count, int threshold, cv::Mat frame_bgr) {
    if (frame_bgr.empty()) return;

    std::vector<uchar> jpg_buf;
    if (!cv::imencode(".jpg", frame_bgr, jpg_buf, {cv::IMWRITE_JPEG_QUALITY, 85})) {
        printf("[AlertReport] JPEG 编码失败: cam=%d count=%d\n", cam_id, detection_count);
        return;
    }

    std::string image_b64 = base64_encode_bytes(jpg_buf.data(), jpg_buf.size());
    int camera_id = (cam_id == 0) ? g_report_camera_id_cam0 : g_report_camera_id_cam1;

    std::ostringstream detection_result;
    detection_result << "{\"type\":\"box_count_exceeded\",\"cam\":" << cam_id
                     << ",\"count\":" << detection_count
                     << ",\"threshold\":" << threshold << "}";

    std::ostringstream body;
    body << "{";
    body << "\"cameraId\":" << camera_id << ",";
    body << "\"detectionTime\":\"" << local_time_iso8601() << "\",";
    body << "\"detectionResult\":\"" << json_escape(detection_result.str()) << "\",";
    body << "\"imageBase64\":\"" << image_b64 << "\"";
    body << "}";

    std::string response_head;
    bool ok = post_json_to_report_server(body.str(), &response_head);
    if (ok) {
        printf("[AlertReport] 上报成功: cam=%d cameraId=%d count=%d threshold=%d\n",
               cam_id, camera_id, detection_count, threshold);
    } else {
        std::string brief = response_head.empty() ? "(no response)" : response_head.substr(0, 120);
        printf("[AlertReport] 上报失败: cam=%d cameraId=%d count=%d threshold=%d resp=%s\n",
               cam_id, camera_id, detection_count, threshold, brief.c_str());
    }
}

void report_detection_count_alert_async(int cam_id, int detection_count, int threshold, const cv::Mat& frame_bgr) {
    if (frame_bgr.empty()) return;
    cv::Mat frame_copy = frame_bgr.clone();
    if (frame_copy.empty()) return;
    try {
        std::thread(report_detection_count_alert_worker, cam_id, detection_count, threshold, frame_copy).detach();
    } catch (...) {
        printf("[AlertReport] 启动上报线程失败: cam=%d count=%d\n", cam_id, detection_count);
    }
}

void maybe_trigger_detection_count_alert(int cam_id, int detection_count, const cv::Mat& frame_bgr) {
    if (cam_id < 0 || cam_id > 1) return;

    int threshold = g_box_count_alert_threshold.load();
    if (threshold <= 0) {
        std::lock_guard<std::mutex> lock(g_box_alert_state_mutex);
        g_box_count_over_state[cam_id] = false;
        return;
    }

    bool should_report = false;
    {
        std::lock_guard<std::mutex> lock(g_box_alert_state_mutex);
        bool over = detection_count > threshold;
        bool was_over = g_box_count_over_state[cam_id];
        g_box_count_over_state[cam_id] = over;
        if (!over || was_over) {
            return;
        }

        long long now_ms = monotonic_now_ms();
        int cooldown_ms = g_box_count_alert_cooldown_ms.load();
        if (cooldown_ms < 0) cooldown_ms = 0;
        if (now_ms - g_last_box_alert_ms[cam_id] < cooldown_ms) {
            return;
        }
        g_last_box_alert_ms[cam_id] = now_ms;
        should_report = true;
    }

    if (should_report) {
        report_detection_count_alert_async(cam_id, detection_count, threshold, frame_bgr);
    }
}

// =====================================================================
// Forbidden area
// =====================================================================

int forbidden_area_camera_id_from_cam(int cam_id) {
    return (cam_id == 0) ? 1 : 2;
}

bool fetch_forbidden_area_from_server(int cam_id, ForbiddenAreaCache* cache_out, std::string* err_msg) {
    if (!cache_out) return false;
    *cache_out = ForbiddenAreaCache{};
    cache_out->last_fetch_ms = monotonic_now_ms();

    std::string path = g_forbidden_area_path;
    if (path.empty()) path = "/api/rknn/forbidden-area";
    if (path[0] != '/') path = "/" + path;
    path += (path.find('?') == std::string::npos) ? "?" : "&";
    path += "cameraId=" + std::to_string(forbidden_area_camera_id_from_cam(cam_id));

    std::string response_head;
    std::string response_body;
    int status_code = -1;
    int timeout_ms = g_forbidden_area_fetch_timeout_ms.load();
    bool ok = http_request_to_report_server("GET", path, "application/json", "",
                                            timeout_ms, &response_head, &response_body, &status_code);
    if (!ok) {
        if (err_msg) {
            std::ostringstream oss;
            oss << "HTTP fail status=" << status_code << " head="
                << (response_head.empty() ? "(empty)" : response_head.substr(0, 120));
            *err_msg = oss.str();
        }
        return false;
    }

    bool exists = false;
    if (!json_extract_bool_field(response_body, "exists", &exists)) {
        // 未匹配到 exists，按"无有效区域"处理，避免解析异常时阻塞主流程
        cache_out->valid = false;
        return true;
    }
    if (!exists) {
        cache_out->valid = false;
        return true;
    }

    int img_w = 0;
    int img_h = 0;
    (void)json_extract_int_field(response_body, "imageWidth", &img_w);
    (void)json_extract_int_field(response_body, "imageHeight", &img_h);

    std::vector<cv::Point2f> points;
    if (!parse_points_from_forbidden_area_body(response_body, &points) || points.size() < 4) {
        cache_out->valid = false;
        if (err_msg) *err_msg = "points parse failed or less than 4";
        return true;
    }

    if (points.size() > 4) {
        points.resize(4);
    }
    cache_out->valid = true;
    cache_out->image_width = img_w;
    cache_out->image_height = img_h;
    cache_out->points = points;
    return true;
}

void refresh_forbidden_area_cache_if_needed(int cam_id) {
    if (cam_id < 0 || cam_id > 1) return;

    long long now_ms = monotonic_now_ms();
    int interval_ms = g_forbidden_area_fetch_interval_ms.load();
    if (interval_ms < 500) interval_ms = 500;

    {
        std::lock_guard<std::mutex> lock(g_forbidden_area_mutex);
        if (g_forbidden_area_fetching[cam_id]) return;
        if (now_ms - g_forbidden_area_cache[cam_id].last_fetch_ms < interval_ms) return;
        g_forbidden_area_fetching[cam_id] = true;
    }

    ForbiddenAreaCache fetched;
    std::string err_msg;
    bool fetch_ok = fetch_forbidden_area_from_server(cam_id, &fetched, &err_msg);

    static std::atomic<int> fetch_fail_counter[2];
    {
        std::lock_guard<std::mutex> lock(g_forbidden_area_mutex);
        g_forbidden_area_fetching[cam_id] = false;
        if (fetch_ok) {
            g_forbidden_area_cache[cam_id] = fetched;
            fetch_fail_counter[cam_id].store(0);
        } else {
            g_forbidden_area_cache[cam_id].last_fetch_ms = now_ms;
            int fail_n = fetch_fail_counter[cam_id].fetch_add(1) + 1;
            if (fail_n <= 3 || (fail_n % 60) == 0) {
                printf("[ForbiddenArea] 拉取失败: cam=%d err=%s\n", cam_id, err_msg.c_str());
            }
        }
    }
}

bool get_forbidden_area_polygon_for_frame(int cam_id, const cv::Size& frame_size, std::vector<cv::Point2f>* polygon_out) {
    if (!polygon_out) return false;
    polygon_out->clear();
    if (cam_id < 0 || cam_id > 1) return false;
    if (frame_size.width <= 0 || frame_size.height <= 0) return false;

    ForbiddenAreaCache cache;
    {
        std::lock_guard<std::mutex> lock(g_forbidden_area_mutex);
        cache = g_forbidden_area_cache[cam_id];
    }
    if (!cache.valid || cache.points.size() != 4) return false;

    float sx = 1.0f;
    float sy = 1.0f;
    if (cache.image_width > 0) sx = (float)frame_size.width / (float)cache.image_width;
    if (cache.image_height > 0) sy = (float)frame_size.height / (float)cache.image_height;

    polygon_out->reserve(cache.points.size());
    for (const auto& p : cache.points) {
        float x = p.x * sx;
        float y = p.y * sy;
        if (x < 0.0f) x = 0.0f;
        if (y < 0.0f) y = 0.0f;
        if (x > (float)(frame_size.width - 1)) x = (float)(frame_size.width - 1);
        if (y > (float)(frame_size.height - 1)) y = (float)(frame_size.height - 1);
        polygon_out->push_back(cv::Point2f(x, y));
    }
    return polygon_out->size() == 4;
}

// =====================================================================
// Intrusion detection
// =====================================================================

bool collect_intrusion_hits_for_frame(
    int cam_id,
    const cv::Size& frame_size,
    const std::vector<DetectionResultItem>& detections,
    std::vector<cv::Point2f>* polygon_out,
    std::vector<DetectionResultItem>* hit_detections_out) {

    if (!polygon_out || !hit_detections_out) return false;
    polygon_out->clear();
    hit_detections_out->clear();
    if (cam_id < 0 || cam_id > 1) return false;
    if (frame_size.width <= 0 || frame_size.height <= 0) return false;

    if (!get_forbidden_area_polygon_for_frame(cam_id, frame_size, polygon_out)) {
        return false;
    }

    for (const auto& det : detections) {
        float cx = (det.x1 + det.x2) * 0.5f;
        float cy = (det.y1 + det.y2) * 0.5f;
        if (cv::pointPolygonTest(*polygon_out, cv::Point2f(cx, cy), false) >= 0.0) {
            hit_detections_out->push_back(det);
        }
    }
    return true;
}

void draw_forbidden_area_overlay_if_available(int cam_id, cv::Mat& frame_bgr) {
    if (frame_bgr.empty()) return;
    refresh_forbidden_area_cache_if_needed(cam_id);
    std::vector<cv::Point2f> polygon;
    if (!get_forbidden_area_polygon_for_frame(cam_id, frame_bgr.size(), &polygon)) return;

    std::vector<cv::Point> poly_i;
    poly_i.reserve(polygon.size());
    for (const auto& p : polygon) {
        poly_i.push_back(cv::Point((int)(p.x + 0.5f), (int)(p.y + 0.5f)));
    }
    if (poly_i.size() < 3) return;

    const cv::Point* pts = poly_i.data();
    int npts = (int)poly_i.size();
    cv::polylines(frame_bgr, &pts, &npts, 1, true, cv::Scalar(0, 0, 255), 2);
    for (const auto& p : poly_i) {
        cv::circle(frame_bgr, p, 3, cv::Scalar(0, 0, 255), -1);
    }
}

void draw_intrusion_boxes_overlay_if_needed(
    int cam_id,
    cv::Mat& frame_bgr,
    const std::vector<DetectionResultItem>& detections) {

    if (frame_bgr.empty()) return;
    if (detections.empty()) return;

    std::vector<cv::Point2f> polygon;
    std::vector<DetectionResultItem> hits;
    if (!collect_intrusion_hits_for_frame(cam_id, frame_bgr.size(), detections, &polygon, &hits)) return;
    if (hits.empty()) return;

    for (const auto& det : hits) {
        int x1 = std::max(0, std::min((int)det.x1, frame_bgr.cols - 1));
        int y1 = std::max(0, std::min((int)det.y1, frame_bgr.rows - 1));
        int x2 = std::max(0, std::min((int)det.x2, frame_bgr.cols - 1));
        int y2 = std::max(0, std::min((int)det.y2, frame_bgr.rows - 1));
        if (x2 <= x1 || y2 <= y1) continue;

        cv::rectangle(frame_bgr, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 0, 255), 2);

        std::string label = label_name_for_detection(det);
        char text[192];
        snprintf(text, sizeof(text), "INTR %s %.1f%%", label.c_str(), det.score * 100.0f);
        int baseline = 0;
        cv::Size ts = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        int tx = x1;
        int ty = y1 - ts.height - baseline;
        if (ty < 0) ty = 0;
        if (tx + ts.width > frame_bgr.cols) tx = std::max(0, frame_bgr.cols - ts.width);
        cv::rectangle(frame_bgr, cv::Rect(tx, ty, ts.width, ts.height + baseline), cv::Scalar(0, 0, 255), -1);
        cv::putText(frame_bgr, text, cv::Point(tx, ty + ts.height),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
    }
}

// build_detections_from_tracks and label_name_for_detection are in http_ctrl_tracker_draw.cpp

// =====================================================================
// Intrusion alert
// =====================================================================

void report_forbidden_area_intrusion_alert_worker(
    int cam_id,
    std::vector<DetectionResultItem> hit_detections,
    std::vector<cv::Point2f> polygon,
    cv::Mat frame_bgr) {

    if (frame_bgr.empty()) return;
    if (hit_detections.empty()) return;

    std::vector<cv::Point> poly_i;
    poly_i.reserve(polygon.size());
    for (const auto& p : polygon) {
        poly_i.push_back(cv::Point((int)(p.x + 0.5f), (int)(p.y + 0.5f)));
    }
    if (poly_i.size() >= 3) {
        const cv::Point* pts = poly_i.data();
        int npts = (int)poly_i.size();
        cv::polylines(frame_bgr, &pts, &npts, 1, true, cv::Scalar(0, 0, 255), 3);
    }
    for (const auto& p : poly_i) {
        cv::circle(frame_bgr, p, 4, cv::Scalar(0, 0, 255), -1);
    }
    for (const auto& det : hit_detections) {
        int x1 = std::max(0, std::min((int)det.x1, frame_bgr.cols - 1));
        int y1 = std::max(0, std::min((int)det.y1, frame_bgr.rows - 1));
        int x2 = std::max(0, std::min((int)det.x2, frame_bgr.cols - 1));
        int y2 = std::max(0, std::min((int)det.y2, frame_bgr.rows - 1));
        if (x2 <= x1 || y2 <= y1) continue;
        cv::rectangle(frame_bgr, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 0, 255), 2);
    }

    std::vector<uchar> jpg_buf;
    if (!cv::imencode(".jpg", frame_bgr, jpg_buf, {cv::IMWRITE_JPEG_QUALITY, 85})) {
        printf("[IntrusionReport] JPEG 编码失败: cam=%d\n", cam_id);
        return;
    }
    std::string image_b64 = base64_encode_bytes(jpg_buf.data(), jpg_buf.size());
    int camera_id = (cam_id == 0) ? g_report_camera_id_cam0 : g_report_camera_id_cam1;

    std::ostringstream detection_result;
    detection_result << "{\"type\":\"env_intrusion\",\"cam\":" << cam_id
                     << ",\"hitCount\":" << hit_detections.size()
                     << ",\"zonePoints\":[";
    for (size_t i = 0; i < polygon.size(); ++i) {
        if (i > 0) detection_result << ",";
        detection_result << "{\"x\":" << (int)(polygon[i].x + 0.5f)
                         << ",\"y\":" << (int)(polygon[i].y + 0.5f) << "}";
    }
    detection_result << "],\"objects\":[";
    for (size_t i = 0; i < hit_detections.size(); ++i) {
        const auto& det = hit_detections[i];
        if (i > 0) detection_result << ",";
        float cx = (det.x1 + det.x2) * 0.5f;
        float cy = (det.y1 + det.y2) * 0.5f;
        detection_result << "{\"label\":\"" << json_escape(label_name_for_detection(det)) << "\""
                         << ",\"cls\":" << det.label
                         << ",\"score\":" << std::fixed << std::setprecision(3) << det.score
                         << ",\"center\":{\"x\":" << (int)(cx + 0.5f) << ",\"y\":" << (int)(cy + 0.5f) << "}"
                         << ",\"box\":{\"x1\":" << (int)(det.x1 + 0.5f)
                         << ",\"y1\":" << (int)(det.y1 + 0.5f)
                         << ",\"x2\":" << (int)(det.x2 + 0.5f)
                         << ",\"y2\":" << (int)(det.y2 + 0.5f) << "}}";
    }
    detection_result << "]}";

    std::ostringstream body;
    body << "{";
    body << "\"cameraId\":" << camera_id << ",";
    body << "\"detectionTime\":\"" << local_time_iso8601() << "\",";
    body << "\"detectionResult\":\"" << json_escape(detection_result.str()) << "\",";
    body << "\"imageBase64\":\"" << image_b64 << "\"";
    body << "}";

    std::string response_head;
    bool ok = post_json_to_report_server(body.str(), &response_head);
    if (ok) {
        printf("[IntrusionReport] 上报成功: cam=%d cameraId=%d hit=%zu\n",
               cam_id, camera_id, hit_detections.size());
    } else {
        std::string brief = response_head.empty() ? "(no response)" : response_head.substr(0, 120);
        printf("[IntrusionReport] 上报失败: cam=%d cameraId=%d hit=%zu resp=%s\n",
               cam_id, camera_id, hit_detections.size(), brief.c_str());
    }
}

void report_forbidden_area_intrusion_alert_async(
    int cam_id,
    const std::vector<DetectionResultItem>& hit_detections,
    const std::vector<cv::Point2f>& polygon,
    const cv::Mat& frame_bgr) {

    if (frame_bgr.empty() || hit_detections.empty()) return;
    cv::Mat frame_copy = frame_bgr.clone();
    if (frame_copy.empty()) return;
    try {
        std::thread(report_forbidden_area_intrusion_alert_worker, cam_id, hit_detections, polygon, frame_copy).detach();
    } catch (...) {
        printf("[IntrusionReport] 启动上报线程失败: cam=%d\n", cam_id);
    }
}

void maybe_trigger_forbidden_area_intrusion_alert(
    int cam_id,
    const std::vector<DetectionResultItem>& detections,
    const cv::Mat& frame_bgr) {

    if (cam_id < 0 || cam_id > 1) return;
    if (frame_bgr.empty()) return;

    refresh_forbidden_area_cache_if_needed(cam_id);

    std::vector<cv::Point2f> polygon;
    std::vector<DetectionResultItem> hit_detections;
    if (!collect_intrusion_hits_for_frame(cam_id, frame_bgr.size(), detections, &polygon, &hit_detections)) {
        std::lock_guard<std::mutex> lock(g_intrusion_alert_state_mutex);
        g_intrusion_over_state[cam_id] = false;
        return;
    }

    bool intrusion = !hit_detections.empty();
    bool should_report = false;
    {
        std::lock_guard<std::mutex> lock(g_intrusion_alert_state_mutex);
        bool was_intrusion = g_intrusion_over_state[cam_id];
        g_intrusion_over_state[cam_id] = intrusion;
        if (!intrusion || was_intrusion) {
            return;
        }

        long long now_ms = monotonic_now_ms();
        int cooldown_ms = g_intrusion_alert_cooldown_ms.load();
        if (cooldown_ms < 0) cooldown_ms = 0;
        if (now_ms - g_last_intrusion_alert_ms[cam_id] < cooldown_ms) {
            return;
        }
        g_last_intrusion_alert_ms[cam_id] = now_ms;
        should_report = true;
    }

    if (should_report) {
        report_forbidden_area_intrusion_alert_async(cam_id, hit_detections, polygon, frame_bgr);
    }
}
