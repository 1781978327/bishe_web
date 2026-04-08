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

#include "yamnet.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void pad_or_trim(const std::vector<float> &array, std::vector<float> &result, int array_shape, int length)
{
    if (array_shape > length)
    {
        std::copy(array.begin(), array.begin() + length, result.begin());
    }
    else
    {
        std::copy(array.begin(), array.end(), result.begin());
        std::fill(result.begin() + array_shape, result.end(), 0.0f);
    }
}

static int argmax(float array[], int size)
{
    int max_index = 0;
    float max_value = array[0];

    for (int i = 1; i < size; i++)
    {
        if (array[i] > max_value)
        {
            max_index = i;
            max_value = array[i];
        }
    }

    return max_index;
}

static int compare_result(const void *a, const void *b)
{
    ResultEntry *ra = (ResultEntry *)a;
    ResultEntry *rb = (ResultEntry *)b;
    if (rb->score > ra->score) return 1;
    if (rb->score < ra->score) return -1;
    return 0;
}

int read_label(LabelEntry *label)
{
    FILE *fp;
    char line[256];

    fp = fopen(LABEL_PATH, "r");
    if (fp == NULL)
    {
        perror("Error opening file");
        return -1;
    }

    int count = 0;
    while (fgets(line, sizeof(line), fp))
    {
        label[count].token = strdup(strchr(line, ' ') + 1); // Get token after the first space
        label[count].index = atoi(line);                    // Get index before the first space
        count++;
    }

    fclose(fp);

    return 0;
}

int audio_preprocess(audio_buffer_t *audio, float *audio_pad_or_trim)
{
    std::vector<float> ori_audio_data(audio->data, audio->data + audio->num_frames);
    std::vector<float> audio_data(N_SAMPLES);

    pad_or_trim(ori_audio_data, audio_data, audio->num_frames, N_SAMPLES);
    memcpy(audio_pad_or_trim, audio_data.data(), N_SAMPLES * sizeof(float));
    return 0;
}

int post_process(float *scores, LabelEntry *label, ResultEntry *result)
{
    int num_rows = N_ROWS;
    int num_columns = LABEL_NUM;

    float mean_scores[LABEL_NUM] = {0};

    for (int j = 0; j < num_columns; j++)
    {
        float sum = 0;
        for (int i = 0; i < num_rows; i++)
        {
            sum += scores[i * num_columns + j];
        }
        mean_scores[j] = sum / num_rows;
    }

    // Fill all results
    for (int j = 0; j < TOP_N; j++)
    {
        result[j].index = -1;
        result[j].score = 0.0f;
        result[j].token = NULL;
    }

    for (int j = 0; j < num_columns; j++)
    {
        if (mean_scores[j] > result[TOP_N - 1].score)
        {
            result[TOP_N - 1].index = j;
            result[TOP_N - 1].score = mean_scores[j];
            result[TOP_N - 1].token = label[j].token;
            // Re-sort top N
            qsort(result, TOP_N, sizeof(ResultEntry), compare_result);
        }
    }

    return 0;
}

// ========== 异常检测实现 ==========

int check_anomaly_keywords(const AnomalyKeyword *keywords, int n_keywords,
                           ResultEntry *top_results, int top_n,
                           const char **matched_keyword, float *matched_score)
{
    int found_count = 0;
    *matched_keyword = NULL;
    *matched_score = 0.0f;

    for (int i = 0; i < top_n; i++)
    {
        if (top_results[i].token == NULL) continue;

        for (int k = 0; k < n_keywords; k++)
        {
            // 检查关键词是否出现在标签中（支持部分匹配）
            if (strstr(top_results[i].token, keywords[k].keyword) != NULL)
            {
                // 检查是否超过阈值
                if (top_results[i].score >= keywords[k].threshold)
                {
                    found_count++;
                    // 记录置信度最高的匹配
                    if (*matched_keyword == NULL || top_results[i].score > *matched_score)
                    {
                        *matched_keyword = top_results[i].token;
                        *matched_score = top_results[i].score;
                    }
                    printf("      [ALARM] Matched keyword '%s' at rank %d, score=%.4f\n",
                           keywords[k].keyword, i + 1, top_results[i].score);
                }
            }
        }
    }

    return found_count;
}

int is_anomaly(ResultEntry *top_results, int top_n, const char **matched_keyword, float *matched_score)
{
    int found = check_anomaly_keywords(
        anomaly_keywords,
        sizeof(anomaly_keywords) / sizeof(AnomalyKeyword),
        top_results, top_n,
        matched_keyword, matched_score);

    return found > 0 ? 1 : 0;
}
