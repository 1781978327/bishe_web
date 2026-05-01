#pragma once

#include "sound_globals.h"

// ========== 事件音频存储 ==========
#define RT_MAX_EVENT_AUDIO_SEC RT_EVENT_CLIP_SEC  // 每个事件固定保存 6 秒（前3秒 + 后3秒）
#define RT_MAX_EVENTS_STORED 100

typedef struct {
    float *data;
    int capacity;    // 最大帧数
    int frames;      // 当前帧数
} EventAudio;

extern EventAudio g_event_audio;
extern int rt_event_audio_count;

void event_audio_init();
void event_audio_append_limited(const float *chunk, int chunk_frames);
void event_audio_append_silence(int chunk_frames);
void event_audio_reset();
void event_audio_free();

double rt_get_timestamp();
void rt_add_keyword(char keywords[][64], int *count, const char *kw);
void rt_push_event(float start_sec, float end_sec, float max_score);
void rt_push_window_status(int window_id,
                            float start_sec,
                            float end_sec,
                            int anomaly,
                            const char *matched_kw,
                            float matched_score,
                            const ResultEntry *results,
                            int result_count);
void rt_reset_current_event();

void rt_ring_init(RingBuffer *rb, int sample_rate, int sec);
void rt_ring_push(RingBuffer *rb, float *data, int frames);
void rt_ring_get_latest(RingBuffer *rb, float *out, int frames);
void rt_ring_free(RingBuffer *rb);

int save_recent_audio_for_emergency_kws(char *audio_path,
                                         size_t audio_path_size,
                                         float *duration_out);

void event_audio_capture_pre_roll(RingBuffer *rb, int pre_frames);
void rt_finalize_current_event(int pad_missing_post_roll);
