/**
 * audio_capture.h - Real-time audio capture from ALSA device
 * 
 * 支持从 USB 摄像头麦克风实时采集音频
 */

#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    void *handle;              // ALSA PCM 句柄
    int sample_rate;           // 采样率
    int channels;               // 通道数
    int frames_per_buffer;     // 每缓冲区帧数
    int total_frames;          // 总采集帧数
    float *data;               // 采集的数据 (float, 归一化)
    int data_size;             // data 缓冲区大小
    int16_t *raw_data;         // S16 临时缓冲区
    int raw_data_size;         // raw_data 缓冲区大小
    int is_recording;          // 正在采集标志
} audio_capture_t;

/**
 * 打开并初始化音频采集设备
 * 
 * @param device     ALSA 设备名，如 "hw:0,0" (card 0, device 0)
 * @param sample_rate 采样率，如 16000
 * @param channels   通道数，如 1 (单声道)
 * @param duration_sec 采集时长（秒）
 * @return           采集句柄，失败返回 NULL
 */
audio_capture_t* capture_open(const char *device, int sample_rate, int channels, int duration_sec);

/**
 * 开始采集音频
 * 
 * @param cap 采集句柄
 * @return    0 成功，-1 失败
 */
int capture_start(audio_capture_t *cap);

/**
 * 采集指定帧数的音频数据
 * 
 * @param cap    采集句柄
 * @param frames 要采集的帧数
 * @param timeout_ms 超时时间（毫秒）
 * @return        实际采集的帧数，-1 失败
 */
int capture_read(audio_capture_t *cap, int frames, int timeout_ms);

/**
 * 停止采集
 * 
 * @param cap 采集句柄
 */
void capture_stop(audio_capture_t *cap);

/**
 * 关闭并释放采集句柄
 * 
 * @param cap 采集句柄
 */
void capture_close(audio_capture_t *cap);

/**
 * 获取采集到的音频数据
 * 
 * @param cap 采集句柄
 * @return    音频数据指针（float 数组）
 */
float* capture_get_data(audio_capture_t *cap);

/**
 * 获取采集的总帧数
 * 
 * @param cap 采集句柄
 * @return    总帧数
 */
int capture_get_frames(audio_capture_t *cap);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_CAPTURE_H
