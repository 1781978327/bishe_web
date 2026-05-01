#ifndef HTTP_CTRL_HTTP_CLIENT_H
#define HTTP_CTRL_HTTP_CLIENT_H

// HTTP client functions for reporting to the backend server.
// Extracted from main_http_ctrl.cc.

#include <string>

int parse_http_status_code(const std::string& response);

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

#endif // HTTP_CTRL_HTTP_CLIENT_H
