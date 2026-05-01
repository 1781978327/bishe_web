#ifndef HTTP_CTRL_ROUTES_H
#define HTTP_CTRL_ROUTES_H

// HTTP route handlers and dispatcher for the vision (http_ctrl) service.
// Extracted from the monolithic main_http_ctrl.cc for the vision service refactoring.

#include <string>

// ---- Individual route handlers ----

void handle_route_status(int client_fd);
void handle_route_threshold_set(int client_fd, const std::string& path);
void handle_route_threshold_get(int client_fd);
void handle_route_detection_count(int client_fd, const std::string& path);
void handle_route_frame(int client_fd, const std::string& path);
void handle_route_tracker(int client_fd, const std::string& route, const std::string& method);
void handle_route_inference_on(int client_fd, const std::string& path);
void handle_route_inference_off(int client_fd, const std::string& path);
void handle_route_camera_switch(int client_fd, int camera_id);

#ifdef USE_RTSP_MPP
void handle_route_rtsp_start(int client_fd);
void handle_route_rtsp_video_start(int client_fd);
void handle_route_rtsp_stop(int client_fd);
#endif

void handle_route_record_status(int client_fd);
void handle_route_record_start(int client_fd, const std::string& path);
void handle_route_record_stop(int client_fd, const std::string& path);
void handle_route_video_status(int client_fd);
void handle_route_video_start(int client_fd, const std::string& post_body);
void handle_route_video_stop(int client_fd);
void handle_route_index(int client_fd);

// ---- Dispatcher and server ----

void handle_client(int client_fd);
void http_server_thread();
void signal_handler(int sig);

#endif // HTTP_CTRL_ROUTES_H
