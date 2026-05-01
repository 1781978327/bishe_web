#include "sound_rt_monitor.h"
#include "sound_globals.h"
#include "sound_event_audio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <string>

#include "audio_capture.h"
#include "process.h"
#include "asr_vosk.h"

// Forward-declare from sound_denoise.h
int ffmpeg_rt_filter_is_ready();
int ffmpeg_filter_buffer_inplace(float *data,
                                 int num_frames,
                                 int sample_rate,
                                 int num_channels,
                                 const char *context);
int sox_denoise_is_ready();
int sox_denoise_buffer_inplace(float *data,
                               int num_frames,
                               int sample_rate,
                               int num_channels,
                               const char *context);

// Forward-declare from sound_http_utils.h
AsrResult run_asr_locked(const float *data, int num_frames);
std::string summarize_top_results(const ResultEntry *results, int result_count, int max_items);

// Forward-declare from sound_audio_config.h
void apply_rt_capture_pulse_settings(const char *device);
void apply_rt_capture_mixer(const char *device);

void *rt_monitor_thread(void *arg) {
    (void)arg;
    rknn_app_context_t *ctx = (rknn_app_context_t *)arg;
    rknn_app_context_t local_ctx;
    if (!ctx || !model_initialized) {
        memcpy(&local_ctx, &rknn_app_ctx, sizeof(rknn_app_context_t));
        ctx = &local_ctx;
    }

    int frames_per_chunk = (int)(rt_sample_rate * RT_CHUNK_DURATION_SEC);
    int frames_per_hop = (int)(rt_sample_rate * RT_HOP_SEC);

    rt_session_start = rt_get_timestamp();
    rt_total_chunks = 0;
    rt_anomaly_count = 0;
    rt_last_asr_emit_ts = -1.0;

    printf("[RT] Monitor thread started, device=%s, rate=%d\n", rt_device, rt_sample_rate);

    while (rt_running) {
        int finalize_after_inference = 0;
        int got = capture_read(rt_cap, frames_per_hop, 1000);
        if (got > 0) {
            float *all_data = capture_get_data(rt_cap);
            int total_frames = capture_get_frames(rt_cap);
            int start = total_frames - got;
            if (all_data && start >= 0) {
                const float *latest_frames = all_data + start;
                rt_ring_push(&rt_ring, all_data + start, got);

                if (rt_current_event.active) {
                    if (rt_current_event.skip_next_chunk_append) {
                        rt_current_event.skip_next_chunk_append = 0;
                    } else if (rt_current_event.post_frames_collected < rt_current_event.post_frames_target) {
                        int copy_frames = got;
                        int remain = rt_current_event.post_frames_target - rt_current_event.post_frames_collected;
                        if (copy_frames > remain) copy_frames = remain;
                        event_audio_append_limited(latest_frames, copy_frames);
                        rt_current_event.post_frames_collected += copy_frames;
                        if (rt_current_event.post_frames_collected >= rt_current_event.post_frames_target) {
                            finalize_after_inference = 1;
                        }
                    }
                }
            }
        }

        if (rt_ring.size >= frames_per_chunk) {
            rt_total_chunks++;

            float chunk_data[frames_per_chunk];
            rt_ring_get_latest(&rt_ring, chunk_data, frames_per_chunk);
            int ffmpeg_ok = 0;
            if (ffmpeg_rt_filter_is_ready()) {
                ffmpeg_ok = (ffmpeg_filter_buffer_inplace(chunk_data, frames_per_chunk, rt_sample_rate, 1,
                                                          "realtime-window") == 0);
            }
            if (!ffmpeg_ok && sox_denoise_is_ready()) {
                (void)sox_denoise_buffer_inplace(chunk_data, frames_per_chunk, rt_sample_rate, 1,
                                                 "realtime-window-fallback-sox");
            }

            audio_buffer_t audio;
            audio.data = chunk_data;
            audio.num_frames = frames_per_chunk;
            audio.num_channels = 1;
            audio.sample_rate = rt_sample_rate;

            memset(rt_result, 0, sizeof(rt_result));
            int ret = inference_yamnet_model(ctx, &audio, rt_labels, rt_result);

            double current_ts = rt_get_timestamp() - rt_session_start;

            if (ret == 0) {
                // Optional console transcript output every ~3 seconds.
                if (g_rt_print_asr && g_asr_enabled &&
                    (rt_last_asr_emit_ts < 0.0 || current_ts - rt_last_asr_emit_ts >= RT_CHUNK_DURATION_SEC - 0.05)) {
                    AsrResult asr = run_asr_locked(chunk_data, frames_per_chunk);
                    rt_last_asr_emit_ts = current_ts;
                    if (asr.ok && !asr.text.empty()) {
                        printf("[RT ASR 3s] t=%.1fs lang=%s conf=%.3f text=%s\n",
                               current_ts,
                               asr.lang.empty() ? "unknown" : asr.lang.c_str(),
                               asr.confidence,
                               asr.text.c_str());
                    }
                }

                const char *matched_kw = NULL;
                float matched_score = 0.0f;
                int is_anom = is_anomaly(rt_result, TOP_N, &matched_kw, &matched_score);
                double window_start = current_ts - RT_CHUNK_DURATION_SEC;
                if (window_start < 0.0) {
                    window_start = 0.0;
                }

                rt_push_window_status(rt_total_chunks,
                                      (float)window_start,
                                      (float)current_ts,
                                      is_anom,
                                      matched_kw,
                                      matched_score,
                                      rt_result,
                                      TOP_N);

                if (g_rt_print_window) {
                    std::string top_summary = summarize_top_results(rt_result, TOP_N, 3);
                    printf("[RT WINDOW] #%d [%.1fs - %.1fs] anomaly=%s matched=%s score=%.4f top=%s\n",
                           rt_total_chunks,
                           window_start,
                           current_ts,
                           is_anom ? "true" : "false",
                           matched_kw ? matched_kw : "-",
                           matched_kw ? matched_score : 0.0f,
                           top_summary.c_str());
                }

                if (is_anom) {
                    rt_anomaly_count++;

                    if (!rt_current_event.active) {
                        event_audio_capture_pre_roll(&rt_ring, frames_per_chunk);
                        rt_current_event.active = 1;
                        rt_current_event.start_sec = current_ts - RT_EVENT_PRE_SEC;
                        if (rt_current_event.start_sec < 0.0f) {
                            rt_current_event.start_sec = 0.0f;
                        }
                        rt_current_event.trigger_sec = current_ts;
                        rt_current_event.end_sec = current_ts + RT_EVENT_POST_SEC;
                        rt_current_event.keyword_count = 0;
                        rt_current_event.max_score = matched_score;
                        rt_current_event.trigger_count = 1;
                        rt_current_event.post_frames_collected = 0;
                        rt_current_event.post_frames_target = (int)(rt_sample_rate * RT_EVENT_POST_SEC);
                        rt_current_event.skip_next_chunk_append = 1;
                        rt_add_keyword(rt_current_event.keywords, &rt_current_event.keyword_count, matched_kw);
                        printf("[RT EVENT] 异常已触发，开始截取 %.1fs 前置 + %.1fs 后置音频\n",
                               (float)RT_EVENT_PRE_SEC, (float)RT_EVENT_POST_SEC);
                    } else {
                        rt_current_event.trigger_count++;
                        rt_add_keyword(rt_current_event.keywords, &rt_current_event.keyword_count, matched_kw);
                        if (matched_score > rt_current_event.max_score) {
                            rt_current_event.max_score = matched_score;
                        }
                    }
                }
            }

            if (finalize_after_inference && rt_current_event.active) {
                rt_finalize_current_event(0);
            }
        }
        usleep(50000);
    }

    // 最终化最后一个事件
    if (rt_current_event.active) {
        rt_finalize_current_event(1);
    }
    rt_ring_free(&rt_ring);

    printf("[RT] Monitor thread exited\n");
    return NULL;
}

int rt_start(const char *device) {
    pthread_mutex_lock(&rt_mutex);

    if (rt_active) {
        pthread_mutex_unlock(&rt_mutex);
        return -1;  // 已经在运行
    }

    strncpy(rt_device, device, sizeof(rt_device) - 1);

    // 读取标签
    int ret = read_label(rt_labels);
    if (ret != 0) {
        printf("[RT ERROR] read_label fail: %d\n", ret);
        pthread_mutex_unlock(&rt_mutex);
        return -2;
    }

    // 打开音频设备。默认走 parec，也就是 Pulse 当前默认麦克风。
    rt_cap = capture_open(rt_device, rt_sample_rate, 1, 60);
    if (!rt_cap &&
        strcmp(rt_device, g_rt_default_device) == 0 &&
        g_rt_fallback_device[0] != '\0' &&
        strcmp(g_rt_fallback_device, g_rt_default_device) != 0) {
        printf("[RT WARN] Failed to open preferred device %s, fallback to %s\n",
               g_rt_default_device, g_rt_fallback_device);
        strncpy(rt_device, g_rt_fallback_device, sizeof(rt_device) - 1);
        rt_device[sizeof(rt_device) - 1] = '\0';
        rt_cap = capture_open(rt_device, rt_sample_rate, 1, 60);
    }

    if (!rt_cap) {
        printf("[RT ERROR] Failed to open audio device: %s\n", rt_device);
        pthread_mutex_unlock(&rt_mutex);
        return -3;
    }

    apply_rt_capture_pulse_settings(rt_device);
    apply_rt_capture_mixer(rt_device);

    capture_set_gain(rt_cap, g_rt_capture_volume);
    printf("[RT] Capture gain set to %.2f (%.0f%%)\n",
           g_rt_capture_volume, g_rt_capture_volume * 100.0f);

    ret = capture_start(rt_cap);
    if (ret != 0) {
        printf("[RT ERROR] Failed to start capture\n");
        capture_close(rt_cap);
        rt_cap = NULL;
        pthread_mutex_unlock(&rt_mutex);
        return -4;
    }

    // 初始化环形缓冲区
    rt_ring_init(&rt_ring, rt_sample_rate, RT_RING_BUFFER_SEC);

    // 清空事件队列
    rt_event_head = 0;
    rt_event_tail = 0;
    rt_event_count = 0;
    rt_window_head = 0;
    rt_window_tail = 0;
    rt_window_count = 0;

    // 初始化事件音频存储
    event_audio_init();

    // 启动监测线程
    rt_running = 1;
    rt_active = 1;
    pthread_mutex_unlock(&rt_mutex);

    pthread_create(&rt_thread, NULL, rt_monitor_thread, NULL);

    printf("[RT] Realtime monitoring started on %s\n", rt_device);
    return 0;
}

int rt_stop() {
    pthread_mutex_lock(&rt_mutex);
    if (!rt_active) {
        pthread_mutex_unlock(&rt_mutex);
        return 0;
    }
    rt_running = 0;
    rt_active = 0;
    pthread_mutex_unlock(&rt_mutex);

    pthread_join(rt_thread, NULL);

    if (rt_cap) {
        capture_stop(rt_cap);
        capture_close(rt_cap);
        rt_cap = NULL;
    }

    event_audio_free();

    printf("[RT] Realtime monitoring stopped\n");
    return 0;
}
