import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.ProxySelector;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MultiServiceRiskAnalyzer {
    private static final Path WORK_DIR = Paths.get("/home/orangepi/Desktop/web/测试大模型");
    private static final Path PYTHON_EXAMPLE = WORK_DIR.resolve("new.py");
    private static final String DEFAULT_MODEL = "qwen3-vl:235b-instruct";

    public static void main(String[] args) {
        try {
            boolean dryRun = hasArg(args, "--dry-run");

            String apiKey = firstNonBlank(
                System.getenv("VISION_API_KEY"),
                System.getProperty("vision.apiKey"),
                readQuotedAssignment(PYTHON_EXAMPLE, "api_key")
            );
            String baseUrl = firstNonBlank(
                System.getenv("VISION_BASE_URL"),
                System.getProperty("vision.baseUrl"),
                readQuotedAssignment(PYTHON_EXAMPLE, "base_url"),
                "https://api.866646.xyz"
            );
            String proxyUrl = firstNonBlank(
                System.getenv("HTTPS_PROXY"),
                System.getenv("HTTP_PROXY"),
                System.getProperty("https.proxy"),
                System.getProperty("http.proxy")
            );
            String model = firstNonBlank(
                System.getenv("RISK_MODEL"),
                System.getProperty("risk.model"),
                DEFAULT_MODEL
            );

            HttpClient localClient = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(5))
                .version(HttpClient.Version.HTTP_1_1)
                .build();

            HttpClient.Builder aiClientBuilder = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(20))
                .version(HttpClient.Version.HTTP_1_1);
            if (!isBlank(proxyUrl)) {
                ProxySelector selector = buildProxySelector(proxyUrl);
                if (selector != null) {
                    aiClientBuilder.proxy(selector);
                }
            }
            HttpClient aiClient = aiClientBuilder.build();

            ServiceSnapshot sensorRecentHistory = fetchFirstAvailable(localClient,
                "sensor_recent_history_2",
                "http://127.0.0.1:8080/api/sensor/history?page=0&size=2"
            );
            ServiceSnapshot sensorThreshold = fetchFirstAvailable(localClient,
                "sensor_threshold",
                "http://127.0.0.1:8080/api/sensor/threshold"
            );
            ServiceSnapshot soundRealtimeStatus = fetchFirstAvailable(localClient,
                "sound_realtime_status",
                "http://127.0.0.1:8080/api/sound/realtime/status",
                "http://127.0.0.1:8089/realtime/status"
            );
            ServiceSnapshot soundRecentWindows = fetchFirstAvailable(localClient,
                "sound_recent_windows_5",
                "http://127.0.0.1:8080/api/sound/realtime/windows?limit=5",
                "http://127.0.0.1:8089/realtime/windows?limit=5"
            );
            ServiceSnapshot soundRealtimeEvents = fetchFirstAvailable(localClient,
                "sound_realtime_events",
                "http://127.0.0.1:8080/api/sound/realtime/events",
                "http://127.0.0.1:8089/realtime/events"
            );
            ServiceSnapshot visionStatus = fetchFirstAvailable(localClient,
                "vision_status",
                "http://127.0.0.1:8080/api/rknn/status",
                "http://127.0.0.1:8091/api/status"
            );
            ServiceSnapshot visionCountCam0 = fetchFirstAvailable(localClient,
                "vision_detection_count_cam0",
                "http://127.0.0.1:8080/api/rknn/detection/count?cam=0",
                "http://127.0.0.1:8091/api/detection/count?cam=0"
            );
            ServiceSnapshot visionCountCam1 = fetchFirstAvailable(localClient,
                "vision_detection_count_cam1",
                "http://127.0.0.1:8080/api/rknn/detection/count?cam=1",
                "http://127.0.0.1:8091/api/detection/count?cam=1"
            );
            ServiceSnapshot recentVideoAlerts = fetchFirstAvailable(localClient,
                "recent_video_alerts",
                "http://127.0.0.1:8080/api/detection/record/page?current=1&size=5&eventType=video"
            );

            boolean visionInferenceOriginallyOn = payloadContainsTrue(visionStatus, "\"inference_enabled\":true");
            BinarySnapshot cameraFrame1 = fetchBinaryFirstAvailable(localClient,
                "camera_frame_1",
                "http://127.0.0.1:8080/api/rknn/frame/current?cameraId=1&track=false",
                "http://127.0.0.1:8091/api/frame?track=0&cam=0"
            );
            BinarySnapshot cameraFrame2 = fetchBinaryFirstAvailable(localClient,
                "camera_frame_2",
                "http://127.0.0.1:8080/api/rknn/frame/current?cameraId=2&track=false",
                "http://127.0.0.1:8091/api/frame?track=0&cam=1"
            );

            boolean visionInferenceStartedByAnalyzer = false;
            if (!cameraFrame1.available || !cameraFrame2.available) {
                visionInferenceStartedByAnalyzer = tryStartVisionInference(localClient);
                if (visionInferenceStartedByAnalyzer) {
                    sleepQuietly(5000);
                    cameraFrame1 = fetchBinaryFirstAvailable(localClient,
                        "camera_frame_1",
                        "http://127.0.0.1:8080/api/rknn/frame/current?cameraId=1&track=false",
                        "http://127.0.0.1:8091/api/frame?track=0&cam=0"
                    );
                    cameraFrame2 = fetchBinaryFirstAvailable(localClient,
                        "camera_frame_2",
                        "http://127.0.0.1:8080/api/rknn/frame/current?cameraId=2&track=false",
                        "http://127.0.0.1:8091/api/frame?track=0&cam=1"
                    );
                    if (!visionInferenceOriginallyOn) {
                        tryStopVisionInference(localClient);
                    }
                }
            }

            List<String> missingSources = new ArrayList<>();
            collectMissing(missingSources, sensorRecentHistory);
            collectMissing(missingSources, sensorThreshold);
            collectMissing(missingSources, soundRealtimeStatus);
            collectMissing(missingSources, soundRecentWindows);
            collectMissing(missingSources, soundRealtimeEvents);
            collectMissing(missingSources, visionStatus);
            collectMissing(missingSources, visionCountCam0);
            collectMissing(missingSources, visionCountCam1);
            collectMissing(missingSources, recentVideoAlerts);
            collectMissing(missingSources, cameraFrame1);
            collectMissing(missingSources, cameraFrame2);

            String telemetrySummary = buildTelemetrySummary(
                sensorRecentHistory,
                sensorThreshold,
                soundRealtimeStatus,
                soundRecentWindows,
                soundRealtimeEvents,
                visionStatus,
                visionCountCam0,
                visionCountCam1,
                recentVideoAlerts,
                cameraFrame1,
                cameraFrame2,
                missingSources
            );

            String prompt = buildRiskPrompt(telemetrySummary, missingSources, cameraFrame1, cameraFrame2);

            System.out.println("=== Multi-Service Risk Analyzer ===");
            System.out.println("Time: " + LocalDateTime.now());
            System.out.println("Sensor history source: " + sensorRecentHistory.sourceLabel());
            System.out.println("Sound windows source: " + soundRecentWindows.sourceLabel());
            System.out.println("Camera frame 1 source: " + cameraFrame1.sourceLabel());
            System.out.println("Camera frame 2 source: " + cameraFrame2.sourceLabel());
            System.out.println("Vision inference auto-started: " + visionInferenceStartedByAnalyzer);
            System.out.println("Missing sources: " + (missingSources.isEmpty() ? "(none)" : String.join(", ", missingSources)));
            System.out.println();
            System.out.println("=== Telemetry Summary ===");
            System.out.println(telemetrySummary);

            if (dryRun) {
                System.out.println();
                System.out.println("=== AI Prompt Preview ===");
                System.out.println(prompt);
                return;
            }

            if (isBlank(apiKey)) {
                System.out.println();
                System.out.println("未找到 AI API Key。");
                System.out.println("你可以先用 --dry-run 看聚合数据，或者设置：");
                System.out.println("  export VISION_API_KEY=你的密钥");
                return;
            }

            String payload = buildMultimodalPayload(model, prompt, cameraFrame1, cameraFrame2);

            HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(trimTrailingSlash(baseUrl) + "/v1/chat/completions"))
                .timeout(Duration.ofSeconds(180))
                .version(HttpClient.Version.HTTP_1_1)
                .header("Authorization", "Bearer " + apiKey)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(payload, StandardCharsets.UTF_8))
                .build();

            System.out.println();
            System.out.println("=== AI Evaluation ===");
            System.out.println("Base URL: " + baseUrl);
            System.out.println("Model: " + model);
            System.out.println("Proxy: " + (isBlank(proxyUrl) ? "(none)" : proxyUrl));

            HttpResponse<String> response = aiClient.send(request, HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));
            if (response.statusCode() == 200) {
                String answer = extractAssistantContent(response.body());
                System.out.println("✅ AI 融合评估成功");
                System.out.println(answer == null ? response.body() : answer);
            } else {
                System.out.println("❌ AI 调用失败");
                System.out.println("状态码: " + response.statusCode());
                System.out.println("返回: " + response.body());
            }
        } catch (Exception e) {
            System.out.println("执行失败: " + e.getMessage());
            e.printStackTrace(System.out);
        }
    }

    private static String buildTelemetrySummary(
        ServiceSnapshot sensorRecentHistory,
        ServiceSnapshot sensorThreshold,
        ServiceSnapshot soundRealtimeStatus,
        ServiceSnapshot soundRecentWindows,
        ServiceSnapshot soundRealtimeEvents,
        ServiceSnapshot visionStatus,
        ServiceSnapshot visionCountCam0,
        ServiceSnapshot visionCountCam1,
        ServiceSnapshot recentVideoAlerts,
        BinarySnapshot cameraFrame1,
        BinarySnapshot cameraFrame2,
        List<String> missingSources
    ) {
        StringBuilder sb = new StringBuilder();
        appendBlock(sb, "sensor_recent_history_2", sensorRecentHistory.payloadOrUnavailable());
        appendBlock(sb, "sensor_threshold", sensorThreshold.payloadOrUnavailable());
        appendBlock(sb, "sound_realtime_status", soundRealtimeStatus.payloadOrUnavailable());
        appendBlock(sb, "sound_recent_windows_5", soundRecentWindows.payloadOrUnavailable());
        appendBlock(sb, "sound_realtime_events", soundRealtimeEvents.payloadOrUnavailable());
        appendBlock(sb, "vision_status", visionStatus.payloadOrUnavailable());
        appendBlock(sb, "vision_detection_count_cam0", visionCountCam0.payloadOrUnavailable());
        appendBlock(sb, "vision_detection_count_cam1", visionCountCam1.payloadOrUnavailable());
        appendBlock(sb, "recent_video_alerts", recentVideoAlerts.payloadOrUnavailable());
        appendBlock(sb, "camera_frame_1_meta", cameraFrame1.telemetryDescription());
        appendBlock(sb, "camera_frame_2_meta", cameraFrame2.telemetryDescription());
        appendBlock(sb, "missing_sources", missingSources.isEmpty() ? "[]" : missingSources.toString());
        return sb.toString();
    }

    private static String buildRiskPrompt(
        String telemetrySummary,
        List<String> missingSources,
        BinarySnapshot cameraFrame1,
        BinarySnapshot cameraFrame2
    ) {
        return ""
            + "请根据下面的多源监测数据与附带的两路摄像头当前帧，进行融合风险分析。\n"
            + "你需要重点综合：\n"
            + "1. 两张摄像头图片里的人员、姿态、冲突、跌倒、闯入、环境异常等视觉信息；\n"
            + "2. 最近 5 次声音实时窗口状态，注意这些窗口既可能是异常，也可能是非异常；\n"
            + "3. 最近 2 次传感器数据及当前阈值，判断异常是否持续；\n"
            + "4. 视觉服务状态、两路检测计数、近期视频报警记录；\n"
            + "5. 如果有缺失源，要明确写出不确定性。\n\n"
            + "附图说明：\n"
            + "- 第 1 张图：cameraId=1，对应第一路摄像头当前帧\n"
            + "- 第 2 张图：cameraId=2，对应第二路摄像头当前帧\n"
            + "- 如果某张图缺失，请在 missing_sources 与 reasons 里说明\n\n"
            + "风险等级定义：\n"
            + "1=安全正常，无明显异常；\n"
            + "2=轻微关注，有弱异常信号但暂无直接危险；\n"
            + "3=中等风险，出现持续异常或需要人工复核；\n"
            + "4=高风险，出现明确报警征兆，需要尽快处置；\n"
            + "5=紧急危险，存在明显人身/财产风险，应立即响应。\n\n"
            + "你必须只返回严格 JSON，格式如下：\n"
            + "{\n"
            + "  \"risk_level\": 1,\n"
            + "  \"level_name\": \"安全正常\",\n"
            + "  \"summary\": \"一句话总结当前风险\",\n"
            + "  \"camera_findings\": [\"图像观察1\", \"图像观察2\"],\n"
            + "  \"audio_findings\": [\"声音观察1\", \"声音观察2\"],\n"
            + "  \"sensor_findings\": [\"传感器观察1\", \"传感器观察2\"],\n"
            + "  \"reasons\": [\"原因1\", \"原因2\"],\n"
            + "  \"immediate_actions\": [\"建议1\", \"建议2\"],\n"
            + "  \"missing_sources\": [\"缺失源1\"]\n"
            + "}\n\n"
            + "下面是原始监测数据：\n"
            + telemetrySummary + "\n\n"
            + "当前缺失源列表：" + (missingSources.isEmpty() ? "[]" : missingSources.toString()) + "\n"
            + "图像可用性：camera1=" + cameraFrame1.available + ", camera2=" + cameraFrame2.available;
    }

    private static String buildMultimodalPayload(
        String model,
        String prompt,
        BinarySnapshot cameraFrame1,
        BinarySnapshot cameraFrame2
    ) {
        StringBuilder userContent = new StringBuilder();
        userContent.append("[");
        appendTextContent(userContent, prompt);

        if (cameraFrame1.available) {
            appendTextContent(userContent, "下面是第 1 张图，来自 cameraId=1 的当前帧。");
            appendImageContent(userContent, cameraFrame1);
        }
        if (cameraFrame2.available) {
            appendTextContent(userContent, "下面是第 2 张图，来自 cameraId=2 的当前帧。");
            appendImageContent(userContent, cameraFrame2);
        }

        userContent.append("]");

        return "{"
            + "\"model\":\"" + escapeJson(model) + "\","
            + "\"messages\":["
            + "{\"role\":\"system\",\"content\":\""
            + escapeJson("你是校园安全风险研判助手。你必须结合图像与多源监测数据做融合分析，并严格输出 JSON，不要输出 Markdown。")
            + "\"},"
            + "{\"role\":\"user\",\"content\":" + userContent + "}"
            + "]"
            + "}";
    }

    private static void appendTextContent(StringBuilder userContent, String text) {
        if (userContent.length() > 1) {
            userContent.append(",");
        }
        userContent.append("{\"type\":\"text\",\"text\":\"")
            .append(escapeJson(text))
            .append("\"}");
    }

    private static void appendImageContent(StringBuilder userContent, BinarySnapshot snapshot) {
        if (userContent.length() > 1) {
            userContent.append(",");
        }
        userContent.append("{\"type\":\"image_url\",\"image_url\":{\"url\":\"")
            .append(snapshot.dataUri())
            .append("\"}}");
    }

    private static void appendBlock(StringBuilder sb, String title, String content) {
        sb.append("[").append(title).append("]").append("\n");
        sb.append(content == null ? "(null)" : content).append("\n\n");
    }

    private static void collectMissing(List<String> missing, ServiceSnapshot snapshot) {
        if (snapshot == null || !snapshot.available) {
            missing.add(snapshot == null ? "unknown" : snapshot.name);
        }
    }

    private static void collectMissing(List<String> missing, BinarySnapshot snapshot) {
        if (snapshot == null || !snapshot.available) {
            missing.add(snapshot == null ? "unknown_binary" : snapshot.name);
        }
    }

    private static ServiceSnapshot fetchFirstAvailable(HttpClient client, String name, String... urls) {
        String lastError = "unavailable";
        String lastUrl = null;

        for (String url : urls) {
            if (isBlank(url)) {
                continue;
            }
            lastUrl = url;
            try {
                HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(url))
                    .timeout(Duration.ofSeconds(8))
                    .version(HttpClient.Version.HTTP_1_1)
                    .GET()
                    .build();
                HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));
                if (response.statusCode() >= 200 && response.statusCode() < 300) {
                    return new ServiceSnapshot(name, url, true, response.body(), null);
                }
                lastError = "HTTP " + response.statusCode();
            } catch (Exception e) {
                lastError = e.getClass().getSimpleName() + ": " + e.getMessage();
            }
        }

        return new ServiceSnapshot(name, lastUrl, false, null, lastError);
    }

    private static BinarySnapshot fetchBinaryFirstAvailable(HttpClient client, String name, String... urls) {
        String lastError = "unavailable";
        String lastUrl = null;

        for (String url : urls) {
            if (isBlank(url)) {
                continue;
            }
            lastUrl = url;
            try {
                HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(url))
                    .timeout(Duration.ofSeconds(8))
                    .version(HttpClient.Version.HTTP_1_1)
                    .GET()
                    .build();
                HttpResponse<byte[]> response = client.send(request, HttpResponse.BodyHandlers.ofByteArray());
                if (response.statusCode() >= 200 && response.statusCode() < 300
                    && response.body() != null && response.body().length > 0) {
                    String contentType = response.headers().firstValue("Content-Type").orElse("image/jpeg");
                    return new BinarySnapshot(name, url, true, response.body(), contentType, null);
                }
                lastError = "HTTP " + response.statusCode();
            } catch (Exception e) {
                lastError = e.getClass().getSimpleName() + ": " + e.getMessage();
            }
        }

        return new BinarySnapshot(name, lastUrl, false, null, null, lastError);
    }

    private static boolean tryStartVisionInference(HttpClient client) {
        try {
            HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create("http://127.0.0.1:8080/api/rknn/inference/on?track=false"))
                .timeout(Duration.ofSeconds(20))
                .version(HttpClient.Version.HTTP_1_1)
                .POST(HttpRequest.BodyPublishers.noBody())
                .build();
            HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));
            return response.statusCode() >= 200 && response.statusCode() < 300;
        } catch (Exception ignored) {
            return false;
        }
    }

    private static void tryStopVisionInference(HttpClient client) {
        try {
            HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create("http://127.0.0.1:8080/api/rknn/inference/off"))
                .timeout(Duration.ofSeconds(20))
                .version(HttpClient.Version.HTTP_1_1)
                .POST(HttpRequest.BodyPublishers.noBody())
                .build();
            client.send(request, HttpResponse.BodyHandlers.discarding());
        } catch (Exception ignored) {
        }
    }

    private static boolean payloadContainsTrue(ServiceSnapshot snapshot, String token) {
        return snapshot != null && snapshot.available && snapshot.payload != null && snapshot.payload.contains(token);
    }

    private static void sleepQuietly(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static String readQuotedAssignment(Path path, String variableName) {
        if (path == null || !Files.exists(path)) {
            return null;
        }
        try {
            String text = Files.readString(path, StandardCharsets.UTF_8);
            Pattern pattern = Pattern.compile("(?m)^\\s*" + Pattern.quote(variableName) + "\\s*=\\s*(?:r)?\"([^\"]*)\"");
            Matcher matcher = pattern.matcher(text);
            if (matcher.find()) {
                return matcher.group(1);
            }
        } catch (IOException ignored) {
        }
        return null;
    }

    private static String extractAssistantContent(String responseBody) {
        if (isBlank(responseBody)) {
            return null;
        }
        Pattern pattern = Pattern.compile("\"message\"\\s*:\\s*\\{.*?\"content\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"", Pattern.DOTALL);
        Matcher matcher = pattern.matcher(responseBody);
        if (!matcher.find()) {
            return null;
        }
        return unescapeJson(matcher.group(1));
    }

    private static String unescapeJson(String value) {
        if (value == null) {
            return null;
        }
        return value
            .replace("\\\\", "\\")
            .replace("\\\"", "\"")
            .replace("\\n", "\n")
            .replace("\\r", "\r")
            .replace("\\t", "\t")
            .replace("\\/", "/");
    }

    private static String escapeJson(String value) {
        if (value == null) {
            return "";
        }
        return value
            .replace("\\", "\\\\")
            .replace("\"", "\\\"")
            .replace("\n", "\\n")
            .replace("\r", "\\r")
            .replace("\t", "\\t");
    }

    private static String trimTrailingSlash(String value) {
        if (value == null) {
            return "";
        }
        int end = value.length();
        while (end > 0 && value.charAt(end - 1) == '/') {
            end--;
        }
        return value.substring(0, end);
    }

    private static String firstNonBlank(String... values) {
        for (String value : values) {
            if (!isBlank(value)) {
                return value;
            }
        }
        return null;
    }

    private static boolean hasArg(String[] args, String target) {
        for (String arg : args) {
            if (target.equals(arg)) {
                return true;
            }
        }
        return false;
    }

    private static ProxySelector buildProxySelector(String proxyUrl) {
        try {
            URI uri = URI.create(proxyUrl);
            String host = uri.getHost();
            int port = uri.getPort();
            if (isBlank(host) || port <= 0) {
                return null;
            }
            return ProxySelector.of(new InetSocketAddress(host, port));
        } catch (Exception ignored) {
            return null;
        }
    }

    private static boolean isBlank(String value) {
        return value == null || value.trim().isEmpty();
    }

    private static String normalizeContentType(String value) {
        if (isBlank(value)) {
            return "image/jpeg";
        }
        String trimmed = value.trim();
        int semicolon = trimmed.indexOf(';');
        if (semicolon > 0) {
            trimmed = trimmed.substring(0, semicolon).trim();
        }
        return trimmed.startsWith("image/") ? trimmed : "image/jpeg";
    }

    private static class ServiceSnapshot {
        private final String name;
        private final String url;
        private final boolean available;
        private final String payload;
        private final String error;

        private ServiceSnapshot(String name, String url, boolean available, String payload, String error) {
            this.name = name;
            this.url = url;
            this.available = available;
            this.payload = payload;
            this.error = error;
        }

        private String sourceLabel() {
            return available ? url : "(unavailable)";
        }

        private String payloadOrUnavailable() {
            if (available) {
                return payload;
            }
            return "{\"available\":false,\"error\":\"" + escapeJson(error == null ? "unavailable" : error) + "\"}";
        }
    }

    private static class BinarySnapshot {
        private final String name;
        private final String url;
        private final boolean available;
        private final byte[] data;
        private final String contentType;
        private final String error;

        private BinarySnapshot(String name, String url, boolean available, byte[] data, String contentType, String error) {
            this.name = name;
            this.url = url;
            this.available = available;
            this.data = data;
            this.contentType = contentType;
            this.error = error;
        }

        private String sourceLabel() {
            return available ? url : "(unavailable)";
        }

        private String telemetryDescription() {
            if (!available || data == null) {
                return "{\"available\":false,\"error\":\"" + escapeJson(error == null ? "unavailable" : error) + "\"}";
            }
            return "{"
                + "\"available\":true,"
                + "\"source\":\"" + escapeJson(url == null ? "" : url) + "\","
                + "\"contentType\":\"" + escapeJson(normalizeContentType(contentType)) + "\","
                + "\"bytes\":" + data.length
                + "}";
        }

        private String dataUri() {
            if (!available || data == null) {
                return "";
            }
            return "data:" + normalizeContentType(contentType) + ";base64," + Base64.getEncoder().encodeToString(data);
        }
    }
}
