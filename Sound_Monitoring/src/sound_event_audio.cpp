#include "sound_event_audio.h"
#include "sound_globals.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <algorithm>
#include <string>
#include <vector>

#include "audio_utils.h"
#include "audio_capture.h"

// Forward-declare from sound_report.h
int report_to_spring_boot(const char *audio_path, float duration,
                          const char *keywords, float confidence);

// Forward-declare from sound_denoise.h
int ffmpeg_rt_filter_is_ready();
int ffmpeg_filter_wav_inplace(const char *wav_path,
                              int sample_rate,
                              int num_channels,
                              const char *context);
int sox_denoise_wav_inplace(const char *wav_path, const char *context);

// Forward-declare from sound_http_utils.h
std::string summarize_top_results(const ResultEntry *results, int result_count, int max_items);

// ========== 事件音频存储实现 ==========
int rt_event_audio_count = 0;
EventAudio g_event_audio = {0};

void event_audio_init() {
    g_event_audio.capacity = rt_sample_rate * RT_MAX_EVENT_AUDIO_SEC;
    g_event_audio.data = (float *)calloc(g_event_audio.capacity, sizeof(float));
    g_event_audio.frames = 0;
}

void event_audio_append_limited(const float *chunk, int chunk_frames) {
    if (!g_event_audio.data || !chunk || chunk_frames <= 0) return;
    if (g_event_audio.frames >= g_event_audio.capacity) return;

    int copy_frames = chunk_frames;
    if (g_event_audio.frames + copy_frames > g_event_audio.capacity) {
        copy_frames = g_event_audio.capacity - g_event_audio.frames;
    }
    if (copy_frames <= 0) return;

    memcpy(g_event_audio.data + g_event_audio.frames, chunk, copy_frames * sizeof(float));
    g_event_audio.frames += copy_frames;
}

void event_audio_append_silence(int chunk_frames) {
    if (!g_event_audio.data || chunk_frames <= 0) return;
    if (g_event_audio.frames >= g_event_audio.capacity) return;

    int copy_frames = chunk_frames;
    if (g_event_audio.frames + copy_frames > g_event_audio.capacity) {
        copy_frames = g_event_audio.capacity - g_event_audio.frames;
    }
    if (copy_frames <= 0) return;

    memset(g_event_audio.data + g_event_audio.frames, 0, copy_frames * sizeof(float));
    g_event_audio.frames += copy_frames;
}

void event_audio_reset() {
    g_event_audio.frames = 0;
}

void event_audio_free() {
    if (g_event_audio.data) { free(g_event_audio.data); g_event_audio.data = NULL; }
}

double rt_get_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

void rt_add_keyword(char keywords[][64], int *count, const char *kw) {
    char cleaned[64] = {0};
    int j = 0;
    for (int i = 0; kw[i] && j < 63; i++) {
        if (kw[i] != '\n' && kw[i] != '\r') cleaned[j++] = kw[i];
    }
    for (int i = 0; i < *count; i++) {
        if (strcmp(keywords[i], cleaned) == 0) return;
    }
    if (*count < RT_MAX_KEYWORDS) {
        strncpy(keywords[*count], cleaned, 63);
        (*count)++;
    }
}

void rt_push_event(float start_sec, float end_sec, float max_score) {
    // 保存音频
    char audio_path[512];
    snprintf(audio_path, sizeof(audio_path), "%s/event_%d_%.1fs.wav",
             rt_save_dir, rt_event_audio_count, start_sec);

    int saved = 0;
    float duration = 0.0f;
    if (g_event_audio.frames > 0) {
        // 创建目录
        char mk_cmd[256];
        snprintf(mk_cmd, sizeof(mk_cmd), "mkdir -p %s", rt_save_dir);
        system(mk_cmd);

        saved = save_audio(audio_path, g_event_audio.data,
                          g_event_audio.frames, rt_sample_rate, 1);
        duration = (float)g_event_audio.frames / rt_sample_rate;
        if (saved == 0) {
            int ffmpeg_ok = 0;
            if (ffmpeg_rt_filter_is_ready()) {
                ffmpeg_ok = (ffmpeg_filter_wav_inplace(audio_path, rt_sample_rate, 1, "realtime-event") == 0);
            }
            if (!ffmpeg_ok) {
                (void)sox_denoise_wav_inplace(audio_path, "realtime-event");
            }
            printf("[RT] Audio saved: %s (%d frames, %.1fs)\n",
                   audio_path, g_event_audio.frames, duration);
        }
        rt_event_audio_count++;
    }
    event_audio_reset();

    // 构建关键词字符串
    char keywords_str[512] = {0};
    for (int i = 0; i < rt_current_event.keyword_count && i < RT_MAX_KEYWORDS; i++) {
        if (i > 0) strncat(keywords_str, ", ", 2);
        strncat(keywords_str, rt_current_event.keywords[i], 63);
    }

    // 上报到 Spring Boot（仅当置信度达到上报阈值）
    if (saved == 0 && strlen(audio_path) > 0) {
        if (max_score >= RT_REPORT_MIN_CONFIDENCE) {
            report_to_spring_boot(audio_path, duration, keywords_str, max_score);
        } else {
            printf("[REPORT] Skip Spring Boot report: confidence %.4f < %.2f\n",
                   max_score, RT_REPORT_MIN_CONFIDENCE);
        }
    }

    pthread_mutex_lock(&rt_mutex);
    if (rt_event_count >= RT_EVENT_QUEUE_SIZE) {
        rt_event_tail = (rt_event_tail + 1) % RT_EVENT_QUEUE_SIZE;
        rt_event_count--;
    }
    RealtimeEvent *evt = &rt_event_queue[rt_event_head];
    evt->event_id = rt_event_head;
    evt->start_sec = start_sec;
    evt->end_sec = end_sec;
    evt->duration = end_sec - start_sec;
    evt->max_score = max_score;
    evt->trigger_count = rt_current_event.trigger_count;
    evt->keyword_count = rt_current_event.keyword_count;
    memcpy(evt->keywords, rt_current_event.keywords, sizeof(rt_current_event.keywords));

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(evt->timestamp, sizeof(evt->timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    rt_event_head = (rt_event_head + 1) % RT_EVENT_QUEUE_SIZE;
    rt_event_count++;

    printf("[RT EVENT] #%d [%.1fs - %.1fs] keywords: ", evt->event_id, start_sec, end_sec);
    for (int i = 0; i < evt->keyword_count; i++) printf("%s ", evt->keywords[i]);
    printf("(score=%.4f)\n", max_score);

    pthread_mutex_unlock(&rt_mutex);
    pthread_cond_signal(&rt_cond);
}

void rt_push_window_status(int window_id,
                            float start_sec,
                            float end_sec,
                            int anomaly,
                            const char *matched_kw,
                            float matched_score,
                            const ResultEntry *results,
                            int result_count) {
    char top_summary[256] = {0};
    std::string summary = summarize_top_results(results, result_count, 3);
    strncpy(top_summary, summary.c_str(), sizeof(top_summary) - 1);

    pthread_mutex_lock(&rt_mutex);
    if (rt_window_count >= RT_WINDOW_QUEUE_SIZE) {
        rt_window_tail = (rt_window_tail + 1) % RT_WINDOW_QUEUE_SIZE;
        rt_window_count--;
    }

    RealtimeWindowStatus *window = &rt_window_queue[rt_window_head];
    memset(window, 0, sizeof(*window));
    window->window_id = window_id;
    window->start_sec = start_sec;
    window->end_sec = end_sec;
    window->anomaly = anomaly ? 1 : 0;
    window->matched_score = anomaly ? matched_score : 0.0f;
    if (anomaly && matched_kw) {
        strncpy(window->matched_keyword, matched_kw, sizeof(window->matched_keyword) - 1);
    }
    strncpy(window->top_summary, top_summary, sizeof(window->top_summary) - 1);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(window->timestamp, sizeof(window->timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    rt_window_head = (rt_window_head + 1) % RT_WINDOW_QUEUE_SIZE;
    rt_window_count++;
    pthread_mutex_unlock(&rt_mutex);
}

void rt_reset_current_event() {
    memset(&rt_current_event, 0, sizeof(rt_current_event));
}

void rt_ring_init(RingBuffer *rb, int sample_rate, int sec) {
    rb->capacity = sample_rate * sec;
    rb->data = (float *)calloc(rb->capacity, sizeof(float));
    rb->size = 0;
    rb->write_idx = 0;
}

void rt_ring_push(RingBuffer *rb, float *data, int frames) {
    for (int i = 0; i < frames; i++) {
        rb->data[rb->write_idx] = data[i];
        rb->write_idx = (rb->write_idx + 1) % rb->capacity;
        if (rb->size < rb->capacity) rb->size++;
    }
}

void rt_ring_get_latest(RingBuffer *rb, float *out, int frames) {
    if (frames > rb->size) frames = rb->size;
    int start_idx = (rb->write_idx - frames + rb->capacity) % rb->capacity;
    for (int i = 0; i < frames; i++) {
        out[i] = rb->data[(start_idx + i) % rb->capacity];
    }
}

void rt_ring_free(RingBuffer *rb) {
    if (rb->data) { free(rb->data); rb->data = NULL; }
    rb->capacity = 0;
    rb->size = 0;
    rb->write_idx = 0;
}

int save_recent_audio_for_emergency_kws(char *audio_path,
                                         size_t audio_path_size,
                                         float *duration_out) {
    if (!audio_path || audio_path_size == 0) return -1;
    audio_path[0] = '\0';
    if (duration_out) *duration_out = 0.0f;

    std::vector<float> frames;
    int sample_rate = 0;
    int copy_frames = 0;

    pthread_mutex_lock(&rt_mutex);
    sample_rate = rt_sample_rate;
    if (rt_ring.data && rt_ring.size > 0 && sample_rate > 0) {
        int need_frames = sample_rate * RT_EVENT_CLIP_SEC;
        copy_frames = std::min(need_frames, rt_ring.size);
        if (copy_frames > 0) {
            frames.resize(copy_frames);
            rt_ring_get_latest(&rt_ring, frames.data(), copy_frames);
        }
    }
    pthread_mutex_unlock(&rt_mutex);

    if (frames.empty()) {
        printf("[KWS REPORT] No realtime audio buffer available for keyword event\n");
        return -1;
    }

    struct stat st;
    if (stat(rt_save_dir, &st) != 0) {
        if (mkdir(rt_save_dir, 0755) != 0 && errno != EEXIST) {
            printf("[KWS REPORT] Failed to create audio dir: %s\n", rt_save_dir);
            return -1;
        }
    }

    time_t now = time(NULL);
    int audio_id = __sync_fetch_and_add(&g_emergency_kws_audio_count, 1);
    snprintf(audio_path, audio_path_size, "%s/kws_event_%d_%ld.wav",
             rt_save_dir, audio_id, (long)now);

    int ret = save_audio(audio_path, frames.data(), copy_frames, sample_rate, 1);
    if (ret != 0) {
        printf("[KWS REPORT] Failed to save keyword audio: %s\n", audio_path);
        audio_path[0] = '\0';
        return -1;
    }

    int ffmpeg_ok = 0;
    if (ffmpeg_rt_filter_is_ready()) {
        ffmpeg_ok = (ffmpeg_filter_wav_inplace(audio_path, sample_rate, 1, "kws-event") == 0);
    }
    if (!ffmpeg_ok) {
        (void)sox_denoise_wav_inplace(audio_path, "kws-event");
    }

    if (duration_out) {
        *duration_out = (float)copy_frames / (float)sample_rate;
    }
    printf("[KWS REPORT] Keyword audio saved: %s (%d frames, %.1fs)\n",
           audio_path,
           copy_frames,
           sample_rate > 0 ? (float)copy_frames / (float)sample_rate : 0.0f);
    return 0;
}

void event_audio_capture_pre_roll(RingBuffer *rb, int pre_frames) {
    event_audio_reset();
    if (!g_event_audio.data || !rb || pre_frames <= 0) return;

    int frames_to_copy = pre_frames;
    if (frames_to_copy > g_event_audio.capacity) {
        frames_to_copy = g_event_audio.capacity;
    }
    if (frames_to_copy <= 0) return;

    int available = rb->size;
    if (available > frames_to_copy) {
        available = frames_to_copy;
    }
    int missing = frames_to_copy - available;
    if (missing > 0) {
        memset(g_event_audio.data, 0, missing * sizeof(float));
    }
    if (available > 0) {
        rt_ring_get_latest(rb, g_event_audio.data + missing, available);
    }
    g_event_audio.frames = frames_to_copy;
}

void rt_finalize_current_event(int pad_missing_post_roll) {
    if (!rt_current_event.active) return;

    if (pad_missing_post_roll &&
        rt_current_event.post_frames_collected < rt_current_event.post_frames_target) {
        event_audio_append_silence(rt_current_event.post_frames_target -
                                   rt_current_event.post_frames_collected);
        rt_current_event.post_frames_collected = rt_current_event.post_frames_target;
    }

    rt_push_event(rt_current_event.start_sec, rt_current_event.end_sec, rt_current_event.max_score);
    rt_reset_current_event();
}
