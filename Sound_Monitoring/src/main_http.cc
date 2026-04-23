/**
 * 声音异常检测 HTTP 服务器
 *
 * 提供 REST API 供 Spring Boot 调用 YAMNet 音频异常检测
 *
 * 编译:
 *   cd src/build && cmake .. && make
 *
 * 运行:
 *   sudo ./rknn_yamnet_demo_http 8089
 *
 * API:
 *   POST /analyze            - 分析音频文件 (Content-Type: application/json)
 *                             Body: {"audio_path": "/path/to/audio.wav"}
 *                             Response: {"success": true, "events": [...], "event_count": N}
 *
 *   POST /analyze/upload     - 上传二进制音频进行推理（支持 MP3/WAV/OGG 等格式）
 *                             Content-Type: multipart/form-data 或 application/octet-stream
 *                             Body (multipart): field "audio" = 文件数据
 *                             Body (raw):       原始音频二进制数据
 *                             Response: {"success": true, "events": [...], "saved": true/false}
 *
 *   POST /realtime/start     - 启动实时麦克风监测
 *                             Body: {"device": "parec"} (可选)
 *                             Response: {"success": true, "message": "started"}
 *
 *   POST /realtime/stop     - 停止实时监测
 *                             Response: {"success": true, "message": "stopped"}
 *
 *   GET  /realtime/status   - 查询实时监测状态
 *                             Response: {"running": true, "device": "parec"}
 *
 *   GET  /realtime/events   - 获取累积的异常事件
 *                             Response: {"events": [...], "count": N}
 *
 *   GET  /realtime/windows?limit=5
 *                             - 获取最近 N 个实时检测窗口状态（不限于异常）
 *                             Response: {"windows": [...], "count": N}
 *
 *   GET  /realtime/transcript?seconds=120
 *                             - 获取最近 N 秒（最大120秒）的麦克风转写文本
 *                             Response: {"success": true, "transcript": "...", "asr_lang": "cn"}
 *
 *   POST /wake/start         - 启动紧急关键词唤醒监测（复用 emergency_monitor）
 *   POST /wake/stop          - 停止紧急关键词唤醒监测
 *   GET  /wake/status        - 查询唤醒监测状态
 *   GET  /wake/events?limit=20
 *                             - 获取最近唤醒日志（读取 emergency_log.txt）
 *
 *   GET /health             - 健康检查
 *   GET /config             - 获取当前配置
 *   PUT /config             - 更新配置 (threshold, keywords)
 *
 * 音频格式要求:
 *   - YAMNet 要求: WAV/FLAC 格式，16kHz 采样率，单声道，16-bit PCM
 *   - MP3/OGG/AAC 等格式会在上传后自动通过 ffmpeg 转换为 WAV
 *   - 非 16kHz 音频会自动重采样
 *   - 异常音频会自动保存到 ./anomaly_audio/ 目录
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <sstream>
#include <string>

#include "yamnet.h"
#include "audio_utils.h"
#include "audio_capture.h"
#include "process.h"
#include "asr_vosk.h"

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

static AnomalyEvent events[MAX_ANOMALY_EVENTS];
static int event_count = 0;
static AnomalyEvent current_event;
static int has_current_event = 0;

// ========== 模型上下文（全局初始化一次）==========
static int model_initialized = 0;
static rknn_app_context_t rknn_app_ctx;
static LabelEntry labels[LABEL_NUM];
static char model_path[512] = "./model/yamnet.rknn";
static char label_path[512] = "./model/yamnet_class_map.txt";

// ========== ASR (Vosk) ==========
static VoskAsrEngine g_asr_engine;
static int g_asr_enabled = 0;
static char g_asr_model_cn[512] = {0};
static char g_asr_model_en[512] = {0};
static pthread_mutex_t g_asr_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_rt_transcript_dump_idx = 0;
static int g_rt_print_asr = 1;
static int g_rt_print_window = 1;
static int g_auto_start_realtime = 1;

// ========== 紧急关键词唤醒（复用外部 C++ 程序） ==========
static int g_emergency_kws_auto_start = 0;
static int g_emergency_kws_report_enabled = 1;
static int g_emergency_kws_running = 0;
static pid_t g_emergency_kws_pid = -1;
static pthread_mutex_t g_emergency_kws_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_emergency_kws_cmd[512] = "./wake/emergency_monitor";
static char g_emergency_kws_workdir[512] = "./wake";
static char g_emergency_kws_log_path[512] = "./wake/emergency_log.txt";
static char g_emergency_kws_pid_file[512] = "/tmp/emergency_monitor.pid";
static char g_emergency_kws_stdout_path[512] = "/tmp/emergency_monitor.out";
static pthread_t g_emergency_kws_report_thread;
static pthread_mutex_t g_emergency_kws_report_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_emergency_kws_report_running = 0;
static long g_emergency_kws_report_offset = 0;
static int g_emergency_kws_audio_count = 0;

// ========== 配置 ==========
typedef struct {
    float anomaly_threshold;
    int save_anomaly;
} ServerConfig;

static ServerConfig config = {
    .anomaly_threshold = 0.1f,
    .save_anomaly = 0
};

// ========== SoX 降噪配置 ==========
static int g_sox_denoise_enabled = 0;
static float g_sox_denoise_amount = 0.25f;
static char g_sox_bin[256] = "sox";
static char g_sox_denoise_profile[512] = {0};
static float g_rt_capture_volume = 1.0f;
static int g_rt_pulse_set_default_source = 0;
static char g_rt_pulse_source_volume[32] = "";
static int g_rt_amixer_enabled = 0;
static char g_rt_amixer_card[32] = "5";
static char g_rt_amixer_control[64] = "Mic";
static char g_rt_amixer_volume[32] = "60%";
static char g_rt_amixer_auto_gain_control[32] = "";
static int g_rt_ffmpeg_filter_enabled = 0;
static char g_rt_ffmpeg_bin[256] = "ffmpeg";
static char g_rt_ffmpeg_audio_filter[512] =
    "afftdn=nf=-40,highpass=f=100,lowpass=f=3500,volume=6.0";
static char g_runtime_audio_config_path[512] = {0};

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

static char g_rt_default_device[256] = DEFAULT_RT_DEVICE;
static char g_rt_fallback_device[256] = FALLBACK_RT_DEVICE;

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

static volatile int rt_running = 0;
static volatile int rt_active = 0;
static pthread_t rt_thread;
static pthread_mutex_t rt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rt_cond = PTHREAD_COND_INITIALIZER;

// 实时事件队列（生产者写入，消费者HTTP读取）
static RealtimeEvent rt_event_queue[RT_EVENT_QUEUE_SIZE];
static volatile int rt_event_head = 0;  // 写入位置
static volatile int rt_event_tail = 0;   // 读取位置
static volatile int rt_event_count = 0;  // 当前队列事件数

static RealtimeWindowStatus rt_window_queue[RT_WINDOW_QUEUE_SIZE];
static volatile int rt_window_head = 0;
static volatile int rt_window_tail = 0;
static volatile int rt_window_count = 0;

// 当前正在构建的事件
static struct {
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
} rt_current_event = {0};

// 环形缓冲区
typedef struct {
    float *data;
    int capacity;
    int size;
    int write_idx;
} RingBuffer;

static RingBuffer rt_ring = {0};
static audio_capture_t *rt_cap = NULL;
static char rt_device[256] = DEFAULT_RT_DEVICE;
static int rt_sample_rate = 16000;

// 实时线程本地数据（避免全局混乱）
static ResultEntry rt_result[TOP_N];
static LabelEntry rt_labels[LABEL_NUM];
static int rt_total_chunks = 0;
static double rt_session_start = 0;
static int rt_anomaly_count = 0;
static char rt_save_dir[256] = "./alarm_audio";
static double rt_last_asr_emit_ts = -1.0;

// ========== 前向声明 ==========
static void json_escape_string(const char* input, char* output, size_t output_size);
static AsrResult run_asr_locked(const float *data, int num_frames);
static int should_log_http_request(const char *method, const char *path);
static std::string summarize_top_results(const ResultEntry *results, int result_count, int max_items);
static void init_runtime_audio_yaml_config();
static void apply_rt_capture_pulse_settings(const char *device);
static void apply_rt_capture_mixer(const char *device);
static void init_rt_ffmpeg_filter_config();
static int ffmpeg_rt_filter_is_ready();
static int ffmpeg_filter_wav_inplace(const char *wav_path,
                                     int sample_rate,
                                     int num_channels,
                                     const char *context);
static int ffmpeg_filter_buffer_inplace(float *data,
                                        int num_frames,
                                        int sample_rate,
                                        int num_channels,
                                        const char *context);
static int sox_denoise_is_ready();
static int sox_denoise_wav_inplace(const char *wav_path, const char *context);
static int sox_denoise_buffer_inplace(float *data,
                                      int num_frames,
                                      int sample_rate,
                                      int num_channels,
                                      const char *context);
static void init_emergency_kws_config();
static int emergency_kws_start();
static int emergency_kws_stop();
static void emergency_kws_refresh_state();
static int emergency_kws_reporter_start();
static int emergency_kws_reporter_stop();
static int save_recent_audio_for_emergency_kws(char *audio_path,
                                               size_t audio_path_size,
                                               float *duration_out);
static void parse_emergency_log_line(const std::string &line,
                                     std::string *timestamp,
                                     std::string *keyword);

// ========== Spring Boot 上报函数 ==========
static int report_to_spring_boot_with_result(const char *audio_path, float duration,
                                             const char *keywords, float confidence,
                                             const char *result_prefix) {
    // 构建JSON请求体
    char json_body[2048];
    char detection_result[1024];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", tm_info);

    // JSON转义关键词
    char escaped_keywords[1024] = {0};
    json_escape_string(keywords, escaped_keywords, sizeof(escaped_keywords));

    if (confidence >= 0.0f) {
        snprintf(detection_result, sizeof(detection_result),
                 "%s - %s (置信度: %.2f%%)",
                 result_prefix && result_prefix[0] ? result_prefix : "声音异常",
                 escaped_keywords,
                 confidence * 100.0f);
    } else {
        snprintf(detection_result, sizeof(detection_result),
                 "%s - %s",
                 result_prefix && result_prefix[0] ? result_prefix : "声音异常",
                 escaped_keywords);
    }

    snprintf(json_body, sizeof(json_body),
        "{"
        "\"cameraId\": -1,"
        "\"cameraName\": \"声音监测\","
        "\"detectionTime\": \"%s\","
        "\"detectionResult\": \"%s\","
        "\"audioUrl\": \"%s\","
        "\"audioDuration\": %.1f,"
        "\"soundKeywords\": \"%s\""
        "}",
        time_str,
        detection_result,
        audio_path ? audio_path : "",
        duration,
        escaped_keywords);

    printf("[REPORT] Sending to Spring Boot: %s\n", json_body);

    // 创建socket连接
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("[REPORT ERROR] Failed to create socket\n");
        return -1;
    }

    struct timeval timeout;
    timeout.tv_sec = SPRING_BOOT_TIMEOUT_MS / 1000;
    timeout.tv_usec = (SPRING_BOOT_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct hostent *server = gethostbyname("localhost");
    if (server == NULL) {
        printf("[REPORT ERROR] Failed to resolve localhost\n");
        close(sock);
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(8080);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("[REPORT ERROR] Failed to connect to Spring Boot\n");
        close(sock);
        return -1;
    }

    // 构建HTTP请求
    char http_request[4096];
    int body_len = strlen(json_body);
    int req_len = snprintf(http_request, sizeof(http_request),
        "POST /api/detection/record/sound/report HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        body_len, json_body);

    // 发送请求
    int sent = send(sock, http_request, req_len, 0);
    if (sent < 0) {
        printf("[REPORT ERROR] Failed to send request\n");
        close(sock);
        return -1;
    }

    // 读取响应
    char response[1024];
    int received = recv(sock, response, sizeof(response) - 1, 0);
    close(sock);

    if (received > 0) {
        response[received] = '\0';
        // 检查HTTP状态码
        if (strstr(response, "200 OK") || strstr(response, "\"code\":200")) {
            printf("[REPORT] Successfully reported to Spring Boot\n");
            return 0;
        } else {
            printf("[REPORT WARNING] Unexpected response: %.100s\n", response);
            return -1;
        }
    }

    printf("[REPORT ERROR] No response from Spring Boot\n");
    return -1;
}

static int report_to_spring_boot(const char *audio_path, float duration,
                                  const char *keywords, float confidence) {
    return report_to_spring_boot_with_result(audio_path, duration, keywords, confidence, "声音异常");
}

// ========== 事件音频存储 ==========
#define RT_MAX_EVENT_AUDIO_SEC RT_EVENT_CLIP_SEC  // 每个事件固定保存 6 秒（前3秒 + 后3秒）
#define RT_MAX_EVENTS_STORED 100
static int rt_event_audio_count = 0;

typedef struct {
    float *data;
    int capacity;    // 最大帧数
    int frames;      // 当前帧数
} EventAudio;

static EventAudio g_event_audio = {0};

static void event_audio_init() {
    g_event_audio.capacity = rt_sample_rate * RT_MAX_EVENT_AUDIO_SEC;
    g_event_audio.data = (float *)calloc(g_event_audio.capacity, sizeof(float));
    g_event_audio.frames = 0;
}

static void event_audio_append_limited(const float *chunk, int chunk_frames) {
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

static void event_audio_append_silence(int chunk_frames) {
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

static void event_audio_reset() {
    g_event_audio.frames = 0;
}

static void event_audio_free() {
    if (g_event_audio.data) { free(g_event_audio.data); g_event_audio.data = NULL; }
}

static double rt_get_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void rt_add_keyword(char keywords[][64], int *count, const char *kw) {
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

static void rt_push_event(float start_sec, float end_sec, float max_score) {
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

static void rt_push_window_status(int window_id,
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

static void rt_reset_current_event() {
    memset(&rt_current_event, 0, sizeof(rt_current_event));
}

static void rt_ring_init(RingBuffer *rb, int sample_rate, int sec) {
    rb->capacity = sample_rate * sec;
    rb->data = (float *)calloc(rb->capacity, sizeof(float));
    rb->size = 0;
    rb->write_idx = 0;
}

static void rt_ring_push(RingBuffer *rb, float *data, int frames) {
    for (int i = 0; i < frames; i++) {
        rb->data[rb->write_idx] = data[i];
        rb->write_idx = (rb->write_idx + 1) % rb->capacity;
        if (rb->size < rb->capacity) rb->size++;
    }
}

static void rt_ring_get_latest(RingBuffer *rb, float *out, int frames) {
    if (frames > rb->size) frames = rb->size;
    int start_idx = (rb->write_idx - frames + rb->capacity) % rb->capacity;
    for (int i = 0; i < frames; i++) {
        out[i] = rb->data[(start_idx + i) % rb->capacity];
    }
}

static void rt_ring_free(RingBuffer *rb) {
    if (rb->data) { free(rb->data); rb->data = NULL; }
    rb->capacity = 0;
    rb->size = 0;
    rb->write_idx = 0;
}

static int save_recent_audio_for_emergency_kws(char *audio_path,
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

static void event_audio_capture_pre_roll(RingBuffer *rb, int pre_frames) {
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

static void rt_finalize_current_event(int pad_missing_post_roll) {
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

static void *rt_monitor_thread(void *arg) {
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

static int rt_start(const char *device) {
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

static int rt_stop() {
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

// ========== HTTP 服务器 ==========
static int server_socket = -1;

static void json_escape_string(const char* input, char* output, size_t output_size) {
    size_t j = 0;
    if (output_size == 0) return;

    for (size_t i = 0; input[i] != '\0' && j + 1 < output_size; i++) {
        char c = input[i];

        if ((c == '"' || c == '\\') && j + 2 < output_size) {
            output[j++] = '\\';
            output[j++] = c;
        } else if (c == '\n' && j + 2 < output_size) {
            output[j++] = '\\';
            output[j++] = 'n';
        } else if (c == '\r' && j + 2 < output_size) {
            output[j++] = '\\';
            output[j++] = 'r';
        } else if (c == '\t' && j + 2 < output_size) {
            output[j++] = '\\';
            output[j++] = 't';
        } else if ((unsigned char)c >= 0x20) {
            output[j++] = c;
        }
    }

    output[j] = '\0';
}

static int parse_content_length(const char* headers) {
    const char* line = headers;

    while (line && *line) {
        const char* line_end = strstr(line, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);

        if (line_len == 0) {
            break;
        }

        if (line_len >= 15 && strncasecmp(line, "Content-Length:", 15) == 0) {
            const char* value = line + 15;
            while (*value == ' ' || *value == '\t') value++;
            return atoi(value);
        }

        if (!line_end) break;
        line = line_end + 2;
    }

    return 0;
}

static int path_exists(const char *path) {
    if (!path || path[0] == '\0') return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

static int parse_bool_text_local(const char *value, int default_value) {
    if (!value || value[0] == '\0') return default_value;
    if (strcmp(value, "1") == 0 ||
        strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0) {
        return 1;
    }
    if (strcmp(value, "0") == 0 ||
        strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0) {
        return 0;
    }
    return default_value;
}

static std::string trim_copy(const std::string &value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

static std::string strip_yaml_comment(const std::string &line) {
    bool in_single = false;
    bool in_double = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (c == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (c == '#' && !in_single && !in_double) {
            return trim_copy(line.substr(0, i));
        }
    }
    return trim_copy(line);
}

static std::string unquote_yaml_value(const std::string &value) {
    std::string trimmed = trim_copy(value);
    if (trimmed == "null" || trimmed == "~") return "";
    if (trimmed.size() >= 2) {
        char first = trimmed.front();
        char last = trimmed.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return trimmed.substr(1, trimmed.size() - 2);
        }
    }
    return trimmed;
}

static void copy_string_value(char *dest, size_t dest_size, const std::string &value) {
    if (!dest || dest_size == 0) return;
    strncpy(dest, value.c_str(), dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static void apply_runtime_audio_yaml_value(const std::string &key_path, const std::string &raw_value) {
    std::string value = unquote_yaml_value(raw_value);

    if (key_path == "realtime.device") {
        copy_string_value(g_rt_default_device, sizeof(g_rt_default_device), value);
    } else if (key_path == "realtime.fallback_device") {
        copy_string_value(g_rt_fallback_device, sizeof(g_rt_fallback_device), value);
    } else if (key_path == "realtime.auto_start") {
        g_auto_start_realtime = parse_bool_text_local(value.c_str(), g_auto_start_realtime);
    } else if (key_path == "realtime.print_asr") {
        g_rt_print_asr = parse_bool_text_local(value.c_str(), g_rt_print_asr);
    } else if (key_path == "realtime.print_window") {
        g_rt_print_window = parse_bool_text_local(value.c_str(), g_rt_print_window);
    } else if (key_path == "realtime.capture_volume") {
        float capture_volume = (float)atof(value.c_str());
        if (capture_volume > 2.0f && capture_volume <= 200.0f) {
            capture_volume /= 100.0f;
        }
        if (capture_volume >= 0.0f && capture_volume <= 2.0f) {
            g_rt_capture_volume = capture_volume;
        }
    } else if (key_path == "realtime.pulse.set_default_source") {
        g_rt_pulse_set_default_source = parse_bool_text_local(value.c_str(), g_rt_pulse_set_default_source);
    } else if (key_path == "realtime.pulse.source_volume") {
        copy_string_value(g_rt_pulse_source_volume, sizeof(g_rt_pulse_source_volume), value);
    } else if (key_path == "realtime.mixer.enabled") {
        g_rt_amixer_enabled = parse_bool_text_local(value.c_str(), g_rt_amixer_enabled);
    } else if (key_path == "realtime.mixer.card") {
        copy_string_value(g_rt_amixer_card, sizeof(g_rt_amixer_card), value);
    } else if (key_path == "realtime.mixer.control") {
        copy_string_value(g_rt_amixer_control, sizeof(g_rt_amixer_control), value);
    } else if (key_path == "realtime.mixer.volume") {
        copy_string_value(g_rt_amixer_volume, sizeof(g_rt_amixer_volume), value);
    } else if (key_path == "realtime.mixer.auto_gain_control") {
        copy_string_value(g_rt_amixer_auto_gain_control, sizeof(g_rt_amixer_auto_gain_control), value);
    } else if (key_path == "realtime.filter.ffmpeg.enabled") {
        g_rt_ffmpeg_filter_enabled = parse_bool_text_local(value.c_str(), g_rt_ffmpeg_filter_enabled);
    } else if (key_path == "realtime.filter.ffmpeg.bin") {
        copy_string_value(g_rt_ffmpeg_bin, sizeof(g_rt_ffmpeg_bin), value);
    } else if (key_path == "realtime.filter.ffmpeg.audio_filter") {
        copy_string_value(g_rt_ffmpeg_audio_filter, sizeof(g_rt_ffmpeg_audio_filter), value);
    } else if (key_path == "realtime.filter.sox.enabled") {
        g_sox_denoise_enabled = parse_bool_text_local(value.c_str(), g_sox_denoise_enabled);
    } else if (key_path == "realtime.filter.sox.bin") {
        copy_string_value(g_sox_bin, sizeof(g_sox_bin), value);
    } else if (key_path == "realtime.filter.sox.profile") {
        copy_string_value(g_sox_denoise_profile, sizeof(g_sox_denoise_profile), value);
    } else if (key_path == "realtime.filter.sox.amount") {
        float sox_amount = (float)atof(value.c_str());
        if (sox_amount > 0.0f && sox_amount <= 1.0f) {
            g_sox_denoise_amount = sox_amount;
        }
    }
}

static int resolve_runtime_audio_config_path(char *path_out, size_t path_size) {
    if (!path_out || path_size == 0) return -1;

    const char *env_path = getenv("SOUND_MONITORING_CONFIG");
    const char *candidates[] = {
        "./config/runtime_audio.yaml",
        "../config/runtime_audio.yaml",
        "../../config/runtime_audio.yaml",
        NULL
    };

    if (env_path && env_path[0]) {
        std::ifstream env_file(env_path);
        if (env_file.good()) {
            copy_string_value(path_out, path_size, env_path);
            return 0;
        }
        printf("[CONFIG] SOUND_MONITORING_CONFIG not found: %s\n", env_path);
    }

    for (int i = 0; candidates[i] != NULL; ++i) {
        const char *candidate = candidates[i];
        std::ifstream candidate_file(candidate);
        if (candidate_file.good()) {
            copy_string_value(path_out, path_size, candidate);
            return 0;
        }
    }

    path_out[0] = '\0';
    return -1;
}

static void init_runtime_audio_yaml_config() {
    char config_path[512] = {0};
    if (resolve_runtime_audio_config_path(config_path, sizeof(config_path)) != 0) {
        printf("[CONFIG] runtime_audio.yaml not found, use built-in defaults\n");
        return;
    }

    std::ifstream in(config_path);
    if (!in.is_open()) {
        printf("[CONFIG] failed to open runtime audio config: %s\n", config_path);
        return;
    }

    copy_string_value(g_runtime_audio_config_path, sizeof(g_runtime_audio_config_path), config_path);

    std::vector<std::string> sections;
    std::string raw_line;
    while (std::getline(in, raw_line)) {
        std::string line = strip_yaml_comment(raw_line);
        if (line.empty()) continue;

        size_t non_space = raw_line.find_first_not_of(" \t");
        if (non_space == std::string::npos) continue;

        int indent = 0;
        for (size_t i = 0; i < raw_line.size(); ++i) {
            if (raw_line[i] == ' ') {
                indent += 1;
            } else if (raw_line[i] == '\t') {
                indent += 2;
            } else {
                break;
            }
        }

        std::string working = raw_line.substr(non_space);
        working = strip_yaml_comment(working);
        if (working.empty() || working[0] == '-') continue;

        size_t colon = working.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim_copy(working.substr(0, colon));
        std::string value = trim_copy(working.substr(colon + 1));
        int level = indent / 2;

        if ((int)sections.size() > level) {
            sections.resize(level);
        }

        if (value.empty()) {
            if ((int)sections.size() == level) {
                sections.push_back(key);
            } else if ((int)sections.size() > level) {
                sections[level] = key;
            }
            continue;
        }

        std::string key_path;
        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i].empty()) continue;
            if (!key_path.empty()) key_path += ".";
            key_path += sections[i];
        }
        if (!key_path.empty()) key_path += ".";
        key_path += key;

        apply_runtime_audio_yaml_value(key_path, value);
    }

    copy_string_value(rt_device, sizeof(rt_device), g_rt_default_device);
    printf("[CONFIG] loaded runtime audio config: %s\n", g_runtime_audio_config_path);
    printf("[CONFIG] realtime defaults: device=%s, mixer=%s, ffmpeg=%s, sox=%s\n",
           g_rt_default_device,
           g_rt_amixer_enabled ? "on" : "off",
           g_rt_ffmpeg_filter_enabled ? "on" : "off",
           g_sox_denoise_enabled ? "on" : "off");
}

static std::string shell_quote_single(const char *input) {
    std::string out = "'";
    if (input) {
        for (const char *p = input; *p; ++p) {
            if (*p == '\'') {
                out += "'\\''";
            } else {
                out.push_back(*p);
            }
        }
    }
    out += "'";
    return out;
}

static std::string run_command_capture(const std::string &cmd) {
    std::string output;
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return output;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        output += buffer;
    }
    pclose(pipe);
    return output;
}

static std::string resolve_default_pulse_source_name() {
    std::string info = run_command_capture("pactl info 2>/dev/null");
    std::istringstream iss(info);
    std::string line;
    while (std::getline(iss, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.rfind("Default Source:", 0) == 0) {
            return trim_copy(trimmed.substr(strlen("Default Source:")));
        }
        if (trimmed.rfind("默认信源：", 0) == 0) {
            return trim_copy(trimmed.substr(strlen("默认信源：")));
        }
        if (trimmed.rfind("默认信源:", 0) == 0) {
            return trim_copy(trimmed.substr(strlen("默认信源:")));
        }
    }
    return "";
}

static std::string extract_pulse_source_name_from_device(const char *device) {
    if (!device || device[0] == '\0') {
        return "";
    }
    if (strncmp(device, "parec:", 6) == 0) {
        return std::string(device + 6);
    }
    if (strncmp(device, "pulse:", 6) == 0) {
        return std::string(device + 6);
    }
    if (strcmp(device, "parec") == 0 || strcmp(device, "pulse") == 0) {
        return resolve_default_pulse_source_name();
    }
    return "";
}

static int resolve_alsa_card_from_pulse_source(const char *source_name, char *card_out, size_t card_out_size) {
    if (!source_name || source_name[0] == '\0' || !card_out || card_out_size == 0) {
        return -1;
    }

    std::string sources = run_command_capture("pactl list sources 2>/dev/null");
    std::istringstream iss(sources);
    std::string line;
    bool in_target = false;

    while (std::getline(iss, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.rfind("Source #", 0) == 0 || trimmed.rfind("信源 #", 0) == 0) {
            in_target = false;
            continue;
        }

        if (trimmed.rfind("Name:", 0) == 0) {
            in_target = trim_copy(trimmed.substr(strlen("Name:"))) == source_name;
            continue;
        }
        if (trimmed.rfind("名称：", 0) == 0) {
            in_target = trim_copy(trimmed.substr(strlen("名称："))) == source_name;
            continue;
        }
        if (trimmed.rfind("名称:", 0) == 0) {
            in_target = trim_copy(trimmed.substr(strlen("名称:"))) == source_name;
            continue;
        }

        if (!in_target) {
            continue;
        }

        size_t pos = trimmed.find("alsa.card = \"");
        if (pos != std::string::npos) {
            pos += strlen("alsa.card = \"");
            size_t end = trimmed.find('"', pos);
            if (end != std::string::npos && end > pos) {
                copy_string_value(card_out, card_out_size, trimmed.substr(pos, end - pos));
                return 0;
            }
        }
    }

    return -1;
}

static void apply_rt_capture_pulse_settings(const char *device) {
    std::string source_name = extract_pulse_source_name_from_device(device);
    if (source_name.empty()) {
        return;
    }

    if (g_rt_pulse_set_default_source) {
        std::ostringstream cmd;
        cmd << "pactl set-default-source " << shell_quote_single(source_name.c_str()) << " >/dev/null 2>&1";
        int ret = system(cmd.str().c_str());
        if (ret != 0) {
            printf("[RT WARN] Failed to set Pulse default source: %s (ret=%d)\n", source_name.c_str(), ret);
        } else {
            printf("[RT] Pulse default source set to: %s\n", source_name.c_str());
        }
    }

    if (g_rt_pulse_source_volume[0] != '\0') {
        std::ostringstream cmd;
        cmd << "pactl set-source-volume " << shell_quote_single(source_name.c_str())
            << " " << shell_quote_single(g_rt_pulse_source_volume) << " >/dev/null 2>&1";
        int ret = system(cmd.str().c_str());
        if (ret != 0) {
            printf("[RT WARN] Failed to set Pulse source volume: %s -> %s (ret=%d)\n",
                   source_name.c_str(), g_rt_pulse_source_volume, ret);
        } else {
            printf("[RT] Pulse source volume applied: %s -> %s\n",
                   source_name.c_str(), g_rt_pulse_source_volume);
        }
    }
}

static void init_rt_capture_mixer_config() {
    const char *env_enabled = getenv("RT_AMIXER_ENABLED");
    const char *env_card = getenv("RT_AMIXER_CARD");
    const char *env_control = getenv("RT_AMIXER_CONTROL");
    const char *env_volume = getenv("RT_AMIXER_VOLUME");
    const char *env_auto_gain = getenv("RT_AMIXER_AUTO_GAIN_CONTROL");

    g_rt_amixer_enabled = parse_bool_text_local(env_enabled, g_rt_amixer_enabled);
    if (!g_rt_amixer_enabled) {
        printf("[RT] Hardware amixer setup disabled by RT_AMIXER_ENABLED=%s\n",
               env_enabled ? env_enabled : "0");
        return;
    }

    if (env_card && env_card[0]) {
        strncpy(g_rt_amixer_card, env_card, sizeof(g_rt_amixer_card) - 1);
        g_rt_amixer_card[sizeof(g_rt_amixer_card) - 1] = '\0';
    }
    if (env_control && env_control[0]) {
        strncpy(g_rt_amixer_control, env_control, sizeof(g_rt_amixer_control) - 1);
        g_rt_amixer_control[sizeof(g_rt_amixer_control) - 1] = '\0';
    }
    if (env_volume && env_volume[0]) {
        strncpy(g_rt_amixer_volume, env_volume, sizeof(g_rt_amixer_volume) - 1);
        g_rt_amixer_volume[sizeof(g_rt_amixer_volume) - 1] = '\0';
    }
    if (env_auto_gain && env_auto_gain[0]) {
        strncpy(g_rt_amixer_auto_gain_control, env_auto_gain, sizeof(g_rt_amixer_auto_gain_control) - 1);
        g_rt_amixer_auto_gain_control[sizeof(g_rt_amixer_auto_gain_control) - 1] = '\0';
    }

    if (system("command -v amixer >/dev/null 2>&1") != 0) {
        g_rt_amixer_enabled = 0;
        printf("[RT] amixer not found, skip hardware mic setup\n");
        return;
    }

    printf("[RT] Hardware mic setup enabled: amixer -c %s sset %s %s%s%s\n",
           g_rt_amixer_card,
           g_rt_amixer_control,
           g_rt_amixer_volume,
           g_rt_amixer_auto_gain_control[0] ? ", Auto Gain Control=" : "",
           g_rt_amixer_auto_gain_control[0] ? g_rt_amixer_auto_gain_control : "");
}

static void apply_rt_capture_mixer(const char *device) {
    if (!g_rt_amixer_enabled) return;

    char effective_card[32];
    copy_string_value(effective_card, sizeof(effective_card), g_rt_amixer_card);

    if (strcasecmp(effective_card, "auto") == 0 || effective_card[0] == '\0') {
        std::string source_name = extract_pulse_source_name_from_device(device);
        if (!source_name.empty() &&
            resolve_alsa_card_from_pulse_source(source_name.c_str(), effective_card, sizeof(effective_card)) == 0) {
            printf("[RT] Resolved mixer card from Pulse source %s -> card %s\n",
                   source_name.c_str(), effective_card);
        } else {
            printf("[RT WARN] Failed to resolve mixer card automatically for device: %s\n",
                   device ? device : "(null)");
            return;
        }
    }

    std::ostringstream cmd;
    cmd << "amixer -c " << shell_quote_single(effective_card)
        << " sset " << shell_quote_single(g_rt_amixer_control)
        << " " << shell_quote_single(g_rt_amixer_volume)
        << " >/dev/null 2>&1";

    int ret = system(cmd.str().c_str());
    if (ret != 0) {
        printf("[RT WARN] Failed to apply hardware mic setup: amixer -c %s sset %s %s (ret=%d)\n",
               effective_card, g_rt_amixer_control, g_rt_amixer_volume, ret);
        return;
    }

    printf("[RT] Applied hardware mic setup: amixer -c %s sset %s %s\n",
           effective_card, g_rt_amixer_control, g_rt_amixer_volume);

    if (g_rt_amixer_auto_gain_control[0] != '\0') {
        std::ostringstream agc_cmd;
        agc_cmd << "amixer -c " << shell_quote_single(effective_card)
                << " sset 'Auto Gain Control' " << shell_quote_single(g_rt_amixer_auto_gain_control)
                << " >/dev/null 2>&1";

        int agc_ret = system(agc_cmd.str().c_str());
        if (agc_ret != 0) {
            printf("[RT WARN] Failed to apply auto gain control: amixer -c %s sset 'Auto Gain Control' %s (ret=%d)\n",
                   effective_card, g_rt_amixer_auto_gain_control, agc_ret);
        } else {
            printf("[RT] Applied auto gain control: amixer -c %s sset 'Auto Gain Control' %s\n",
                   effective_card, g_rt_amixer_auto_gain_control);
        }
    }
}

static std::string dirname_from_path(const std::string &path) {
    if (path.empty()) return ".";
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

static int path_has_whitespace(const char *path) {
    if (!path) return 1;
    for (const char *p = path; *p; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') return 1;
    }
    return 0;
}

static void derive_emergency_kws_paths_from_cmd(int lock_workdir, int lock_log) {
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

static void resolve_emergency_kws_default_cmd(int lock_cmd, int lock_workdir, int lock_log) {
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

static void init_emergency_kws_config() {
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

static int create_temp_wav_path(const char *prefix, char *path_out, size_t path_size) {
    if (!prefix || !path_out || path_size == 0) return -1;

    char tmpl[256];
    snprintf(tmpl, sizeof(tmpl), "/tmp/%s_XXXXXX.wav", prefix);
    int fd = mkstemps(tmpl, 4);
    if (fd < 0) {
        return -1;
    }
    close(fd);

    if (unlink(tmpl) != 0 && errno != ENOENT) {
        return -1;
    }

    strncpy(path_out, tmpl, path_size - 1);
    path_out[path_size - 1] = '\0';
    return 0;
}

static int ffmpeg_rt_filter_is_ready() {
    return g_rt_ffmpeg_filter_enabled && g_rt_ffmpeg_audio_filter[0] != '\0';
}

static int ffmpeg_filter_wav_to_path(const char *input_wav,
                                     const char *output_wav,
                                     int sample_rate,
                                     int num_channels,
                                     const char *context) {
    if (!ffmpeg_rt_filter_is_ready()) return -1;
    if (!input_wav || !output_wav || sample_rate <= 0 || num_channels <= 0) return -1;

    std::ostringstream cmd;
    cmd << shell_quote_single(g_rt_ffmpeg_bin)
        << " -y -hide_banner -loglevel error"
        << " -i " << shell_quote_single(input_wav)
        << " -af " << shell_quote_single(g_rt_ffmpeg_audio_filter)
        << " -ar " << sample_rate
        << " -ac " << num_channels
        << " -c:a pcm_s16le "
        << shell_quote_single(output_wav)
        << " >/dev/null 2>&1";

    int ret = system(cmd.str().c_str());
    if (ret != 0) {
        printf("[DENOISE] FFmpeg 失败: context=%s, in=%s, out=%s, af=%s, ret=%d\n",
               context ? context : "unknown",
               input_wav,
               output_wav,
               g_rt_ffmpeg_audio_filter,
               ret);
        return -1;
    }
    return 0;
}

static int ffmpeg_filter_wav_to_temp(const char *input_wav,
                                     char *output_wav,
                                     size_t output_size,
                                     int sample_rate,
                                     int num_channels,
                                     const char *context) {
    if (!ffmpeg_rt_filter_is_ready()) return -1;
    if (create_temp_wav_path("yamnet_ffmpeg_out", output_wav, output_size) != 0) {
        printf("[DENOISE] FFmpeg 创建临时输出文件失败: context=%s\n",
               context ? context : "unknown");
        return -1;
    }

    if (ffmpeg_filter_wav_to_path(input_wav, output_wav, sample_rate, num_channels, context) != 0) {
        unlink(output_wav);
        output_wav[0] = '\0';
        return -1;
    }
    return 0;
}

static int ffmpeg_filter_wav_inplace(const char *wav_path,
                                     int sample_rate,
                                     int num_channels,
                                     const char *context) {
    if (!ffmpeg_rt_filter_is_ready()) return -1;
    if (!wav_path || wav_path[0] == '\0') return -1;

    char filtered_path[512] = {0};
    if (ffmpeg_filter_wav_to_temp(wav_path, filtered_path, sizeof(filtered_path),
                                  sample_rate, num_channels, context) != 0) {
        return -1;
    }

    if (rename(filtered_path, wav_path) != 0) {
        printf("[DENOISE] FFmpeg 覆盖目标文件失败: context=%s, target=%s, err=%s\n",
               context ? context : "unknown", wav_path, strerror(errno));
        unlink(filtered_path);
        return -1;
    }
    return 0;
}

static int ffmpeg_filter_buffer_inplace(float *data,
                                        int num_frames,
                                        int sample_rate,
                                        int num_channels,
                                        const char *context) {
    if (!ffmpeg_rt_filter_is_ready()) return 0;
    if (!data || num_frames <= 0 || sample_rate <= 0 || num_channels <= 0) return -1;

    char input_wav[512] = {0};
    char output_wav[512] = {0};
    audio_buffer_t filtered_audio;
    memset(&filtered_audio, 0, sizeof(filtered_audio));
    int ret = -1;

    if (create_temp_wav_path("yamnet_ffmpeg_in", input_wav, sizeof(input_wav)) != 0) {
        printf("[DENOISE] FFmpeg 创建临时输入文件失败: context=%s\n",
               context ? context : "unknown");
        goto cleanup;
    }

    if (save_audio(input_wav, data, num_frames, sample_rate, num_channels) != 0) {
        printf("[DENOISE] FFmpeg 写入临时 wav 失败: context=%s\n",
               context ? context : "unknown");
        goto cleanup;
    }

    if (ffmpeg_filter_wav_to_temp(input_wav, output_wav, sizeof(output_wav),
                                  sample_rate, num_channels, context) != 0) {
        goto cleanup;
    }

    if (read_audio(output_wav, &filtered_audio) != 0) {
        printf("[DENOISE] FFmpeg 读取处理结果失败: context=%s\n",
               context ? context : "unknown");
        goto cleanup;
    }

    if (filtered_audio.num_channels != num_channels) {
        if (num_channels == 1 && filtered_audio.num_channels == 2) {
            if (convert_channels(&filtered_audio) != 0) {
                printf("[DENOISE] FFmpeg 声道转换失败: context=%s\n",
                       context ? context : "unknown");
                goto cleanup;
            }
        } else {
            printf("[DENOISE] FFmpeg 声道数不匹配: context=%s, expected=%d, got=%d\n",
                   context ? context : "unknown", num_channels, filtered_audio.num_channels);
            goto cleanup;
        }
    }

    if (filtered_audio.sample_rate != sample_rate) {
        if (resample_audio(&filtered_audio, filtered_audio.sample_rate, sample_rate) != 0) {
            printf("[DENOISE] FFmpeg 重采样失败: context=%s, expected=%d, got=%d\n",
                   context ? context : "unknown", sample_rate, filtered_audio.sample_rate);
            goto cleanup;
        }
    }

    {
        int copy_frames = std::min(num_frames, filtered_audio.num_frames);
        memcpy(data, filtered_audio.data, copy_frames * sizeof(float));
        if (copy_frames < num_frames) {
            memset(data + copy_frames, 0, (num_frames - copy_frames) * sizeof(float));
        }
    }
    ret = 0;

cleanup:
    if (filtered_audio.data) free(filtered_audio.data);
    if (input_wav[0]) unlink(input_wav);
    if (output_wav[0]) unlink(output_wav);
    return ret;
}

static int sox_denoise_is_ready() {
    return g_sox_denoise_enabled && g_sox_denoise_profile[0] != '\0';
}

static int sox_denoise_wav_to_path(const char *input_wav,
                                   const char *output_wav,
                                   const char *context) {
    if (!sox_denoise_is_ready()) return -1;
    if (!input_wav || !output_wav) return -1;

    std::ostringstream cmd;
    cmd << shell_quote_single(g_sox_bin)
        << " " << shell_quote_single(input_wav)
        << " " << shell_quote_single(output_wav)
        << " noisered "
        << shell_quote_single(g_sox_denoise_profile)
        << " " << g_sox_denoise_amount
        << " >/dev/null 2>&1";

    int ret = system(cmd.str().c_str());
    if (ret != 0) {
        printf("[DENOISE] SoX 失败: context=%s, in=%s, out=%s, profile=%s, amount=%.2f, ret=%d\n",
               context ? context : "unknown",
               input_wav,
               output_wav,
               g_sox_denoise_profile,
               g_sox_denoise_amount,
               ret);
        return -1;
    }
    return 0;
}

static int sox_denoise_wav_to_temp(const char *input_wav,
                                   char *output_wav,
                                   size_t output_size,
                                   const char *context) {
    if (!sox_denoise_is_ready()) return -1;
    if (create_temp_wav_path("yamnet_sox_out", output_wav, output_size) != 0) {
        printf("[DENOISE] 创建临时输出文件失败: context=%s\n", context ? context : "unknown");
        return -1;
    }

    if (sox_denoise_wav_to_path(input_wav, output_wav, context) != 0) {
        unlink(output_wav);
        output_wav[0] = '\0';
        return -1;
    }
    return 0;
}

static int sox_denoise_wav_inplace(const char *wav_path, const char *context) {
    if (!sox_denoise_is_ready()) return -1;
    if (!wav_path || wav_path[0] == '\0') return -1;

    char denoised_path[512] = {0};
    if (sox_denoise_wav_to_temp(wav_path, denoised_path, sizeof(denoised_path), context) != 0) {
        return -1;
    }

    if (rename(denoised_path, wav_path) != 0) {
        printf("[DENOISE] 覆盖目标文件失败: context=%s, target=%s, err=%s\n",
               context ? context : "unknown", wav_path, strerror(errno));
        unlink(denoised_path);
        return -1;
    }
    return 0;
}

static int sox_denoise_buffer_inplace(float *data,
                                      int num_frames,
                                      int sample_rate,
                                      int num_channels,
                                      const char *context) {
    if (!sox_denoise_is_ready()) return 0;
    if (!data || num_frames <= 0 || sample_rate <= 0 || num_channels <= 0) return -1;

    char input_wav[512] = {0};
    char output_wav[512] = {0};
    audio_buffer_t denoised_audio;
    memset(&denoised_audio, 0, sizeof(denoised_audio));
    int ret = -1;

    if (create_temp_wav_path("yamnet_sox_in", input_wav, sizeof(input_wav)) != 0) {
        printf("[DENOISE] 创建临时输入文件失败: context=%s\n", context ? context : "unknown");
        goto cleanup;
    }

    if (save_audio(input_wav, data, num_frames, sample_rate, num_channels) != 0) {
        printf("[DENOISE] 写入临时 wav 失败: context=%s\n", context ? context : "unknown");
        goto cleanup;
    }

    if (sox_denoise_wav_to_temp(input_wav, output_wav, sizeof(output_wav), context) != 0) {
        goto cleanup;
    }

    if (read_audio(output_wav, &denoised_audio) != 0) {
        printf("[DENOISE] 读取降噪结果失败: context=%s\n", context ? context : "unknown");
        goto cleanup;
    }

    if (denoised_audio.num_channels != num_channels) {
        if (num_channels == 1 && denoised_audio.num_channels == 2) {
            if (convert_channels(&denoised_audio) != 0) {
                printf("[DENOISE] 声道转换失败: context=%s\n", context ? context : "unknown");
                goto cleanup;
            }
        } else {
            printf("[DENOISE] 声道数不匹配: context=%s, expected=%d, got=%d\n",
                   context ? context : "unknown", num_channels, denoised_audio.num_channels);
            goto cleanup;
        }
    }

    if (denoised_audio.sample_rate != sample_rate) {
        if (resample_audio(&denoised_audio, denoised_audio.sample_rate, sample_rate) != 0) {
            printf("[DENOISE] 重采样失败: context=%s, expected=%d, got=%d\n",
                   context ? context : "unknown", sample_rate, denoised_audio.sample_rate);
            goto cleanup;
        }
    }

    {
        int copy_frames = std::min(num_frames, denoised_audio.num_frames);
        memcpy(data, denoised_audio.data, copy_frames * sizeof(float));
        if (copy_frames < num_frames) {
            memset(data + copy_frames, 0, (num_frames - copy_frames) * sizeof(float));
        }
    }

    ret = 0;

cleanup:
    if (denoised_audio.data) free(denoised_audio.data);
    if (input_wav[0]) unlink(input_wav);
    if (output_wav[0]) unlink(output_wav);
    return ret;
}

static void init_rt_ffmpeg_filter_config() {
    const char *env_enabled = getenv("RT_FFMPEG_FILTER_ENABLED");
    const char *env_bin = getenv("RT_FFMPEG_BIN");
    const char *env_filter = getenv("RT_FFMPEG_AUDIO_FILTER");

    if (env_bin && env_bin[0]) {
        strncpy(g_rt_ffmpeg_bin, env_bin, sizeof(g_rt_ffmpeg_bin) - 1);
        g_rt_ffmpeg_bin[sizeof(g_rt_ffmpeg_bin) - 1] = '\0';
    }

    if (env_filter && env_filter[0]) {
        strncpy(g_rt_ffmpeg_audio_filter, env_filter, sizeof(g_rt_ffmpeg_audio_filter) - 1);
        g_rt_ffmpeg_audio_filter[sizeof(g_rt_ffmpeg_audio_filter) - 1] = '\0';
    }

    g_rt_ffmpeg_filter_enabled = parse_bool_text_local(env_enabled, g_rt_ffmpeg_filter_enabled);
    if (!g_rt_ffmpeg_filter_enabled) {
        printf("[DENOISE] FFmpeg 实时滤波已禁用 (RT_FFMPEG_FILTER_ENABLED=%s)\n",
               env_enabled ? env_enabled : "0");
        return;
    }

    if (g_rt_ffmpeg_audio_filter[0] == '\0') {
        g_rt_ffmpeg_filter_enabled = 0;
        printf("[DENOISE] FFmpeg 实时滤波未配置 audio filter，保持关闭\n");
        return;
    }

    std::ostringstream probe_cmd;
    probe_cmd << "command -v " << shell_quote_single(g_rt_ffmpeg_bin) << " >/dev/null 2>&1";
    if (system(probe_cmd.str().c_str()) != 0) {
        g_rt_ffmpeg_filter_enabled = 0;
        printf("[DENOISE] 未找到 FFmpeg 可执行文件: %s\n", g_rt_ffmpeg_bin);
        return;
    }

    printf("[DENOISE] FFmpeg 实时滤波已启用: bin=%s, af=%s\n",
           g_rt_ffmpeg_bin, g_rt_ffmpeg_audio_filter);
}

static void init_sox_denoise_config() {
    const char *env_enabled = getenv("SOX_DENOISE_ENABLED");
    const char *env_bin = getenv("SOX_BIN");
    const char *env_profile = getenv("SOX_DENOISE_PROFILE");
    const char *env_amount = getenv("SOX_DENOISE_AMOUNT");

    if (env_bin && env_bin[0]) {
        strncpy(g_sox_bin, env_bin, sizeof(g_sox_bin) - 1);
        g_sox_bin[sizeof(g_sox_bin) - 1] = '\0';
    }

    if (env_amount && env_amount[0]) {
        float amount = (float)atof(env_amount);
        if (amount > 0.0f && amount <= 1.0f) {
            g_sox_denoise_amount = amount;
        }
    }

    int enabled = parse_bool_text_local(env_enabled, g_sox_denoise_enabled);
    if (!enabled) {
        g_sox_denoise_enabled = 0;
        printf("[DENOISE] SoX 降噪已禁用 (SOX_DENOISE_ENABLED=%s)\n",
               env_enabled ? env_enabled : "0");
        return;
    }

    if (env_profile && env_profile[0] && path_exists(env_profile)) {
        strncpy(g_sox_denoise_profile, env_profile, sizeof(g_sox_denoise_profile) - 1);
        g_sox_denoise_profile[sizeof(g_sox_denoise_profile) - 1] = '\0';
    } else if (g_sox_denoise_profile[0] != '\0' && path_exists(g_sox_denoise_profile)) {
        // Keep YAML/default configured profile.
    } else {
        const char *candidates[] = {
            "../../../speech_camera2_80.prof",
            "../../../noise_camera2.prof",
            "../../../noise.prof",
            "../../speech_camera2_80.prof",
            "../../noise_camera2.prof",
            "../../noise.prof",
            "../speech_camera2_80.prof",
            "../noise_camera2.prof",
            "../noise.prof",
            "./speech_camera2_80.prof",
            "./noise_camera2.prof",
            "./noise.prof",
            NULL
        };
        for (int i = 0; candidates[i] != NULL; ++i) {
            if (path_exists(candidates[i])) {
                strncpy(g_sox_denoise_profile, candidates[i], sizeof(g_sox_denoise_profile) - 1);
                g_sox_denoise_profile[sizeof(g_sox_denoise_profile) - 1] = '\0';
                break;
            }
        }
    }

    if (g_sox_denoise_profile[0] == '\0') {
        g_sox_denoise_enabled = 0;
        printf("[DENOISE] 未找到 noise profile，SoX 降噪保持关闭\n");
        return;
    }

    std::ostringstream probe_cmd;
    probe_cmd << "command -v " << shell_quote_single(g_sox_bin) << " >/dev/null 2>&1";
    if (system(probe_cmd.str().c_str()) != 0) {
        g_sox_denoise_enabled = 0;
        printf("[DENOISE] 未找到 SoX 可执行文件: %s\n", g_sox_bin);
        return;
    }

    g_sox_denoise_enabled = 1;
    printf("[DENOISE] SoX 降噪已启用: bin=%s, profile=%s, amount=%.2f\n",
           g_sox_bin, g_sox_denoise_profile, g_sox_denoise_amount);
}

static int parse_seconds_from_query(const char *query, int default_sec, int max_sec) {
    if (!query || query[0] == '\0') return default_sec;

    const char *key = strstr(query, "seconds=");
    if (!key) return default_sec;
    key += 8;
    int sec = atoi(key);
    if (sec <= 0) return default_sec;
    if (sec > max_sec) return max_sec;
    return sec;
}

static int parse_int_from_query(const char *query, const char *key, int default_value, int max_value) {
    if (!query || query[0] == '\0' || !key || key[0] == '\0') return default_value;

    std::string needle = std::string(key) + "=";
    const char *hit = strstr(query, needle.c_str());
    if (!hit) return default_value;

    hit += needle.size();
    int value = atoi(hit);
    if (value <= 0) return default_value;
    if (max_value > 0 && value > max_value) return max_value;
    return value;
}

static int parse_flag_from_query(const char *query, const char *key) {
    if (!query || !key || key[0] == '\0') return 0;

    std::string needle = std::string(key) + "=";
    const char *hit = strstr(query, needle.c_str());
    if (!hit) return 0;

    hit += needle.size();
    if (strncmp(hit, "1", 1) == 0) return 1;
    if (strncasecmp(hit, "true", 4) == 0) return 1;
    if (strncasecmp(hit, "yes", 3) == 0) return 1;
    return 0;
}

static int emergency_kws_is_process_alive(pid_t pid) {
    if (pid <= 0) return 0;
    if (kill(pid, 0) == 0) return 1;
    return errno == EPERM;
}

static int emergency_kws_write_pid_file_locked(pid_t pid) {
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

static int emergency_kws_read_pid_file_locked(pid_t *pid_out) {
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

static void emergency_kws_remove_pid_file_locked() {
    if (g_emergency_kws_pid_file[0]) {
        (void)remove(g_emergency_kws_pid_file);
    }
}

static int emergency_kws_try_reap_locked(pid_t pid, int *status_out) {
    if (pid <= 0) return 0;
    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) {
        if (status_out) *status_out = status;
        return 1;
    }
    return 0;
}

static void emergency_kws_refresh_state_locked() {
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

static int emergency_kws_start_locked() {
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

static int emergency_kws_stop_locked() {
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

static int emergency_kws_start() {
    pthread_mutex_lock(&g_emergency_kws_mutex);
    int ret = emergency_kws_start_locked();
    pthread_mutex_unlock(&g_emergency_kws_mutex);
    if (ret == 0 || ret == 1) {
        (void)emergency_kws_reporter_start();
    }
    return ret;
}

static int emergency_kws_stop() {
    pthread_mutex_lock(&g_emergency_kws_mutex);
    int ret = emergency_kws_stop_locked();
    pthread_mutex_unlock(&g_emergency_kws_mutex);
    if (ret == 0 || ret == 1) {
        (void)emergency_kws_reporter_stop();
    }
    return ret;
}

static void emergency_kws_refresh_state() {
    pthread_mutex_lock(&g_emergency_kws_mutex);
    emergency_kws_refresh_state_locked();
    pthread_mutex_unlock(&g_emergency_kws_mutex);
}

static void *emergency_kws_report_thread_main(void *arg) {
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

static int emergency_kws_reporter_start() {
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

static int emergency_kws_reporter_stop() {
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

static AsrResult run_asr_locked(const float *data, int num_frames) {
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

static int should_log_http_request(const char *method, const char *path) {
    if (!method || !path) return 1;
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/realtime/events") == 0 ||
            strcmp(path, "/realtime/windows") == 0 ||
            strcmp(path, "/realtime/status") == 0 ||
            strcmp(path, "/wake/events") == 0 ||
            strcmp(path, "/wake/status") == 0 ||
            strcmp(path, "/health") == 0) {
            return 0;
        }
    }
    return 1;
}

static std::string summarize_top_results(const ResultEntry *results, int result_count, int max_items) {
    if (!results || result_count <= 0 || max_items <= 0) {
        return "(none)";
    }

    std::ostringstream oss;
    int limit = std::min(result_count, max_items);
    for (int i = 0; i < limit; ++i) {
        if (i > 0) {
            oss << ", ";
        }
        std::string token = results[i].token ? results[i].token : "<null>";
        std::replace(token.begin(), token.end(), '\n', ' ');
        std::replace(token.begin(), token.end(), '\r', ' ');
        oss << token
            << "(" << results[i].score << ")";
    }
    return oss.str();
}

void signal_handler(int sig) {
    printf("\n收到信号 %d，关闭服务器...\n", sig);
    if (server_socket >= 0) close(server_socket);
    (void)emergency_kws_stop();
    if (rt_active) rt_stop();
    g_asr_engine.Close();
    if (model_initialized) {
        release_yamnet_model(&rknn_app_ctx);
    }
    exit(0);
}

void send_response(int client_fd, const char* status, const char* content_type, 
                   const char* body, int body_len) {
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body_len);
    send(client_fd, header, header_len, 0);
    if (body && body_len > 0) {
        send(client_fd, body, body_len, 0);
    }
}

void send_json_response(int client_fd, int status_code, const char* message) {
    char body[1024];
    char escaped_message[768];
    json_escape_string(message, escaped_message, sizeof(escaped_message));
    int len = snprintf(body, sizeof(body), "{\"status\": %d, \"message\": \"%s\"}", status_code, escaped_message);
    send_response(client_fd, status_code == 0 ? "200 OK" : "400 Bad Request",
                 "application/json", body, len);
}

// ========== 事件管理 ==========
static void reset_events() {
    memset(events, 0, sizeof(events));
    event_count = 0;
    has_current_event = 0;
}

static void add_keyword(AnomalyEvent *evt, const char *keyword) {
    for (int i = 0; i < evt->keyword_count; i++) {
        if (strcmp(evt->keywords[i], keyword) == 0) return;
    }
    if (evt->keyword_count < 10) {
        strncpy(evt->keywords[evt->keyword_count], keyword, 63);
        evt->keywords[evt->keyword_count][63] = '\0';
        evt->keyword_count++;
    }
}

static void start_event(float start_sec, const char *keyword, float score) {
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

static int merge_event(float chunk_end, const char *keyword, float score) {
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

static void finalize_event() {
    if (!has_current_event || event_count >= MAX_ANOMALY_EVENTS) {
        has_current_event = 0;
        return;
    }
    events[event_count++] = current_event;
    has_current_event = 0;
}

// ========== 音频格式转换 (使用ffmpeg) ==========
static int convert_to_wav(const char *input_path, char *wav_path, size_t path_size) {
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
static char anomaly_save_dir[256] = "./anomaly_audio";

static int save_anomaly_audio_to_file(const char *original_path, float *audio_data,
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
typedef struct {
    const char *boundary;
    int boundary_len;
    int headers_done;
    int body_start;
    int body_end;
    int content_start;
    int content_len;
    char filename[256];
    char fieldname[64];
} MultipartContext;

static int multipart_find(const char *data, int data_len,
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

static int multipart_parse_headers(const char *data, int data_len,
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
static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int analyze_audio(const char *audio_path, char *result_json, size_t json_size) {
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

static void parse_emergency_log_line(const std::string &line,
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

int init_model() {
    printf("Loading model from: %s\n", model_path);

    // 读取标签
    int ret = read_label(labels);
    if (ret != 0) {
        printf("[ERROR] read label fail! ret=%d\n", ret);
        return -1;
    }
    printf("Loaded %d labels\n", LABEL_NUM);

    // 初始化模型
    ret = init_yamnet_model(model_path, &rknn_app_ctx);
    if (ret != 0) {
        printf("[ERROR] init_yamnet_model fail! ret=%d\n", ret);
        return -1;
    }

    model_initialized = 1;
    printf("[OK] YAMNet model initialized\n");

    const char *enable_asr_env = getenv("ENABLE_VOSK_ASR");
    int enable_asr = parse_bool_text_local(enable_asr_env, 0);
    const char *env_cn = getenv("VOSK_MODEL_CN");
    const char *env_en = getenv("VOSK_MODEL_EN");
    const char *default_cn_candidates[] = {
        "./model/vosk-model-small-cn-0.22",
        "../model/vosk-model-small-cn-0.22",
        NULL
    };
    const char *default_en_candidates[] = {
        "./model/vosk-model-small-en-us-0.15",
        "../model/vosk-model-small-en-us-0.15",
        NULL
    };

    if (!enable_asr) {
        printf("[INFO] ASR disabled by default. Set ENABLE_VOSK_ASR=1 and "
               "VOSK_MODEL_CN / VOSK_MODEL_EN to enable transcript.\n");
        return 0;
    }

    if (env_cn && env_cn[0]) {
        strncpy(g_asr_model_cn, env_cn, sizeof(g_asr_model_cn) - 1);
    } else {
        for (int i = 0; default_cn_candidates[i] != NULL; ++i) {
            if (path_exists(default_cn_candidates[i])) {
                strncpy(g_asr_model_cn, default_cn_candidates[i], sizeof(g_asr_model_cn) - 1);
                break;
            }
        }
    }

    if (env_en && env_en[0]) {
        strncpy(g_asr_model_en, env_en, sizeof(g_asr_model_en) - 1);
    } else {
        for (int i = 0; default_en_candidates[i] != NULL; ++i) {
            if (path_exists(default_en_candidates[i])) {
                strncpy(g_asr_model_en, default_en_candidates[i], sizeof(g_asr_model_en) - 1);
                break;
            }
        }
    }

    if (g_asr_model_cn[0] || g_asr_model_en[0]) {
        g_asr_enabled = g_asr_engine.Init(
            g_asr_model_cn[0] ? g_asr_model_cn : NULL,
            g_asr_model_en[0] ? g_asr_model_en : NULL) ? 1 : 0;

        if (g_asr_enabled) {
            printf("[OK] ASR initialized (CN=%s, EN=%s)\n",
                   g_asr_model_cn[0] ? g_asr_model_cn : "<none>",
                   g_asr_model_en[0] ? g_asr_model_en : "<none>");
        } else {
            printf("[WARN] ASR init failed: %s\n", g_asr_engine.LastError().c_str());
        }
    } else {
        printf("[INFO] ASR disabled. Set ENABLE_VOSK_ASR=1 and "
               "VOSK_MODEL_CN / VOSK_MODEL_EN to enable transcript.\n");
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int port = 8089;
    const char *auto_rt_env = getenv("AUTO_START_REALTIME");
    const char *rt_print_asr_env = getenv("RT_PRINT_ASR");
    const char *rt_print_window_env = getenv("RT_PRINT_WINDOW");
    const char *rt_capture_volume_env = getenv("RT_CAPTURE_VOLUME");
    const char *rt_capture_volume_percent_env = getenv("RT_CAPTURE_VOLUME_PERCENT");

    init_runtime_audio_yaml_config();
    init_emergency_kws_config();

    if (auto_rt_env && auto_rt_env[0]) {
        if (strcmp(auto_rt_env, "0") == 0 ||
            strcasecmp(auto_rt_env, "false") == 0 ||
            strcasecmp(auto_rt_env, "no") == 0) {
            g_auto_start_realtime = 0;
        } else {
            g_auto_start_realtime = 1;
        }
    }

    if (rt_print_asr_env && rt_print_asr_env[0]) {
        if (strcmp(rt_print_asr_env, "0") == 0 ||
            strcasecmp(rt_print_asr_env, "false") == 0 ||
            strcasecmp(rt_print_asr_env, "no") == 0) {
            g_rt_print_asr = 0;
        } else {
            g_rt_print_asr = 1;
        }
    }

    if (rt_print_window_env && rt_print_window_env[0]) {
        if (strcmp(rt_print_window_env, "0") == 0 ||
            strcasecmp(rt_print_window_env, "false") == 0 ||
            strcasecmp(rt_print_window_env, "no") == 0) {
            g_rt_print_window = 0;
        } else {
            g_rt_print_window = 1;
        }
    }

    if (rt_capture_volume_env && rt_capture_volume_env[0]) {
        float volume = (float)atof(rt_capture_volume_env);
        if (volume > 2.0f && volume <= 200.0f) {
            volume /= 100.0f;
        }
        if (volume >= 0.0f && volume <= 2.0f) {
            g_rt_capture_volume = volume;
        }
    }

    if (rt_capture_volume_percent_env && rt_capture_volume_percent_env[0]) {
        float volume_percent = (float)atof(rt_capture_volume_percent_env);
        if (volume_percent >= 0.0f && volume_percent <= 200.0f) {
            g_rt_capture_volume = volume_percent / 100.0f;
        }
    }

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("======================================================================\n");
    printf("  声音异常检测 HTTP 服务器\n");
    printf("======================================================================\n");

    init_rt_capture_mixer_config();
    init_rt_ffmpeg_filter_config();
    init_sox_denoise_config();

    // 初始化模型
    if (init_model() != 0) {
        printf("[ERROR] Failed to initialize model\n");
        return 1;
    }

    // 启动服务器
    if (start_server(port) < 0) {
        return 1;
    }

    // 默认自动启动实时分类采集，使转写接口开箱即用。
    if (g_auto_start_realtime) {
        int rt_ret = rt_start(g_rt_default_device);
        if (rt_ret == 0) {
            printf("[OK] Realtime auto-start enabled, device=%s\n", rt_device);
        } else if (rt_ret == -1) {
            printf("[INFO] Realtime already running\n");
        } else {
            printf("[WARN] Realtime auto-start failed (ret=%d). "
                   "Use POST /realtime/start to start manually.\n", rt_ret);
        }
    } else {
        printf("[INFO] Realtime auto-start disabled by AUTO_START_REALTIME=%s\n", auto_rt_env);
    }

    if (g_emergency_kws_auto_start) {
        int kws_ret = emergency_kws_start();
        if (kws_ret == 0) {
            printf("[OK] Emergency wake auto-start enabled\n");
        } else if (kws_ret == 1) {
            printf("[INFO] Emergency wake already running\n");
        } else {
            printf("[WARN] Emergency wake auto-start failed (ret=%d). "
                   "Use POST /wake/start to start manually.\n", kws_ret);
        }
    } else {
        printf("[INFO] Emergency wake auto-start disabled (EMERGENCY_KWS_AUTO_START=0)\n");
    }

    printf("\n======================================================================\n");
    printf("  服务器启动成功！\n");
    printf("======================================================================\n");
    printf("  端口: %d\n", port);
    printf("  音频配置文件: %s\n",
           g_runtime_audio_config_path[0] ? g_runtime_audio_config_path : "(not found, using built-in defaults)");
    printf("  默认实时输入: %s\n", g_rt_default_device);
    printf("  API:\n");
    printf("    POST /analyze           - 分析音频文件 (本地路径)\n");
    printf("    POST /analyze/upload    - 上传音频文件 (支持 MP3/WAV/OGG 等)\n");
    printf("                             返回结果新增 transcript/asr_lang/asr_confidence\n");
    printf("    POST /realtime/start    - 启动实时麦克风监测\n");
    printf("    POST /realtime/stop     - 停止实时监测\n");
    printf("    GET  /realtime/status  - 查询监测状态\n");
    printf("    GET  /realtime/events  - 获取异常事件\n");
    printf("    GET  /realtime/windows?limit=5 - 获取最近窗口状态\n");
    printf("    GET  /realtime/transcript?seconds=120 - 获取最近麦克风转写\n");
    printf("    POST /wake/start       - 启动紧急关键词唤醒监测\n");
    printf("    POST /wake/stop        - 停止紧急关键词唤醒监测\n");
    printf("    GET  /wake/status      - 查询唤醒监测状态\n");
    printf("    GET  /wake/events?limit=20 - 获取最近关键词唤醒日志\n");
    printf("    GET  /health           - 健康检查\n");
    printf("    GET  /config           - 获取配置\n");
    printf("======================================================================\n");
    printf("  ASR 配置:\n");
    printf("    export ENABLE_VOSK_ASR=1\n");
    printf("    export VOSK_MODEL_CN=/path/to/vosk-model-small-cn-0.22\n");
    printf("    export VOSK_MODEL_EN=/path/to/vosk-model-small-en-us-0.15\n");
    printf("    export VOSK_LIB_PATH=/path/to/libvosk.so   (可选)\n");
    printf("    export RT_PRINT_ASR=1   # 实时打印每3秒转写（默认开）\n");
    printf("    export RT_PRINT_WINDOW=1   # 实时打印每个3秒检测窗口（默认开）\n");
    printf("    export RT_AMIXER_ENABLED=0\n");
    printf("    export RT_AMIXER_CARD=5\n");
    printf("    export RT_AMIXER_CONTROL=Mic\n");
    printf("    export RT_AMIXER_VOLUME=60%%\n");
    printf("    export RT_CAPTURE_VOLUME=1.0   # 软件增益，默认不额外缩放\n");
    printf("    export SOUND_MONITORING_CONFIG=/path/to/runtime_audio.yaml\n");
    printf("  唤醒词配置:\n");
    printf("    export EMERGENCY_KWS_AUTO_START=1\n");
    printf("    export EMERGENCY_KWS_REPORT_ENABLED=1   # 紧急关键词触发后自动上报安全记录并触发 AI 分析\n");
    printf("    export EMERGENCY_KWS_CMD=./wake/emergency_monitor\n");
    printf("    export EMERGENCY_KWS_WORKDIR=./wake\n");
    printf("    export EMERGENCY_KWS_LOG_PATH=./wake/emergency_log.txt\n");
    printf("    export EMERGENCY_KWS_MODEL_DIR=/path/to/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01\n");
    printf("  SoX 降噪配置:\n");
    printf("    export SOX_DENOISE_ENABLED=1\n");
    printf("    export SOX_DENOISE_PROFILE=/path/to/speech_camera2_80.prof\n");
    printf("    export SOX_DENOISE_AMOUNT=0.25\n");
    printf("    export SOX_BIN=sox\n");
    printf("  FFmpeg 实时滤波配置:\n");
    printf("    export RT_FFMPEG_FILTER_ENABLED=1\n");
    printf("    export RT_FFMPEG_BIN=ffmpeg\n");
    printf("    export RT_FFMPEG_AUDIO_FILTER='afftdn=nf=-40,highpass=f=100,lowpass=f=3500,volume=6.0'\n");
    printf("  当前硬件麦克风设置: amixer -c %s sset %s %s\n",
           g_rt_amixer_card, g_rt_amixer_control, g_rt_amixer_volume);
    printf("  当前软件录音增益: %.2f (%.0f%%)\n", g_rt_capture_volume, g_rt_capture_volume * 100.0f);
    printf("  当前默认实时输入: %s\n", g_rt_default_device);
    printf("  当前实时 FFmpeg 滤波: %s (bin=%s, af=%s)\n",
           ffmpeg_rt_filter_is_ready() ? "enabled" : "disabled",
           g_rt_ffmpeg_bin,
           g_rt_ffmpeg_audio_filter);
    printf("======================================================================\n");
    printf("  示例:\n");
    printf("    curl -X POST http://localhost:%d/analyze \\\n", port);
    printf("         -H \"Content-Type: application/json\" \\\n");
    printf("         -d '{\"audio_path\": \"/path/to/test.wav\"}'\n");
    printf("    curl -X POST http://localhost:%d/analyze/upload \\\n", port);
    printf("         -F 'audio=@/path/to/test.mp3'\n");
    printf("    curl -X POST http://localhost:%d/analyze/upload \\\n", port);
    printf("         -H \"Content-Type: audio/mpeg\" \\\n");
    printf("         --data-binary @test.mp3\n");
    printf("    curl -X POST http://localhost:%d/realtime/start \\\n", port);
    printf("         -H \"Content-Type: application/json\" \\\n");
    printf("         -d '{\"device\": \"parec\"}'\n");
    printf("    curl http://localhost:%d/realtime/status\n", port);
    printf("    curl http://localhost:%d/realtime/events\n", port);
    printf("    curl http://localhost:%d/realtime/windows?limit=5\n", port);
    printf("    curl http://localhost:%d/realtime/transcript?seconds=120\n", port);
    printf("======================================================================\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        pthread_t thread;
        int *client_fd_ptr = (int*)malloc(sizeof(int));
        *client_fd_ptr = client_fd;
        pthread_create(&thread, NULL, client_handler, client_fd_ptr);
        pthread_detach(thread);
    }

    return 0;
}
