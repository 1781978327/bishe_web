// Copyright (c) 2024 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
        Real-time Microphone Anomaly Detection
-------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include "yamnet.h"
#include "audio_utils.h"
#include "audio_capture.h"

/*-------------------------------------------
            Configuration
-------------------------------------------*/
#define MAX_ANOMALY_EVENTS 100
#define RING_BUFFER_SEC 30        // 环形缓冲区时长（秒）
#define CHUNK_DURATION_SEC 3      // 每次推理的音频长度
#define HOP_SEC 1.5               // 滑动步长
#define MERGE_GAP_SEC 2.0         // 事件合并间隔
#define DETECTION_THRESHOLD 0.05 // 检测阈值
#define MIN_EVENT_COUNT 2         // 最少触发次数才算有效事件
#define COOLDOWN_SEC 5.0         // 事件触发后冷却时间
#define SAVE_RAW_FRAMES 480000    // 保存约30秒原始音频

// 全局：原始音频保存
static float *g_raw_audio = NULL;
static int g_raw_capacity = 0;
static int g_raw_count = 0;
static int g_save_raw = 0;

// 事件结构体
typedef struct {
    int event_id;
    float start_time;
    float end_time;
    float max_score;
    char keywords[10][64];
    int keyword_count;
    int trigger_count;
} AnomalyEvent;

static volatile int g_running = 1;
static AnomalyEvent g_events[MAX_ANOMALY_EVENTS];
static int g_event_count = 0;
static float g_last_event_time = -999.0f;

// 环形缓冲区
typedef struct {
    float *data;
    int capacity;   // 总帧数
    int size;       // 当前有效帧数
    int write_idx;   // 写入位置
} RingBuffer;

static RingBuffer g_ring = {0};

// 当前正在构建的事件
static struct {
    int active;
    float start_sec;
    float last_trigger_sec;
    char keywords[10][64];
    int keyword_count;
    float max_score;
    int trigger_count;
} g_current_event = {0};

// 信号处理
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// 环形缓冲区操作
static void ring_init(RingBuffer *rb, int sample_rate, int sec) {
    rb->capacity = sample_rate * sec;
    rb->data = (float *)calloc(rb->capacity, sizeof(float));
    rb->size = 0;
    rb->write_idx = 0;
}

static void ring_push(RingBuffer *rb, float *data, int frames) {
    for (int i = 0; i < frames; i++) {
        rb->data[rb->write_idx] = data[i];
        rb->write_idx = (rb->write_idx + 1) % rb->capacity;
        if (rb->size < rb->capacity) rb->size++;
    }
}

static void ring_get_latest(RingBuffer *rb, float *out, int frames) {
    if (frames > rb->size) frames = rb->size;
    int start_idx = (rb->write_idx - frames + rb->capacity) % rb->capacity;
    for (int i = 0; i < frames; i++) {
        out[i] = rb->data[(start_idx + i) % rb->capacity];
    }
}

static void ring_free(RingBuffer *rb) {
    if (rb->data) free(rb->data);
    rb->data = NULL;
}

// 获取当前时间戳
static double get_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// 添加关键词到事件
static void add_keyword(char keywords[][64], int *count, const char *kw) {
    char cleaned[64] = {0};
    int j = 0;
    for (int i = 0; kw[i] && j < 63; i++) {
        if (kw[i] != '\n' && kw[i] != '\r') {
            cleaned[j++] = kw[i];
        }
    }
    
    for (int i = 0; i < *count; i++) {
        if (strcmp(keywords[i], cleaned) == 0) return;
    }
    if (*count < 10) {
        strncpy(keywords[*count], cleaned, 63);
        (*count)++;
    }
}

// 保存事件
static void save_event(const char *keyword, float start_time, float end_time, 
                       float score, float *audio_data, int frames, int sample_rate) {
    if (g_event_count >= MAX_ANOMALY_EVENTS) return;
    
    AnomalyEvent *evt = &g_events[g_event_count];
    evt->event_id = g_event_count;
    evt->start_time = start_time;
    evt->end_time = end_time;
    evt->max_score = score;
    evt->trigger_count = g_current_event.trigger_count;
    evt->keyword_count = g_current_event.keyword_count;
    memcpy(evt->keywords, g_current_event.keywords, sizeof(g_current_event.keywords));
    
    g_event_count++;
    
    // 打印告警
    printf("\n");
    printf("========================================\n");
    printf("  [!!! ANOMALY ALARM !!!]\n");
    printf("========================================\n");
    printf("  Event ID:    #%03d\n", g_event_count - 1);
    printf("  Time:        %.1fs - %.1fs\n", start_time, end_time);
    printf("  Duration:    %.1fs\n", end_time - start_time);
    printf("  Confidence:  %.4f\n", score);
    printf("  Keywords:    ");
    for (int i = 0; i < g_current_event.keyword_count; i++) {
        printf("%s", g_current_event.keywords[i]);
        if (i < g_current_event.keyword_count - 1) printf(", ");
    }
    printf("\n");
    printf("  Triggers:    %d times\n", g_current_event.trigger_count);
    printf("========================================\n");
    
    // 保存音频片段
    char filename[256];
    snprintf(filename, sizeof(filename), "alarm_%03d_%.1fs.wav", 
             g_event_count - 1, start_time);
    save_audio(filename, audio_data, frames, sample_rate, 1);
    printf("  [SAVED] %s\n\n", filename);
}

// 重置当前事件
static void reset_current_event() {
    memset(&g_current_event, 0, sizeof(g_current_event));
}

// 尝试结束并保存当前事件
static void try_finalize_event(double current_time, float *audio_data, int frames, int sample_rate) {
    if (!g_current_event.active) return;
    
    float gap = current_time - g_current_event.last_trigger_sec;
    
    if (gap >= MERGE_GAP_SEC && g_current_event.trigger_count >= MIN_EVENT_COUNT) {
        // 保存事件
        save_event(g_current_event.keywords[0], 
                   g_current_event.start_sec,
                   g_current_event.last_trigger_sec + CHUNK_DURATION_SEC,
                   g_current_event.max_score,
                   audio_data, frames, sample_rate);
    }
    
    reset_current_event();
}

/*-------------------------------------------
                  Main Function
-------------------------------------------*/
int main(int argc, char **argv)
{
    // 参数解析
    const char *model_path = NULL;
    const char *device = "mic_boost";  // 默认使用软件增益设备
    int sample_rate = 16000;
    int detection_mode = 0;  // 0=实时, 1=文件

    int opt;
    while ((opt = getopt(argc, argv, "m:d:r:s")) != -1) {
        switch (opt) {
            case 'm': model_path = optarg; break;
            case 'd': device = optarg; break;
            case 'r': sample_rate = atoi(optarg); break;
            case 's': g_save_raw = 1; break;
            default:
                printf("Usage: %s -m <model.rknn> [-d mic_boost] [-r 16000] [-s]\n", argv[0]);
                printf("  -m  模型路径 (required)\n");
                printf("  -d  ALSA 设备 (default: mic_boost，已启用软件增益)\n");
                printf("      hw:4,0 = 原始USB摄像头麦，mic_boost = 软件增益版\n");
                printf("  -r  采样率 (default: 16000)\n");
                printf("  -s  保存原始录音到 mic_raw.wav\n");
                return 0;
        }
    }

    if (!model_path) {
        printf("Error: Model path (-m) is required!\n");
        printf("Usage: %s -m <model.rknn> [-d hw:0,0] [-r 16000]\n", argv[0]);
        return -1;
    }

    printf("\n");
    printf("================================================================================\n");
    printf("  YAMNet Real-time Anomaly Detection\n");
    printf("================================================================================\n");
    printf("  Model:    %s\n", model_path);
    printf("  Device:   %s (with software gain boost)\n", device);
    printf("  Rate:     %d Hz\n", sample_rate);
    printf("================================================================================\n\n");

    if (g_save_raw) {
        g_raw_capacity = SAVE_RAW_FRAMES;
        g_raw_audio = (float *)calloc(g_raw_capacity, sizeof(float));
        if (!g_raw_audio) {
            printf("[ERROR] Failed to allocate raw audio buffer\n");
            return -1;
        }
        printf("[INFO] Raw audio saving ENABLED\n\n");
    }

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化模型
    int ret;
    rknn_app_context_t rknn_app_ctx;
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

    ret = init_yamnet_model(model_path, &rknn_app_ctx);
    if (ret != 0) {
        printf("[ERROR] init_yamnet_model fail! ret=%d\n", ret);
        return -1;
    }

    // 读取标签
    LabelEntry label[LABEL_NUM];
    memset(label, 0, sizeof(label));
    ret = read_label(label);
    if (ret != 0) {
        printf("[ERROR] read_label fail!\n");
        return -1;
    }

    // 初始化环形缓冲区
    ring_init(&g_ring, sample_rate, RING_BUFFER_SEC);

    // 打开音频采集设备
    audio_capture_t *cap = capture_open(device, sample_rate, 1, 60);
    if (!cap) {
        printf("[ERROR] Failed to open audio device: %s\n", device);
        return -1;
    }

    // 开始采集
    ret = capture_start(cap);
    if (ret != 0) {
        printf("[ERROR] Failed to start capture\n");
        capture_close(cap);
        return -1;
    }

    printf("[INFO] Recording from microphone...\n");
    printf("[INFO] Press Ctrl+C to stop\n\n");

    ResultEntry result[TOP_N];
    int frames_per_chunk = (int)(sample_rate * CHUNK_DURATION_SEC);
    int frames_per_hop = (int)(sample_rate * HOP_SEC);
    double start_time = get_timestamp();
    int total_chunks = 0;
    int anomaly_count = 0;

    // 主循环
    while (g_running) {
        // 读取音频数据
        int got = capture_read(cap, frames_per_hop, 1000);
        if (got > 0) {
            float *all_data = capture_get_data(cap);
            int total_frames = capture_get_frames(cap);
            int start = total_frames - got;
            if (!all_data || start < 0) {
                continue;
            }
            float *latest_chunk = all_data + start;

            // 将数据放入环形缓冲区
            ring_push(&g_ring, latest_chunk, got);
            // 保存原始音频
            if (g_save_raw && g_raw_count + got <= g_raw_capacity) {
                memcpy(g_raw_audio + g_raw_count, latest_chunk, got * sizeof(float));
                g_raw_count += got;
            }
        }

        // 检查是否有足够的数据进行推理
        if (g_ring.size >= frames_per_chunk) {
            total_chunks++;

            // 获取最近一个 chunk 的数据
            float chunk_data[frames_per_chunk];
            ring_get_latest(&g_ring, chunk_data, frames_per_chunk);

            // 构建 audio_buffer
            audio_buffer_t audio;
            audio.data = chunk_data;
            audio.num_frames = frames_per_chunk;
            audio.num_channels = 1;
            audio.sample_rate = sample_rate;

            // 推理
            memset(result, 0, sizeof(result));
            ret = inference_yamnet_model(&rknn_app_ctx, &audio, label, result);

            double current_ts = get_timestamp() - start_time;

            if (ret == 0) {
                // 检测异常
                const char *matched_kw = NULL;
                float matched_score = 0.0f;
                int is_anom = is_anomaly(result, TOP_N, &matched_kw, &matched_score);

                // 每5个chunk打印一次Top结果（调试用）
                if (total_chunks % 5 == 1) {
                    printf("\n[DEBUG chunk #%03d @ %.1fs] Top results:\n", total_chunks, current_ts);
                    for (int d = 0; d < 5; d++) {
                        if (result[d].score < 0.01) break;
                        const char *name = (result[d].index >= 0 && result[d].index < LABEL_NUM && label[result[d].index].token)
                                           ? label[result[d].index].token : "unknown";
                        printf("  %2d. %-30s %.4f\n", d + 1, name, result[d].score);
                    }
                    if (is_anom) {
                        printf("  --> ANOMALY: %s (%.4f)\n", matched_kw, matched_score);
                    }
                }

                if (is_anom) {
                    anomaly_count++;

                    // 检查冷却期
                    if (current_ts - g_last_event_time > COOLDOWN_SEC) {
                        // 尝试完成之前的事件
                        try_finalize_event(current_ts, chunk_data, frames_per_chunk, sample_rate);
                    }

                    if (!g_current_event.active) {
                        // 开始新事件
                        g_current_event.active = 1;
                        g_current_event.start_sec = current_ts - CHUNK_DURATION_SEC;
                        g_current_event.last_trigger_sec = current_ts;
                        g_current_event.keyword_count = 0;
                        g_current_event.max_score = matched_score;
                        g_current_event.trigger_count = 1;
                        add_keyword(g_current_event.keywords, &g_current_event.keyword_count, matched_kw);

                        printf("[NEW] Event started at %.1fs, keyword: %s (%.4f)\n",
                               current_ts, matched_kw, matched_score);
                    } else {
                        // 更新当前事件
                        g_current_event.last_trigger_sec = current_ts;
                        g_current_event.trigger_count++;
                        add_keyword(g_current_event.keywords, &g_current_event.keyword_count, matched_kw);
                        if (matched_score > g_current_event.max_score) {
                            g_current_event.max_score = matched_score;
                        }

                        printf("[MERGE] Event continued, triggers: %d, keyword: %s (%.4f)\n",
                               g_current_event.trigger_count, matched_kw, matched_score);
                    }

                    g_last_event_time = current_ts;
                } else {
                    // 非异常，检查是否结束事件
                    if (g_current_event.active) {
                        try_finalize_event(current_ts, chunk_data, frames_per_chunk, sample_rate);
                    }
                }
            }

            // 进度显示
            if (total_chunks % 10 == 0) {
                printf("[RUNNING] Time: %.0fs | Chunks: %d | Events: %d | Anomaly: %d\n",
                       current_ts, total_chunks, g_event_count, anomaly_count);
            }
        }

        usleep(50000);  // 50ms 间隔
    }

    printf("\n\n");

    // 停止并清理
    capture_stop(cap);
    capture_close(cap);
    ring_free(&g_ring);

    // 保存原始录音
    if (g_save_raw && g_raw_audio && g_raw_count > 0) {
        save_audio("mic_raw.wav", g_raw_audio, g_raw_count, sample_rate, 1);
        printf("\n[SAVED] Raw audio: mic_raw.wav (%.1f sec, %d frames)\n",
               (float)g_raw_count / sample_rate, g_raw_count);
        free(g_raw_audio);
        g_raw_audio = NULL;
    }

    // 最终化最后一个事件
    double final_time = get_timestamp() - start_time;
    try_finalize_event(final_time, NULL, 0, sample_rate);

    // 释放模型
    release_yamnet_model(&rknn_app_ctx);
    for (int i = 0; i < LABEL_NUM; i++) {
        if (label[i].token) free(label[i].token);
    }

    // 打印统计
    printf("\n================================================================================\n");
    printf("  Session Summary\n");
    printf("================================================================================\n");
    printf("  Total Time:     %.1f seconds\n", final_time);
    printf("  Total Chunks:   %d\n", total_chunks);
    printf("  Anomaly Hits:   %d\n", anomaly_count);
    printf("  Events Saved:   %d\n", g_event_count);
    printf("================================================================================\n");

    if (g_event_count > 0) {
        printf("\n  Detected Events:\n");
        printf("  ---------------------------------------------------------------------------\n");
        printf("  %-6s %-12s %-12s %-8s %-6s  %s\n", 
               "ID", "Start(s)", "End(s)", "Dur(s)", "Score", "Keywords");
        printf("  ---------------------------------------------------------------------------\n");
        for (int i = 0; i < g_event_count; i++) {
            AnomalyEvent *e = &g_events[i];
            printf("  %-6d %-12.1f %-12.1f %-8.1f %-6.4f  ", 
                   e->event_id, e->start_time, e->end_time, 
                   e->end_time - e->start_time, e->max_score);
            for (int j = 0; j < e->keyword_count; j++) {
                printf("%s", e->keywords[j]);
                if (j < e->keyword_count - 1) printf(", ");
            }
            printf("\n");
        }
        printf("================================================================================\n");
    }

    printf("\n[OK] Session ended.\n");
    return 0;
}
