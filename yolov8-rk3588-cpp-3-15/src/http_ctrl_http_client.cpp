#include "http_ctrl_http_client.h"
#include "http_ctrl_globals.h"
#include "http_ctrl_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sstream>

// ---------------------- HTTP status parsing ----------------------

int parse_http_status_code(const std::string& response) {
    size_t line_end = response.find("\r\n");
    std::string first_line = (line_end == std::string::npos) ? response : response.substr(0, line_end);
    size_t sp1 = first_line.find(' ');
    if (sp1 == std::string::npos) return -1;
    size_t sp2 = first_line.find(' ', sp1 + 1);
    std::string code_text = first_line.substr(sp1 + 1, (sp2 == std::string::npos) ? std::string::npos : (sp2 - sp1 - 1));
    char* endptr = nullptr;
    long code = strtol(code_text.c_str(), &endptr, 10);
    if (endptr == code_text.c_str() || *endptr != '\0') return -1;
    return (int)code;
}

// ---------------------- HTTP request to report server ----------------------

bool http_request_to_report_server(
    const std::string& method,
    const std::string& request_path,
    const std::string& content_type,
    const std::string& body,
    int timeout_ms,
    std::string* response_head,
    std::string* response_body,
    int* status_code_out) {

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    if (timeout_ms < 100) timeout_ms = 100;
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)g_report_server_port);

    if (inet_pton(AF_INET, g_report_server_host.c_str(), &serv_addr.sin_addr) <= 0) {
        struct hostent* server = gethostbyname(g_report_server_host.c_str());
        if (server == nullptr || server->h_length <= 0) {
            close(sock);
            return false;
        }
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return false;
    }

    std::string path = request_path;
    if (path.empty()) path = "/";
    if (path[0] != '/') {
        path = "/" + path;
    }

    std::ostringstream oss;
    oss << method << " " << path << " HTTP/1.1\r\n";
    oss << "Host: " << g_report_server_host << ":" << g_report_server_port << "\r\n";
    if (!content_type.empty()) {
        oss << "Content-Type: " << content_type << "\r\n";
    }
    if (!body.empty()) {
        oss << "Content-Length: " << body.size() << "\r\n";
    }
    oss << "Connection: close\r\n\r\n";
    if (!body.empty()) {
        oss << body;
    }
    const std::string req = oss.str();

    bool sent_ok = send_all_bytes(sock, req.data(), req.size());
    if (!sent_ok) {
        close(sock);
        return false;
    }

    std::string response;
    char resp_buf[2048];
    while (true) {
        int n = recv(sock, resp_buf, sizeof(resp_buf), 0);
        if (n <= 0) break;
        response.append(resp_buf, (size_t)n);
    }
    close(sock);

    if (response.empty()) return false;
    if (response_head) {
        size_t head_end = response.find("\r\n\r\n");
        *response_head = response.substr(0, head_end == std::string::npos ? std::min((size_t)256, response.size()) : head_end);
    }
    if (response_body) {
        size_t body_pos = response.find("\r\n\r\n");
        *response_body = (body_pos == std::string::npos) ? "" : response.substr(body_pos + 4);
    }

    int status_code = parse_http_status_code(response);
    if (status_code_out) {
        *status_code_out = status_code;
    }
    return status_code >= 200 && status_code < 300;
}

// ---------------------- POST JSON helper ----------------------

bool post_json_to_report_server(const std::string& body, std::string* response_head) {
    int status_code = -1;
    return http_request_to_report_server(
        "POST",
        g_report_server_path,
        "application/json",
        body,
        3000,
        response_head,
        nullptr,
        &status_code);
}
