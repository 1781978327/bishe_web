#pragma once

#include <cstddef>

// Create a unique temporary WAV file path
int create_temp_wav_path(const char *prefix, char *path_out, size_t path_size);

// Check if FFmpeg real-time filter is configured and ready
int ffmpeg_rt_filter_is_ready();

// Filter a WAV file via FFmpeg, writing output to a specified path
int ffmpeg_filter_wav_to_path(const char *input_wav,
                              const char *output_wav,
                              int sample_rate,
                              int num_channels,
                              const char *context);

// Filter a WAV file via FFmpeg, writing output to a temporary file
int ffmpeg_filter_wav_to_temp(const char *input_wav,
                              char *output_wav,
                              size_t output_size,
                              int sample_rate,
                              int num_channels,
                              const char *context);

// Filter a WAV file via FFmpeg in-place (overwrite the original)
int ffmpeg_filter_wav_inplace(const char *wav_path,
                              int sample_rate,
                              int num_channels,
                              const char *context);

// Filter an in-memory audio buffer via FFmpeg in-place
int ffmpeg_filter_buffer_inplace(float *data,
                                 int num_frames,
                                 int sample_rate,
                                 int num_channels,
                                 const char *context);

// Check if SoX denoise is configured and ready
int sox_denoise_is_ready();

// Denoise a WAV file via SoX, writing output to a specified path
int sox_denoise_wav_to_path(const char *input_wav,
                            const char *output_wav,
                            const char *context);

// Denoise a WAV file via SoX, writing output to a temporary file
int sox_denoise_wav_to_temp(const char *input_wav,
                            char *output_wav,
                            size_t output_size,
                            const char *context);

// Denoise a WAV file via SoX in-place (overwrite the original)
int sox_denoise_wav_inplace(const char *wav_path, const char *context);

// Denoise an in-memory audio buffer via SoX in-place
int sox_denoise_buffer_inplace(float *data,
                               int num_frames,
                               int sample_rate,
                               int num_channels,
                               const char *context);

// Initialize FFmpeg filter configuration from environment variables
void init_rt_ffmpeg_filter_config();

// Initialize SoX denoise configuration from environment variables
void init_sox_denoise_config();
