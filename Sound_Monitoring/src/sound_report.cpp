#include "sound_report.h"
#include "sound_globals.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>

// Forward-declare from sound_http_utils.h
void json_escape_string(const char* input, char* output, size_t output_size);

// ========== Spring Boot 上报函数 ==========
int report_to_spring_boot_with_result(const char *audio_path, float duration,
                                       const char *keywords, float confidence,
                                       const char *result_prefix) {
    // 构建JSON请求体
    char json_body[2048];
    char detection_result[1024];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", tm_info);

    // JSON转义关键词
    char escaped_keywords[1024] = {0};
    json_escape_string(keywords, escaped_keywords, sizeof(escaped_keywords));

    if (confidence >= 0.0f) {
        snprintf(detection_result, sizeof(detection_result),
                 "%s - %s (置信度: %.2f%%)",
                 result_prefix && result_prefix[0] ? result_prefix : "声音异常",
                 escaped_keywords,
                 confidence * 100.0f);
    } else {
        snprintf(detection_result, sizeof(detection_result),
                 "%s - %s",
                 result_prefix && result_prefix[0] ? result_prefix : "声音异常",
                 escaped_keywords);
    }

    snprintf(json_body, sizeof(json_body),
        "{"
        "\"cameraId\": -1,"
        "\"cameraName\": \"声音监测\","
        "\"detectionTime\": \"%s\","
        "\"detectionResult\": \"%s\","
        "\"audioUrl\": \"%s\","
        "\"audioDuration\": %.1f,"
        "\"soundKeywords\": \"%s\""
        "}",
        time_str,
        detection_result,
        audio_path ? audio_path : "",
        duration,
        escaped_keywords);

    printf("[REPORT] Sending to Spring Boot: %s\n", json_body);

    // 创建socket连接
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("[REPORT ERROR] Failed to create socket\n");
        return -1;
    }

    struct timeval timeout;
    timeout.tv_sec = SPRING_BOOT_TIMEOUT_MS / 1000;
    timeout.tv_usec = (SPRING_BOOT_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct hostent *server = gethostbyname("localhost");
    if (server == NULL) {
        printf("[REPORT ERROR] Failed to resolve localhost\n");
        close(sock);
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(8080);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("[REPORT ERROR] Failed to connect to Spring Boot\n");
        close(sock);
        return -1;
    }

    // 构建HTTP请求
    char http_request[4096];
    int body_len = strlen(json_body);
    int req_len = snprintf(http_request, sizeof(http_request),
        "POST /api/detection/record/sound/report HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        body_len, json_body);

    // 发送请求
    int sent = send(sock, http_request, req_len, 0);
    if (sent < 0) {
        printf("[REPORT ERROR] Failed to send request\n");
        close(sock);
        return -1;
    }

    // 读取响应
    char response[1024];
    int received = recv(sock, response, sizeof(response) - 1, 0);
    close(sock);

    if (received > 0) {
        response[received] = '\0';
        // 检查HTTP状态码
        if (strstr(response, "200 OK") || strstr(response, "\"code\":200")) {
            printf("[REPORT] Successfully reported to Spring Boot\n");
            return 0;
        } else {
            printf("[REPORT WARNING] Unexpected response: %.100s\n", response);
            return -1;
        }
    }

    printf("[REPORT ERROR] No response from Spring Boot\n");
    return -1;
}

int report_to_spring_boot(const char *audio_path, float duration,
                          const char *keywords, float confidence) {
    return report_to_spring_boot_with_result(audio_path, duration, keywords, confidence, "声音异常");
}
