#ifndef _RKNN_YAMNET_DEMO_PROCESS_H_
#define _RKNN_YAMNET_DEMO_PROCESS_H_

#include "rknn_api.h"
#include "easy_timer.h"

#define LABEL_NUM 521
#define SAMPLE_RATE 16000
#define CHUNK_LENGTH 3
#define N_SAMPLES CHUNK_LENGTH *SAMPLE_RATE // AUDIO_LENGTH
#define N_ROWS CHUNK_LENGTH * 2
#define LABEL_PATH "./model/yamnet_class_map.txt"

typedef struct
{
    int index;
    char *token;
    float score;
} ResultEntry;

typedef struct
{
    int index;
    char *token;
} LabelEntry;

#define TOP_N 10

// ========== 异常检测配置 ==========
#define ANOMALY_THRESHOLD 0.1     // 异常关键词最低置信度阈值
#define MAX_ANOMALY_KEYWORDS 20   // 最大关键词数量

typedef struct
{
    const char *keyword;          // 关键词（如 "Scream", "Cry"）
    float threshold;             // 该关键词的触发阈值（可单独设置）
} AnomalyKeyword;

// 异常关键词列表 - 前5名中出现这些词则触发异常
static const AnomalyKeyword anomaly_keywords[] = {
    {"Scream",   0.05},
    {"Screaming", 0.05},
    {"Cry",      0.05},
    {"Crying",   0.05},
    {"Sob",      0.05},
    {"Shriek",   0.05},
    {"Yell",     0.05},
    {"Yelling",  0.05},
    {"Shout",    0.05},
    {"Baby",     0.03},
    {"Wail",     0.05},
    {"Wailing",  0.05},
    {"Moan",     0.05},
    {"Moaning",  0.05},
    {"Whimper",  0.05},
    {"Whimpering", 0.05},
    {"Howl",     0.05},
    {"Howling",  0.05},
    {"Bawl",     0.05},
    {"Squeal",   0.05},
};

int audio_preprocess(audio_buffer_t *audio, float *audio_pad_or_trim);
int post_process(float *scores, LabelEntry *label, ResultEntry *result);
int read_label(LabelEntry *label);

// ========== 异常检测函数 ==========
// 检查前 N 名中是否包含异常关键词
// 返回值: 1=异常, 0=正常
// 输出: anomaly_keyword 匹配的关键词（如果有）
int is_anomaly(ResultEntry *top_results, int top_n, const char **matched_keyword, float *matched_score);

// 从关键词列表中查找匹配的关键词
// keywords: 关键词列表
// n_keywords: 关键词数量
// top_results: Top N 结果
// top_n: 前几名
// matched_keyword: 输出，匹配的关键词
// matched_score: 输出，该关键词的置信度
// 返回值: 匹配的个数
int check_anomaly_keywords(const AnomalyKeyword *keywords, int n_keywords,
                           ResultEntry *top_results, int top_n,
                           const char **matched_keyword, float *matched_score);

#endif //_RKNN_YAMNET_DEMO_PROCESS_H_
