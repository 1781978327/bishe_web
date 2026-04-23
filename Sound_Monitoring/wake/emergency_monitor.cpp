// Emergency keyword monitor for Sound_Monitoring.
// Captures microphone audio with ALSA and runs keyword spotting through sherpa-onnx C API.

#include <alsa/asoundlib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "sherpa-onnx/c-api/c-api.h"

static volatile int g_keep_running = 1;
static FILE *g_log_file = NULL;

static void signal_handler(int) {
    g_keep_running = 0;
    printf("\n[WAKE] Stop signal received, shutting down...\n");
}

static int file_exists(const char *path) {
    if (!path || path[0] == '\0') return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

static std::string join_path(const std::string &dir, const std::string &name) {
    if (dir.empty()) return name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

static int model_dir_ready(const std::string &model_dir) {
    std::string encoder = join_path(model_dir, "encoder-epoch-12-avg-2-chunk-16-left-64.onnx");
    std::string decoder = join_path(model_dir, "decoder-epoch-12-avg-2-chunk-16-left-64.onnx");
    std::string joiner = join_path(model_dir, "joiner-epoch-12-avg-2-chunk-16-left-64.onnx");
    std::string tokens = join_path(model_dir, "tokens.txt");
    return file_exists(encoder.c_str()) &&
           file_exists(decoder.c_str()) &&
           file_exists(joiner.c_str()) &&
           file_exists(tokens.c_str());
}

static int resolve_model_dir(std::string *model_dir_out) {
    if (!model_dir_out) return 0;

    const char *env_model_dir = getenv("EMERGENCY_KWS_MODEL_DIR");
    std::vector<std::string> candidates;
    if (env_model_dir && env_model_dir[0]) {
        candidates.emplace_back(env_model_dir);
    }

    candidates.emplace_back("./sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01");
    candidates.emplace_back("./sherpa-onnx/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01");
    candidates.emplace_back("../sherpa-onnx/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01");
    candidates.emplace_back("../../语音唤醒/sherpa-onnx/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01");
    candidates.emplace_back("/home/orangepi/Desktop/web/语音唤醒/sherpa-onnx/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01");

    for (const auto &candidate : candidates) {
        if (model_dir_ready(candidate)) {
            *model_dir_out = candidate;
            return 1;
        }
    }

    return 0;
}

static void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

static void log_emergency_event(const char *keyword) {
    char ts[64];
    get_timestamp(ts, sizeof(ts));

    printf("\n============================================================\n");
    printf("[WAKE] Emergency keyword detected\n");
    printf("time: %s\n", ts);
    printf("keyword: %s\n", keyword ? keyword : "");
    printf("============================================================\n\n");

    if (g_log_file) {
        fprintf(g_log_file, "[%s] 检测到紧急关键词: %s\n", ts, keyword ? keyword : "");
        fflush(g_log_file);
    }
}

static int create_keywords_temp_file(char *path_out, size_t path_out_size) {
    if (!path_out || path_out_size == 0) return -1;

    char tmpl[] = "/tmp/emergency_keywords_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        fprintf(stderr, "[WAKE] Failed to create temp keywords file\n");
        return -1;
    }

    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        remove(tmpl);
        fprintf(stderr, "[WAKE] Failed to open temp keywords file\n");
        return -1;
    }

    // Format: "pinyin @display_keyword"
    const char *keywords[] = {
        "d ǎ j ià @打架",
        "j iù m ìng @救命",
        "j iù j iù w ǒ @救救我",
        "b āng b āng m áng @帮帮忙",
        "b āng zh ù w ǒ @帮助我",
        "b ào j ǐng @报警",
        "y āo y āo l íng @幺幺零",
        "y ī y ī l íng @一一零",
        "j iào j ǐng ch á @叫警察",
        "y ǒu r én @有人",
        "t íng zh ǐ @停止",
        "b ú y ào @不要",
        "z ǒu k āi @走开",
        "f àng k āi w ǒ @放开我",
        "w ēi x iǎn @危险",
        "t āo p ǎo @逃跑",
        "zh uī w ǒ @追我",
        "g ōng j ī @攻击",
        "sh ā r én @杀人",
        "q iǎng j ié @抢劫",
        "t ōu d ào @偷盗",
        "h uǒ z āi @火灾",
        "zh áo h uǒ @着火",
        "l òu q ì @漏气",
        "zh ōng d ú @中毒",
        "sh òu sh āng @受伤",
        "ch ū x uè @出血",
        "h ūn d ǎo @昏倒",
        "x īn z àng b ìng @心脏病",
        "j í j iù @急救",
        NULL
    };

    for (int i = 0; keywords[i] != NULL; ++i) {
        fprintf(f, "%s\n", keywords[i]);
    }
    fclose(f);

    strncpy(path_out, tmpl, path_out_size - 1);
    path_out[path_out_size - 1] = '\0';
    return 0;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    const char *log_path = getenv("EMERGENCY_KWS_LOG_PATH");
    if (!log_path || log_path[0] == '\0') {
        log_path = "./emergency_log.txt";
    }
    g_log_file = fopen(log_path, "a");
    if (!g_log_file) {
        fprintf(stderr, "[WAKE] Warning: failed to open log file: %s\n", log_path);
    }

    std::string model_dir;
    if (!resolve_model_dir(&model_dir)) {
        fprintf(stderr, "[WAKE] Failed to locate KWS model directory.\n");
        fprintf(stderr, "[WAKE] Set EMERGENCY_KWS_MODEL_DIR to a valid directory.\n");
        if (g_log_file) fclose(g_log_file);
        return -1;
    }
    printf("[WAKE] Model directory: %s\n", model_dir.c_str());

    char keywords_file[256] = {0};
    if (create_keywords_temp_file(keywords_file, sizeof(keywords_file)) != 0) {
        if (g_log_file) fclose(g_log_file);
        return -1;
    }

    std::string encoder = join_path(model_dir, "encoder-epoch-12-avg-2-chunk-16-left-64.onnx");
    std::string decoder = join_path(model_dir, "decoder-epoch-12-avg-2-chunk-16-left-64.onnx");
    std::string joiner = join_path(model_dir, "joiner-epoch-12-avg-2-chunk-16-left-64.onnx");
    std::string tokens = join_path(model_dir, "tokens.txt");

    SherpaOnnxKeywordSpotterConfig config;
    memset(&config, 0, sizeof(config));
    config.model_config.transducer.encoder = encoder.c_str();
    config.model_config.transducer.decoder = decoder.c_str();
    config.model_config.transducer.joiner = joiner.c_str();
    config.model_config.tokens = tokens.c_str();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 2;
    config.model_config.debug = 0;
    config.keywords_file = keywords_file;

    const SherpaOnnxKeywordSpotter *kws = SherpaOnnxCreateKeywordSpotter(&config);
    if (!kws) {
        fprintf(stderr, "[WAKE] Failed to create keyword spotter.\n");
        remove(keywords_file);
        if (g_log_file) fclose(g_log_file);
        return -1;
    }

    const SherpaOnnxOnlineStream *stream = SherpaOnnxCreateKeywordStream(kws);
    if (!stream) {
        fprintf(stderr, "[WAKE] Failed to create online stream.\n");
        SherpaOnnxDestroyKeywordSpotter(kws);
        remove(keywords_file);
        if (g_log_file) fclose(g_log_file);
        return -1;
    }

    const char *device = getenv("EMERGENCY_KWS_ALSA_DEVICE");
    if (!device || device[0] == '\0') {
        device = "default";
    }
    unsigned int sample_rate = 16000;
    int channels = 1;

    snd_pcm_t *capture_handle = NULL;
    snd_pcm_hw_params_t *hw_params = NULL;
    int err = snd_pcm_open(&capture_handle, device, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        fprintf(stderr, "[WAKE] Failed to open ALSA capture device %s: %s\n", device, snd_strerror(err));
        SherpaOnnxDestroyOnlineStream(stream);
        SherpaOnnxDestroyKeywordSpotter(kws);
        remove(keywords_file);
        if (g_log_file) fclose(g_log_file);
        return -1;
    }

    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(capture_handle, hw_params);
    snd_pcm_hw_params_set_access(capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(capture_handle, hw_params, &sample_rate, 0);
    snd_pcm_hw_params_set_channels(capture_handle, hw_params, channels);

    err = snd_pcm_hw_params(capture_handle, hw_params);
    if (err < 0) {
        fprintf(stderr, "[WAKE] Failed to set ALSA hardware params: %s\n", snd_strerror(err));
        snd_pcm_close(capture_handle);
        SherpaOnnxDestroyOnlineStream(stream);
        SherpaOnnxDestroyKeywordSpotter(kws);
        remove(keywords_file);
        if (g_log_file) fclose(g_log_file);
        return -1;
    }

    err = snd_pcm_prepare(capture_handle);
    if (err < 0) {
        fprintf(stderr, "[WAKE] Failed to prepare ALSA capture: %s\n", snd_strerror(err));
        snd_pcm_close(capture_handle);
        SherpaOnnxDestroyOnlineStream(stream);
        SherpaOnnxDestroyKeywordSpotter(kws);
        remove(keywords_file);
        if (g_log_file) fclose(g_log_file);
        return -1;
    }

    printf("[WAKE] Listening for emergency keywords on ALSA device: %s\n", device);
    printf("[WAKE] Press Ctrl+C to stop.\n");

    const int chunk_size = 1600;  // 100 ms @ 16 kHz
    short pcm[chunk_size];
    float samples[chunk_size];
    char last_keyword[256] = {0};

    while (g_keep_running) {
        err = snd_pcm_readi(capture_handle, pcm, chunk_size);
        if (err != chunk_size) {
            if (err < 0) {
                fprintf(stderr, "[WAKE] ALSA read error: %s\n", snd_strerror(err));
                snd_pcm_prepare(capture_handle);
            }
            continue;
        }

        for (int i = 0; i < chunk_size; ++i) {
            samples[i] = pcm[i] / 32768.0f;
        }

        SherpaOnnxOnlineStreamAcceptWaveform(stream, sample_rate, samples, chunk_size);

        while (SherpaOnnxIsKeywordStreamReady(kws, stream)) {
            SherpaOnnxDecodeKeywordStream(kws, stream);
            const SherpaOnnxKeywordResult *result = SherpaOnnxGetKeywordResult(kws, stream);

            if (result && result->keyword && result->keyword[0] != '\0') {
                if (strcmp(result->keyword, last_keyword) != 0) {
                    log_emergency_event(result->keyword);
                    strncpy(last_keyword, result->keyword, sizeof(last_keyword) - 1);
                    last_keyword[sizeof(last_keyword) - 1] = '\0';
                }
                SherpaOnnxResetKeywordStream(kws, stream);
            }

            if (result) {
                SherpaOnnxDestroyKeywordResult(result);
            }
        }
    }

    snd_pcm_close(capture_handle);
    SherpaOnnxDestroyOnlineStream(stream);
    SherpaOnnxDestroyKeywordSpotter(kws);
    remove(keywords_file);
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }

    printf("[WAKE] Monitor stopped.\n");
    return 0;
}
