#include "sound_http_handlers.h"
#include "sound_globals.h"
#include "sound_http_utils.h"
#include "sound_offline.h"
#include "sound_rt_monitor.h"
#include "sound_kws.h"
#include "sound_denoise.h"
#include "sound_event_audio.h"
#include "sound_audio_config.h"
#include "audio_utils.h"
#include "process.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <sstream>
#include <string>

void handle_health(int client_fd) {
    const char *body = "{\"status\": \"ok\", \"service\": \"sound-server\", \"model\": \"yamnet\"}";
    send_response(client_fd, "200 OK", "application/json", body, strlen(body));
}

void handle_realtime_start(int client_fd, const char *body) {
    char device[256];
    strncpy(device, g_rt_default_device, sizeof(device) - 1);
    device[sizeof(device) - 1] = '\0';

    // 解析 device 参数（可选）
    const char *dev_ptr = strstr(body, "\"device\"");
    if (dev_ptr) {
        dev_ptr += 8;
        while (*dev_ptr == ' ' || *dev_ptr == ':') dev_ptr++;
        if (*dev_ptr == '"') dev_ptr++;
        int i = 0;
        while (*dev_ptr && *dev_ptr != '"' && *dev_ptr != '\n' && i < (int)sizeof(device) - 1) {
            device[i++] = *dev_ptr++;
        }
        device[i] = '\0';
    }

    printf("[HTTP] POST /realtime/start -> device=%s\n", device);

    int ret = rt_start(device);
    char result[512];
    if (ret == 0) {
        (void)emergency_kws_start();
        pthread_mutex_lock(&rt_mutex);
        char actual_device[256];
        strncpy(actual_device, rt_device, sizeof(actual_device) - 1);
        actual_device[sizeof(actual_device) - 1] = '\0';
        pthread_mutex_unlock(&rt_mutex);

        snprintf(result, sizeof(result),
            "{\"success\": true, \"message\": \"Realtime monitoring started\", \"device\": \"%s\"}", actual_device);
        send_response(client_fd, "200 OK", "application/json", result, strlen(result));
    } else if (ret == -1) {
        snprintf(result, sizeof(result),
            "{\"success\": false, \"error\": \"Already running\"}");
        send_response(client_fd, "409 Conflict", "application/json", result, strlen(result));
    } else {
        snprintf(result, sizeof(result),
            "{\"success\": false, \"error\": \"Failed to start. Check device and permissions.\"}");
        send_response(client_fd, "500 Internal Server Error", "application/json", result, strlen(result));
    }
}

void handle_realtime_stop(int client_fd) {
    printf("[HTTP] POST /realtime/stop\n");
    (void)emergency_kws_stop();
    rt_stop();
    char result[256];
    snprintf(result, sizeof(result), "{\"success\": true, \"message\": \"Realtime monitoring stopped\"}");
    send_response(client_fd, "200 OK", "application/json", result, strlen(result));
}

void handle_realtime_status(int client_fd) {
    pthread_mutex_lock(&rt_mutex);
    int running = rt_active;
    char device[256];
    strncpy(device, rt_device, sizeof(device) - 1);
    device[sizeof(device) - 1] = '\0';
    pthread_mutex_unlock(&rt_mutex);

    char escaped_ffmpeg_bin[512] = {0};
    char escaped_ffmpeg_filter[1024] = {0};
    json_escape_string(g_rt_ffmpeg_bin, escaped_ffmpeg_bin, sizeof(escaped_ffmpeg_bin));
    json_escape_string(g_rt_ffmpeg_audio_filter, escaped_ffmpeg_filter, sizeof(escaped_ffmpeg_filter));

    char result[2048];
    snprintf(result, sizeof(result),
        "{\"running\": %s, \"device\": \"%s\", \"sample_rate\": %d, "
        "\"capture_volume\": %.2f, \"mixer_enabled\": %s, \"mixer_card\": \"%s\", "
        "\"mixer_control\": \"%s\", \"mixer_volume\": \"%s\", "
        "\"ffmpeg_filter_enabled\": %s, \"ffmpeg_bin\": \"%s\", \"ffmpeg_audio_filter\": \"%s\"}",
        running ? "true" : "false", device, rt_sample_rate, g_rt_capture_volume,
        g_rt_amixer_enabled ? "true" : "false",
        g_rt_amixer_card,
        g_rt_amixer_control,
        g_rt_amixer_volume,
        ffmpeg_rt_filter_is_ready() ? "true" : "false",
        escaped_ffmpeg_bin,
        escaped_ffmpeg_filter);
    send_response(client_fd, "200 OK", "application/json", result, strlen(result));
}

void handle_realtime_events(int client_fd) {
    pthread_mutex_lock(&rt_mutex);
    int count = rt_event_count;
    int head = rt_event_head;
    int tail = rt_event_tail;
    RealtimeEvent queue[RT_EVENT_QUEUE_SIZE];
    memcpy(queue, rt_event_queue, sizeof(rt_event_queue));
    pthread_mutex_unlock(&rt_mutex);

    char body[8192];
    int written = snprintf(body, sizeof(body),
        "{\"success\": true, \"count\": %d, \"events\": [", count);

    for (int i = 0; i < count && written < (int)sizeof(body) - 200; i++) {
        int idx = (tail + i) % RT_EVENT_QUEUE_SIZE;
        RealtimeEvent *e = &queue[idx];
        written += snprintf(body + written, sizeof(body) - written,
            "%s{\"id\": %d, \"start\": %.2f, \"end\": %.2f, \"duration\": %.2f, "
            "\"confidence\": %.4f, \"timestamp\": \"%s\", \"keywords\": [",
            i > 0 ? "," : "", e->event_id, e->start_sec, e->end_sec,
            e->duration, e->max_score, e->timestamp);
        for (int j = 0; j < e->keyword_count && written < (int)sizeof(body) - 100; j++) {
            written += snprintf(body + written, sizeof(body) - written,
                "%s\"%s\"", j > 0 ? "," : "", e->keywords[j]);
        }
        written += snprintf(body + written, sizeof(body) - written, "]}");
    }
    written += snprintf(body + written, sizeof(body) - written, "]}");

    send_response(client_fd, "200 OK", "application/json", body, strlen(body));
}

void handle_realtime_windows(int client_fd, const char *query) {
    int total = 0;
    int head = 0;
    int limit = parse_int_from_query(query, "limit", 5, RT_WINDOW_QUEUE_SIZE);
    RealtimeWindowStatus queue[RT_WINDOW_QUEUE_SIZE];

    pthread_mutex_lock(&rt_mutex);
    total = rt_window_count;
    head = rt_window_head;
    memcpy(queue, rt_window_queue, sizeof(rt_window_queue));
    pthread_mutex_unlock(&rt_mutex);

    int returned = std::min(limit, total);
    std::ostringstream out;
    out << "{"
        << "\"success\": true,"
        << "\"count\": " << total << ","
        << "\"returned\": " << returned << ","
        << "\"limit\": " << limit << ","
        << "\"windows\": [";

    for (int i = 0; i < returned; ++i) {
        int idx = (head - 1 - i + RT_WINDOW_QUEUE_SIZE) % RT_WINDOW_QUEUE_SIZE;
        RealtimeWindowStatus *w = &queue[idx];

        char escaped_keyword[256] = {0};
        char escaped_summary[1024] = {0};
        json_escape_string(w->matched_keyword, escaped_keyword, sizeof(escaped_keyword));
        json_escape_string(w->top_summary, escaped_summary, sizeof(escaped_summary));

        if (i > 0) out << ",";
        out << "{"
            << "\"id\": " << w->window_id << ","
            << "\"start\": " << w->start_sec << ","
            << "\"end\": " << w->end_sec << ","
            << "\"duration\": " << (w->end_sec - w->start_sec) << ","
            << "\"anomaly\": " << (w->anomaly ? "true" : "false") << ","
            << "\"matched_keyword\": \"" << escaped_keyword << "\","
            << "\"matched_score\": " << w->matched_score << ","
            << "\"top_summary\": \"" << escaped_summary << "\","
            << "\"timestamp\": \"" << w->timestamp << "\""
            << "}";
    }

    out << "]}";
    std::string body = out.str();
    send_response(client_fd, "200 OK", "application/json", body.c_str(), (int)body.size());
}

void handle_realtime_transcript(int client_fd, const char *query) {
    int req_seconds = parse_seconds_from_query(query, RT_TRANSCRIPT_DEFAULT_SEC, RT_TRANSCRIPT_MAX_SEC);
    int should_save_audio = parse_flag_from_query(query, "save_audio");

    if (!g_asr_enabled) {
        const char *body =
            "{\"success\": false, \"audio_saved\": false, \"audio_path\": \"\", "
            "\"error\": \"ASR disabled\", "
            "\"hint\": \"Set ENABLE_VOSK_ASR=1 and VOSK_MODEL_CN / VOSK_MODEL_EN to enable\"}";
        send_response(client_fd, "200 OK", "application/json", body, strlen(body));
        return;
    }

    int running = 0;
    int sample_rate = 0;
    int available_frames = 0;
    std::vector<float> frames;

    pthread_mutex_lock(&rt_mutex);
    running = rt_active ? 1 : 0;
    sample_rate = rt_sample_rate;
    available_frames = rt_ring.size;
    int need_frames = req_seconds * sample_rate;
    int copy_frames = std::min(need_frames, available_frames);
    if (copy_frames > 0) {
        frames.resize(copy_frames);
        rt_ring_get_latest(&rt_ring, frames.data(), copy_frames);
    }
    pthread_mutex_unlock(&rt_mutex);

    if (frames.empty()) {
        char body[512];
        snprintf(body, sizeof(body),
                 "{\"success\": true, \"running\": %s, \"seconds_requested\": %d, "
                 "\"seconds_used\": 0.0, \"frames\": 0, \"full_text\": \"\", \"transcript\": \"\", "
                 "\"asr_lang\": \"\", \"asr_confidence\": 0.0, \"segments\": [], "
                 "\"audio_saved\": false, \"audio_path\": \"\", "
                 "\"error\": \"No microphone audio in buffer\"}",
                 running ? "true" : "false", req_seconds);
        send_response(client_fd, "200 OK", "application/json", body, strlen(body));
        return;
    }

    const int total_frames = (int)frames.size();
    bool audio_saved = false;
    char saved_audio_path[512] = {0};
    if (should_save_audio) {
        const char *dump_dir = "./debug_audio";
        struct stat st;
        if (stat(dump_dir, &st) != 0) {
            if (mkdir(dump_dir, 0755) != 0 && errno != EEXIST) {
                printf("[RT ASR] Failed to create dump dir: %s\n", dump_dir);
            }
        }

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", tm_info);
        int dump_id = __sync_fetch_and_add(&g_rt_transcript_dump_idx, 1);
        snprintf(saved_audio_path, sizeof(saved_audio_path),
                 "%s/rt_transcript_%s_%d_%ds.wav", dump_dir, ts, dump_id, req_seconds);

        int save_ret = save_audio(saved_audio_path, frames.data(), total_frames, sample_rate, 1);
        if (save_ret == 0) {
            audio_saved = true;
            printf("[RT ASR] Saved transcript audio: %s\n", saved_audio_path);
        } else {
            printf("[RT ASR] Failed to save transcript audio: %s\n", saved_audio_path);
            saved_audio_path[0] = '\0';
        }
    }

    const int chunk_frames = std::max(1, (int)(RT_CHUNK_DURATION_SEC * sample_rate));
    const float used_seconds = (float)total_frames / (float)sample_rate;

    bool any_ok = false;
    std::string full_text;
    std::string last_lang;
    double max_conf = 0.0;
    std::string merged_error;
    std::ostringstream segs;
    segs << "[";

    int seg_idx = 0;
    for (int start = 0; start < total_frames; start += chunk_frames, ++seg_idx) {
        int seg_len = std::min(chunk_frames, total_frames - start);
        AsrResult asr = run_asr_locked(frames.data() + start, seg_len);

        char escaped_seg_text[2048] = {0};
        char escaped_seg_error[512] = {0};
        if (!asr.text.empty()) {
            json_escape_string(asr.text.c_str(), escaped_seg_text, sizeof(escaped_seg_text));
        }
        if (!asr.error.empty()) {
            json_escape_string(asr.error.c_str(), escaped_seg_error, sizeof(escaped_seg_error));
        }

        if (seg_idx > 0) segs << ",";
        float seg_start_sec = (float)start / (float)sample_rate;
        float seg_end_sec = (float)(start + seg_len) / (float)sample_rate;

        segs << "{"
             << "\"id\":" << seg_idx << ","
             << "\"start\":" << seg_start_sec << ","
             << "\"end\":" << seg_end_sec << ","
             << "\"lang\":\"" << (asr.lang.empty() ? "" : asr.lang) << "\","
             << "\"confidence\":" << (asr.ok ? asr.confidence : 0.0) << ","
             << "\"text\":\"" << escaped_seg_text << "\","
             << "\"error\":\"" << escaped_seg_error << "\""
             << "}";

        if (asr.ok) {
            any_ok = true;
            if (!asr.text.empty()) {
                if (!full_text.empty()) full_text += " ";
                full_text += asr.text;
            }
            if (!asr.lang.empty()) {
                last_lang = asr.lang;
            }
            if (asr.confidence > max_conf) {
                max_conf = asr.confidence;
            }
        } else if (!asr.error.empty()) {
            if (!merged_error.empty()) merged_error += "; ";
            merged_error += asr.error;
        }
    }
    segs << "]";

    char escaped_full_text[8192] = {0};
    char escaped_error[1024] = {0};
    if (!full_text.empty()) {
        json_escape_string(full_text.c_str(), escaped_full_text, sizeof(escaped_full_text));
    }
    if (!merged_error.empty()) {
        json_escape_string(merged_error.c_str(), escaped_error, sizeof(escaped_error));
    }

    std::ostringstream out;
    out << "{"
        << "\"success\":" << (any_ok ? "true" : "false") << ","
        << "\"running\":" << (running ? "true" : "false") << ","
        << "\"seconds_requested\":" << req_seconds << ","
        << "\"seconds_used\":" << used_seconds << ","
        << "\"frames\":" << total_frames << ","
        << "\"asr_lang\":\"" << last_lang << "\","
        << "\"asr_confidence\":" << max_conf << ","
        << "\"full_text\":\"" << escaped_full_text << "\","
        << "\"transcript\":\"" << escaped_full_text << "\","
        << "\"segments\":" << segs.str() << ","
        << "\"audio_saved\":" << (audio_saved ? "true" : "false") << ","
        << "\"audio_path\":\"" << saved_audio_path << "\","
        << "\"error\":\"" << escaped_error << "\""
        << "}";

    std::string body = out.str();
    send_response(client_fd, "200 OK", "application/json", body.c_str(), (int)body.size());
}

void handle_wake_start(int client_fd) {
    int ret = emergency_kws_start();
    if (ret == 0) {
        const char *body = "{\"success\": true, \"message\": \"Emergency keyword monitor started\"}";
        send_response(client_fd, "200 OK", "application/json", body, strlen(body));
        return;
    }
    if (ret == 1) {
        const char *body = "{\"success\": true, \"message\": \"Emergency keyword monitor already running\"}";
        send_response(client_fd, "200 OK", "application/json", body, strlen(body));
        return;
    }
    const char *body = "{\"success\": false, \"message\": \"Failed to start emergency keyword monitor\"}";
    send_response(client_fd, "500 Internal Server Error", "application/json", body, strlen(body));
}

void handle_wake_stop(int client_fd) {
    int ret = emergency_kws_stop();
    if (ret == 0) {
        const char *body = "{\"success\": true, \"message\": \"Emergency keyword monitor stopped\"}";
        send_response(client_fd, "200 OK", "application/json", body, strlen(body));
        return;
    }
    if (ret == 1) {
        const char *body = "{\"success\": true, \"message\": \"Emergency keyword monitor is not running\"}";
        send_response(client_fd, "200 OK", "application/json", body, strlen(body));
        return;
    }
    const char *body = "{\"success\": false, \"message\": \"Failed to stop emergency keyword monitor\"}";
    send_response(client_fd, "500 Internal Server Error", "application/json", body, strlen(body));
}

void handle_wake_status(int client_fd) {
    int running = 0;
    int auto_start = 0;
    int pid_value = -1;
    char cmd[512] = {0};
    char workdir[512] = {0};
    char log_path[512] = {0};
    char pid_file[512] = {0};

    pthread_mutex_lock(&g_emergency_kws_mutex);
    emergency_kws_refresh_state_locked();
    running = g_emergency_kws_running;
    auto_start = g_emergency_kws_auto_start;
    pid_value = (int)g_emergency_kws_pid;
    strncpy(cmd, g_emergency_kws_cmd, sizeof(cmd) - 1);
    strncpy(workdir, g_emergency_kws_workdir, sizeof(workdir) - 1);
    strncpy(log_path, g_emergency_kws_log_path, sizeof(log_path) - 1);
    strncpy(pid_file, g_emergency_kws_pid_file, sizeof(pid_file) - 1);
    pthread_mutex_unlock(&g_emergency_kws_mutex);

    char escaped_cmd[1024] = {0};
    char escaped_workdir[1024] = {0};
    char escaped_log_path[1024] = {0};
    char escaped_pid_file[1024] = {0};
    json_escape_string(cmd, escaped_cmd, sizeof(escaped_cmd));
    json_escape_string(workdir, escaped_workdir, sizeof(escaped_workdir));
    json_escape_string(log_path, escaped_log_path, sizeof(escaped_log_path));
    json_escape_string(pid_file, escaped_pid_file, sizeof(escaped_pid_file));

    char body[4096];
    snprintf(body, sizeof(body),
             "{\"success\": true, \"running\": %s, \"pid\": %d, \"auto_start\": %s, "
             "\"report_enabled\": %s, \"report_watcher_running\": %s, "
             "\"command\": \"%s\", \"workdir\": \"%s\", \"log_path\": \"%s\", \"pid_file\": \"%s\"}",
             running ? "true" : "false",
             pid_value,
             auto_start ? "true" : "false",
             g_emergency_kws_report_enabled ? "true" : "false",
             g_emergency_kws_report_running ? "true" : "false",
             escaped_cmd,
             escaped_workdir,
             escaped_log_path,
             escaped_pid_file);
    send_response(client_fd, "200 OK", "application/json", body, strlen(body));
}

void handle_wake_events(int client_fd, const char *query) {
    int limit = parse_int_from_query(query, "limit", 20, 200);
    char log_path[512] = {0};

    pthread_mutex_lock(&g_emergency_kws_mutex);
    strncpy(log_path, g_emergency_kws_log_path, sizeof(log_path) - 1);
    pthread_mutex_unlock(&g_emergency_kws_mutex);

    std::ifstream in(log_path);
    if (!in.good()) {
        char escaped_log_path[1024] = {0};
        json_escape_string(log_path, escaped_log_path, sizeof(escaped_log_path));
        char body[2048];
        snprintf(body, sizeof(body),
                 "{\"success\": false, \"count\": 0, \"events\": [], \"log_path\": \"%s\", "
                 "\"error\": \"log file not found\"}",
                 escaped_log_path);
        send_response(client_fd, "200 OK", "application/json", body, strlen(body));
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = trim_copy(line);
        if (!trimmed.empty()) lines.push_back(trimmed);
    }

    int start = 0;
    if ((int)lines.size() > limit) {
        start = (int)lines.size() - limit;
    }

    std::ostringstream out;
    out << "{";
    out << "\"success\": true,";
    out << "\"count\": " << ((int)lines.size() - start) << ",";
    out << "\"events\": [";

    int out_idx = 0;
    for (int i = start; i < (int)lines.size(); ++i) {
        std::string ts;
        std::string kw;
        parse_emergency_log_line(lines[i], &ts, &kw);

        char escaped_ts[256] = {0};
        char escaped_kw[512] = {0};
        char escaped_raw[2048] = {0};
        json_escape_string(ts.c_str(), escaped_ts, sizeof(escaped_ts));
        json_escape_string(kw.c_str(), escaped_kw, sizeof(escaped_kw));
        json_escape_string(lines[i].c_str(), escaped_raw, sizeof(escaped_raw));

        if (out_idx++ > 0) out << ",";
        out << "{"
            << "\"id\":" << out_idx << ","
            << "\"timestamp\":\"" << escaped_ts << "\","
            << "\"keyword\":\"" << escaped_kw << "\","
            << "\"raw\":\"" << escaped_raw << "\""
            << "}";
    }

    char escaped_log_path[1024] = {0};
    json_escape_string(log_path, escaped_log_path, sizeof(escaped_log_path));
    out << "],";
    out << "\"log_path\":\"" << escaped_log_path << "\"";
    out << "}";

    std::string body = out.str();
    send_response(client_fd, "200 OK", "application/json", body.c_str(), (int)body.size());
}

void handle_config(int client_fd) {
    char escaped_ffmpeg_bin[512] = {0};
    char escaped_ffmpeg_filter[1024] = {0};
    char escaped_config_path[1024] = {0};
    char escaped_default_device[512] = {0};
    char escaped_kws_cmd[1024] = {0};
    char escaped_kws_workdir[1024] = {0};
    char escaped_kws_log_path[1024] = {0};
    json_escape_string(g_rt_ffmpeg_bin, escaped_ffmpeg_bin, sizeof(escaped_ffmpeg_bin));
    json_escape_string(g_rt_ffmpeg_audio_filter, escaped_ffmpeg_filter, sizeof(escaped_ffmpeg_filter));
    json_escape_string(g_runtime_audio_config_path, escaped_config_path, sizeof(escaped_config_path));
    json_escape_string(g_rt_default_device, escaped_default_device, sizeof(escaped_default_device));
    json_escape_string(g_emergency_kws_cmd, escaped_kws_cmd, sizeof(escaped_kws_cmd));
    json_escape_string(g_emergency_kws_workdir, escaped_kws_workdir, sizeof(escaped_kws_workdir));
    json_escape_string(g_emergency_kws_log_path, escaped_kws_log_path, sizeof(escaped_kws_log_path));

    int kws_running = 0;
    int kws_pid = -1;
    pthread_mutex_lock(&g_emergency_kws_mutex);
    emergency_kws_refresh_state_locked();
    kws_running = g_emergency_kws_running;
    kws_pid = (int)g_emergency_kws_pid;
    pthread_mutex_unlock(&g_emergency_kws_mutex);

    char body[8192];
    snprintf(body, sizeof(body),
        "{\"model_path\": \"%s\", \"anomaly_threshold\": %.2f, \"save_anomaly\": %d, "
        "\"keywords_count\": %d, \"asr_enabled\": %s, \"asr_model_cn\": \"%s\", \"asr_model_en\": \"%s\", "
        "\"runtime_audio_config_path\": \"%s\", \"rt_default_device\": \"%s\", "
        "\"sox_denoise_enabled\": %s, \"sox_bin\": \"%s\", \"sox_denoise_profile\": \"%s\", "
        "\"sox_denoise_amount\": %.2f, "
        "\"rt_ffmpeg_filter_enabled\": %s, \"rt_ffmpeg_bin\": \"%s\", \"rt_ffmpeg_audio_filter\": \"%s\", "
        "\"rt_capture_volume\": %.2f, "
        "\"rt_amixer_enabled\": %s, \"rt_amixer_card\": \"%s\", "
        "\"rt_amixer_control\": \"%s\", \"rt_amixer_volume\": \"%s\", "
        "\"wake_enabled\": true, \"wake_running\": %s, \"wake_pid\": %d, "
        "\"wake_auto_start\": %s, \"wake_report_enabled\": %s, \"wake_report_watcher_running\": %s, "
        "\"wake_command\": \"%s\", \"wake_workdir\": \"%s\", \"wake_log_path\": \"%s\"}",
        model_path, config.anomaly_threshold, config.save_anomaly,
        (int)(sizeof(anomaly_keywords) / sizeof(AnomalyKeyword)),
        g_asr_enabled ? "true" : "false",
        g_asr_model_cn,
        g_asr_model_en,
        escaped_config_path,
        escaped_default_device,
        sox_denoise_is_ready() ? "true" : "false",
        g_sox_bin,
        g_sox_denoise_profile,
        g_sox_denoise_amount,
        ffmpeg_rt_filter_is_ready() ? "true" : "false",
        escaped_ffmpeg_bin,
        escaped_ffmpeg_filter,
        g_rt_capture_volume,
        g_rt_amixer_enabled ? "true" : "false",
        g_rt_amixer_card,
        g_rt_amixer_control,
        g_rt_amixer_volume,
        kws_running ? "true" : "false",
        kws_pid,
        g_emergency_kws_auto_start ? "true" : "false",
        g_emergency_kws_report_enabled ? "true" : "false",
        g_emergency_kws_report_running ? "true" : "false",
        escaped_kws_cmd,
        escaped_kws_workdir,
        escaped_kws_log_path);
    send_response(client_fd, "200 OK", "application/json", body, strlen(body));
}

void handle_client(int client_fd) {
    std::vector<char> request_buffer(4096);
    int total_len = 0;
    int header_len = -1;
    int content_length = 0;

    while (1) {
        if (total_len >= (int)request_buffer.size() - 1) {
            size_t new_size = request_buffer.size() * 2;
            if (new_size < request_buffer.size() + 1024) {
                new_size = request_buffer.size() + 1024;
            }
            request_buffer.resize(new_size);
        }

        int len = recv(client_fd,
                       request_buffer.data() + total_len,
                       request_buffer.size() - 1 - total_len,
                       0);
        if (len <= 0) {
            close(client_fd);
            return;
        }

        total_len += len;
        request_buffer[total_len] = '\0';

        if (header_len < 0) {
            char *header_end = strstr(request_buffer.data(), "\r\n\r\n");
            if (header_end) {
                header_len = (int)(header_end - request_buffer.data()) + 4;
                content_length = parse_content_length(request_buffer.data());

                if (content_length < 0) {
                    send_json_response(client_fd, 1, "Invalid Content-Length");
                    close(client_fd);
                    return;
                }

                size_t required_size = (size_t)header_len + (size_t)content_length + 1;
                if (required_size > request_buffer.size()) {
                    request_buffer.resize(required_size);
                }
            }
        }

        if (header_len >= 0 && total_len >= header_len + content_length) {
            break;
        }
    }

    if (header_len < 0) {
        send_json_response(client_fd, 1, "Malformed HTTP request");
        close(client_fd);
        return;
    }

    // 解析请求
    char method[16], path[256], version[16];
    if (sscanf(request_buffer.data(), "%s %s %s", method, path, version) != 3) {
        close(client_fd);
        return;
    }

    // 分离路径和查询参数
    char *query = strchr(path, '?');
    const char *query_str = NULL;
    if (query) query_str = query + 1;
    if (query) *query = '\0';

    if (should_log_http_request(method, path)) {
        printf("[HTTP] %s %s\n", method, path);
    }

    // 找到请求体（HTTP body）
    char *body = request_buffer.data() + header_len;
    body[content_length] = '\0';

    // 提取 Content-Type 头
    char http_content_type[128] = {0};
    const char *ct_line = strstr(request_buffer.data(), "Content-Type:");
    if (ct_line && ct_line < request_buffer.data() + header_len) {
        const char *ct_end = strstr(ct_line, "\r\n");
        if (ct_end && ct_end < request_buffer.data() + header_len) {
            int ct_len = ct_end - ct_line - 13; // skip "Content-Type:"
            if (ct_len > 0 && ct_len < (int)sizeof(http_content_type) - 1) {
                const char *ct_val = ct_line + 13;
                while (*ct_val == ' ' || *ct_val == '\t') { ct_val++; ct_len--; }
                memcpy(http_content_type, ct_val, ct_len);
                http_content_type[ct_len] = '\0';
            }
        }
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/analyze") == 0) {
        handle_analyze(client_fd, body);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/analyze/upload") == 0) {
        handle_analyze_upload(client_fd, http_content_type[0] ? http_content_type : NULL, body, content_length);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/realtime/start") == 0) {
        handle_realtime_start(client_fd, body);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/realtime/stop") == 0) {
        handle_realtime_stop(client_fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/wake/start") == 0) {
        handle_wake_start(client_fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/wake/stop") == 0) {
        handle_wake_stop(client_fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        handle_health(client_fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/config") == 0) {
        handle_config(client_fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/realtime/status") == 0) {
        handle_realtime_status(client_fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/realtime/events") == 0) {
        handle_realtime_events(client_fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/wake/status") == 0) {
        handle_wake_status(client_fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/wake/events") == 0) {
        handle_wake_events(client_fd, query_str);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/realtime/windows") == 0) {
        handle_realtime_windows(client_fd, query_str);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/realtime/transcript") == 0) {
        handle_realtime_transcript(client_fd, query_str);
    } else {
        char resp[] = "{\"error\": \"Not Found\"}";
        send_response(client_fd, "404 Not Found", "application/json", resp, strlen(resp));
    }

    close(client_fd);
}

void* client_handler(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    handle_client(client_fd);
    return NULL;
}

int start_server(int port) {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket failed");
        return -1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(server_socket);
        return -1;
    }

    if (listen(server_socket, 10) < 0) {
        perror("listen failed");
        close(server_socket);
        return -1;
    }

    return 0;
}
