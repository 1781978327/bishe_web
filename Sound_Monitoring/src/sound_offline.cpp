#include "sound_offline.h"
#include "sound_globals.h"
#include "sound_http_utils.h"
#include "sound_denoise.h"
#include "process.h"
#include "audio_utils.h"
#include "asr_vosk.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>

AsrResult run_asr_locked(const float *data, int num_frames) {
    AsrResult out;
    if (!g_asr_enabled) {
        out.enabled = false;
        out.ok = false;
        out.error = "ASR is disabled";
        return out;
    }

    pthread_mutex_lock(&g_asr_mutex);
    out = g_asr_engine.TranscribeFloatMono16k(data, num_frames);
    pthread_mutex_unlock(&g_asr_mutex);
    return out;
}

// ========== 事件管理 ==========
void reset_events() {
    memset(events, 0, sizeof(events));
    event_count = 0;
    has_current_event = 0;
}

void add_keyword(AnomalyEvent *evt, const char *keyword) {
    for (int i = 0; i < evt->keyword_count; i++) {
        if (strcmp(evt->keywords[i], keyword) == 0) return;
    }
    if (evt->keyword_count < 10) {
        strncpy(evt->keywords[evt->keyword_count], keyword, 63);
        evt->keywords[evt->keyword_count][63] = '\0';
        evt->keyword_count++;
    }
}

void start_event(float start_sec, const char *keyword, float score) {
    if (event_count >= MAX_ANOMALY_EVENTS) return;
    AnomalyEvent *evt = &events[event_count];
    evt->start_sec = start_sec;
    evt->end_sec = start_sec + CHUNK_LENGTH;
    evt->keyword_count = 0;
    evt->max_score = score;
    evt->chunk_count = 1;
    add_keyword(evt, keyword);
    current_event = *evt;
    has_current_event = 1;
}

int merge_event(float chunk_end, const char *keyword, float score) {
    float gap = (float)chunk_end / SAMPLE_RATE - current_event.end_sec;
    if (gap <= MERGE_GAP_SEC) {
        current_event.end_sec = (float)chunk_end / SAMPLE_RATE;
        if (score > current_event.max_score) current_event.max_score = score;
        current_event.chunk_count++;
        add_keyword(&current_event, keyword);
        return 1;
    }
    return 0;
}

void finalize_event() {
    if (!has_current_event || event_count >= MAX_ANOMALY_EVENTS) {
        has_current_event = 0;
        return;
    }
    events[event_count++] = current_event;
    has_current_event = 0;
}

// ========== 音频格式转换 (使用ffmpeg) ==========
int convert_to_wav(const char *input_path, char *wav_path, size_t path_size) {
    const char *ext = strrchr(input_path, '.');
    if (ext) {
        if (strcasecmp(ext, ".wav") == 0) {
            strncpy(wav_path, input_path, path_size - 1);
            wav_path[path_size - 1] = '\0';
            return 0;
        }
    }

    snprintf(wav_path, path_size, "/tmp/yamnet_convert_%d.wav", (int)time(NULL));

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -i \"%s\" -ar 16000 -ac 1 -acodec pcm_s16le \"%s\" 2>/dev/null",
        input_path, wav_path);

    int result = system(cmd);
    if (result == 0) {
        printf("[CONVERT] Converted: %s -> %s\n", input_path, wav_path);
        return 0;
    } else {
        printf("[CONVERT] Failed to convert: %s\n", input_path);
        return -1;
    }
}

// ========== 保存异常音频 ==========
int save_anomaly_audio_to_file(const char *original_path, float *audio_data,
                                int num_frames, int sample_rate,
                                int num_channels, const char *event_keywords,
                                float max_score, int event_idx) {
    char dir[512];
    snprintf(dir, sizeof(dir), "mkdir -p %s", anomaly_save_dir);
    if (system(dir) != 0) {
        printf("[ANOMALY_SAVE] Failed to create dir: %s\n", anomaly_save_dir);
        return -1;
    }

    char filename[512];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", tm_info);

    // 从原始路径提取文件名
    const char *base = strrchr(original_path, '/');
    base = base ? base + 1 : original_path;

    snprintf(filename, sizeof(filename), "%s/%s_event%d_%.3f.wav",
              anomaly_save_dir, time_str, event_idx, max_score);

    int ret = save_audio(filename, audio_data, num_frames, sample_rate, num_channels);
    if (ret == 0) {
        (void)sox_denoise_wav_inplace(filename, "offline-anomaly");
        printf("[ANOMALY_SAVE] Saved anomaly audio: %s (%.1fs, keywords: %s)\n",
               filename, (float)num_frames / sample_rate, event_keywords);
    } else {
        printf("[ANOMALY_SAVE] Failed to save: %s\n", filename);
    }
    return ret;
}

// ========== multipart/form-data 解析 ==========
int multipart_find(const char *data, int data_len,
                   const char *boundary, int boundary_len,
                   int start) {
    // 查找 \r\n--boundary 或 --boundary (结尾)
    for (int i = start; i <= data_len - boundary_len - 2; i++) {
        if (data[i] == '-' && i + 1 < data_len && data[i+1] == '-') {
            if (memcmp(data + i + 2, boundary, boundary_len) == 0) {
                int after = i + 2 + boundary_len;
                if (after >= data_len) return i; // 结尾 --
                if (data[after] == '-' && after + 1 < data_len) return i; // 结尾 --
                if (data[after] == '\r' && after + 2 < data_len && data[after+1] == '\n') {
                    return i; // 普通边界
                }
            }
        }
    }
    return -1;
}

int multipart_parse_headers(const char *data, int data_len,
                             char *filename, size_t fn_size,
                             char *fieldname, size_t fn2_size) {
    // 解析 Content-Disposition
    const char *cd = strstr(data, "Content-Disposition:");
    if (!cd) return -1;

    // 提取 field name
    const char *name_start = strstr(cd, "name=\"");
    if (name_start) {
        name_start += 6;
        const char *name_end = strstr(name_start, "\"");
        if (name_end && (size_t)(name_end - name_start) < fn2_size) {
            memcpy(fieldname, name_start, name_end - name_start);
            fieldname[name_end - name_start] = '\0';
        }
    }

    // 提取 filename
    const char *fn_start = strstr(cd, "filename=\"");
    if (fn_start) {
        fn_start += 10;
        const char *fn_end = strstr(fn_start, "\"");
        if (fn_end && (size_t)(fn_end - fn_start) < fn_size) {
            memcpy(filename, fn_start, fn_end - fn_start);
            filename[fn_end - fn_start] = '\0';
        }
    }

    // 跳过 headers 找到 \r\n\r\n
    const char *body = strstr(cd, "\r\n\r\n");
    if (!body) return -1;
    return (int)(body + 4 - data);
}

// ========== 音频分析 ==========
double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

int analyze_audio(const char *audio_path, char *result_json, size_t json_size) {
    int ret = 0;
    audio_buffer_t full_audio;
    ResultEntry result[TOP_N];
    float duration = 0.0f;
    int hop_frames = HOP_LENGTH;
    int num_chunks = 0;
    int anomaly_count = 0;
    char *json_ptr = NULL;
    int written = 0;
    size_t remaining = 0;
    char temp_wav[512] = {0};
    char *actual_path = NULL;
    int need_delete_temp = 0;
    char denoised_wav[512] = {0};
    int need_delete_denoised = 0;
    double inference_start = 0.0;
    double chunk_inference_total = 0.0;
    int inference_chunk_count = 0;
    double inference_total = 0.0;
    AsrResult asr_result;
    char escaped_transcript[4096] = {0};
    char escaped_asr_error[512] = {0};

    memset(&full_audio, 0, sizeof(audio_buffer_t));
    memset(result, 0, sizeof(result));
    reset_events();

    int anomaly_saved = 0;

    if (convert_to_wav(audio_path, temp_wav, sizeof(temp_wav)) == 0) {
        if (strcasecmp(temp_wav, audio_path) != 0) {
            actual_path = temp_wav;
            need_delete_temp = 1;
        } else {
            actual_path = (char*)audio_path;
        }
    } else {
        actual_path = (char*)audio_path;
    }

    if (sox_denoise_is_ready() && actual_path && path_exists(actual_path)) {
        if (sox_denoise_wav_to_temp(actual_path, denoised_wav, sizeof(denoised_wav), "offline-analyze") == 0) {
            actual_path = denoised_wav;
            need_delete_denoised = 1;
        }
    }

    printf("[ANALYZE] Loading audio: %s\n", actual_path);

    // 读取音频
    ret = read_audio(actual_path, &full_audio);
    if (ret != 0) {
        snprintf(result_json, json_size, "{\"success\": false, \"error\": \"Failed to read audio file\"}");
        if (need_delete_temp && temp_wav[0]) {
            unlink(temp_wav);
        }
        if (need_delete_denoised && denoised_wav[0]) {
            unlink(denoised_wav);
        }
        return -1;
    }

    // 转换通道
    if (full_audio.num_channels == 2) {
        ret = convert_channels(&full_audio);
        if (ret != 0) {
            snprintf(result_json, json_size, "{\"success\": false, \"error\": \"Failed to convert channels\"}");
            goto cleanup;
        }
    }

    // 重采样
    if (full_audio.sample_rate != SAMPLE_RATE) {
        ret = resample_audio(&full_audio, full_audio.sample_rate, SAMPLE_RATE);
        if (ret != 0) {
            snprintf(result_json, json_size, "{\"success\": false, \"error\": \"Failed to resample audio\"}");
            goto cleanup;
        }
    }

    duration = (float)full_audio.num_frames / SAMPLE_RATE;
    printf("[ANALYZE] Audio: %.1fs, %d Hz, %d ch\n", duration, full_audio.sample_rate, full_audio.num_channels);

    // 语音转文字（整段音频）
    if (g_asr_enabled) {
        asr_result = run_asr_locked(full_audio.data, full_audio.num_frames);
        if (asr_result.ok) {
            printf("[ASR] Transcript (%s, conf=%.4f): %s\n",
                   asr_result.lang.empty() ? "unknown" : asr_result.lang.c_str(),
                   asr_result.confidence,
                   asr_result.text.c_str());
        } else {
            printf("[ASR] Failed: %s\n", asr_result.error.c_str());
        }
    }

    // 开始推理计时
    inference_start = get_time_ms();

    // 滑动窗口分析
    for (int frame_pos = 0; frame_pos + N_SAMPLES <= full_audio.num_frames; frame_pos += hop_frames) {
        num_chunks++;

        int chunk_frames = N_SAMPLES;
        if (frame_pos + chunk_frames > full_audio.num_frames) {
            chunk_frames = full_audio.num_frames - frame_pos;
        }

        // 准备 chunk 数据
        audio_buffer_t chunk_audio;
        chunk_audio.data = full_audio.data + frame_pos;
        chunk_audio.num_frames = chunk_frames;
        chunk_audio.num_channels = 1;
        chunk_audio.sample_rate = SAMPLE_RATE;

        // 推理（计时）
        double chunk_start = get_time_ms();
        ret = inference_yamnet_model(&rknn_app_ctx, &chunk_audio, labels, result);
        double chunk_elapsed = get_time_ms() - chunk_start;
        chunk_inference_total += chunk_elapsed;
        inference_chunk_count++;

        if (ret != 0) {
            printf("[WARN] chunk %d inference failed (%.2f ms)\n", num_chunks, chunk_elapsed);
            continue;
        }

        float chunk_time_start = (float)frame_pos / SAMPLE_RATE;
        float chunk_time_end = (float)(frame_pos + chunk_frames) / SAMPLE_RATE;

        // 检查异常
        const char *matched_kw = NULL;
        float matched_sc = 0.0f;
        int is_anom = is_anomaly(result, TOP_N, &matched_kw, &matched_sc);

        if (is_anom && matched_kw != NULL) {
            anomaly_count++;
            printf("[ANALYZE] chunk #%d [%.1fs - %.1fs]: ANOMALY [%s %.3f] (%.2f ms)\n",
                   num_chunks, chunk_time_start, chunk_time_end, matched_kw, matched_sc, chunk_elapsed);

            if (has_current_event) {
                if (!merge_event(frame_pos + chunk_frames, matched_kw, matched_sc)) {
                    finalize_event();
                    start_event(chunk_time_start, matched_kw, matched_sc);
                }
            } else {
                start_event(chunk_time_start, matched_kw, matched_sc);
            }
        } else {
            if (has_current_event) {
                float gap = chunk_time_start - current_event.end_sec;
                if (gap > MERGE_GAP_SEC) {
                    finalize_event();
                }
            }
        }
    }

    inference_total = get_time_ms() - inference_start;
    printf("[ANALYZE] Inference done: %d chunks, total %.2f ms (avg %.2f ms/chunk), audio duration %.2fs\n",
           inference_chunk_count, inference_total,
           inference_chunk_count > 0 ? inference_total / inference_chunk_count : 0.0, duration);

    finalize_event();

    // 生成 JSON 结果
    printf("[ANALYZE] Found %d anomaly events\n", event_count);

    // 保存异常音频（如果有）
    if (event_count > 0 && full_audio.data && full_audio.num_frames > 0) {
        char keywords_str[1024] = {0};
        for (int i = 0; i < event_count; i++) {
            AnomalyEvent *evt = &events[i];
            for (int j = 0; j < evt->keyword_count; j++) {
                if (strlen(keywords_str) < sizeof(keywords_str) - 64) {
                    if (keywords_str[0]) strncat(keywords_str, ", ", 2);
                    strncat(keywords_str, evt->keywords[j], 63);
                }
            }
        }
        anomaly_saved = save_anomaly_audio_to_file(
            audio_path,
            full_audio.data,
            full_audio.num_frames,
            SAMPLE_RATE,
            1,
            keywords_str,
            (event_count > 0) ? events[0].max_score : 0.0f,
            0
        );
    }

    json_ptr = result_json;
    if (!asr_result.text.empty()) {
        json_escape_string(asr_result.text.c_str(), escaped_transcript, sizeof(escaped_transcript));
    }
    if (!asr_result.error.empty()) {
        json_escape_string(asr_result.error.c_str(), escaped_asr_error, sizeof(escaped_asr_error));
    }

    written = snprintf(json_ptr, json_size,
        "{\"success\": true, \"audio_path\": \"%s\", \"duration\": %.1f, "
        "\"total_chunks\": %d, \"anomaly_count\": %d, \"anomaly_saved\": %s, "
        "\"asr_enabled\": %s, \"asr_lang\": \"%s\", \"asr_confidence\": %.6f, "
        "\"transcript\": \"%s\", \"asr_error\": \"%s\", \"events\": [",
        audio_path, duration, num_chunks, event_count,
        anomaly_saved == 0 ? "true" : "false",
        g_asr_enabled ? "true" : "false",
        asr_result.lang.empty() ? "" : asr_result.lang.c_str(),
        asr_result.ok ? asr_result.confidence : 0.0,
        escaped_transcript,
        escaped_asr_error);

    json_ptr += written;
    remaining = json_size - written;

    for (int i = 0; i < event_count && remaining > 100; i++) {
        AnomalyEvent *evt = &events[i];
        int len = snprintf(json_ptr, remaining,
            "%s{\"id\": %d, \"start\": %.2f, \"end\": %.2f, "
            "\"duration\": %.2f, \"confidence\": %.4f, \"keywords\": [",
            i > 0 ? "," : "", i, evt->start_sec, evt->end_sec,
            evt->end_sec - evt->start_sec, evt->max_score);

        json_ptr += len;
        remaining -= len;

        for (int j = 0; j < evt->keyword_count && remaining > 50; j++) {
            len = snprintf(json_ptr, remaining, "%s\"%s\"", j > 0 ? "," : "", evt->keywords[j]);
            json_ptr += len;
            remaining -= len;
        }

        len = snprintf(json_ptr, remaining, "]}");
        json_ptr += len;
        remaining -= len;
    }

    snprintf(json_ptr, remaining, "]}");

cleanup:
    if (full_audio.data) free(full_audio.data);
    if (need_delete_temp && temp_wav[0]) {
        unlink(temp_wav);
    }
    if (need_delete_denoised && denoised_wav[0]) {
        unlink(denoised_wav);
    }
    return 0;
}

// ========== 请求处理 ==========
void handle_analyze(int client_fd, const char *body) {
    char audio_path[512] = {0};

    // 简单 JSON 解析：查找 "audio_path":"..." 或 "audio_path": "..."
    const char *key = "\"audio_path\"";
    const char *ptr = strstr(body, key);
    if (!ptr) {
        send_json_response(client_fd, 1, "Invalid request format. Use: {\"audio_path\": \"/path/to/audio.wav\"}");
        return;
    }

    // 跳过 key 和可能的空格、冒号
    ptr += strlen(key);
    while (*ptr == ' ' || *ptr == ':') ptr++;

    // 跳过开始的引号
    if (*ptr == '"') ptr++;

    // 复制路径直到结束引号或换行
    int i = 0;
    while (*ptr && *ptr != '"' && *ptr != '\n' && i < 511) {
        audio_path[i++] = *ptr++;
    }
    audio_path[i] = '\0';

    if (strlen(audio_path) == 0) {
        send_json_response(client_fd, 1, "Invalid request format. audio_path is empty");
        return;
    }

    printf("[HTTP] POST /analyze -> %s\n", audio_path);

    char result[32768];
    analyze_audio(audio_path, result, sizeof(result));

    printf("[HTTP] Response: %s\n", result);
    send_response(client_fd, "200 OK", "application/json", result, strlen(result));
}

// ========== 上传二进制音频分析 ==========
void handle_analyze_upload(int client_fd, const char *content_type,
                           const char *body, int body_len) {
    char tmp_path[512];
    char tmp_input_path[512];
    int is_multipart = 0;
    const char *file_data = NULL;
    int file_data_len = 0;
    char filename_hint[256] = "uploaded_audio";

    // 判断是否是 multipart/form-data
    if (content_type && strstr(content_type, "multipart/form-data")) {
        is_multipart = 1;
        const char *boundary = strstr(content_type, "boundary=");
        if (!boundary) {
            send_json_response(client_fd, 1, "Missing boundary in multipart/form-data");
            return;
        }
        boundary += 9; // skip "boundary="

        // 解析 multipart
        char fn_buf[256] = {0};
        char fdname_buf[64] = {0};
        int hdr_end = multipart_parse_headers(body, body_len, fn_buf, sizeof(fn_buf),
                                               fdname_buf, sizeof(fdname_buf));
        if (hdr_end < 0) {
            send_json_response(client_fd, 1, "Failed to parse multipart headers");
            return;
        }

        if (fn_buf[0]) {
            strncpy(filename_hint, fn_buf, sizeof(filename_hint) - 1);
            // 提取 multipart body
            const char *data_start = body + hdr_end;
            int data_start_off = hdr_end;

            // 找到结尾 boundary
            char end_b[256];
            snprintf(end_b, sizeof(end_b), "\r\n--%s", boundary);
            const char *end_pos = strstr(data_start, end_b);
            if (!end_pos) {
                // try without \r\n
                snprintf(end_b, sizeof(end_b), "--%s", boundary);
                end_pos = strstr(data_start, end_b);
            }
            int data_len = end_pos ? (int)(end_pos - data_start) : (body_len - data_start_off);
            // trim trailing \r\n
            while (data_len > 0 && (data_start[data_len-1] == '\r' || data_start[data_len-1] == '\n')) {
                data_len--;
            }

            file_data = data_start;
            file_data_len = data_len;
        } else {
            send_json_response(client_fd, 1, "No file found in multipart data");
            return;
        }
    } else {
        // 原始二进制数据
        file_data = body;
        file_data_len = body_len;
    }

    if (!file_data || file_data_len <= 0) {
        send_json_response(client_fd, 1, "Empty audio data");
        return;
    }

    // 生成临时文件
    const char *ext = ".dat";
    if (content_type) {
        if (strstr(content_type, "audio/wav") || strstr(content_type, "audio/x-wav")) ext = ".wav";
        else if (strstr(content_type, "audio/mpeg") || strstr(content_type, "audio/mp3")) ext = ".mp3";
        else if (strstr(content_type, "audio/ogg")) ext = ".ogg";
        else if (strstr(content_type, "audio/aac") || strstr(content_type, "audio/mp4")) ext = ".aac";
        else if (strstr(content_type, "audio/flac")) ext = ".flac";
    }
    // 如果是 multipart，尝试从 filename 提取扩展名
    if (is_multipart && strlen(filename_hint) > 4) {
        const char *p = strrchr(filename_hint, '.');
        if (p) ext = p;
    }

    snprintf(tmp_input_path, sizeof(tmp_input_path), "/tmp/yamnet_upload_%d%s", (int)time(NULL), ext);

    FILE *fp = fopen(tmp_input_path, "wb");
    if (!fp) {
        send_json_response(client_fd, 1, "Failed to create temp file");
        return;
    }
    fwrite(file_data, 1, file_data_len, fp);
    fclose(fp);

    printf("[HTTP] POST /analyze/upload -> %s (%d bytes)\n", tmp_input_path, file_data_len);

    char result[32768];
    analyze_audio(tmp_input_path, result, sizeof(result));

    // 删除临时文件
    unlink(tmp_input_path);

    printf("[HTTP] Response: %s\n", result);
    send_response(client_fd, "200 OK", "application/json", result, strlen(result));
}
