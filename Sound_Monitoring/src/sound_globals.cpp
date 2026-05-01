#include "sound_globals.h"

// ========== 异常事件 ==========
AnomalyEvent events[MAX_ANOMALY_EVENTS] = {};
int event_count = 0;
AnomalyEvent current_event = {};
int has_current_event = 0;

// ========== 模型上下文 ==========
int model_initialized = 0;
rknn_app_context_t rknn_app_ctx = {};
LabelEntry labels[LABEL_NUM] = {};
char model_path[512] = "./model/yamnet.rknn";
char label_path[512] = "./model/yamnet_class_map.txt";

// ========== ASR (Vosk) ==========
VoskAsrEngine g_asr_engine;
int g_asr_enabled = 0;
char g_asr_model_cn[512] = {0};
char g_asr_model_en[512] = {0};
pthread_mutex_t g_asr_mutex = PTHREAD_MUTEX_INITIALIZER;
int g_rt_transcript_dump_idx = 0;
int g_rt_print_asr = 1;
int g_rt_print_window = 1;
int g_auto_start_realtime = 1;

// ========== 紧急关键词唤醒 ==========
int g_emergency_kws_auto_start = 0;
int g_emergency_kws_report_enabled = 1;
int g_emergency_kws_running = 0;
pid_t g_emergency_kws_pid = -1;
pthread_mutex_t g_emergency_kws_mutex = PTHREAD_MUTEX_INITIALIZER;
char g_emergency_kws_cmd[512] = "./wake/emergency_monitor";
char g_emergency_kws_workdir[512] = "./wake";
char g_emergency_kws_log_path[512] = "./wake/emergency_log.txt";
char g_emergency_kws_pid_file[512] = "/tmp/emergency_monitor.pid";
char g_emergency_kws_stdout_path[512] = "/tmp/emergency_monitor.out";
pthread_t g_emergency_kws_report_thread;
pthread_mutex_t g_emergency_kws_report_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int g_emergency_kws_report_running = 0;
long g_emergency_kws_report_offset = 0;
int g_emergency_kws_audio_count = 0;

// ========== 配置 ==========
ServerConfig config = {
    .anomaly_threshold = 0.1f,
    .save_anomaly = 0
};

// ========== SoX / FFmpeg / 降噪配置 ==========
int g_sox_denoise_enabled = 0;
float g_sox_denoise_amount = 0.25f;
char g_sox_bin[256] = "sox";
char g_sox_denoise_profile[512] = {0};
float g_rt_capture_volume = 1.0f;
int g_rt_pulse_set_default_source = 0;
char g_rt_pulse_source_volume[32] = "";
int g_rt_amixer_enabled = 0;
char g_rt_amixer_card[32] = "5";
char g_rt_amixer_control[64] = "Mic";
char g_rt_amixer_volume[32] = "60%";
char g_rt_amixer_auto_gain_control[32] = "";
int g_rt_ffmpeg_filter_enabled = 0;
char g_rt_ffmpeg_bin[256] = "ffmpeg";
char g_rt_ffmpeg_audio_filter[512] =
    "afftdn=nf=-40,highpass=f=100,lowpass=f=3500,volume=6.0";
char g_runtime_audio_config_path[512] = {0};

// ========== 实时监测状态 ==========
char g_rt_default_device[256] = DEFAULT_RT_DEVICE;
char g_rt_fallback_device[256] = FALLBACK_RT_DEVICE;

volatile int rt_running = 0;
volatile int rt_active = 0;
pthread_t rt_thread;
pthread_mutex_t rt_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t rt_cond = PTHREAD_COND_INITIALIZER;

RealtimeEvent rt_event_queue[RT_EVENT_QUEUE_SIZE] = {};
volatile int rt_event_head = 0;
volatile int rt_event_tail = 0;
volatile int rt_event_count = 0;

RealtimeWindowStatus rt_window_queue[RT_WINDOW_QUEUE_SIZE] = {};
volatile int rt_window_head = 0;
volatile int rt_window_tail = 0;
volatile int rt_window_count = 0;

RtCurrentEvent rt_current_event = {0};

RingBuffer rt_ring = {0};
audio_capture_t *rt_cap = NULL;
char rt_device[256] = DEFAULT_RT_DEVICE;
int rt_sample_rate = 16000;

ResultEntry rt_result[TOP_N] = {};
LabelEntry rt_labels[LABEL_NUM] = {};
int rt_total_chunks = 0;
double rt_session_start = 0;
int rt_anomaly_count = 0;
char rt_save_dir[256] = "./alarm_audio";
double rt_last_asr_emit_ts = -1.0;

// ========== HTTP 服务器 ==========
int server_socket = -1;

// ========== 异常音频保存目录 ==========
char anomaly_save_dir[256] = "./anomaly_audio";
