#include "sound_denoise.h"
#include "sound_globals.h"
#include "sound_audio_config.h"
#include "sound_http_utils.h"
#include "audio_utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <algorithm>
#include <sstream>
#include <string>

int create_temp_wav_path(const char *prefix, char *path_out, size_t path_size) {
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

int ffmpeg_rt_filter_is_ready() {
    return g_rt_ffmpeg_filter_enabled && g_rt_ffmpeg_audio_filter[0] != '\0';
}

int ffmpeg_filter_wav_to_path(const char *input_wav,
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

int ffmpeg_filter_wav_to_temp(const char *input_wav,
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

int ffmpeg_filter_wav_inplace(const char *wav_path,
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

int ffmpeg_filter_buffer_inplace(float *data,
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

int sox_denoise_is_ready() {
    return g_sox_denoise_enabled && g_sox_denoise_profile[0] != '\0';
}

int sox_denoise_wav_to_path(const char *input_wav,
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

int sox_denoise_wav_to_temp(const char *input_wav,
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

int sox_denoise_wav_inplace(const char *wav_path, const char *context) {
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

int sox_denoise_buffer_inplace(float *data,
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

void init_rt_ffmpeg_filter_config() {
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

void init_sox_denoise_config() {
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
