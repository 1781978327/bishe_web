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
                Includes
-------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "yamnet.h"
#include "audio_utils.h"

/*-------------------------------------------
            Macro Definitions
-------------------------------------------*/
#define MAX_ANOMALY_CLIPS 1000
#define MAX_ANOMALY_EVENTS 100
#define HOP_LENGTH (CHUNK_LENGTH * SAMPLE_RATE / 2)  // 滑动步长：1.5秒重叠
#define MERGE_GAP_SEC 2.0f  // 间隔小于此值则合并为同一事件（秒）

// 异常事件结构体
typedef struct {
    float start_sec;
    float end_sec;
    char keywords[10][64];  // 最多记录10个不同关键词
    int keyword_count;
    float max_score;         // 最高置信度
    int chunk_count;        // 该事件包含的chunk数
    int chunk_indices[20];   // 包含的chunk索引
} AnomalyEvent;

static AnomalyEvent events[MAX_ANOMALY_EVENTS];
static int event_count = 0;

// 当前正在构建的事件
static AnomalyEvent current_event;
static int has_current_event = 0;

// 创建输出目录
static int create_output_dir(const char *base_path) {
    char output_dir[512];
    snprintf(output_dir, sizeof(output_dir), "%s_anomaly_output", base_path);
    
    char *dot = strrchr(output_dir, '/');
    if (dot) {
        char *ext = strrchr(dot, '.');
        if (ext && strcmp(ext, ".wav") == 0) {
            *ext = '\0';
        }
    }

    struct stat st;
    if (stat(output_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", output_dir);
        system(cmd);
    }

    if (mkdir(output_dir, 0755) != 0 && errno != EEXIST) {
        printf("[ERROR] Failed to create output directory: %s\n", output_dir);
        return -1;
    }
    return 0;
}

// 获取输出目录路径
static void get_output_dir(const char *audio_path, char *out_dir, size_t out_size) {
    snprintf(out_dir, out_size, "%s_anomaly_output", audio_path);
    char *dot = strrchr(out_dir, '/');
    if (dot) {
        char *ext = strrchr(dot, '.');
        if (ext && strcmp(ext, ".wav") == 0) {
            *ext = '\0';
        }
    }
}

// 添加关键词到事件中（避免重复）
static void add_keyword_to_event(AnomalyEvent *evt, const char *keyword, float score) {
    // 清理换行符和回车
    char cleaned[64];
    int j = 0;
    for (int i = 0; keyword[i] && j < (int)sizeof(cleaned) - 1; i++) {
        char c = keyword[i];
        if (c != '\n' && c != '\r') {
            cleaned[j++] = c;
        }
    }
    cleaned[j] = '\0';

    // 检查是否已存在
    for (int i = 0; i < evt->keyword_count; i++) {
        if (strcmp(evt->keywords[i], cleaned) == 0) {
            return;  // 已存在，不重复添加
        }
    }
    // 添加新关键词
    if (evt->keyword_count < 10) {
        strncpy(evt->keywords[evt->keyword_count], cleaned, 63);
        evt->keywords[evt->keyword_count][63] = '\0';
        evt->keyword_count++;
    }
}

// 开始新事件
static void start_new_event(float start_sec, const char *keyword, float score, int chunk_idx) {
    if (event_count >= MAX_ANOMALY_EVENTS) {
        printf("[WARN] Max events reached!\n");
        return;
    }
    
    AnomalyEvent *evt = &events[event_count];
    evt->start_sec = start_sec;
    evt->end_sec = start_sec + CHUNK_LENGTH;
    evt->keyword_count = 0;
    evt->max_score = score;
    evt->chunk_count = 1;
    evt->chunk_indices[0] = chunk_idx;
    add_keyword_to_event(evt, keyword, score);
    
    has_current_event = 1;
    current_event = *evt;
}

// 尝试合并到当前事件
static int try_merge_event(float chunk_start, float chunk_end, 
                          const char *keyword, float score, int chunk_idx) {
    float gap = chunk_start - current_event.end_sec;
    
    if (gap <= MERGE_GAP_SEC) {
        // 可以合并：扩展结束时间
        current_event.end_sec = chunk_end;
        current_event.max_score = (score > current_event.max_score) ? score : current_event.max_score;
        current_event.chunk_count++;
        if (current_event.chunk_count <= 20) {
            current_event.chunk_indices[current_event.chunk_count - 1] = chunk_idx;
        }
        add_keyword_to_event(&current_event, keyword, score);
        return 1;  // 合并成功
    }
    return 0;  // 需要开始新事件
}

// 结束当前事件并保存
static void finalize_current_event(float *full_audio, int total_frames, 
                                   const char *audio_path, int do_save) {
    if (!has_current_event) return;
    
    if (event_count >= MAX_ANOMALY_EVENTS) {
        has_current_event = 0;
        return;
    }
    
    // 保存事件
    events[event_count] = current_event;
    event_count++;
    has_current_event = 0;
    
    AnomalyEvent *evt = &events[event_count - 1];
    
    // 保存音频片段
    if (do_save) {
        char output_file[512];
        char output_dir[512];
        get_output_dir(audio_path, output_dir, sizeof(output_dir));
        
        // 清理关键词
        char safe_kw[64] = {0};
        for (int i = 0, j = 0; evt->keywords[0][i] && j < (int)sizeof(safe_kw) - 1; i++) {
            char c = evt->keywords[0][i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                (c >= '0' && c <= '9') || c == ' ' || c == '_') {
                safe_kw[j++] = c;
            }
        }
        
        snprintf(output_file, sizeof(output_file), "%s/%02d_%.1fs_%.1fs_%s.wav",
                output_dir, event_count - 1, evt->start_sec, evt->end_sec - evt->start_sec, safe_kw);
        
        int start_frame = (int)(evt->start_sec * SAMPLE_RATE);
        int end_frame = (int)(evt->end_sec * SAMPLE_RATE);
        if (end_frame > total_frames) end_frame = total_frames;
        int num_frames = end_frame - start_frame;
        
        if (num_frames > 0) {
            int ret = save_audio(output_file, full_audio + start_frame, 
                                num_frames, SAMPLE_RATE, 1);
            if (ret == 0) {
                printf("  [SAVE] %s\n", output_file);
            }
        }
    }
}

// 打印事件详情
static void print_event_details(AnomalyEvent *evt, int event_idx) {
    printf("\n  ===== Event #%02d =====\n", event_idx);
    printf("    Time:       %.1fs - %.1fs (%.1fs)\n", 
           evt->start_sec, evt->end_sec, evt->end_sec - evt->start_sec);
    printf("    Duration:   %d chunks (%.1fs)\n", evt->chunk_count, 
           evt->end_sec - evt->start_sec);
    printf("    Max Score:  %.4f\n", evt->max_score);
    printf("    Keywords:   ");
    for (int i = 0; i < evt->keyword_count; i++) {
        printf("%s", evt->keywords[i]);
        if (i < evt->keyword_count - 1) printf(", ");
    }
    printf("\n");
}

// 生成JSON格式的告警数据（供系统上报）
static void generate_json_report(const char *audio_path) {
    char output_file[512];
    char output_dir[512];
    get_output_dir(audio_path, output_dir, sizeof(output_dir));
    
    snprintf(output_file, sizeof(output_file), "%s/alarm_report.json", output_dir);
    
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        printf("[ERROR] Failed to create JSON report: %s\n", output_file);
        return;
    }
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"audio_file\": \"%s\",\n", audio_path);
    fprintf(fp, "  \"total_events\": %d,\n", event_count);
    fprintf(fp, "  \"events\": [\n");
    
    for (int i = 0; i < event_count; i++) {
        AnomalyEvent *evt = &events[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"event_id\": %d,\n", i);
        fprintf(fp, "      \"start_time\": %.2f,\n", evt->start_sec);
        fprintf(fp, "      \"end_time\": %.2f,\n", evt->end_sec);
        fprintf(fp, "      \"duration\": %.2f,\n", evt->end_sec - evt->start_sec);
        fprintf(fp, "      \"confidence\": %.4f,\n", evt->max_score);
        fprintf(fp, "      \"keywords\": [");
        for (int j = 0; j < evt->keyword_count; j++) {
            fprintf(fp, "\"%s\"", evt->keywords[j]);
            if (j < evt->keyword_count - 1) fprintf(fp, ", ");
        }
        fprintf(fp, "],\n");
        fprintf(fp, "      \"audio_file\": \"%02d_%.1fs_%.1fs_%s.wav\"\n", 
               i, evt->start_sec, evt->end_sec - evt->start_sec, evt->keywords[0]);
        fprintf(fp, "    }%s\n", i < event_count - 1 ? "," : "");
    }
    
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    printf("\n  [JSON Report] %s\n", output_file);
}

// 处理一个 chunk 的推理
static int process_chunk(rknn_app_context_t *app_ctx, 
                         float *full_audio, int total_frames,
                         int chunk_start_frame, int chunk_frames,
                         LabelEntry *label, ResultEntry *result) {
    audio_buffer_t chunk_audio;
    chunk_audio.data = full_audio + chunk_start_frame;
    chunk_audio.num_frames = chunk_frames;
    chunk_audio.num_channels = 1;
    chunk_audio.sample_rate = SAMPLE_RATE;

    return inference_yamnet_model(app_ctx, &chunk_audio, label, result);
}

/*-------------------------------------------
                  Main Function
-------------------------------------------*/
int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4)
    {
        printf("Usage: %s <model_path> <audio_path> [--save-anomaly]\n", argv[0]);
        printf("  --save-anomaly: Save detected anomaly audio clips\n");
        return -1;
    }

    const char *model_path = argv[1];
    const char *audio_path = argv[2];
    int save_anomaly = (argc >= 4 && strcmp(argv[3], "--save-anomaly") == 0);

    int ret = 0;
    TIMER timer;
    rknn_app_context_t rknn_app_ctx;
    audio_buffer_t full_audio;
    ResultEntry result[TOP_N];
    LabelEntry label[LABEL_NUM];
    int hop_frames;
    int num_chunks = 0;
    float total_duration = 0.0f;

    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));
    memset(&full_audio, 0, sizeof(audio_buffer_t));
    memset(result, 0, sizeof(result));
    memset(label, 0, sizeof(label));
    memset(events, 0, sizeof(events));
    event_count = 0;
    has_current_event = 0;

    printf("================================================================================\n");
    printf("  YAMNet 异常声音检测 (RKNN) - 事件合并版\n");
    printf("================================================================================\n");
    printf("  Model:   %s\n", model_path);
    printf("  Audio:   %s\n", audio_path);
    printf("  Save:    %s\n", save_anomaly ? "YES" : "NO");
    printf("  Merge:   gap < %.1fs\n", MERGE_GAP_SEC);
    printf("================================================================================\n\n");

    // ---- 读取标签 ----
    ret = read_label(label);
    if (ret != 0) {
        printf("[ERROR] read label fail! ret=%d\n", ret);
        goto out;
    }

    // ---- 读取音频 ----
    ret = read_audio(audio_path, &full_audio);
    if (ret != 0) {
        printf("[ERROR] read audio fail! ret=%d\n", ret);
        goto out;
    }

    if (full_audio.num_channels == 2) {
        ret = convert_channels(&full_audio);
        if (ret != 0) {
            printf("[ERROR] convert channels fail!\n");
            goto out;
        }
    }

    if (full_audio.sample_rate != SAMPLE_RATE) {
        ret = resample_audio(&full_audio, full_audio.sample_rate, SAMPLE_RATE);
        if (ret != 0) {
            printf("[ERROR] resample audio fail!\n");
            goto out;
        }
    }

    total_duration = (float)full_audio.num_frames / SAMPLE_RATE;
    printf("[INFO] Audio loaded: %.1f sec, %d Hz, %d channels, %d frames\n",
           total_duration, full_audio.sample_rate, full_audio.num_channels, full_audio.num_frames);

    // ---- 创建输出目录 ----
    if (save_anomaly) {
        ret = create_output_dir(audio_path);
        if (ret != 0) goto out;
    }

    // ---- 初始化模型 ----
    ret = init_yamnet_model(model_path, &rknn_app_ctx);
    if (ret != 0) {
        printf("[ERROR] init_yamnet_model fail! ret=%d\n", ret);
        goto out;
    }

    // ---- 滑动窗口遍历所有 chunk ----
    hop_frames = HOP_LENGTH;
    timer.tik();

    printf("\n[SCAN] Scanning audio in %.1fs chunks (hop=%.1fs)...\n",
           (float)CHUNK_LENGTH, (float)hop_frames / SAMPLE_RATE);
    printf("--------------------------------------------------------------------------------\n");

    for (int frame_pos = 0; frame_pos + N_SAMPLES <= full_audio.num_frames; frame_pos += hop_frames) {
        num_chunks++;

        int chunk_frames = N_SAMPLES;
        if (frame_pos + chunk_frames > full_audio.num_frames) {
            chunk_frames = full_audio.num_frames - frame_pos;
        }

        ret = process_chunk(&rknn_app_ctx, full_audio.data, full_audio.num_frames,
                           frame_pos, chunk_frames, label, result);
        if (ret != 0) {
            printf("[WARN] chunk %d inference failed\n", num_chunks);
            continue;
        }

        float chunk_start = (float)frame_pos / SAMPLE_RATE;
        float chunk_end = (float)(frame_pos + chunk_frames) / SAMPLE_RATE;

        // ---- 异常检测 ----
        const char *matched_kw = NULL;
        float matched_sc = 0.0f;
        int is_anom = is_anomaly(result, TOP_N, &matched_kw, &matched_sc);

        // DEBUG: 打印每个 chunk 的识别结果
        printf("[chunk #%03d @ %.1fs] ", num_chunks, chunk_start);
        for (int i = 0; i < TOP_N; i++) {
            if (result[i].token) {
                printf("%s(%.3f) ", result[i].token, result[i].score);
            }
        }
        if (is_anom) {
            printf("=> ANOMALY [%s %.3f]\n", matched_kw, matched_sc);
        } else {
            printf("=> normal\n");
        }

        if (is_anom) {
            if (has_current_event) {
                // 尝试合并到当前事件
                int merged = try_merge_event(chunk_start, chunk_end, matched_kw, matched_sc, num_chunks);
                if (merged) {
                    printf("  [MERGE] chunk #%03d (%.1fs) - %s (%.4f)\n", 
                           num_chunks, chunk_start, matched_kw, matched_sc);
                } else {
                    // 无法合并，保存当前事件，开始新事件
                    finalize_current_event(full_audio.data, full_audio.num_frames, audio_path, save_anomaly);
                    start_new_event(chunk_start, matched_kw, matched_sc, num_chunks);
                    printf("\n[NEW EVENT] chunk #%03d (%.1fs - %.1fs)\n", 
                           num_chunks, chunk_start, chunk_end);
                    printf("  Keyword: %s (score=%.4f)\n", matched_kw, matched_sc);
                }
            } else {
                // 开始新事件
                start_new_event(chunk_start, matched_kw, matched_sc, num_chunks);
                printf("\n[NEW EVENT] chunk #%03d (%.1fs - %.1fs)\n", 
                       num_chunks, chunk_start, chunk_end);
                printf("  Keyword: %s (score=%.4f)\n", matched_kw, matched_sc);
            }
        } else {
            // 非异常chunk，如果当前有事件在构建，尝试结束它
            if (has_current_event) {
                float gap = chunk_start - current_event.end_sec;
                if (gap > MERGE_GAP_SEC) {
                    // 超过合并间隔，保存当前事件
                    printf("  [END EVENT] gap=%.1fs > %.1fs, finalizing...\n", gap, MERGE_GAP_SEC);
                    finalize_current_event(full_audio.data, full_audio.num_frames, audio_path, save_anomaly);
                }
            }
        }

        if (num_chunks % 10 == 0) {
            printf("\r[PROGRESS] chunk %d/%d, events=%d...", 
                   num_chunks, 
                   (full_audio.num_frames + hop_frames - 1) / hop_frames,
                   event_count);
            fflush(stdout);
        }
    }

    // ---- 处理最后一个事件 ----
    if (has_current_event) {
        finalize_current_event(full_audio.data, full_audio.num_frames, audio_path, save_anomaly);
    }

    timer.tok();
    timer.print_time("total scan");
    printf("\n\n");

    // ---- 最终报告 ----
    printf("================================================================================\n");
    printf("  检测报告\n");
    printf("================================================================================\n");
    printf("  总音频时长:   %.1f 秒\n", total_duration);
    printf("  Chunk 大小:   %.1f 秒\n", (float)CHUNK_LENGTH);
    printf("  滑动步长:     %.1f 秒\n", (float)hop_frames / SAMPLE_RATE);
    printf("  扫描 Chunk 数: %d\n", num_chunks);
    printf("  合并后事件数:  %d\n", event_count);
    printf("================================================================================\n");

    // ---- 打印所有事件详情 ----
    for (int i = 0; i < event_count; i++) {
        print_event_details(&events[i], i);
    }

    // ---- 生成JSON报告 ----
    if (event_count > 0) {
        generate_json_report(audio_path);
        
        char output_dir[512];
        get_output_dir(audio_path, output_dir, sizeof(output_dir));
        printf("\n[OK] Results saved to: %s/\n", output_dir);
    } else {
        printf("\n[OK] No anomaly events detected.\n");
    }

out:
    release_yamnet_model(&rknn_app_ctx);

    for (int i = 0; i < LABEL_NUM; ++i) {
        if (label[i].token != NULL) {
            free(label[i].token);
            label[i].token = NULL;
        }
    }

    if (full_audio.data != NULL) {
        free(full_audio.data);
    }

    return 0;
}
