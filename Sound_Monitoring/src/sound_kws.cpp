#include "sound_kws.h"
#include "sound_globals.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string>
#include <fstream>

#include "sound_report.h"
#include "sound_event_audio.h"

// Forward-declare from sound_audio_config.h
int path_exists(const char *path);
int parse_bool_text_local(const char *value, int default_value);
void copy_string_value(char *dest, size_t dest_size, const std::string &value);
std::string trim_copy(const std::string &value);

// ========== Path utilities ==========
std::string dirname_from_path(const std::string &path) {
    if (path.empty()) return ".";
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

int path_has_whitespace(const char *path) {
    if (!path) return 1;
    for (const char *p = path; *p; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') return 1;
    }
    return 0;
}

void derive_emergency_kws_paths_from_cmd(int lock_workdir, int lock_log) {
    if (g_emergency_kws_cmd[0] == '\0') return;
    if (path_has_whitespace(g_emergency_kws_cmd)) return;

    std::string cmd_path(g_emergency_kws_cmd);
    std::string workdir = dirname_from_path(cmd_path);
    if (!lock_workdir && !workdir.empty()) {
        copy_string_value(g_emergency_kws_workdir, sizeof(g_emergency_kws_workdir), workdir);
    }

    if (!lock_log && !workdir.empty()) {
        std::string log_path = workdir;
        if (log_path.back() != '/') log_path.push_back('/');
        log_path += "emergency_log.txt";
        copy_string_value(g_emergency_kws_log_path, sizeof(g_emergency_kws_log_path), log_path);
    }
}

void resolve_emergency_kws_default_cmd(int lock_cmd, int lock_workdir, int lock_log) {
    const char *candidates[] = {
        "./wake/emergency_monitor",
        "./build/wake/emergency_monitor",
        "../build/wake/emergency_monitor",
        "../../build/wake/emergency_monitor",
        "../wake/emergency_monitor",
        "../../wake/emergency_monitor",
        "/home/orangepi/Desktop/web/语音唤醒/emergency_monitor",
        NULL
    };

    if (!lock_cmd && !path_exists(g_emergency_kws_cmd)) {
        for (int i = 0; candidates[i] != NULL; ++i) {
            if (path_exists(candidates[i])) {
                copy_string_value(g_emergency_kws_cmd, sizeof(g_emergency_kws_cmd), candidates[i]);
                break;
            }
        }
    }

    derive_emergency_kws_paths_from_cmd(lock_workdir, lock_log);
}

void init_emergency_kws_config() {
    const char *env_auto = getenv("EMERGENCY_KWS_AUTO_START");
    const char *env_report = getenv("EMERGENCY_KWS_REPORT_ENABLED");
    const char *env_cmd = getenv("EMERGENCY_KWS_CMD");
    const char *env_workdir = getenv("EMERGENCY_KWS_WORKDIR");
    const char *env_log = getenv("EMERGENCY_KWS_LOG_PATH");
    const char *env_pid = getenv("EMERGENCY_KWS_PID_FILE");
    const char *env_stdout = getenv("EMERGENCY_KWS_STDOUT_PATH");
    int lock_cmd = (env_cmd && env_cmd[0]) ? 1 : 0;
    int lock_workdir = (env_workdir && env_workdir[0]) ? 1 : 0;
    int lock_log = (env_log && env_log[0]) ? 1 : 0;

    if (env_auto && env_auto[0]) {
        g_emergency_kws_auto_start = parse_bool_text_local(env_auto, g_emergency_kws_auto_start);
    }
    if (env_report && env_report[0]) {
        g_emergency_kws_report_enabled = parse_bool_text_local(env_report, g_emergency_kws_report_enabled);
    }
    if (env_cmd && env_cmd[0]) {
        strncpy(g_emergency_kws_cmd, env_cmd, sizeof(g_emergency_kws_cmd) - 1);
    }
    if (env_workdir && env_workdir[0]) {
        strncpy(g_emergency_kws_workdir, env_workdir, sizeof(g_emergency_kws_workdir) - 1);
    }
    if (env_log && env_log[0]) {
        strncpy(g_emergency_kws_log_path, env_log, sizeof(g_emergency_kws_log_path) - 1);
    }
    if (env_pid && env_pid[0]) {
        strncpy(g_emergency_kws_pid_file, env_pid, sizeof(g_emergency_kws_pid_file) - 1);
    }
    if (env_stdout && env_stdout[0]) {
        strncpy(g_emergency_kws_stdout_path, env_stdout, sizeof(g_emergency_kws_stdout_path) - 1);
    }

    resolve_emergency_kws_default_cmd(lock_cmd, lock_workdir, lock_log);

    emergency_kws_refresh_state();
    printf("[KWS] config: auto_start=%s, report=%s, cmd=%s, workdir=%s\n",
           g_emergency_kws_auto_start ? "on" : "off",
           g_emergency_kws_report_enabled ? "on" : "off",
           g_emergency_kws_cmd,
           g_emergency_kws_workdir);
    if (!path_exists(g_emergency_kws_cmd)) {
        printf("[KWS] warning: command not found: %s\n", g_emergency_kws_cmd);
    }
    if (g_emergency_kws_running) {
        (void)emergency_kws_reporter_start();
    }
}

// ========== Emergency KWS process management ==========
int emergency_kws_is_process_alive(pid_t pid) {
    if (pid <= 0) return 0;
    if (kill(pid, 0) == 0) return 1;
    return errno == EPERM;
}

int emergency_kws_write_pid_file_locked(pid_t pid) {
    if (g_emergency_kws_pid_file[0] == '\0' || pid <= 0) return -1;
    FILE *f = fopen(g_emergency_kws_pid_file, "w");
    if (!f) {
        printf("[KWS] Failed to write pid file: %s\n", g_emergency_kws_pid_file);
        return -1;
    }
    fprintf(f, "%d\n", (int)pid);
    fclose(f);
    return 0;
}

int emergency_kws_read_pid_file_locked(pid_t *pid_out) {
    if (!pid_out || g_emergency_kws_pid_file[0] == '\0') return -1;
    FILE *f = fopen(g_emergency_kws_pid_file, "r");
    if (!f) return -1;

    long v = -1;
    int ok = fscanf(f, "%ld", &v);
    fclose(f);
    if (ok != 1 || v <= 0) return -1;
    *pid_out = (pid_t)v;
    return 0;
}

void emergency_kws_remove_pid_file_locked() {
    if (g_emergency_kws_pid_file[0]) {
        (void)remove(g_emergency_kws_pid_file);
    }
}

int emergency_kws_try_reap_locked(pid_t pid, int *status_out) {
    if (pid <= 0) return 0;
    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) {
        if (status_out) *status_out = status;
        return 1;
    }
    return 0;
}

void emergency_kws_refresh_state_locked() {
    if (g_emergency_kws_pid > 0) {
        int status = 0;
        if (emergency_kws_try_reap_locked(g_emergency_kws_pid, &status)) {
            g_emergency_kws_running = 0;
            g_emergency_kws_pid = -1;
            emergency_kws_remove_pid_file_locked();
        }
    }

    if (emergency_kws_is_process_alive(g_emergency_kws_pid)) {
        g_emergency_kws_running = 1;
        return;
    }

    pid_t pid_from_file = -1;
    if (emergency_kws_read_pid_file_locked(&pid_from_file) == 0) {
        int status = 0;
        if (emergency_kws_try_reap_locked(pid_from_file, &status)) {
            g_emergency_kws_running = 0;
            g_emergency_kws_pid = -1;
            emergency_kws_remove_pid_file_locked();
            return;
        }

        if (emergency_kws_is_process_alive(pid_from_file)) {
            g_emergency_kws_pid = pid_from_file;
            g_emergency_kws_running = 1;
            return;
        }
    }

    g_emergency_kws_running = 0;
    g_emergency_kws_pid = -1;
    emergency_kws_remove_pid_file_locked();
}

int emergency_kws_start_locked() {
    emergency_kws_refresh_state_locked();
    if (g_emergency_kws_running) return 1;

    if (!path_exists(g_emergency_kws_cmd)) {
        printf("[KWS] Command not found: %s\n", g_emergency_kws_cmd);
        return -2;
    }

    char cmd_for_exec[1024] = {0};
    if (g_emergency_kws_cmd[0] == '/') {
        copy_string_value(cmd_for_exec, sizeof(cmd_for_exec), g_emergency_kws_cmd);
    } else {
        char *resolved = realpath(g_emergency_kws_cmd, NULL);
        if (resolved != NULL) {
            copy_string_value(cmd_for_exec, sizeof(cmd_for_exec), resolved);
            free(resolved);
        } else {
            copy_string_value(cmd_for_exec, sizeof(cmd_for_exec), g_emergency_kws_cmd);
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        printf("[KWS] fork failed: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // Child process
        (void)setsid();
        if (g_emergency_kws_workdir[0]) {
            (void)chdir(g_emergency_kws_workdir);
        }

        int fd = -1;
        if (g_emergency_kws_stdout_path[0]) {
            fd = open(g_emergency_kws_stdout_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        }
        if (fd >= 0) {
            (void)dup2(fd, STDOUT_FILENO);
            (void)dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) close(fd);
        }

        execl(cmd_for_exec, cmd_for_exec, (char *)NULL);
        // fallback: run through shell
        execl("/bin/sh", "sh", "-c", g_emergency_kws_cmd, (char *)NULL);
        fprintf(stderr, "[KWS] exec failed: cmd=%s err=%s\n",
                cmd_for_exec, strerror(errno));
        _exit(127);
    }

    g_emergency_kws_pid = pid;
    g_emergency_kws_running = 1;
    (void)emergency_kws_write_pid_file_locked(pid);
    usleep(200 * 1000);

    int early_status = 0;
    if (emergency_kws_try_reap_locked(pid, &early_status)) {
        g_emergency_kws_running = 0;
        g_emergency_kws_pid = -1;
        emergency_kws_remove_pid_file_locked();
        printf("[KWS] Monitor exited immediately after start (pid=%d)\n", (int)pid);
        return -3;
    }

    if (!emergency_kws_is_process_alive(pid)) {
        g_emergency_kws_running = 0;
        g_emergency_kws_pid = -1;
        emergency_kws_remove_pid_file_locked();
        printf("[KWS] Monitor is not alive after start check (pid=%d)\n", (int)pid);
        return -3;
    }

    printf("[KWS] Emergency keyword monitor started (pid=%d)\n", (int)pid);
    return 0;
}

int emergency_kws_stop_locked() {
    emergency_kws_refresh_state_locked();
    if (!g_emergency_kws_running || g_emergency_kws_pid <= 0) return 1;

    pid_t pid = g_emergency_kws_pid;
    (void)kill(pid, SIGTERM);

    for (int i = 0; i < 20; ++i) {
        int status = 0;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0 && errno == ECHILD) break;
        if (!emergency_kws_is_process_alive(pid)) break;
        usleep(100 * 1000);
    }

    if (emergency_kws_is_process_alive(pid)) {
        (void)kill(pid, SIGKILL);
        for (int i = 0; i < 10; ++i) {
            int status = 0;
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) break;
            if (w < 0 && errno == ECHILD) break;
            if (!emergency_kws_is_process_alive(pid)) break;
            usleep(100 * 1000);
        }
    }

    {
        int status = 0;
        (void)waitpid(pid, &status, WNOHANG);
    }

    g_emergency_kws_running = emergency_kws_is_process_alive(pid) ? 1 : 0;
    if (!g_emergency_kws_running) {
        g_emergency_kws_pid = -1;
        emergency_kws_remove_pid_file_locked();
        printf("[KWS] Emergency keyword monitor stopped\n");
        return 0;
    }

    printf("[KWS] Failed to stop process pid=%d\n", (int)pid);
    return -1;
}

int emergency_kws_start() {
    pthread_mutex_lock(&g_emergency_kws_mutex);
    int ret = emergency_kws_start_locked();
    pthread_mutex_unlock(&g_emergency_kws_mutex);
    if (ret == 0 || ret == 1) {
        (void)emergency_kws_reporter_start();
    }
    return ret;
}

int emergency_kws_stop() {
    (void)emergency_kws_reporter_stop();
    pthread_mutex_lock(&g_emergency_kws_mutex);
    int ret = emergency_kws_stop_locked();
    pthread_mutex_unlock(&g_emergency_kws_mutex);
    return ret;
}

void emergency_kws_refresh_state() {
    pthread_mutex_lock(&g_emergency_kws_mutex);
    emergency_kws_refresh_state_locked();
    pthread_mutex_unlock(&g_emergency_kws_mutex);
}

void *emergency_kws_report_thread_main(void *arg) {
    (void)arg;
    printf("[KWS REPORT] Emergency keyword report watcher started\n");

    while (g_emergency_kws_report_running) {
        char log_path[512] = {0};

        pthread_mutex_lock(&g_emergency_kws_mutex);
        strncpy(log_path, g_emergency_kws_log_path, sizeof(log_path) - 1);
        pthread_mutex_unlock(&g_emergency_kws_mutex);

        if (log_path[0] != '\0') {
            struct stat st;
            if (stat(log_path, &st) == 0 && S_ISREG(st.st_mode)) {
                if (g_emergency_kws_report_offset > st.st_size) {
                    g_emergency_kws_report_offset = 0;
                }

                FILE *f = fopen(log_path, "r");
                if (f) {
                    if (g_emergency_kws_report_offset > 0) {
                        (void)fseek(f, g_emergency_kws_report_offset, SEEK_SET);
                    }

                    char line_buf[1024];
                    while (g_emergency_kws_report_running &&
                           fgets(line_buf, sizeof(line_buf), f) != NULL) {
                        long pos = ftell(f);
                        if (pos >= 0) {
                            g_emergency_kws_report_offset = pos;
                        }

                        std::string line = trim_copy(line_buf);
                        if (line.empty()) {
                            continue;
                        }

                        std::string timestamp;
                        std::string keyword;
                        parse_emergency_log_line(line, &timestamp, &keyword);
                        if (keyword.empty()) {
                            continue;
                        }

                        printf("[KWS REPORT] Keyword detected, reporting to Spring Boot: %s\n",
                               keyword.c_str());

                        char audio_path[512] = {0};
                        float audio_duration = 0.0f;
                        if (save_recent_audio_for_emergency_kws(audio_path,
                                                                sizeof(audio_path),
                                                                &audio_duration) != 0) {
                            printf("[KWS REPORT] Continue reporting keyword without audio clip\n");
                        }

                        (void)report_to_spring_boot_with_result(
                            audio_path,
                            audio_duration,
                            keyword.c_str(),
                            -1.0f,
                            "紧急关键词触发");
                    }
                    fclose(f);
                }
            }
        }

        for (int i = 0; i < 10 && g_emergency_kws_report_running; ++i) {
            usleep(100 * 1000);
        }
    }

    printf("[KWS REPORT] Emergency keyword report watcher stopped\n");
    return NULL;
}

int emergency_kws_reporter_start() {
    if (!g_emergency_kws_report_enabled) {
        return 0;
    }

    pthread_mutex_lock(&g_emergency_kws_report_mutex);
    if (g_emergency_kws_report_running) {
        pthread_mutex_unlock(&g_emergency_kws_report_mutex);
        return 0;
    }

    char log_path[512] = {0};
    pthread_mutex_lock(&g_emergency_kws_mutex);
    strncpy(log_path, g_emergency_kws_log_path, sizeof(log_path) - 1);
    pthread_mutex_unlock(&g_emergency_kws_mutex);

    struct stat st;
    if (log_path[0] != '\0' && stat(log_path, &st) == 0 && S_ISREG(st.st_mode)) {
        g_emergency_kws_report_offset = st.st_size;
    } else {
        g_emergency_kws_report_offset = 0;
    }

    g_emergency_kws_report_running = 1;
    int ret = pthread_create(&g_emergency_kws_report_thread, NULL,
                             emergency_kws_report_thread_main, NULL);
    if (ret != 0) {
        g_emergency_kws_report_running = 0;
        printf("[KWS REPORT] Failed to start report watcher: %s\n", strerror(ret));
        pthread_mutex_unlock(&g_emergency_kws_report_mutex);
        return -1;
    }

    pthread_mutex_unlock(&g_emergency_kws_report_mutex);
    return 0;
}

int emergency_kws_reporter_stop() {
    pthread_t thread;
    int should_join = 0;

    pthread_mutex_lock(&g_emergency_kws_report_mutex);
    if (g_emergency_kws_report_running) {
        g_emergency_kws_report_running = 0;
        thread = g_emergency_kws_report_thread;
        should_join = 1;
    }
    pthread_mutex_unlock(&g_emergency_kws_report_mutex);

    if (should_join) {
        pthread_join(thread, NULL);
    }
    return 0;
}

void parse_emergency_log_line(const std::string &line,
                               std::string *timestamp,
                               std::string *keyword) {
    if (timestamp) timestamp->clear();
    if (keyword) keyword->clear();
    if (line.empty()) return;

    size_t left = line.find('[');
    size_t right = line.find(']');
    if (timestamp && left != std::string::npos && right != std::string::npos && right > left + 1) {
        *timestamp = line.substr(left + 1, right - left - 1);
    }

    size_t pos = line.rfind("关键词:");
    if (pos != std::string::npos) {
        if (keyword) *keyword = trim_copy(line.substr(pos + strlen("关键词:")));
        return;
    }

    pos = line.rfind(':');
    if (pos != std::string::npos && pos + 1 < line.size() && keyword) {
        *keyword = trim_copy(line.substr(pos + 1));
    }
}
