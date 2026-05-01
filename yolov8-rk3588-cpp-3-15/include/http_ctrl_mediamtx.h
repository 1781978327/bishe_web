#ifndef HTTP_CTRL_MEDIAMTX_H
#define HTTP_CTRL_MEDIAMTX_H

// Mediamtx process management functions extracted from main_http_ctrl.cc.

#include <string>

std::string resolve_mediamtx_binary_path();

bool start_mediamtx_if_needed(const char* reason, std::string* detail = nullptr);

#endif // HTTP_CTRL_MEDIAMTX_H
