/**
 * 声音异常检测 HTTP 服务器
 *
 * 提供 REST API 供 Spring Boot 调用 YAMNet 音频异常检测
 *
 * 编译:
 *   cd src/build && cmake .. && make
 *
 * 运行:
 *   sudo ./rknn_yamnet_demo_http 8089
 *
 * API 详见 sound_http_handlers.h
 */

#include "sound_globals.h"
#include "sound_audio_config.h"
#include "sound_denoise.h"
#include "sound_http_utils.h"
#include "sound_http_handlers.h"
#include "sound_kws.h"
#include "sound_rt_monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int init_model() {
    printf("Loading model from: %s\n", model_path);

    int ret = read_label(labels);
    if (ret != 0) {
        printf("[ERROR] read label fail! ret=%d\n", ret);
        return -1;
    }
    printf("Loaded %d labels\n", LABEL_NUM);

    ret = init_yamnet_model(model_path, &rknn_app_ctx);
    if (ret != 0) {
        printf("[ERROR] init_yamnet_model fail! ret=%d\n", ret);
        return -1;
    }

    model_initialized = 1;
    printf("[OK] YAMNet model initialized\n");

    const char *enable_asr_env = getenv("ENABLE_VOSK_ASR");
    int enable_asr = parse_bool_text_local(enable_asr_env, 0);
    const char *env_cn = getenv("VOSK_MODEL_CN");
    const char *env_en = getenv("VOSK_MODEL_EN");
    const char *default_cn_candidates[] = {
        "./model/vosk-model-small-cn-0.22",
        "../model/vosk-model-small-cn-0.22",
        NULL
    };
    const char *default_en_candidates[] = {
        "./model/vosk-model-small-en-us-0.15",
        "../model/vosk-model-small-en-us-0.15",
        NULL
    };

    if (!enable_asr) {
        printf("[INFO] ASR disabled by default. Set ENABLE_VOSK_ASR=1 and "
               "VOSK_MODEL_CN / VOSK_MODEL_EN to enable transcript.\n");
        return 0;
    }

    if (env_cn && env_cn[0]) {
        strncpy(g_asr_model_cn, env_cn, sizeof(g_asr_model_cn) - 1);
    } else {
        for (int i = 0; default_cn_candidates[i] != NULL; ++i) {
            if (path_exists(default_cn_candidates[i])) {
                strncpy(g_asr_model_cn, default_cn_candidates[i], sizeof(g_asr_model_cn) - 1);
                break;
            }
        }
    }

    if (env_en && env_en[0]) {
        strncpy(g_asr_model_en, env_en, sizeof(g_asr_model_en) - 1);
    } else {
        for (int i = 0; default_en_candidates[i] != NULL; ++i) {
            if (path_exists(default_en_candidates[i])) {
                strncpy(g_asr_model_en, default_en_candidates[i], sizeof(g_asr_model_en) - 1);
                break;
            }
        }
    }

    if (g_asr_model_cn[0] || g_asr_model_en[0]) {
        g_asr_enabled = g_asr_engine.Init(
            g_asr_model_cn[0] ? g_asr_model_cn : NULL,
            g_asr_model_en[0] ? g_asr_model_en : NULL) ? 1 : 0;

        if (g_asr_enabled) {
            printf("[OK] ASR initialized (CN=%s, EN=%s)\n",
                   g_asr_model_cn[0] ? g_asr_model_cn : "<none>",
                   g_asr_model_en[0] ? g_asr_model_en : "<none>");
        } else {
            printf("[WARN] ASR init failed: %s\n", g_asr_engine.LastError().c_str());
        }
    } else {
        printf("[INFO] ASR disabled. Set ENABLE_VOSK_ASR=1 and "
               "VOSK_MODEL_CN / VOSK_MODEL_EN to enable transcript.\n");
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int port = 8089;
    const char *auto_rt_env = getenv("AUTO_START_REALTIME");
    const char *rt_print_asr_env = getenv("RT_PRINT_ASR");
    const char *rt_print_window_env = getenv("RT_PRINT_WINDOW");
    const char *rt_capture_volume_env = getenv("RT_CAPTURE_VOLUME");
    const char *rt_capture_volume_percent_env = getenv("RT_CAPTURE_VOLUME_PERCENT");

    init_runtime_audio_yaml_config();
    init_emergency_kws_config();

    if (auto_rt_env && auto_rt_env[0]) {
        if (strcmp(auto_rt_env, "0") == 0 ||
            strcasecmp(auto_rt_env, "false") == 0 ||
            strcasecmp(auto_rt_env, "no") == 0) {
            g_auto_start_realtime = 0;
        } else {
            g_auto_start_realtime = 1;
        }
    }

    if (rt_print_asr_env && rt_print_asr_env[0]) {
        if (strcmp(rt_print_asr_env, "0") == 0 ||
            strcasecmp(rt_print_asr_env, "false") == 0 ||
            strcasecmp(rt_print_asr_env, "no") == 0) {
            g_rt_print_asr = 0;
        } else {
            g_rt_print_asr = 1;
        }
    }

    if (rt_print_window_env && rt_print_window_env[0]) {
        if (strcmp(rt_print_window_env, "0") == 0 ||
            strcasecmp(rt_print_window_env, "false") == 0 ||
            strcasecmp(rt_print_window_env, "no") == 0) {
            g_rt_print_window = 0;
        } else {
            g_rt_print_window = 1;
        }
    }

    if (rt_capture_volume_env && rt_capture_volume_env[0]) {
        float volume = (float)atof(rt_capture_volume_env);
        if (volume > 2.0f && volume <= 200.0f) {
            volume /= 100.0f;
        }
        if (volume >= 0.0f && volume <= 2.0f) {
            g_rt_capture_volume = volume;
        }
    }

    if (rt_capture_volume_percent_env && rt_capture_volume_percent_env[0]) {
        float volume_percent = (float)atof(rt_capture_volume_percent_env);
        if (volume_percent >= 0.0f && volume_percent <= 200.0f) {
            g_rt_capture_volume = volume_percent / 100.0f;
        }
    }

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("======================================================================\n");
    printf("  声音异常检测 HTTP 服务器\n");
    printf("======================================================================\n");

    init_rt_capture_mixer_config();
    init_rt_ffmpeg_filter_config();
    init_sox_denoise_config();

    if (init_model() != 0) {
        printf("[ERROR] Failed to initialize model\n");
        return 1;
    }

    if (start_server(port) < 0) {
        return 1;
    }

    if (g_auto_start_realtime) {
        int rt_ret = rt_start(g_rt_default_device);
        if (rt_ret == 0) {
            printf("[OK] Realtime auto-start enabled, device=%s\n", rt_device);
        } else if (rt_ret == -1) {
            printf("[INFO] Realtime already running\n");
        } else {
            printf("[WARN] Realtime auto-start failed (ret=%d). "
                   "Use POST /realtime/start to start manually.\n", rt_ret);
        }
    } else {
        printf("[INFO] Realtime auto-start disabled by AUTO_START_REALTIME=%s\n", auto_rt_env);
    }

    if (g_emergency_kws_auto_start) {
        int kws_ret = emergency_kws_start();
        if (kws_ret == 0) {
            printf("[OK] Emergency wake auto-start enabled\n");
        } else if (kws_ret == 1) {
            printf("[INFO] Emergency wake already running\n");
        } else {
            printf("[WARN] Emergency wake auto-start failed (ret=%d). "
                   "Use POST /wake/start to start manually.\n", kws_ret);
        }
    } else {
        printf("[INFO] Emergency wake auto-start disabled (EMERGENCY_KWS_AUTO_START=0)\n");
    }

    printf("\n======================================================================\n");
    printf("  服务器启动成功！\n");
    printf("======================================================================\n");
    printf("  端口: %d\n", port);
    printf("  音频配置文件: %s\n",
           g_runtime_audio_config_path[0] ? g_runtime_audio_config_path : "(not found, using built-in defaults)");
    printf("  默认实时输入: %s\n", g_rt_default_device);
    printf("  API:\n");
    printf("    POST /analyze           - 分析音频文件 (本地路径)\n");
    printf("    POST /analyze/upload    - 上传音频文件 (支持 MP3/WAV/OGG 等)\n");
    printf("    POST /realtime/start    - 启动实时麦克风监测\n");
    printf("    POST /realtime/stop     - 停止实时监测\n");
    printf("    GET  /realtime/status  - 查询监测状态\n");
    printf("    GET  /realtime/events  - 获取异常事件\n");
    printf("    GET  /realtime/windows?limit=5 - 获取最近窗口状态\n");
    printf("    GET  /realtime/transcript?seconds=120 - 获取最近麦克风转写\n");
    printf("    POST /wake/start       - 启动紧急关键词唤醒监测\n");
    printf("    POST /wake/stop        - 停止紧急关键词唤醒监测\n");
    printf("    GET  /wake/status      - 查询唤醒监测状态\n");
    printf("    GET  /wake/events?limit=20 - 获取最近关键词唤醒日志\n");
    printf("    GET  /health           - 健康检查\n");
    printf("    GET  /config           - 获取配置\n");
    printf("======================================================================\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        pthread_t thread;
        int *client_fd_ptr = (int*)malloc(sizeof(int));
        *client_fd_ptr = client_fd;
        pthread_create(&thread, NULL, client_handler, client_fd_ptr);
        pthread_detach(thread);
    }

    return 0;
}
