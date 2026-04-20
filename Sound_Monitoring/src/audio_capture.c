/**
 * audio_capture.c - Real-time audio capture from ALSA / PulseAudio
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <alsa/asoundlib.h>

#include "audio_capture.h"

typedef enum {
    CAPTURE_BACKEND_ALSA = 0,
    CAPTURE_BACKEND_PAREC = 1,
} capture_backend_type_t;

typedef struct {
    capture_backend_type_t type;
    snd_pcm_t *pcm;
    int pipe_fd;
    pid_t parec_pid;
    char source_name[256];
} capture_backend_t;

static int is_parec_device(const char *device)
{
    if (!device || device[0] == '\0') return 0;
    return strcmp(device, "parec") == 0 ||
           strcmp(device, "pulse") == 0 ||
           strncmp(device, "parec:", 6) == 0 ||
           strncmp(device, "pulse:", 6) == 0;
}

static const char *extract_parec_source(const char *device)
{
    if (!device) return NULL;
    if (strncmp(device, "parec:", 6) == 0) return device + 6;
    if (strncmp(device, "pulse:", 6) == 0) return device + 6;
    return NULL;
}

static void close_parec_process(capture_backend_t *backend)
{
    if (!backend) return;

    if (backend->parec_pid > 0) {
        kill(backend->parec_pid, SIGTERM);
        waitpid(backend->parec_pid, NULL, 0);
        backend->parec_pid = -1;
    }

    if (backend->pipe_fd >= 0) {
        close(backend->pipe_fd);
        backend->pipe_fd = -1;
    }
}

static int start_parec_process(capture_backend_t *backend, int sample_rate, int channels)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        fprintf(stderr, "[PAREC ERROR] pipe failed: %s\n", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[PAREC ERROR] fork failed: %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        char rate_arg[64];
        char channels_arg[64];
        char format_arg[] = "--format=s16le";
        char device_arg[320];
        char *argv[10];
        int argc = 0;

        snprintf(rate_arg, sizeof(rate_arg), "--rate=%d", sample_rate);
        snprintf(channels_arg, sizeof(channels_arg), "--channels=%d", channels);

        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(pipefd[1]);

        argv[argc++] = (char *)"parec";
        argv[argc++] = (char *)"--record";
        argv[argc++] = (char *)"--raw";
        argv[argc++] = format_arg;
        argv[argc++] = rate_arg;
        argv[argc++] = channels_arg;

        if (backend->source_name[0] != '\0') {
            snprintf(device_arg, sizeof(device_arg), "--device=%s", backend->source_name);
            argv[argc++] = device_arg;
        }

        argv[argc++] = (char *)"/dev/fd/1";
        argv[argc] = NULL;

        execvp("parec", argv);
        _exit(127);
    }

    close(pipefd[1]);
    backend->pipe_fd = pipefd[0];
    backend->parec_pid = pid;

    usleep(100 * 1000);
    if (waitpid(pid, NULL, WNOHANG) == pid) {
        fprintf(stderr, "[PAREC ERROR] parec exited early\n");
        close(backend->pipe_fd);
        backend->pipe_fd = -1;
        backend->parec_pid = -1;
        return -1;
    }

    printf("[PAREC] Started source: %s, rate=%d, channels=%d\n",
           backend->source_name[0] ? backend->source_name : "default",
           sample_rate, channels);
    return 0;
}

static int open_alsa_device(capture_backend_t *backend, const char *device, int sample_rate, int channels)
{
    int ret;
    snd_pcm_t *pcm = NULL;
    unsigned int actual_rate = (unsigned int)sample_rate;

    ret = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_open: %s\n", snd_strerror(ret));
        return -1;
    }

    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    ret = snd_pcm_hw_params_any(pcm, hw_params);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_hw_params_any: %s\n", snd_strerror(ret));
        snd_pcm_close(pcm);
        return -1;
    }

    ret = snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_hw_params_set_access: %s\n", snd_strerror(ret));
        snd_pcm_close(pcm);
        return -1;
    }

    ret = snd_pcm_hw_params_set_format(pcm, hw_params, SND_PCM_FORMAT_S16_LE);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_hw_params_set_format: %s\n", snd_strerror(ret));
        snd_pcm_close(pcm);
        return -1;
    }

    ret = snd_pcm_hw_params_set_rate_near(pcm, hw_params, &actual_rate, 0);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_hw_params_set_rate_near: %s\n", snd_strerror(ret));
        snd_pcm_close(pcm);
        return -1;
    }

    ret = snd_pcm_hw_params_set_channels(pcm, hw_params, channels);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_hw_params_set_channels: %s\n", snd_strerror(ret));
        snd_pcm_close(pcm);
        return -1;
    }

    ret = snd_pcm_hw_params(pcm, hw_params);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_hw_params: %s\n", snd_strerror(ret));
        snd_pcm_close(pcm);
        return -1;
    }

    snd_pcm_sw_params_t *sw_params;
    snd_pcm_sw_params_alloca(&sw_params);
    ret = snd_pcm_sw_params_current(pcm, sw_params);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_sw_params_current: %s\n", snd_strerror(ret));
        snd_pcm_close(pcm);
        return -1;
    }

    ret = snd_pcm_sw_params_set_avail_min(pcm, sw_params, sample_rate / 10);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_sw_params_set_avail_min: %s\n", snd_strerror(ret));
        snd_pcm_close(pcm);
        return -1;
    }

    ret = snd_pcm_sw_params(pcm, sw_params);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_sw_params: %s\n", snd_strerror(ret));
        snd_pcm_close(pcm);
        return -1;
    }

    backend->type = CAPTURE_BACKEND_ALSA;
    backend->pcm = pcm;
    printf("[ALSA] Opened device: %s, rate=%u, channels=%d\n", device, actual_rate, channels);
    return 0;
}

audio_capture_t* capture_open(const char *device, int sample_rate, int channels, int duration_sec)
{
    audio_capture_t *cap = (audio_capture_t *)calloc(1, sizeof(audio_capture_t));
    if (!cap) return NULL;

    cap->sample_rate = sample_rate;
    cap->channels = channels;
    cap->frames_per_buffer = sample_rate / 10;
    cap->total_frames = 0;
    cap->is_recording = 0;
    cap->capture_gain = 1.0f;

    int buf_size = sample_rate * (duration_sec + 1) * channels;
    cap->data = (float *)calloc(buf_size, sizeof(float));
    if (!cap->data) {
        free(cap);
        return NULL;
    }
    cap->data_size = buf_size;

    cap->raw_data = (int16_t *)calloc(sample_rate * channels, sizeof(int16_t));
    if (!cap->raw_data) {
        free(cap->data);
        free(cap);
        return NULL;
    }
    cap->raw_data_size = sample_rate * channels;

    capture_backend_t *backend = (capture_backend_t *)calloc(1, sizeof(capture_backend_t));
    if (!backend) {
        free(cap->raw_data);
        free(cap->data);
        free(cap);
        return NULL;
    }
    backend->pipe_fd = -1;
    backend->parec_pid = -1;
    cap->handle = backend;

    if (is_parec_device(device)) {
        backend->type = CAPTURE_BACKEND_PAREC;
        const char *source_name = extract_parec_source(device);
        if (source_name && source_name[0] != '\0') {
            strncpy(backend->source_name, source_name, sizeof(backend->source_name) - 1);
            backend->source_name[sizeof(backend->source_name) - 1] = '\0';
        }
        printf("[PAREC] Prepared device: %s\n", device ? device : "parec");
        return cap;
    }

    if (open_alsa_device(backend, device, sample_rate, channels) != 0) {
        free(backend);
        free(cap->raw_data);
        free(cap->data);
        free(cap);
        return NULL;
    }

    return cap;
}

int capture_start(audio_capture_t *cap)
{
    if (!cap || !cap->handle) return -1;

    capture_backend_t *backend = (capture_backend_t *)cap->handle;
    if (backend->type == CAPTURE_BACKEND_PAREC) {
        if (start_parec_process(backend, cap->sample_rate, cap->channels) != 0) {
            return -1;
        }
        cap->is_recording = 1;
        cap->total_frames = 0;
        return 0;
    }

    int ret = snd_pcm_prepare(backend->pcm);
    if (ret < 0) {
        fprintf(stderr, "[ALSA ERROR] snd_pcm_prepare: %s\n", snd_strerror(ret));
        return -1;
    }

    ret = snd_pcm_start(backend->pcm);
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
    (void)timeout_ms;

    capture_backend_t *backend = (capture_backend_t *)cap->handle;

    int current_samples = cap->total_frames * cap->channels;
    int needed_output_samples = frames * cap->channels;

    if (current_samples + needed_output_samples > cap->data_size) {
        int new_size = current_samples + needed_output_samples + cap->sample_rate * cap->channels;
        float *new_data = (float *)realloc(cap->data, new_size * sizeof(float));
        if (!new_data) return -1;
        cap->data = new_data;
        cap->data_size = new_size;
    }

    int needed_samples = frames * cap->channels;
    if (needed_samples > cap->raw_data_size) {
        int16_t *new_raw = (int16_t *)realloc(cap->raw_data, needed_samples * sizeof(int16_t));
        if (!new_raw) return -1;
        cap->raw_data = new_raw;
        cap->raw_data_size = needed_samples;
    }

    int read_frames = 0;
    if (backend->type == CAPTURE_BACKEND_PAREC) {
        size_t bytes_needed = (size_t)frames * cap->channels * sizeof(int16_t);
        size_t bytes_read = 0;
        unsigned char *dst = (unsigned char *)cap->raw_data;

        while (bytes_read < bytes_needed) {
            ssize_t n = read(backend->pipe_fd, dst + bytes_read, bytes_needed - bytes_read);
            if (n > 0) {
                bytes_read += (size_t)n;
                continue;
            }
            if (n == 0) {
                if (bytes_read == 0) {
                    fprintf(stderr, "[PAREC ERROR] Stream closed\n");
                    return -1;
                }
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "[PAREC ERROR] read failed: %s\n", strerror(errno));
            return -1;
        }

        read_frames = (int)(bytes_read / (sizeof(int16_t) * cap->channels));
    } else {
        snd_pcm_sframes_t ret = snd_pcm_readi(backend->pcm, cap->raw_data, frames);

        if (ret == -EAGAIN) {
            return 0;
        } else if (ret == -EPIPE) {
            fprintf(stderr, "[ALSA WARNING] Buffer underrun, recovering...\n");
            ret = snd_pcm_prepare(backend->pcm);
            if (ret < 0) return -1;
            ret = snd_pcm_start(backend->pcm);
            if (ret < 0) return -1;
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "[ALSA ERROR] snd_pcm_readi: %s\n", snd_strerror(ret));
            return -1;
        }

        read_frames = (int)ret;
    }

    for (int i = 0; i < read_frames * cap->channels; i++) {
        float sample = cap->raw_data[i] / 32768.0f;
        sample *= cap->capture_gain;
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        cap->data[current_samples + i] = sample;
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
    if (!cap || !cap->handle) return;

    capture_backend_t *backend = (capture_backend_t *)cap->handle;
    cap->is_recording = 0;

    if (backend->type == CAPTURE_BACKEND_PAREC) {
        close_parec_process(backend);
        printf("[PAREC] Recording stopped, total frames: %d\n", cap->total_frames);
        return;
    }

    snd_pcm_drop(backend->pcm);
    printf("[ALSA] Recording stopped, total frames: %d\n", cap->total_frames);
}

void capture_close(audio_capture_t *cap)
{
    if (!cap) return;

    if (cap->handle) {
        capture_backend_t *backend = (capture_backend_t *)cap->handle;
        if (backend->type == CAPTURE_BACKEND_PAREC) {
            close_parec_process(backend);
        } else if (backend->pcm) {
            snd_pcm_close(backend->pcm);
            backend->pcm = NULL;
        }
        free(backend);
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
