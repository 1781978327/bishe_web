#include "algo_reporter.h"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <opencv2/imgcodecs.hpp>

namespace algo_reporter {
namespace {

long getenv_long(const char* k, long defv) {
  const char* v = std::getenv(k);
  if (!v || !*v) return defv;
  char* end = nullptr;
  long x = std::strtol(v, &end, 10);
  return (end && end != v) ? x : defv;
}

float getenv_float(const char* k, float defv) {
  const char* v = std::getenv(k);
  if (!v || !*v) return defv;
  char* end = nullptr;
  float x = std::strtof(v, &end);
  return (end && end != v) ? x : defv;
}

int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct State {
  std::mutex mu;
  std::unordered_map<int, int64_t> last_report_ms_by_cls;
};

State& st() {
  static State s;
  return s;
}

bool file_exists(const std::string& path) {
  if (path.empty()) return false;
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool directory_exists(const std::string& path) {
  if (path.empty()) return false;
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string dirname_copy(const std::string& path) {
  if (path.empty()) return "";
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return ".";
  if (pos == 0) return "/";
  return path.substr(0, pos);
}

std::string join_path_copy(const std::string& base, const std::string& leaf) {
  if (base.empty()) return leaf;
  if (leaf.empty()) return base;
  if (leaf[0] == '/') return leaf;
  if (base[base.size() - 1] == '/') return base + leaf;
  return base + "/" + leaf;
}

bool looks_like_project_root(const std::string& path) {
  return directory_exists(path) &&
         directory_exists(join_path_copy(path, "src")) &&
         directory_exists(join_path_copy(path, "model")) &&
         file_exists(join_path_copy(path, "CMakeLists.txt"));
}

std::string detect_project_root() {
  static std::string cached_root;
  if (!cached_root.empty()) return cached_root;

  std::vector<std::string> seeds;
  const char* env_root = std::getenv("RKNN_HTTP_CTRL_ROOT");
  if (env_root && *env_root) {
    seeds.emplace_back(env_root);
  }

  char exe_path[PATH_MAX] = {0};
  ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (exe_len > 0) {
    exe_path[exe_len] = '\0';
    seeds.emplace_back(dirname_copy(exe_path));
  }

  char cwd_buf[PATH_MAX] = {0};
  if (getcwd(cwd_buf, sizeof(cwd_buf) - 1) != nullptr) {
    seeds.emplace_back(cwd_buf);
  }

  for (const auto& seed : seeds) {
    std::string cur = seed;
    for (int depth = 0; depth < 6 && !cur.empty(); ++depth) {
      if (looks_like_project_root(cur)) {
        cached_root = cur;
        return cached_root;
      }
      std::string parent = dirname_copy(cur);
      if (parent == cur) break;
      cur = parent;
    }
  }

  return "";
}

std::string find_report_test_binary() {
  const char* env_bin = std::getenv("ALGO_REPORT_TEST_BIN");
  if (env_bin && *env_bin && access(env_bin, X_OK) == 0) {
    return env_bin;
  }

  std::string root = detect_project_root();
  if (root.empty()) return "";

  const char* relative_candidates[] = {
    "build/report_test",
    "build_release/report_test",
    "install/report_test"
  };
  for (const char* rel : relative_candidates) {
    std::string candidate = join_path_copy(root, rel);
    if (access(candidate.c_str(), X_OK) == 0) {
      return candidate;
    }
  }
  return "";
}

float min_conf_for_cls(const Config& cfg, int cls_id) {
  switch (cls_id) {
    case 1: return cfg.min_conf_fall;
    case 2: return cfg.min_conf_fight;
    case 3: return cfg.min_conf_knife;
    default: return 1.0f;
  }
}

}  // namespace

Config load_config_from_env() {
  Config cfg;
  cfg.min_conf_fall = getenv_float("CV_MIN_CONF_FALL", cfg.min_conf_fall);
  cfg.min_conf_fight = getenv_float("CV_MIN_CONF_FIGHT", cfg.min_conf_fight);
  cfg.min_conf_knife = getenv_float("CV_MIN_CONF_KNIFE", cfg.min_conf_knife);
  cfg.cooldown_ms = (int)getenv_long("CV_COOLDOWN_MS", cfg.cooldown_ms);
  return cfg;
}

bool ensure_login(const Config& /*cfg*/) {
  // 兼容旧调用点：不再做任何登录逻辑
  return true;
}

void start_heartbeat(const Config& /*cfg*/) {
  // 兼容旧调用点：不再发送心跳，上报由外部脚本负责
}

void maybe_report_event(const Config& cfg, int cls_id, float conf, const cv::Mat& frame_bgr) {
  if (cls_id != 1 && cls_id != 2 && cls_id != 3) return;
  if (conf < min_conf_for_cls(cfg, cls_id)) return;
  if (frame_bgr.empty()) return;

  // 限频：同一类事件 cooldown 内只打印一次
  {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    int64_t now = now_ms();
    auto it = s.last_report_ms_by_cls.find(cls_id);
    if (it != s.last_report_ms_by_cls.end() && now - it->second < cfg.cooldown_ms) {
      return;
    }
    s.last_report_ms_by_cls[cls_id] = now;
  }

  printf("[algo_reporter] 命中告警: cls=%d conf=%.3f\n", cls_id, conf);
  fflush(stdout);

  // 将当前帧保存为临时 JPEG，供独立的 report_test 进程读取并上报
  const char* img_path = "/tmp/yolo_event.jpg";
  std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 80};
  try {
    cv::imwrite(img_path, frame_bgr, params);
  } catch (...) {
    // 保存失败就只打印日志，不影响主流程
    return;
  }

  // 启动独立进程进行 HTTP 上报，避免在推理进程内部直接使用 libcurl 导致卡死
  std::thread([cls_id, conf]() {
    std::string report_bin = find_report_test_binary();
    if (report_bin.empty()) {
      std::printf("[algo_reporter] 跳过上报: 未找到 report_test 可执行文件\n");
      std::fflush(stdout);
      return;
    }
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd),
                  "\"%s\" %d %.3f >/tmp/report_test.log 2>&1",
                  report_bin.c_str(), cls_id, conf);
    std::system(cmd);
  }).detach();
}

}  // namespace algo_reporter
