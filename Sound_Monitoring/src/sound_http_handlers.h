#pragma once

void handle_health(int client_fd);
void handle_realtime_start(int client_fd, const char *body);
void handle_realtime_stop(int client_fd);
void handle_realtime_status(int client_fd);
void handle_realtime_events(int client_fd);
void handle_realtime_windows(int client_fd, const char *query);
void handle_realtime_transcript(int client_fd, const char *query);
void handle_wake_start(int client_fd);
void handle_wake_stop(int client_fd);
void handle_wake_status(int client_fd);
void handle_wake_events(int client_fd, const char *query);
void handle_config(int client_fd);
void handle_client(int client_fd);
void* client_handler(void* arg);
int start_server(int port);
