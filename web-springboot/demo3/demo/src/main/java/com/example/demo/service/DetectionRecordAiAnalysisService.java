package com.example.demo.service;

import com.example.demo.entity.DetectionRecord;
import com.example.demo.entity.SensorData;
import com.example.demo.entity.SensorThreshold;
import com.example.demo.repository.DetectionRecordRepository;
import com.example.demo.repository.SensorDataRepository;
import com.example.demo.repository.SensorThresholdRepository;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.annotation.PostConstruct;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.data.domain.PageRequest;
import org.springframework.scheduling.annotation.Async;
import org.springframework.stereotype.Service;

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
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

@Slf4j
@Service
@RequiredArgsConstructor
public class DetectionRecordAiAnalysisService {

    private static final Path UPLOAD_ROOT = Paths.get("./uploads").toAbsolutePath().normalize();
    private static final String DEFAULT_AI_BASE_URL = "https://api.866646.xyz";
    private static final String DEFAULT_AI_MODEL = "qwen3-vl:235b-instruct";

    private final DetectionRecordRepository detectionRecordRepository;
    private final SensorDataRepository sensorDataRepository;
    private final SensorThresholdRepository sensorThresholdRepository;
    private final SoundService soundService;
    private final RknnService rknnService;
    private final ObjectMapper objectMapper;

    @Value("${ai.analysis.enabled:true}")
    private boolean aiAnalysisEnabled;

    @Value("${ai.analysis.base-url:https://api.866646.xyz}")
    private String aiAnalysisBaseUrl;

    @Value("${ai.analysis.api-key:}")
    private String aiAnalysisApiKey;

    @Value("${ai.analysis.model:qwen3-vl:235b-instruct}")
    private String aiAnalysisModel;

    @Value("${ai.analysis.sensor-history-size:2}")
    private int sensorHistorySize;

    @Value("${ai.analysis.sound-window-limit:5}")
    private int soundWindowLimit;

    @PostConstruct
    public void initLocalAiEnvFallback() {
        applyLocalAiEnvFallback();
    }

    public void preparePendingAnalysis(DetectionRecord record) {
        if (record == null) {
            return;
        }
        if (!aiAnalysisEnabled) {
            record.setAiAnalysisStatus("SKIPPED");
            record.setAiAnalysisResult("AI融合分析已关闭");
            record.setAiAnalysisTime(LocalDateTime.now());
            return;
        }
        record.setAiAnalysisStatus("PENDING");
        record.setAiAnalysisResult(null);
        record.setAiAnalysisTime(null);
    }

    @Async
    public void requestAnalysis(Long recordId) {
        if (recordId == null) {
            return;
        }

        DetectionRecord initialRecord = detectionRecordRepository.findById(recordId).orElse(null);
        if (initialRecord == null) {
            return;
        }

        if (!aiAnalysisEnabled) {
            updateAnalysisResult(initialRecord, "SKIPPED", "AI融合分析已关闭");
            return;
        }

        if (isBlank(aiAnalysisApiKey)) {
            updateAnalysisResult(initialRecord, "FAILED", "未配置 VISION_API_KEY，无法执行 AI 融合分析");
            return;
        }

        updateAnalysisStatus(initialRecord, "RUNNING");

        try {
            Map<String, Object> context = buildAnalysisContext(initialRecord);
            List<ImageSnapshot> images = collectImages(initialRecord);
            String prompt = buildPrompt(context, images);
            String aiContent = callAi(prompt, images);
            updateAnalysisResult(initialRecord, "SUCCESS", aiContent);
            log.info("AI融合分析完成: recordId={}, status=SUCCESS", recordId);
        } catch (Exception e) {
            log.error("AI融合分析失败: recordId={}", recordId, e);
            updateAnalysisResult(initialRecord, "FAILED", "AI融合分析失败: " + safeMessage(e));
        }
    }

    private Map<String, Object> buildAnalysisContext(DetectionRecord record) {
        Map<String, Object> context = new LinkedHashMap<>();
        List<String> missingSources = new ArrayList<>();

        context.put("analysisTime", LocalDateTime.now().toString());
        context.put("triggerRecord", buildTriggerRecord(record));

        List<Map<String, Object>> sensorHistory = buildSensorHistory();
        context.put("sensorHistoryLatest", sensorHistory);
        if (sensorHistory.isEmpty()) {
            missingSources.add("sensorHistoryLatest");
        }

        Map<String, Object> thresholds = buildThresholds();
        context.put("sensorThresholds", thresholds);
        if (thresholds.isEmpty()) {
            missingSources.add("sensorThresholds");
        }

        Map<String, Object> soundStatus = soundService.getRealtimeStatus();
        context.put("soundRealtimeStatus", soundStatus);
        if (!isSuccessMap(soundStatus)) {
            missingSources.add("soundRealtimeStatus");
        }

        Map<String, Object> soundWindows = soundService.getRealtimeWindows(soundWindowLimit);
        context.put("soundRecentWindows", soundWindows);
        if (!isSuccessMap(soundWindows)) {
            missingSources.add("soundRecentWindows");
        }

        Map<String, Object> soundEvents = soundService.getRealtimeEvents();
        context.put("soundRealtimeEvents", soundEvents);
        if (!isSuccessMap(soundEvents)) {
            missingSources.add("soundRealtimeEvents");
        }

        Map<String, Object> visionStatus = rknnService.getStatus();
        context.put("visionStatus", visionStatus);
        if (!isVisionMapAvailable(visionStatus)) {
            missingSources.add("visionStatus");
        }

        Map<String, Object> visionCountCam0 = rknnService.getDetectionCount(0);
        context.put("visionDetectionCountCam0", visionCountCam0);
        if (!isVisionMapAvailable(visionCountCam0)) {
            missingSources.add("visionDetectionCountCam0");
        }

        Map<String, Object> visionCountCam1 = rknnService.getDetectionCount(1);
        context.put("visionDetectionCountCam1", visionCountCam1);
        if (!isVisionMapAvailable(visionCountCam1)) {
            missingSources.add("visionDetectionCountCam1");
        }

        context.put("missingSources", missingSources);
        return context;
    }

    private Map<String, Object> buildTriggerRecord(DetectionRecord record) {
        Map<String, Object> trigger = new LinkedHashMap<>();
        trigger.put("id", record.getId());
        trigger.put("cameraId", record.getCameraId());
        trigger.put("cameraName", record.getCameraName());
        trigger.put("eventType", inferEventType(record));
        trigger.put("detectionTime", record.getDetectionTime() == null ? null : record.getDetectionTime().toString());
        trigger.put("detectionResult", record.getAiDescription());
        trigger.put("hasImage", !isBlank(record.getImageUrl()));
        trigger.put("imageUrl", record.getImageUrl());
        trigger.put("hasAudio", !isBlank(record.getAudioUrl()));
        trigger.put("audioUrl", record.getAudioUrl());
        trigger.put("audioDuration", record.getAudioDuration());
        trigger.put("soundKeywords", record.getSoundKeywords());
        trigger.put("processed", Boolean.TRUE.equals(record.getIsProcessed()));
        return trigger;
    }

    private List<Map<String, Object>> buildSensorHistory() {
        return sensorDataRepository.findAllByOrderByCreateTimeDesc(
                PageRequest.of(0, Math.max(sensorHistorySize, 1)))
            .getContent()
            .stream()
            .map(this::toSensorMap)
            .toList();
    }

    private Map<String, Object> toSensorMap(SensorData sensorData) {
        Map<String, Object> item = new LinkedHashMap<>();
        item.put("temperature", sensorData.getTemperature());
        item.put("humidity", sensorData.getHumidity());
        item.put("smoke", sensorData.getSmoke());
        item.put("light", sensorData.getLight());
        item.put("alertMessage", sensorData.getAlertMessage());
        item.put("createTime", sensorData.getCreateTime() == null ? null : sensorData.getCreateTime().toString());
        return item;
    }

    private Map<String, Object> buildThresholds() {
        SensorThreshold threshold = sensorThresholdRepository.findTopByOrderByIdDesc().orElse(null);
        if (threshold == null) {
            return Map.of();
        }
        Map<String, Object> map = new LinkedHashMap<>();
        map.put("temperature", threshold.getTemperatureThreshold());
        map.put("humidity", threshold.getHumidityThreshold());
        map.put("smoke", threshold.getSmokeThreshold());
        map.put("light", threshold.getLightThreshold());
        return map;
    }

    private List<ImageSnapshot> collectImages(DetectionRecord record) {
        List<ImageSnapshot> images = new ArrayList<>();

        byte[] recordImage = tryReadRecordImage(record.getImageUrl());
        if (recordImage != null && recordImage.length > 0) {
            images.add(new ImageSnapshot("触发异常记录附图", "image/jpeg", recordImage));
            return images;
        }

        if (record.getCameraId() != null && (record.getCameraId() == 1L || record.getCameraId() == 2L)) {
            addCurrentFrame(images, record.getCameraId().intValue(),
                "当前摄像头画面 cameraId=" + record.getCameraId());
            return images;
        }

        addCurrentFrame(images, 1, "摄像头当前帧 cameraId=1");
        addCurrentFrame(images, 2, "摄像头当前帧 cameraId=2");
        return images;
    }

    private void addCurrentFrame(List<ImageSnapshot> images, int cameraId, String label) {
        try {
            byte[] data = rknnService.getCurrentFrame(cameraId, false);
            if (data != null && data.length > 0) {
                images.add(new ImageSnapshot(label, "image/jpeg", data));
            }
        } catch (Exception e) {
            log.debug("获取当前帧失败: cameraId={}, err={}", cameraId, e.getMessage());
        }
    }

    private byte[] tryReadRecordImage(String imageUrl) {
        if (isBlank(imageUrl) || !imageUrl.startsWith("/api/file/")) {
            return null;
        }

        try {
            String relativePath = imageUrl.substring("/api/file/".length());
            Path filePath = UPLOAD_ROOT.resolve(relativePath).normalize();
            if (!filePath.startsWith(UPLOAD_ROOT) || !Files.exists(filePath) || !Files.isRegularFile(filePath)) {
                return null;
            }
            return Files.readAllBytes(filePath);
        } catch (IOException e) {
            log.debug("读取异常附图失败: url={}, err={}", imageUrl, e.getMessage());
            return null;
        }
    }

    private String buildPrompt(Map<String, Object> context, List<ImageSnapshot> images) throws IOException {
        String contextJson = objectMapper.writerWithDefaultPrettyPrinter().writeValueAsString(context);
        return ""
            + "请根据下面的校园安全监测数据做一次跨服务融合研判。\n"
            + "你拿到的是某一条刚上报的异常记录，以及环境监测、声音监测、视觉监测三路的最新状态。\n"
            + "如果附带了图片，请结合图片判断现场是否存在人员冲突、跌倒、闯入、拥挤、异常行为或明显环境风险。\n"
            + "如果数据源缺失，请明确写出不确定性，不要编造。\n"
            + "必须只返回严格 JSON，不要输出 Markdown，不要加代码块。\n\n"
            + "返回格式：\n"
            + "{\n"
            + "  \"summary\": \"一句话总结\",\n"
            + "  \"risk_level\": 1,\n"
            + "  \"trigger_event_type\": \"video|sound|env|unknown\",\n"
            + "  \"cross_service_findings\": [\"发现1\", \"发现2\"],\n"
            + "  \"recommended_actions\": [\"建议1\", \"建议2\"],\n"
            + "  \"data_gaps\": [\"缺失项1\"]\n"
            + "}\n\n"
            + "补充要求：\n"
            + "- risk_level 取值范围 1 到 5。\n"
            + "- trigger_event_type 要根据触发记录判断。\n"
            + "- 如果图片不可用，也要仅基于文本信息继续分析。\n"
            + "- 交叉研判时要关注最近 2 条传感器数据、最近声音窗口、视觉检测计数与当前触发事件之间是否互相印证。\n"
            + "- 如果当前触发记录本身已经是明显异常，要在 summary 里说清楚。\n"
            + "- 当前附图数量：" + images.size() + "。\n\n"
            + "原始输入数据如下：\n"
            + contextJson;
    }

    private String callAi(String prompt, List<ImageSnapshot> images) throws Exception {
        HttpClient.Builder clientBuilder = HttpClient.newBuilder()
            .connectTimeout(Duration.ofSeconds(10))
            .version(HttpClient.Version.HTTP_1_1);

        String proxyUrl = firstNonBlank(
            System.getenv("HTTPS_PROXY"),
            System.getenv("HTTP_PROXY"),
            System.getProperty("https.proxy"),
            System.getProperty("http.proxy")
        );
        ProxySelector proxySelector = buildProxySelector(proxyUrl);
        if (proxySelector != null) {
            clientBuilder.proxy(proxySelector);
        }

        HttpClient client = clientBuilder.build();

        Map<String, Object> systemMessage = Map.of(
            "role", "system",
            "content", "你是校园安全多源融合分析助手。你必须结合异常记录、三个服务程序的最新状态以及可用图片做简洁可靠的研判，并严格输出 JSON。"
        );

        List<Object> userContent = new ArrayList<>();
        userContent.add(Map.of("type", "text", "text", prompt));
        for (ImageSnapshot image : images) {
            userContent.add(Map.of("type", "text", "text", image.label()));
            userContent.add(Map.of(
                "type", "image_url",
                "image_url", Map.of("url", image.dataUri())
            ));
        }

        Map<String, Object> userMessage = Map.of(
            "role", "user",
            "content", userContent
        );

        Map<String, Object> payload = new LinkedHashMap<>();
        payload.put("model", aiAnalysisModel);
        payload.put("messages", List.of(systemMessage, userMessage));

        String requestBody = objectMapper.writeValueAsString(payload);

        HttpRequest request = HttpRequest.newBuilder()
            .uri(URI.create(trimTrailingSlash(aiAnalysisBaseUrl) + "/v1/chat/completions"))
            .timeout(Duration.ofSeconds(180))
            .header("Authorization", "Bearer " + aiAnalysisApiKey)
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(requestBody, StandardCharsets.UTF_8))
            .build();

        HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));
        if (response.statusCode() < 200 || response.statusCode() >= 300) {
            throw new IllegalStateException("AI网关返回 HTTP " + response.statusCode() + ": " + response.body());
        }

        JsonNode root = objectMapper.readTree(response.body());
        JsonNode contentNode = root.path("choices").path(0).path("message").path("content");
        String content = contentNode.isMissingNode() || contentNode.isNull() ? response.body() : contentNode.asText();
        return stripMarkdownFence(content);
    }

    private void updateAnalysisStatus(DetectionRecord record, String status) {
        DetectionRecord latest = detectionRecordRepository.findById(record.getId()).orElse(record);
        latest.setAiAnalysisStatus(status);
        detectionRecordRepository.save(latest);
    }

    private void updateAnalysisResult(DetectionRecord record, String status, String result) {
        DetectionRecord latest = detectionRecordRepository.findById(record.getId()).orElse(record);
        latest.setAiAnalysisStatus(status);
        latest.setAiAnalysisResult(result);
        latest.setAiAnalysisTime(LocalDateTime.now());
        detectionRecordRepository.save(latest);
    }

    private boolean isSuccessMap(Map<String, Object> map) {
        if (map == null || map.isEmpty()) {
            return false;
        }
        Object success = map.get("success");
        return !(success instanceof Boolean b) || b;
    }

    private boolean isVisionMapAvailable(Map<String, Object> map) {
        if (map == null || map.isEmpty()) {
            return false;
        }
        Object success = map.get("success");
        if (success instanceof Boolean b && !b) {
            return false;
        }
        Object error = map.get("error");
        return error == null || error.toString().isBlank();
    }

    private String inferEventType(DetectionRecord record) {
        if ("声音监测".equals(record.getCameraName()) || !isBlank(record.getAudioUrl()) || !isBlank(record.getSoundKeywords())) {
            return "sound";
        }
        String description = record.getAiDescription();
        if ("环境监测".equals(record.getCameraName())) {
            return "env";
        }
        if (!isBlank(description)) {
            String normalized = description.toLowerCase(Locale.ROOT);
            if (normalized.contains("env_") && !normalized.contains("env_intrusion")) {
                return "env";
            }
            if (normalized.contains("声音异常") || normalized.contains("sound_")) {
                return "sound";
            }
            if (normalized.contains("fight") || normalized.contains("fall")
                || normalized.contains("knife") || normalized.contains("env_intrusion")) {
                return "video";
            }
        }
        return "unknown";
    }

    private String stripMarkdownFence(String content) {
        if (content == null) {
            return null;
        }
        String trimmed = content.trim();
        if (!trimmed.startsWith("```")) {
            return trimmed;
        }

        int firstLineBreak = trimmed.indexOf('\n');
        if (firstLineBreak < 0) {
            return trimmed.replace("```", "").trim();
        }

        String withoutStart = trimmed.substring(firstLineBreak + 1);
        int endFence = withoutStart.lastIndexOf("```");
        if (endFence >= 0) {
            return withoutStart.substring(0, endFence).trim();
        }
        return withoutStart.trim();
    }

    private ProxySelector buildProxySelector(String proxyUrl) {
        if (isBlank(proxyUrl)) {
            return null;
        }
        try {
            URI uri = URI.create(proxyUrl);
            if (isBlank(uri.getHost()) || uri.getPort() <= 0) {
                return null;
            }
            return ProxySelector.of(new InetSocketAddress(uri.getHost(), uri.getPort()));
        } catch (Exception ignored) {
            return null;
        }
    }

    private String trimTrailingSlash(String value) {
        if (value == null) {
            return "";
        }
        int end = value.length();
        while (end > 0 && value.charAt(end - 1) == '/') {
            end--;
        }
        return value.substring(0, end);
    }

    private String firstNonBlank(String... values) {
        for (String value : values) {
            if (!isBlank(value)) {
                return value;
            }
        }
        return null;
    }

    private boolean isBlank(String value) {
        return value == null || value.trim().isEmpty();
    }

    private String safeMessage(Exception e) {
        return e == null || e.getMessage() == null || e.getMessage().isBlank()
            ? e == null ? "未知错误" : e.getClass().getSimpleName()
            : e.getMessage();
    }

    private void applyLocalAiEnvFallback() {
        Map<String, String> localEnv = loadLocalAiEnv();
        if (localEnv.isEmpty()) {
            return;
        }

        boolean changed = false;

        if (isBlank(aiAnalysisApiKey)) {
            String apiKey = localEnv.get("VISION_API_KEY");
            if (!isBlank(apiKey)) {
                aiAnalysisApiKey = apiKey;
                changed = true;
            }
        }

        if (isBlank(aiAnalysisBaseUrl) || DEFAULT_AI_BASE_URL.equals(trimTrailingSlash(aiAnalysisBaseUrl))) {
            String baseUrl = localEnv.get("VISION_BASE_URL");
            if (!isBlank(baseUrl)) {
                aiAnalysisBaseUrl = baseUrl;
                changed = true;
            }
        }

        if (isBlank(aiAnalysisModel) || DEFAULT_AI_MODEL.equals(aiAnalysisModel)) {
            String model = localEnv.get("RISK_MODEL");
            if (!isBlank(model)) {
                aiAnalysisModel = model;
                changed = true;
            }
        }

        if (changed) {
            log.info("AI 融合分析配置已从本地 ai.env 加载: baseUrl={}, model={}, apiKeyLoaded={}",
                aiAnalysisBaseUrl, aiAnalysisModel, !isBlank(aiAnalysisApiKey));
        }
    }

    private Map<String, String> loadLocalAiEnv() {
        for (Path candidate : aiEnvCandidates()) {
            if (!Files.exists(candidate) || !Files.isRegularFile(candidate)) {
                continue;
            }

            try {
                Map<String, String> values = new LinkedHashMap<>();
                for (String rawLine : Files.readAllLines(candidate, StandardCharsets.UTF_8)) {
                    String line = rawLine.trim();
                    if (line.isEmpty() || line.startsWith("#")) {
                        continue;
                    }
                    if (line.startsWith("export ")) {
                        line = line.substring("export ".length()).trim();
                    }
                    int idx = line.indexOf('=');
                    if (idx <= 0) {
                        continue;
                    }
                    String key = line.substring(0, idx).trim();
                    String value = stripWrappingQuotes(line.substring(idx + 1).trim());
                    values.put(key, value);
                }

                if (!values.isEmpty()) {
                    log.info("检测到本地 AI 配置文件: {}", candidate.toAbsolutePath().normalize());
                    return values;
                }
            } catch (IOException e) {
                log.warn("读取本地 AI 配置文件失败: path={}, err={}", candidate, e.getMessage());
            }
        }

        return Map.of();
    }

    private List<Path> aiEnvCandidates() {
        Path cwd = Paths.get("").toAbsolutePath().normalize();
        List<Path> candidates = new ArrayList<>();
        candidates.add(cwd.resolve(".runtime/ai.env"));

        Path cursor = cwd;
        for (int i = 0; i < 6 && cursor != null; i++) {
            candidates.add(cursor.resolve(".runtime/ai.env").normalize());
            cursor = cursor.getParent();
        }
        return candidates;
    }

    private String stripWrappingQuotes(String value) {
        if (value == null || value.length() < 2) {
            return value;
        }
        char first = value.charAt(0);
        char last = value.charAt(value.length() - 1);
        if ((first == '\'' && last == '\'') || (first == '"' && last == '"')) {
            return value.substring(1, value.length() - 1);
        }
        return value;
    }

    private record ImageSnapshot(String label, String contentType, byte[] data) {
        private String dataUri() {
            return "data:" + contentType + ";base64," + Base64.getEncoder().encodeToString(data);
        }
    }
}
