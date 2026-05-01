#pragma once

#include <string>
#include <sys/types.h>

// Path utilities
std::string dirname_from_path(const std::string &path);
int path_has_whitespace(const char *path);

// Emergency KWS path derivation
void derive_emergency_kws_paths_from_cmd(int lock_workdir, int lock_log);
void resolve_emergency_kws_default_cmd(int lock_cmd, int lock_workdir, int lock_log);

// Emergency KWS config init
void init_emergency_kws_config();

// Emergency KWS process management (locked variants — caller must hold g_emergency_kws_mutex)
int emergency_kws_is_process_alive(pid_t pid);
int emergency_kws_write_pid_file_locked(pid_t pid);
int emergency_kws_read_pid_file_locked(pid_t *pid_out);
void emergency_kws_remove_pid_file_locked();
int emergency_kws_try_reap_locked(pid_t pid, int *status_out);
void emergency_kws_refresh_state_locked();
int emergency_kws_start_locked();
int emergency_kws_stop_locked();

// Emergency KWS public API (thread-safe, acquires mutex internally)
int emergency_kws_start();
int emergency_kws_stop();
void emergency_kws_refresh_state();

// Emergency KWS report watcher
void *emergency_kws_report_thread_main(void *arg);
int emergency_kws_reporter_start();
int emergency_kws_reporter_stop();

// Log parsing
void parse_emergency_log_line(const std::string &line,
                               std::string *timestamp,
                               std::string *keyword);
