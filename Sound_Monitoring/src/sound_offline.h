#pragma once

#include <stddef.h>
#include "sound_globals.h"
#include "asr_vosk.h"

// ========== Multipart/form-data 解析 ==========
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

// ========== Offline analysis functions ==========

AsrResult run_asr_locked(const float *data, int num_frames);

void reset_events();
void add_keyword(AnomalyEvent *evt, const char *keyword);
void start_event(float start_sec, const char *keyword, float score);
int merge_event(float chunk_end, const char *keyword, float score);
void finalize_event();

int convert_to_wav(const char *input_path, char *wav_path, size_t path_size);

int save_anomaly_audio_to_file(const char *original_path, float *audio_data,
                                int num_frames, int sample_rate,
                                int num_channels, const char *event_keywords,
                                float max_score, int event_idx);

int multipart_find(const char *data, int data_len,
                   const char *boundary, int boundary_len,
                   int start);

int multipart_parse_headers(const char *data, int data_len,
                             char *filename, size_t fn_size,
                             char *fieldname, size_t fn2_size);

double get_time_ms();

int analyze_audio(const char *audio_path, char *result_json, size_t json_size);

void handle_analyze(int client_fd, const char *body);
void handle_analyze_upload(int client_fd, const char *content_type,
                           const char *body, int body_len);
