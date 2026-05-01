#pragma once

#include <pthread.h>
#include <sys/types.h>

#include "yamnet.h"
#include "asr_vosk.h"
#include "audio_capture.h"

// ========== Spring Boot 上报配置 ==========
#define SPRING_BOOT_URL "http://localhost:8080/api/detection/record/sound/report"
#define SPRING_BOOT_TIMEOUT_MS 5000

// ========== 异常事件结构 ==========
#define MAX_ANOMALY_EVENTS 100
#define HOP_LENGTH (CHUNK_LENGTH * SAMPLE_RATE / 2)
#define MERGE_GAP_SEC 2.0f

typedef struct {
    float start_sec;
    float end_sec;
    char keywords[10][64];
    int keyword_count;
    float max_score;
    int chunk_count;
} AnomalyEvent;

extern AnomalyEvent events[MAX_ANOMALY_EVENTS];
extern int event_count;
extern AnomalyEvent current_event;
extern int has_current_event;

// ========== 模型上下文 ==========
extern int model_initialized;
extern rknn_app_context_t rknn_app_ctx;
extern LabelEntry labels[LABEL_NUM];
extern char model_path[512];
extern char label_path[512];

// ========== ASR (Vosk) ==========
extern VoskAsrEngine g_asr_engine;
extern int g_asr_enabled;
extern char g_asr_model_cn[512];
extern char g_asr_model_en[512];
extern pthread_mutex_t g_asr_mutex;
extern int g_rt_transcript_dump_idx;
extern int g_rt_print_asr;
extern int g_rt_print_window;
extern int g_auto_start_realtime;

// ========== 紧急关键词唤醒 ==========
extern int g_emergency_kws_auto_start;
extern int g_emergency_kws_report_enabled;
extern int g_emergency_kws_running;
extern pid_t g_emergency_kws_pid;
extern pthread_mutex_t g_emergency_kws_mutex;
extern char g_emergency_kws_cmd[512];
extern char g_emergency_kws_workdir[512];
extern char g_emergency_kws_log_path[512];
extern char g_emergency_kws_pid_file[512];
extern char g_emergency_kws_stdout_path[512];
extern pthread_t g_emergency_kws_report_thread;
extern pthread_mutex_t g_emergency_kws_report_mutex;
extern volatile int g_emergency_kws_report_running;
extern long g_emergency_kws_report_offset;
extern int g_emergency_kws_audio_count;

// ========== 配置 ==========
typedef struct {
    float anomaly_threshold;
    int save_anomaly;
} ServerConfig;

extern ServerConfig config;

// ========== SoX / FFmpeg / 降噪配置 ==========
extern int g_sox_denoise_enabled;
extern float g_sox_denoise_amount;
extern char g_sox_bin[256];
extern char g_sox_denoise_profile[512];
extern float g_rt_capture_volume;
extern int g_rt_pulse_set_default_source;
extern char g_rt_pulse_source_volume[32];
extern int g_rt_amixer_enabled;
extern char g_rt_amixer_card[32];
extern char g_rt_amixer_control[64];
extern char g_rt_amixer_volume[32];
extern char g_rt_amixer_auto_gain_control[32];
extern int g_rt_ffmpeg_filter_enabled;
extern char g_rt_ffmpeg_bin[256];
extern char g_rt_ffmpeg_audio_filter[512];
extern char g_runtime_audio_config_path[512];

// ========== 实时监测状态 ==========
#define RT_RING_BUFFER_SEC 120
#define RT_TRANSCRIPT_MAX_SEC 120
#define RT_TRANSCRIPT_DEFAULT_SEC 120
#define DEFAULT_RT_DEVICE "parec"
#define FALLBACK_RT_DEVICE "parec:alsa_input.usb-Web_Camera_Web_Camera_202409021440-02.mono-fallback.2"
#define RT_CHUNK_DURATION_SEC 3
#define RT_HOP_SEC 1.5
#define RT_DETECTION_THRESHOLD 0.05
#define RT_REPORT_MIN_CONFIDENCE 0.15f
#define RT_EVENT_PRE_SEC 3
#define RT_EVENT_POST_SEC 3
#define RT_EVENT_CLIP_SEC (RT_EVENT_PRE_SEC + RT_EVENT_POST_SEC)
#define RT_MAX_EVENTS 100
#define RT_MAX_KEYWORDS 10
#define RT_EVENT_QUEUE_SIZE 50
#define RT_WINDOW_QUEUE_SIZE 50

extern char g_rt_default_device[256];
extern char g_rt_fallback_device[256];

typedef struct {
    int event_id;
    float start_sec;
    float end_sec;
    float duration;
    float max_score;
    char keywords[RT_MAX_KEYWORDS][64];
    int keyword_count;
    int trigger_count;
    char timestamp[64];
} RealtimeEvent;

typedef struct {
    int window_id;
    float start_sec;
    float end_sec;
    int anomaly;
    float matched_score;
    char matched_keyword[64];
    char top_summary[256];
    char timestamp[64];
} RealtimeWindowStatus;

extern volatile int rt_running;
extern volatile int rt_active;
extern pthread_t rt_thread;
extern pthread_mutex_t rt_mutex;
extern pthread_cond_t rt_cond;

extern RealtimeEvent rt_event_queue[RT_EVENT_QUEUE_SIZE];
extern volatile int rt_event_head;
extern volatile int rt_event_tail;
extern volatile int rt_event_count;

extern RealtimeWindowStatus rt_window_queue[RT_WINDOW_QUEUE_SIZE];
extern volatile int rt_window_head;
extern volatile int rt_window_tail;
extern volatile int rt_window_count;

// 当前正在构建的事件
extern struct RtCurrentEvent {
    int active;
    float start_sec;
    float trigger_sec;
    float end_sec;
    char keywords[RT_MAX_KEYWORDS][64];
    int keyword_count;
    float max_score;
    int trigger_count;
    int post_frames_collected;
    int post_frames_target;
    int skip_next_chunk_append;
} rt_current_event;

// 环形缓冲区
typedef struct {
    float *data;
    int capacity;
    int size;
    int write_idx;
} RingBuffer;

extern RingBuffer rt_ring;
extern audio_capture_t *rt_cap;
extern char rt_device[256];
extern int rt_sample_rate;

// 实时线程本地数据
extern ResultEntry rt_result[TOP_N];
extern LabelEntry rt_labels[LABEL_NUM];
extern int rt_total_chunks;
extern double rt_session_start;
extern int rt_anomaly_count;
extern char rt_save_dir[256];
extern double rt_last_asr_emit_ts;

// HTTP 服务器
extern int server_socket;

// 异常音频保存目录
extern char anomaly_save_dir[256];
