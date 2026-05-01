#ifndef HTTP_CTRL_UTILS_H
#define HTTP_CTRL_UTILS_H

// General-purpose utility functions extracted from main_http_ctrl.cc.

#include <string>
#include <vector>
#include <cstdint>

// Process memory
long get_process_rss_kb();

// RTSP URL helpers
void refresh_rtsp_urls();

// URL decoding
std::string url_decode(const std::string& str);

// Filesystem helpers
bool file_exists(const std::string& path);
bool directory_exists(const std::string& path);
std::string path_dirname(const std::string& path);
std::string path_join(const std::string& base, const std::string& leaf);

// Project root detection
std::vector<std::string> project_root_search_seeds();
bool looks_like_project_root(const std::string& path);
std::string detect_project_root();
std::string resolve_project_file(const std::string& relative_path);
std::string resolve_project_executable(const std::string& relative_path);

// Default path resolvers
std::string default_label_path();
std::string default_mediamtx_binary_path();
std::string default_record_output_dir();

// Executable finding
std::string find_executable_in_path(const std::string& name);
std::string resolve_ffmpeg_binary_path();
bool is_executable_file(const std::string& path);

// Text parsing helpers
bool parse_bool_flag_text(const std::string& text, bool default_value);
bool is_local_rtsp_host(const std::string& host);
bool is_tcp_service_ready(const std::string& host, int port, int timeout_ms = 300);

// String utilities
std::string trim_copy(const std::string& input);
bool is_all_digits(const std::string& text);

// HTTP query parsing
std::string parse_query_param(const std::string& path, const std::string& key);

// Time helpers
long long monotonic_now_ms();
std::string local_time_iso8601();

// Directory / filename helpers
bool ensure_directory_tree(const std::string& path);
std::string sanitize_filename_component(const std::string& text);
std::string compact_timestamp_for_filename();

// Base64 encoding
std::string base64_encode_bytes(const unsigned char* data, size_t len);

// Socket send helper
bool send_all_bytes(int sock, const char* data, size_t len);

#endif // HTTP_CTRL_UTILS_H
