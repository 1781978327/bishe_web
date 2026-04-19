/**
 * audio_capture.c - Real-time audio capture from ALSA device
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include <endian.h>
#include "audio_capture.h"

#define CHECK_RET(ret, msg) do { \
    if (ret < 0) { \
        fprintf(stderr, "[ALSA ERROR] %s: %s\n", msg, snd_strerror(ret)); \
        return NULL; \
    } \
} while(0)

audio_capture_t* capture_open(const char *device, int sample_rate, int channels, int duration_sec)
{
    int ret;
    audio_capture_t *cap = (audio_capture_t *)calloc(1, sizeof(audio_capture_t));
    if (!cap) return NULL;

    cap->sample_rate = sample_rate;
    cap->channels = channels;
    cap->frames_per_buffer = sample_rate / 10;  // 100ms per read
    cap->total_frames = 0;
    cap->is_recording = 0;
    cap->capture_gain = 1.0f;

    // 计算缓冲区大小：duration_sec + 1秒余量
    int buf_size = sample_rate * (duration_sec + 1);
    cap->data = (float *)calloc(buf_size, sizeof(float));
    if (!cap->data) {
        free(cap);
        return NULL;
    }
    cap->data_size = buf_size;

    // raw_data: 每次读取最多帧数的 S16 缓冲区
    cap->raw_data = (int16_t *)calloc(sample_rate, sizeof(int16_t));
    cap->raw_data_size = sample_rate;

    // 打开 PCM 设备
    snd_pcm_t *pcm;
    ret = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0);
    CHECK_RET(ret, "snd_pcm_open");
    cap->handle = pcm;

    // 设置硬件参数
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    ret = snd_pcm_hw_params_any(pcm, hw_params);
    CHECK_RET(ret, "snd_pcm_hw_params_any");

    ret = snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    CHECK_RET(ret, "snd_pcm_hw_params_set_access");

    ret = snd_pcm_hw_params_set_format(pcm, hw_params, SND_PCM_FORMAT_S16_LE);
    CHECK_RET(ret, "snd_pcm_hw_params_set_format");

    ret = snd_pcm_hw_params_set_rate_near(pcm, hw_params, (unsigned int *)&sample_rate, 0);
    CHECK_RET(ret, "snd_pcm_hw_params_set_rate_near");

    ret = snd_pcm_hw_params_set_channels(pcm, hw_params, channels);
    CHECK_RET(ret, "snd_pcm_hw_params_set_channels");

    ret = snd_pcm_hw_params(pcm, hw_params);
    CHECK_RET(ret, "snd_pcm_hw_params");

    // 设置软件参数（用于减少延迟）
    snd_pcm_sw_params_t *sw_params;
    snd_pcm_sw_params_alloca(&sw_params);
    ret = snd_pcm_sw_params_current(pcm, sw_params);
    CHECK_RET(ret, "snd_pcm_sw_params_current");

    ret = snd_pcm_sw_params_set_avail_min(pcm, sw_params, cap->frames_per_buffer);
    CHECK_RET(ret, "snd_pcm_sw_params_set_avail_min");

    ret = snd_pcm_sw_params(pcm, sw_params);
    CHECK_RET(ret, "snd_pcm_sw_params");

    printf("[ALSA] Opened device: %s, rate=%d, channels=%d\n", device, sample_rate, channels);

    return cap;
}

int capture_start(audio_capture_t *cap)
{
    if (!cap || !cap->handle) return -1;

    int ret = snd_pcm_prepare(cap->handle);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_prepare: %s\n", snd_strerror(ret));
        return -1;
    }

    ret = snd_pcm_start(cap->handle);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_start: %s\n", snd_strerror(ret));
        return -1;
    }

    cap->is_recording = 1;
    cap->total_frames = 0;
    printf("[ALSA] Recording started\n");
    return 0;
}

int capture_read(audio_capture_t *cap, int frames, int timeout_ms)
{
    if (!cap || !cap->handle || !cap->is_recording) return -1;

    // 确保缓冲区足够
    if (cap->total_frames + frames > cap->data_size) {
        // 扩展缓冲区
        int new_size = cap->total_frames + frames + cap->sample_rate;  // 多加1秒
        float *new_data = (float *)realloc(cap->data, new_size * sizeof(float));
        if (!new_data) return -1;
        cap->data = new_data;
        cap->data_size = new_size;
    }

    // 确保 raw_data 足够大
    if (frames > cap->raw_data_size) {
        cap->raw_data = (int16_t *)realloc(cap->raw_data, frames * sizeof(int16_t));
        cap->raw_data_size = frames;
    }

    // 读取 PCM 数据到 raw_data (S16 格式)
    snd_pcm_sframes_t ret = snd_pcm_readi(cap->handle, cap->raw_data, frames);

    if (ret == -EAGAIN) {
        // 非阻塞模式，需要重试
        return 0;
    } else if (ret == -EPIPE) {
        // 缓冲区下溢，尝试恢复
        fprintf(stderr, "[ALSA WARNING] Buffer underrun, recovering...\n");
        ret = snd_pcm_prepare(cap->handle);
        if (ret < 0) return -1;
        ret = snd_pcm_start(cap->handle);
        if (ret < 0) return -1;
        return 0;
    } else if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_readi: %s\n", snd_strerror(ret));
        return -1;
    }

    // 转换为 float 并归一化
    int read_frames = (int)ret;
    for (int i = 0; i < read_frames * cap->channels; i++) {
        float sample = cap->raw_data[i] / 32768.0f;
        sample *= cap->capture_gain;
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        cap->data[cap->total_frames + i] = sample;
    }

    cap->total_frames += read_frames;
    return read_frames;
}

void capture_set_gain(audio_capture_t *cap, float gain)
{
    if (!cap) return;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 2.0f) gain = 2.0f;
    cap->capture_gain = gain;
}

void capture_stop(audio_capture_t *cap)
{
    if (!cap) return;
    cap->is_recording = 0;
    if (cap->handle) {
        snd_pcm_drop(cap->handle);
        printf("[ALSA] Recording stopped, total frames: %d\n", cap->total_frames);
    }
}

void capture_close(audio_capture_t *cap)
{
    if (!cap) return;
    if (cap->handle) {
        snd_pcm_close(cap->handle);
        cap->handle = NULL;
    }
    if (cap->data) {
        free(cap->data);
        cap->data = NULL;
    }
    if (cap->raw_data) {
        free(cap->raw_data);
        cap->raw_data = NULL;
    }
    free(cap);
}

float* capture_get_data(audio_capture_t *cap)
{
    return cap ? cap->data : NULL;
}

int capture_get_frames(audio_capture_t *cap)
{
    return cap ? cap->total_frames : 0;
}
