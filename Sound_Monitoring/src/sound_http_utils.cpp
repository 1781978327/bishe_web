#include "sound_http_utils.h"
#include "sound_globals.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <sstream>
#include <string>

// Forward declarations for functions defined in other modules
extern void rt_stop();
extern int emergency_kws_stop();

void json_escape_string(const char* input, char* output, size_t output_size) {
    size_t j = 0;
    if (output_size == 0) return;

    for (size_t i = 0; input[i] != '\0' && j + 1 < output_size; i++) {
        char c = input[i];

        if ((c == '"' || c == '\\') && j + 2 < output_size) {
            output[j++] = '\\';
            output[j++] = c;
        } else if (c == '\n' && j + 2 < output_size) {
            output[j++] = '\\';
            output[j++] = 'n';
        } else if (c == '\r' && j + 2 < output_size) {
            output[j++] = '\\';
            output[j++] = 'r';
        } else if (c == '\t' && j + 2 < output_size) {
            output[j++] = '\\';
            output[j++] = 't';
        } else if ((unsigned char)c >= 0x20) {
            output[j++] = c;
        }
    }

    output[j] = '\0';
}

int parse_content_length(const char* headers) {
    const char* line = headers;

    while (line && *line) {
        const char* line_end = strstr(line, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);

        if (line_len == 0) {
            break;
        }

        if (line_len >= 15 && strncasecmp(line, "Content-Length:", 15) == 0) {
            const char* value = line + 15;
            while (*value == ' ' || *value == '\t') value++;
            return atoi(value);
        }

        if (!line_end) break;
        line = line_end + 2;
    }

    return 0;
}

int path_exists(const char *path) {
    if (!path || path[0] == '\0') return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

int parse_seconds_from_query(const char *query, int default_sec, int max_sec) {
    if (!query || query[0] == '\0') return default_sec;

    const char *key = strstr(query, "seconds=");
    if (!key) return default_sec;
    key += 8;
    int sec = atoi(key);
    if (sec <= 0) return default_sec;
    if (sec > max_sec) return max_sec;
    return sec;
}

int parse_int_from_query(const char *query, const char *key, int default_value, int max_value) {
    if (!query || query[0] == '\0' || !key || key[0] == '\0') return default_value;

    std::string needle = std::string(key) + "=";
    const char *hit = strstr(query, needle.c_str());
    if (!hit) return default_value;

    hit += needle.size();
    int value = atoi(hit);
    if (value <= 0) return default_value;
    if (max_value > 0 && value > max_value) return max_value;
    return value;
}

int parse_flag_from_query(const char *query, const char *key) {
    if (!query || !key || key[0] == '\0') return 0;

    std::string needle = std::string(key) + "=";
    const char *hit = strstr(query, needle.c_str());
    if (!hit) return 0;

    hit += needle.size();
    if (strncmp(hit, "1", 1) == 0) return 1;
    if (strncasecmp(hit, "true", 4) == 0) return 1;
    if (strncasecmp(hit, "yes", 3) == 0) return 1;
    return 0;
}

int should_log_http_request(const char *method, const char *path) {
    if (!method || !path) return 1;
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/realtime/events") == 0 ||
            strcmp(path, "/realtime/windows") == 0 ||
            strcmp(path, "/realtime/status") == 0 ||
            strcmp(path, "/wake/events") == 0 ||
            strcmp(path, "/wake/status") == 0 ||
            strcmp(path, "/health") == 0) {
            return 0;
        }
    }
    return 1;
}

std::string summarize_top_results(const ResultEntry *results, int result_count, int max_items) {
    if (!results || result_count <= 0 || max_items <= 0) {
        return "(none)";
    }

    std::ostringstream oss;
    int limit = std::min(result_count, max_items);
    for (int i = 0; i < limit; ++i) {
        if (i > 0) {
            oss << ", ";
        }
        std::string token = results[i].token ? results[i].token : "<null>";
        std::replace(token.begin(), token.end(), '\n', ' ');
        std::replace(token.begin(), token.end(), '\r', ' ');
        oss << token
            << "(" << results[i].score << ")";
    }
    return oss.str();
}

void signal_handler(int sig) {
    printf("\n收到信号 %d，关闭服务器...\n", sig);
    if (server_socket >= 0) close(server_socket);
    (void)emergency_kws_stop();
    if (rt_active) rt_stop();
    g_asr_engine.Close();
    if (model_initialized) {
        release_yamnet_model(&rknn_app_ctx);
    }
    exit(0);
}

void send_response(int client_fd, const char* status, const char* content_type,
                   const char* body, int body_len) {
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body_len);
    send(client_fd, header, header_len, 0);
    if (body && body_len > 0) {
        send(client_fd, body, body_len, 0);
    }
}

void send_json_response(int client_fd, int status_code, const char* message) {
    char body[1024];
    char escaped_message[768];
    json_escape_string(message, escaped_message, sizeof(escaped_message));
    int len = snprintf(body, sizeof(body), "{\"status\": %d, \"message\": \"%s\"}", status_code, escaped_message);
    send_response(client_fd, status_code == 0 ? "200 OK" : "400 Bad Request",
                 "application/json", body, len);
}
