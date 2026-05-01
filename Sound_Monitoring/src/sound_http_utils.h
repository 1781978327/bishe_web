#pragma once

#include <cstddef>
#include <string>
#include "yamnet.h"

// JSON escape for embedding strings in JSON output
void json_escape_string(const char* input, char* output, size_t output_size);

// Parse Content-Length from HTTP headers
int parse_content_length(const char* headers);

// Check if a filesystem path exists
int path_exists(const char *path);

// Parse "seconds=N" from a query string
int parse_seconds_from_query(const char *query, int default_sec, int max_sec);

// Parse an integer key from a query string (e.g. "limit=5")
int parse_int_from_query(const char *query, const char *key, int default_value, int max_value);

// Parse a boolean flag from a query string (e.g. "verbose=1")
int parse_flag_from_query(const char *query, const char *key);

// Decide whether to log an HTTP request (skip noisy GET polls)
int should_log_http_request(const char *method, const char *path);

// Summarize top-N results into a compact string
std::string summarize_top_results(const ResultEntry *results, int result_count, int max_items);

// Send a raw HTTP response
void send_response(int client_fd, const char* status, const char* content_type,
                   const char* body, int body_len);

// Send a JSON response with status code and message
void send_json_response(int client_fd, int status_code, const char* message);

// Signal handler for graceful shutdown
void signal_handler(int sig);
