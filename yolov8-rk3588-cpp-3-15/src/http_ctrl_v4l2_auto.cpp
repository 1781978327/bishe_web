// V4L2 auto-detection and camera source assignment.
// Extracted from main_http_ctrl.cc during refactoring.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <glob.h>
#include <algorithm>
#include <cctype>
#include <sstream>

#include "http_ctrl_v4l2_auto.h"
#include "http_ctrl_globals.h"

// -- Utility helpers (originally static in main_http_ctrl.cc, now extern) --
// These live in the main translation unit; we declare them extern here.
extern std::string trim_copy(const std::string& input);
extern bool is_all_digits(const std::string& text);

bool parse_v4l2_source(const std::string& source, std::string* device_path, int* device_index) {
    if (device_path) device_path->clear();
    if (device_index) *device_index = -1;

    const std::string value = trim_copy(source);
    if (value.empty()) return false;

    if (is_all_digits(value)) {
        int idx = atoi(value.c_str());
        if (idx < 0) return false;
        if (device_index) *device_index = idx;
        if (device_path) *device_path = "/dev/video" + std::to_string(idx);
        return true;
    }

    const std::string prefix = "/dev/video";
    if (value.compare(0, prefix.size(), prefix) == 0) {
        std::string suffix = value.substr(prefix.size());
        if (!suffix.empty() && is_all_digits(suffix)) {
            if (device_index) *device_index = atoi(suffix.c_str());
        }
        if (device_path) *device_path = value;
        return true;
    }
    return false;
}

int parse_video_device_index(const std::string& path) {
    const std::string prefix = "/dev/video";
    if (path.compare(0, prefix.size(), prefix) != 0) return -1;
    std::string suffix = path.substr(prefix.size());
    if (suffix.empty() || !is_all_digits(suffix)) return -1;
    return atoi(suffix.c_str());
}

bool query_v4l2_capture_candidate(const std::string& path, AutoCameraCandidate* out) {
    if (!out) return false;
    out->path = path;
    out->card.clear();
    out->bus_info.clear();
    out->index = parse_video_device_index(path);
    if (out->index < 0) return false;

    int fd = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
        close(fd);
        return false;
    }
    close(fd);

    uint32_t caps = cap.capabilities;
    if (caps & V4L2_CAP_DEVICE_CAPS) {
        caps = cap.device_caps;
    }
    bool is_capture =
        (caps & V4L2_CAP_VIDEO_CAPTURE) ||
        (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
    bool is_streaming = (caps & V4L2_CAP_STREAMING);
    if (!is_capture || !is_streaming) {
        return false;
    }

    out->card = reinterpret_cast<const char*>(cap.card);
    out->bus_info = reinterpret_cast<const char*>(cap.bus_info);
    return true;
}

std::vector<AutoCameraCandidate> list_v4l2_capture_candidates() {
    std::vector<AutoCameraCandidate> candidates;

    glob_t glob_result;
    memset(&glob_result, 0, sizeof(glob_result));
    if (glob("/dev/video*", 0, nullptr, &glob_result) != 0) {
        globfree(&glob_result);
        return candidates;
    }

    std::vector<std::string> paths;
    paths.reserve(glob_result.gl_pathc);
    for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
        const char* p = glob_result.gl_pathv[i];
        if (p && *p) paths.emplace_back(p);
    }
    globfree(&glob_result);

    std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
        int ia = parse_video_device_index(a);
        int ib = parse_video_device_index(b);
        if (ia >= 0 && ib >= 0 && ia != ib) return ia < ib;
        return a < b;
    });

    for (const auto& path : paths) {
        AutoCameraCandidate cand;
        if (query_v4l2_capture_candidate(path, &cand)) {
            candidates.push_back(cand);
        }
    }
    return candidates;
}

bool is_auto_source_text(const std::string& source_text) {
    std::string value = trim_copy(source_text);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return value.empty() || value == "auto" || value == "default";
}

void maybe_auto_assign_camera_sources(bool cam0_source_user_set, bool cam1_source_user_set) {
    bool cam0_needs_auto = (!cam0_source_user_set) || is_auto_source_text(g_input_source_cam0);
    bool cam1_needs_auto = (!cam1_source_user_set) || is_auto_source_text(g_input_source_cam1);
    if (!cam0_needs_auto && !cam1_needs_auto) {
        return;
    }

    std::vector<AutoCameraCandidate> candidates = list_v4l2_capture_candidates();
    if (candidates.empty()) {
        printf("[Input-Auto] 未发现可用 V4L2 采集设备，保持原输入源设置\n");
        return;
    }

    std::vector<AutoCameraCandidate> preferred;
    preferred.reserve(candidates.size());
    std::vector<std::string> seen_keys;
    seen_keys.reserve(candidates.size());

    for (const auto& cand : candidates) {
        std::string key = !cand.bus_info.empty() ? cand.bus_info : cand.card;
        if (key.empty()) key = cand.path;
        if (std::find(seen_keys.begin(), seen_keys.end(), key) == seen_keys.end()) {
            preferred.push_back(cand);
            seen_keys.push_back(key);
        }
    }
    for (const auto& cand : candidates) {
        bool existed = false;
        for (const auto& p : preferred) {
            if (p.path == cand.path) {
                existed = true;
                break;
            }
        }
        if (!existed) preferred.push_back(cand);
    }

    std::ostringstream detected_oss;
    detected_oss << "[Input-Auto] 检测到采集设备:";
    for (const auto& cand : preferred) {
        detected_oss << " " << cand.path;
        if (!cand.bus_info.empty()) {
            detected_oss << "(" << cand.bus_info << ")";
        }
    }
    printf("%s\n", detected_oss.str().c_str());

    auto pick_candidate = [&](const std::vector<std::string>& avoid_paths) -> std::string {
        for (const auto& cand : preferred) {
            if (std::find(avoid_paths.begin(), avoid_paths.end(), cand.path) == avoid_paths.end()) {
                return cand.path;
            }
        }
        return "";
    };

    std::string fixed_cam0 = cam0_needs_auto ? "" : trim_copy(g_input_source_cam0);
    std::string fixed_cam1 = cam1_needs_auto ? "" : trim_copy(g_input_source_cam1);
    std::string selected_cam0 = trim_copy(g_input_source_cam0);
    std::string selected_cam1 = trim_copy(g_input_source_cam1);

    if (cam0_needs_auto) {
        std::vector<std::string> avoid;
        if (!fixed_cam1.empty()) avoid.push_back(fixed_cam1);
        std::string picked = pick_candidate(avoid);
        if (!picked.empty()) selected_cam0 = picked;
    }
    if (cam1_needs_auto) {
        std::vector<std::string> avoid;
        if (!fixed_cam0.empty()) avoid.push_back(fixed_cam0);
        if (!selected_cam0.empty()) avoid.push_back(selected_cam0);
        std::string picked = pick_candidate(avoid);
        if (!picked.empty()) selected_cam1 = picked;
    }

    if (cam0_needs_auto && !selected_cam0.empty()) {
        g_input_source_cam0 = selected_cam0;
    }
    if (cam1_needs_auto && !selected_cam1.empty()) {
        g_input_source_cam1 = selected_cam1;
    }

    printf("[Input-Auto] 自动选择: cam0=%s cam1=%s\n",
           g_input_source_cam0.c_str(), g_input_source_cam1.c_str());
}

bool open_camera_input_source(const std::string& requested_source,
                              int default_index,
                              int target_width,
                              int target_height,
                              int target_fps,
                              v4l2_dmabuf::CaptureContext* dmabuf_cap,
                              cv::VideoCapture* cv_cap,
                              bool* using_dmabuf,
                              std::string* normalized_source,
                              std::string* err_msg) {
    if (using_dmabuf) *using_dmabuf = false;
    if (normalized_source) normalized_source->clear();
    if (err_msg) err_msg->clear();
    if (!dmabuf_cap || !cv_cap) {
        if (err_msg) *err_msg = "invalid capture context";
        return false;
    }

    cv_cap->release();
    v4l2_dmabuf::close_capture(dmabuf_cap);

    std::string source = trim_copy(requested_source);
    if (source.empty()) {
        source = std::to_string(default_index);
    }

    std::string device_path;
    int device_index = -1;
    bool is_v4l2 = parse_v4l2_source(source, &device_path, &device_index);
    if (is_v4l2) {
        if (normalized_source) {
            *normalized_source = device_path.empty() ? source : device_path;
        }

        v4l2_dmabuf::CaptureConfig cfg;
        cfg.device_name = device_path;
        cfg.width = target_width;
        cfg.height = target_height;
        cfg.fps = target_fps;
        cfg.dma_buffers = 4;

        std::string dmabuf_err;
        if (v4l2_dmabuf::open_capture(cfg, dmabuf_cap, &dmabuf_err) == 0) {
            if (using_dmabuf) *using_dmabuf = true;
            return true;
        }

        bool opened = false;
        if (device_index >= 0) {
            opened = cv_cap->open(device_index, cv::CAP_V4L2);
            if (!opened) {
                opened = cv_cap->open(device_index);
            }
        } else {
            opened = cv_cap->open(device_path, cv::CAP_V4L2);
            if (!opened) {
                opened = cv_cap->open(device_path);
            }
        }
        if (opened) {
            cv_cap->set(cv::CAP_PROP_FRAME_WIDTH, target_width);
            cv_cap->set(cv::CAP_PROP_FRAME_HEIGHT, target_height);
            cv_cap->set(cv::CAP_PROP_FPS, target_fps);
            return true;
        }

        if (err_msg) {
            *err_msg = "DMABUF失败: " + dmabuf_err + ", OpenCV回退也失败";
        }
        return false;
    }

    // 非 V4L2 源（例如 rtsp://、http://、file），统一走 FFmpeg/OpenCV
    if (normalized_source) *normalized_source = source;
    bool opened = cv_cap->open(source, cv::CAP_FFMPEG);
    std::string lower_source = source;
    std::transform(lower_source.begin(), lower_source.end(), lower_source.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    bool is_network_stream = (lower_source.rfind("rtsp://", 0) == 0) ||
                             (lower_source.rfind("rtmp://", 0) == 0) ||
                             (lower_source.rfind("http://", 0) == 0) ||
                             (lower_source.rfind("https://", 0) == 0) ||
                             (lower_source.rfind("udp://", 0) == 0) ||
                             (lower_source.rfind("tcp://", 0) == 0);
    if (!opened && !is_network_stream) {
        opened = cv_cap->open(source);
    }
    if (opened) {
        return true;
    }
    if (err_msg) {
        *err_msg = "OpenCV/FFmpeg 打开失败: " + source;
    }
    return false;
}
