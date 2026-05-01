#include "http_ctrl_utils.h"
#include "http_ctrl_globals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <limits.h>

#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cctype>

// ---------------------- Process memory ----------------------

long get_process_rss_kb() {
    std::ifstream ifs("/proc/self/status");
    if (!ifs.is_open()) return -1;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            long rss_kb = -1;
            iss >> rss_kb;
            return rss_kb;
        }
    }
    return -1;
}

// ---------------------- RTSP URL helpers ----------------------

void refresh_rtsp_urls() {
    g_rtsp_url_0 = "rtsp://" + g_rtsp_host + ":" + std::to_string(g_rtsp_port) + "/cam0";
    g_rtsp_url_1 = "rtsp://" + g_rtsp_host + ":" + std::to_string(g_rtsp_port) + "/cam1";
    g_rtsp_url_mosaic = "rtsp://" + g_rtsp_host + ":" + std::to_string(g_rtsp_port) + "/cam2";
    g_rtsp_url_video = "rtsp://" + g_rtsp_host + ":" + std::to_string(g_rtsp_port) + "/cam3";
}

// ---------------------- URL decoding ----------------------

std::string url_decode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int val;
            std::istringstream iss(str.substr(i + 1, 2));
            iss >> std::hex >> val;
            result += char(val);
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

// ---------------------- Filesystem helpers ----------------------

bool file_exists(const std::string& path) {
    return !path.empty() && access(path.c_str(), R_OK) == 0;
}

bool directory_exists(const std::string& path) {
    if (path.empty()) return false;
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string path_dirname(const std::string& path) {
    if (path.empty()) return "";
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

std::string path_join(const std::string& base, const std::string& leaf) {
    if (base.empty()) return leaf;
    if (leaf.empty()) return base;
    if (leaf[0] == '/') return leaf;
    if (base[base.size() - 1] == '/') return base + leaf;
    return base + "/" + leaf;
}

// ---------------------- Project root detection ----------------------

std::vector<std::string> project_root_search_seeds() {
    std::vector<std::string> seeds;
    const char* env_root = getenv("RKNN_HTTP_CTRL_ROOT");
    if (env_root && *env_root) {
        seeds.emplace_back(env_root);
    }

    char exe_path[PATH_MAX] = {0};
    ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (exe_len > 0) {
        exe_path[exe_len] = '\0';
        seeds.emplace_back(path_dirname(exe_path));
    }

    char cwd_buf[PATH_MAX] = {0};
    if (getcwd(cwd_buf, sizeof(cwd_buf) - 1) != nullptr) {
        seeds.emplace_back(cwd_buf);
    }

    return seeds;
}

bool looks_like_project_root(const std::string& path) {
    return directory_exists(path) &&
           file_exists(path_join(path, "CMakeLists.txt")) &&
           directory_exists(path_join(path, "src")) &&
           directory_exists(path_join(path, "model"));
}

std::string detect_project_root() {
    static std::string cached_root;
    if (!cached_root.empty()) return cached_root;

    std::vector<std::string> seeds = project_root_search_seeds();
    for (const auto& seed : seeds) {
        std::string cur = seed;
        for (int depth = 0; depth < 6 && !cur.empty(); ++depth) {
            if (looks_like_project_root(cur)) {
                cached_root = cur;
                return cached_root;
            }
            std::string parent = path_dirname(cur);
            if (parent == cur) break;
            cur = parent;
        }
    }

    return "";
}

std::string resolve_project_file(const std::string& relative_path) {
    std::string root = detect_project_root();
    if (root.empty()) return "";
    std::string candidate = path_join(root, relative_path);
    return file_exists(candidate) ? candidate : "";
}

std::string resolve_project_executable(const std::string& relative_path) {
    std::string root = detect_project_root();
    if (root.empty()) return "";
    std::string candidate = path_join(root, relative_path);
    return access(candidate.c_str(), X_OK) == 0 ? candidate : "";
}

// ---------------------- Default path resolvers ----------------------

std::string default_label_path() {
    // 根据模型名称选择合适的标签文件
    if (g_model_path.find("best-coco-person-moto.rknn") != std::string::npos) {
        std::string resolved = resolve_project_file("model/RK3588/best-coco-person-moto.txt");
        if (!resolved.empty()) return resolved;
        return "model/RK3588/best-coco-person-moto.txt";
    }

    std::string resolved = resolve_project_file(DEFAULT_LABEL_REL_PATH);
    if (!resolved.empty()) return resolved;
    return DEFAULT_LABEL_REL_PATH;
}

std::string default_mediamtx_binary_path() {
    std::string resolved = resolve_project_executable(DEFAULT_MEDIAMTX_REL_PATH);
    if (!resolved.empty()) return resolved;
    return DEFAULT_MEDIAMTX_REL_PATH;
}

std::string default_record_output_dir() {
    std::string root = detect_project_root();
    if (!root.empty()) {
        return path_join(root, DEFAULT_RECORD_OUTPUT_REL_PATH);
    }
    return DEFAULT_RECORD_OUTPUT_REL_PATH;
}

// ---------------------- Executable finding ----------------------

std::string find_executable_in_path(const std::string& name) {
    if (name.empty()) return "";
    if (name.find('/') != std::string::npos) {
        return is_executable_file(name) ? name : "";
    }

    const char* env_path = getenv("PATH");
    if (!env_path || !*env_path) return "";
    std::string path_env = env_path;
    size_t start = 0;
    while (start <= path_env.size()) {
        size_t end = path_env.find(':', start);
        std::string dir = path_env.substr(start, end == std::string::npos ? std::string::npos : (end - start));
        if (dir.empty()) dir = ".";
        std::string candidate = path_join(dir, name);
        if (is_executable_file(candidate)) {
            return candidate;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

std::string resolve_ffmpeg_binary_path() {
    std::vector<std::string> candidates;
    candidates.reserve(4);
    if (!g_ffmpeg_bin.empty()) candidates.push_back(g_ffmpeg_bin);
    candidates.push_back(DEFAULT_FFMPEG_BIN);
    candidates.push_back("ffmpeg");

    for (const auto& candidate : candidates) {
        if (candidate.empty()) continue;
        if (candidate.find('/') != std::string::npos) {
            if (is_executable_file(candidate)) return candidate;
        } else {
            std::string resolved = find_executable_in_path(candidate);
            if (!resolved.empty()) return resolved;
        }
    }
    return "";
}

// ---------------------- Text / flag parsing ----------------------

bool parse_bool_flag_text(const std::string& text, bool default_value) {
    if (text.empty()) return default_value;
    std::string v = text;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "off" || v == "no") return false;
    return default_value;
}

bool is_local_rtsp_host(const std::string& host) {
    std::string h = host;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return h == "127.0.0.1" || h == "localhost" || h == "0.0.0.0" || h == "::1";
}

bool is_tcp_service_ready(const std::string& host, int port, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    if (timeout_ms < 100) timeout_ms = 100;
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
        struct hostent* server = gethostbyname(host.c_str());
        if (server == nullptr || server->h_length <= 0) {
            close(sock);
            return false;
        }
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);
    }

    bool ready = (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0);
    close(sock);
    return ready;
}

bool is_executable_file(const std::string& path) {
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

// ---------------------- String utilities ----------------------

std::string trim_copy(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace((unsigned char)input[start])) ++start;
    size_t end = input.size();
    while (end > start && std::isspace((unsigned char)input[end - 1])) --end;
    return input.substr(start, end - start);
}

bool is_all_digits(const std::string& text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (!std::isdigit((unsigned char)c)) return false;
    }
    return true;
}

// ---------------------- HTTP query parsing ----------------------

std::string parse_query_param(const std::string& path, const std::string& key) {
    size_t qpos = path.find('?');
    if (qpos == std::string::npos) return "";
    const std::string query = path.substr(qpos + 1);
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        std::string part = query.substr(start, end == std::string::npos ? std::string::npos : (end - start));
        size_t eq = part.find('=');
        if (eq != std::string::npos) {
            std::string part_key = part.substr(0, eq);
            if (part_key == key) {
                std::string raw = part.substr(eq + 1);
                return url_decode(raw);
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

// ---------------------- Time helpers ----------------------

long long monotonic_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string local_time_iso8601() {
    std::time_t now = std::time(nullptr);
    std::tm tm_local;
    localtime_r(&now, &tm_local);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_local);
    return std::string(buf);
}

// ---------------------- Directory / filename helpers ----------------------

bool ensure_directory_tree(const std::string& path) {
    if (path.empty()) return false;
    if (directory_exists(path)) return true;

    std::string normalized = path;
    while (normalized.size() > 1 && normalized[normalized.size() - 1] == '/') {
        normalized.resize(normalized.size() - 1);
    }

    std::string current;
    size_t start = 0;
    if (!normalized.empty() && normalized[0] == '/') {
        current = "/";
        start = 1;
    }

    while (start <= normalized.size()) {
        size_t end = normalized.find('/', start);
        std::string part = normalized.substr(start, end == std::string::npos ? std::string::npos : (end - start));
        if (!part.empty()) {
            if (!current.empty() && current != "/") current += "/";
            current += part;
            if (!directory_exists(current)) {
                if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return directory_exists(normalized);
}

std::string sanitize_filename_component(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = (unsigned char)text[i];
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.') {
            out.push_back((char)c);
        } else {
            out.push_back('_');
        }
    }
    while (!out.empty() && out[0] == '.') out.erase(out.begin());
    while (!out.empty() && out[out.size() - 1] == '.') out.resize(out.size() - 1);
    return out;
}

std::string compact_timestamp_for_filename() {
    std::time_t now = std::time(nullptr);
    std::tm tm_local;
    localtime_r(&now, &tm_local);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_local);
    return std::string(buf);
}

// ---------------------- Base64 encoding ----------------------

std::string base64_encode_bytes(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = ((uint32_t)data[i]) << 16;
        bool has_b1 = (i + 1 < len);
        bool has_b2 = (i + 2 < len);
        if (has_b1) v |= ((uint32_t)data[i + 1]) << 8;
        if (has_b2) v |= (uint32_t)data[i + 2];

        out.push_back(table[(v >> 18) & 0x3F]);
        out.push_back(table[(v >> 12) & 0x3F]);
        out.push_back(has_b1 ? table[(v >> 6) & 0x3F] : '=');
        out.push_back(has_b2 ? table[v & 0x3F] : '=');
    }
    return out;
}

// ---------------------- Socket send helper ----------------------

bool send_all_bytes(int sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}
