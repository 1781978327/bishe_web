package com.example.demo.service;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.example.demo.dto.ForbiddenAreaPoint;
import com.example.demo.dto.ForbiddenAreaSaveRequest;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.*;
import org.springframework.stereotype.Service;
import org.springframework.util.StringUtils;
import org.springframework.web.client.RestClientException;
import org.springframework.web.client.RestTemplate;
import org.springframework.web.multipart.MultipartFile;
import org.springframework.web.util.UriComponentsBuilder;

import jakarta.annotation.PostConstruct;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.atomic.AtomicBoolean;

@Slf4j
@Service
public class RknnService {

    private static final ObjectMapper OBJECT_MAPPER = new ObjectMapper();

    private static final Path UPLOAD_ROOT = Paths.get("./uploads").toAbsolutePath().normalize();
    private static final Path VIDEO_UPLOAD_ROOT = UPLOAD_ROOT.resolve("videos").normalize();
    private static final long MAX_VIDEO_FILE_SIZE = 200L * 1024 * 1024;
    private static final Set<String> ALLOWED_VIDEO_EXTENSIONS = Set.of(
            ".mp4", ".mov", ".avi", ".mkv", ".flv", ".ts", ".m4v", ".webm"
    );
    private static final Set<String> REMOTE_VIDEO_SOURCE_PREFIXES = Set.of(
            "rtsp://", "rtmp://", "http://", "https://", "udp://", "tcp://"
    );

    @Autowired(required = false)
    private RestTemplate rknnServerRestTemplate;

    @Value("${rknn.server.host:localhost}")
    private String rknnServerHost;

    @Value("${rknn.server.port:8091}")
    private int rknnServerPort;

    private final AtomicBoolean inferenceEnabled = new AtomicBoolean(false);
    private final AtomicBoolean trackingEnabled = new AtomicBoolean(false);
    private final AtomicBoolean rtspStreaming = new AtomicBoolean(false);
    private final AtomicBoolean videoPlaying = new AtomicBoolean(false);
    private final ConcurrentMap<Long, ForbiddenAreaSaveRequest> forbiddenAreaStore = new ConcurrentHashMap<>();

    @PostConstruct
    public void init() {
        log.info("RknnService initialized");
        log.info("  RKNN Server: http://{}:{}", rknnServerHost, rknnServerPort);

        if (isServerAvailable()) {
            log.info("  RKNN HTTP 服务器可用");
        } else {
            log.warn("  RKNN HTTP 服务器不可用");
        }
    }

    private String getBaseUrl() {
        return String.format("http://%s:%d", rknnServerHost, rknnServerPort);
    }

    private String getApiUrl(String endpoint) {
        return getBaseUrl() + endpoint;
    }

    /**
     * 检查 RKNN 服务器是否可用
     */
    public boolean isServerAvailable() {
        if (rknnServerRestTemplate == null) return false;
        try {
            String url = getApiUrl("/api/status");
            ResponseEntity<Map> response = rknnServerRestTemplate.getForEntity(url, Map.class);
            return response.getStatusCode().is2xxSuccessful();
        } catch (RestClientException e) {
            log.debug("RKNN server not available: {}", e.getMessage());
            return false;
        }
    }

    // ==================== 推理控制 ====================

    /**
     * 开启推理（可选开启跟踪）
     */
    public Map<String, Object> startInference(boolean enableTracking) {
        return startInference(enableTracking, null);
    }

    /**
     * 开启推理（可选开启跟踪 + 跟踪算法）
     */
    public Map<String, Object> startInference(boolean enableTracking, String trackerBackend) {
        try {
            String normalizedTracker = normalizeTrackerBackend(trackerBackend);
            // 显式传递 track 参数，避免视觉服务默认行为导致“false 仍开启跟踪”。
            StringBuilder urlBuilder = new StringBuilder(getApiUrl("/api/inference/on"))
                .append("?track=").append(enableTracking ? "1" : "0");
            if (normalizedTracker != null) {
                urlBuilder.append("&tracker=").append(normalizedTracker);
            }
            String url = urlBuilder.toString();
            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(url, null, Map.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                inferenceEnabled.set(true);
                trackingEnabled.set(enableTracking);
                log.info("推理已开启, tracking={}, tracker={}", enableTracking,
                    normalizedTracker == null ? "(default)" : normalizedTracker);
                return response.getBody();
            }
            log.error("开启推理失败: {}", response.getStatusCode());
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (RestClientException e) {
            log.error("开启推理失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    private String normalizeTrackerBackend(String trackerBackend) {
        if (trackerBackend == null) {
            return null;
        }
        String normalized = trackerBackend.trim().toLowerCase(Locale.ROOT);
        if (normalized.isEmpty()) {
            return null;
        }
        if (!"bytetrack".equals(normalized) && !"deepsort".equals(normalized)) {
            throw new IllegalArgumentException("tracker 仅支持 bytetrack 或 deepsort");
        }
        return normalized;
    }

    private boolean parseBooleanLike(Object value) {
        if (value instanceof Boolean b) {
            return b;
        }
        if (value instanceof Number n) {
            return n.intValue() != 0;
        }
        if (value instanceof String s) {
            String normalized = s.trim().toLowerCase(Locale.ROOT);
            return "true".equals(normalized) || "1".equals(normalized) || "on".equals(normalized);
        }
        return false;
    }

    /**
     * 关闭推理
     */
    public Map<String, Object> stopInference() {
        try {
            String url = getApiUrl("/api/inference/off");
            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(url, null, Map.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                inferenceEnabled.set(false);
                trackingEnabled.set(false);
                log.info("推理已关闭");
                return response.getBody();
            }
            log.error("关闭推理失败: {}", response.getStatusCode());
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (RestClientException e) {
            log.error("关闭推理失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    /**
     * 下发模型与标签文件到视觉服务：
     * - 总是先调用 /api/inference/off?unload=1&model=...&labels=...
     * - 若切换前推理开启，则自动按原跟踪状态重启推理
     */
    public Map<String, Object> applyModelAndLabel(String modelPath, String labelPath) {
        try {
            Map<String, Object> status = getStatus();
            boolean inferenceWasEnabled = parseBooleanLike(status.get("inference_enabled"));
            boolean trackerWasEnabled = parseBooleanLike(status.get("tracker_enabled"));
            String trackerBackend = "bytetrack";
            try {
                String parsedTracker = normalizeTrackerBackend((String) status.get("tracker_backend"));
                if (parsedTracker != null) {
                    trackerBackend = parsedTracker;
                }
            } catch (IllegalArgumentException ignore) {
                // 状态接口异常值时使用默认值
            }

            String offUrl = UriComponentsBuilder.fromHttpUrl(getApiUrl("/api/inference/off"))
                    .queryParam("unload", 1)
                    .queryParam("model", modelPath)
                    .queryParam("labels", labelPath)
                    .build()
                    .encode()
                    .toUriString();
            ResponseEntity<Map> offResp = rknnServerRestTemplate.postForEntity(offUrl, null, Map.class);
            if (!offResp.getStatusCode().is2xxSuccessful()) {
                return Map.of("success", false, "error", "HTTP " + offResp.getStatusCode());
            }

            Map<String, Object> result = new LinkedHashMap<>();
            result.put("modelPath", modelPath);
            result.put("labelPath", labelPath);
            result.put("inferenceRestarted", inferenceWasEnabled);
            result.put("offResult", offResp.getBody());

            if (inferenceWasEnabled) {
                String onUrl = UriComponentsBuilder.fromHttpUrl(getApiUrl("/api/inference/on"))
                        .queryParam("track", trackerWasEnabled ? 1 : 0)
                        .queryParam("tracker", trackerBackend)
                        .queryParam("model", modelPath)
                        .queryParam("labels", labelPath)
                        .build()
                        .encode()
                        .toUriString();
                ResponseEntity<Map> onResp = rknnServerRestTemplate.postForEntity(onUrl, null, Map.class);
                if (!onResp.getStatusCode().is2xxSuccessful()) {
                    return Map.of("success", false, "error", "HTTP " + onResp.getStatusCode());
                }
                result.put("onResult", onResp.getBody());
            }

            result.put("success", true);
            return result;
        } catch (RestClientException e) {
            log.error("下发模型与标签失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    /**
     * 设置目标跟踪开关（推理开启时即时生效）
     */
    public Map<String, Object> setTrackerEnabled(boolean enabled) {
        try {
            String url = getApiUrl("/api/tracker/") + (enabled ? "1" : "0");
            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(url, null, Map.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                trackingEnabled.set(enabled);
                log.info("目标跟踪已{}", enabled ? "开启" : "关闭");
                return response.getBody();
            }
            log.error("设置目标跟踪失败: {}", response.getStatusCode());
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (RestClientException e) {
            log.error("设置目标跟踪失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    // ==================== RTSP 推流控制 ====================

    /**
     * 开启摄像头推流（两路 1280x720）
     */
    public Map<String, Object> startCameraRtsp() {
        try {
            String url = getApiUrl("/api/rtsp/start");
            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(url, null, Map.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                rtspStreaming.set(true);
                log.info("摄像头 RTSP 推流已开启");
                return response.getBody();
            }
            log.error("开启摄像头推流失败: {}", response.getStatusCode());
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (RestClientException e) {
            log.error("开启摄像头推流失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    /**
     * 开启视频推流（原始分辨率）
     */
    public Map<String, Object> startVideoRtsp() {
        try {
            String url = getApiUrl("/api/rtsp/video/start");
            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(url, null, Map.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                rtspStreaming.set(true);
                log.info("视频 RTSP 推流已开启");
                return response.getBody();
            }
            log.error("开启视频推流失败: {}", response.getStatusCode());
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (RestClientException e) {
            log.error("开启视频推流失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    /**
     * 停止 RTSP 推流
     */
    public Map<String, Object> stopRtsp() {
        try {
            String url = getApiUrl("/api/rtsp/stop");
            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(url, null, Map.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                rtspStreaming.set(false);
                log.info("RTSP 推流已停止");
                return response.getBody();
            }
            log.error("停止推流失败: {}", response.getStatusCode());
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (RestClientException e) {
            log.error("停止推流失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    // ==================== 摄像头录像控制 ====================

    public Map<String, Object> getRecordingStatus() {
        try {
            String url = getApiUrl("/api/record/status");
            ResponseEntity<Map> response = rknnServerRestTemplate.getForEntity(url, Map.class);

            if (response.getStatusCode().is2xxSuccessful() && response.getBody() != null) {
                return response.getBody();
            }
            return Map.of("success", false, "error", "获取录像状态失败");
        } catch (RestClientException e) {
            log.error("获取录像状态失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    public Map<String, Object> startCameraRecording(Integer cameraId, String name) {
        try {
            int cam = normalizeRecordingCameraId(cameraId);
            UriComponentsBuilder builder = UriComponentsBuilder
                    .fromHttpUrl(getApiUrl("/api/record/start"))
                    .queryParam("cam", cam);
            if (name != null && !name.trim().isEmpty()) {
                builder.queryParam("name", name.trim());
            }

            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(
                    builder.build().encode().toUriString(),
                    null,
                    Map.class
            );

            if (response.getStatusCode().is2xxSuccessful() && response.getBody() != null) {
                log.info("摄像头录像已开启: cameraId={}, cam={}, name={}", cameraId, cam, name);
                return response.getBody();
            }
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (IllegalArgumentException e) {
            throw e;
        } catch (RestClientException e) {
            log.error("开启摄像头录像失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    public Map<String, Object> stopCameraRecording(Integer cameraId) {
        try {
            UriComponentsBuilder builder = UriComponentsBuilder.fromHttpUrl(getApiUrl("/api/record/stop"));
            if (cameraId != null) {
                builder.queryParam("cam", normalizeRecordingCameraId(cameraId));
            }

            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(
                    builder.build().encode().toUriString(),
                    null,
                    Map.class
            );

            if (response.getStatusCode().is2xxSuccessful() && response.getBody() != null) {
                log.info("摄像头录像已停止: cameraId={}", cameraId);
                return response.getBody();
            }
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (IllegalArgumentException e) {
            throw e;
        } catch (RestClientException e) {
            log.error("停止摄像头录像失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    public Map<String, Object> listRecordingFiles(Integer cameraId) {
        try {
            Path outputDir = resolveRecordingOutputDir();
            String prefix = resolveRecordingFilePrefix(cameraId);
            List<Map<String, Object>> files = new ArrayList<>();

            try (var stream = Files.list(outputDir)) {
                stream.filter(Files::isRegularFile)
                    .filter(path -> path.getFileName().toString().toLowerCase(Locale.ROOT).endsWith(".mp4"))
                    .filter(path -> prefix.isEmpty() || path.getFileName().toString().startsWith(prefix))
                    .sorted(Comparator.comparingLong(this::safeLastModifiedTime).reversed())
                    .forEach(path -> files.add(buildRecordingFileInfo(path)));
            }

            Map<String, Object> result = new LinkedHashMap<>();
            result.put("recordOutputDir", outputDir.toString());
            result.put("files", files);
            return result;
        } catch (IllegalArgumentException e) {
            throw e;
        } catch (Exception e) {
            log.error("获取录像文件列表失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    public Path resolveRecordingFile(String fileName) throws IOException {
        if (fileName == null || fileName.isBlank()) {
            throw new IllegalArgumentException("文件名不能为空");
        }
        if (fileName.contains("/") || fileName.contains("\\") || !fileName.toLowerCase(Locale.ROOT).endsWith(".mp4")) {
            throw new IllegalArgumentException("仅支持下载 mp4 录像文件");
        }

        Path outputDir = resolveRecordingOutputDir();
        Path filePath = outputDir.resolve(fileName).normalize();
        if (!filePath.startsWith(outputDir)) {
            throw new IllegalArgumentException("非法文件路径");
        }
        if (!Files.exists(filePath) || !Files.isRegularFile(filePath)) {
            throw new NoSuchFileException(fileName);
        }
        return filePath;
    }

    private int normalizeRecordingCameraId(Integer cameraId) {
        if (cameraId == null) {
            throw new IllegalArgumentException("cameraId 不能为空");
        }
        if (cameraId == 1) return 0;
        if (cameraId == 2) return 1;
        throw new IllegalArgumentException("cameraId 仅支持 1 或 2");
    }

    private Path resolveRecordingOutputDir() throws IOException {
        Map<String, Object> status = getRecordingStatus();
        if (Boolean.FALSE.equals(status.get("success"))) {
            throw new IllegalStateException(String.valueOf(status.getOrDefault("error", "获取录像目录失败")));
        }

        Object rawDir = status.get("record_output_dir");
        String dirText = rawDir instanceof String ? (String) rawDir : "";
        if (dirText.isBlank()) {
            rawDir = status.get("recordOutputDir");
            if (!(rawDir instanceof String fallbackDir) || fallbackDir.isBlank()) {
                throw new IllegalStateException("视觉服务未返回录像目录");
            }
            dirText = fallbackDir;
        }

        Path outputDir = Paths.get(dirText).normalize();
        if (!Files.exists(outputDir) || !Files.isDirectory(outputDir)) {
            throw new IllegalStateException("录像目录不存在: " + outputDir);
        }
        return outputDir;
    }

    private String resolveRecordingFilePrefix(Integer cameraId) {
        if (cameraId == null) {
            return "";
        }
        int cam = normalizeRecordingCameraId(cameraId);
        return cam == 0 ? "cam0_" : "cam1_";
    }

    private long safeLastModifiedTime(Path path) {
        try {
            return Files.getLastModifiedTime(path).toMillis();
        } catch (IOException e) {
            return Long.MIN_VALUE;
        }
    }

    private Map<String, Object> buildRecordingFileInfo(Path path) {
        String name = path.getFileName().toString();
        int cameraId = name.startsWith("cam1_") ? 2 : 1;
        String cameraKey = cameraId == 1 ? "cam0" : "cam1";
        long size = 0L;
        String modifiedAt = "";
        try {
            size = Files.size(path);
            modifiedAt = Files.getLastModifiedTime(path).toInstant().toString();
        } catch (IOException e) {
            log.warn("读取录像文件元信息失败: file={}, err={}", path, e.getMessage());
        }

        Map<String, Object> item = new LinkedHashMap<>();
        item.put("name", name);
        item.put("cameraId", cameraId);
        item.put("cameraKey", cameraKey);
        item.put("size", size);
        item.put("modifiedAt", modifiedAt);
        item.put("downloadUrl", "/api/rknn/record/file?name=" + name);
        return item;
    }

    // ==================== 视频文件控制 ====================

    /**
     * 播放视频文件
     */
    public Map<String, Object> startVideo(String videoPath, boolean loop) {
        try {
            String body = "{\"path\":\"" + escapeJson(videoPath) + "\",\"loop\":" + (loop ? "true" : "false") + "}";
            Map<String, Object> response = postJsonWithRawHttp("/api/video/start", body);
            if (isOperationSuccess(response)) {
                videoPlaying.set(true);
                log.info("视频播放已开启: {}, loop={}", videoPath, loop);
                return response;
            }
            log.error("播放视频失败: {}", extractOperationError(response, "unknown"));
            return response;
        } catch (RestClientException e) {
            log.error("播放视频失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        } catch (IOException e) {
            log.error("播放视频失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    /**
     * 停止视频播放，恢复摄像头
     */
    public Map<String, Object> stopVideo() {
        try {
            String url = getApiUrl("/api/video/stop");
            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(url, null, Map.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                videoPlaying.set(false);
                log.info("视频播放已停止");
                return response.getBody();
            }
            log.error("停止视频失败: {}", response.getStatusCode());
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (RestClientException e) {
            log.error("停止视频失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    public Map<String, Object> uploadVideo(MultipartFile file) throws IOException {
        if (file == null || file.isEmpty()) {
            throw new IllegalArgumentException("视频文件不能为空");
        }
        if (file.getSize() > MAX_VIDEO_FILE_SIZE) {
            throw new IllegalArgumentException("视频文件不能超过 200MB");
        }

        String originalFilename = StringUtils.cleanPath(file.getOriginalFilename());
        if (!StringUtils.hasText(originalFilename)) {
            throw new IllegalArgumentException("视频文件名不能为空");
        }

        String extension = extractVideoExtension(originalFilename);
        if (!ALLOWED_VIDEO_EXTENSIONS.contains(extension)) {
            throw new IllegalArgumentException("仅支持 mp4/mov/avi/mkv/flv/ts/m4v/webm 视频文件");
        }

        String baseName = originalFilename.substring(0, originalFilename.length() - extension.length());
        String safeBaseName = sanitizeVideoFileName(baseName);
        if (!StringUtils.hasText(safeBaseName)) {
            safeBaseName = "video";
        }

        Path targetDir = VIDEO_UPLOAD_ROOT.resolve("default").normalize();
        Files.createDirectories(targetDir);
        if (!targetDir.startsWith(UPLOAD_ROOT)) {
            throw new IllegalStateException("视频上传目录非法");
        }

        Path target = allocateVideoUploadTarget(targetDir, safeBaseName, extension);
        file.transferTo(target.toFile());

        Map<String, Object> result = new LinkedHashMap<>();
        result.put("fileName", target.getFileName().toString());
        result.put("originalFileName", originalFilename);
        result.put("relativePath", VIDEO_UPLOAD_ROOT.relativize(target).toString().replace("\\", "/"));
        result.put("absolutePath", target.toString());
        result.put("url", "/api/uploads/videos/default/" + target.getFileName());
        result.put("size", file.getSize());
        return result;
    }

    public Map<String, Object> startVideoSource(String sourcePath,
                                                boolean loop,
                                                boolean startRtsp,
                                                boolean enableInference,
                                                boolean track,
                                                String trackerBackend) {
        String resolvedSourcePath = normalizeVideoSourcePath(sourcePath);

        Map<String, Object> result = new LinkedHashMap<>();
        result.put("sourcePath", resolvedSourcePath);
        result.put("loop", loop);
        result.put("startRtsp", startRtsp);
        result.put("enableInference", enableInference);
        result.put("track", track);
        if (trackerBackend != null && !trackerBackend.isBlank()) {
            result.put("tracker", trackerBackend.trim().toLowerCase(Locale.ROOT));
        }

        Map<String, Object> videoStartResult = startVideo(resolvedSourcePath, loop);
        result.put("videoStart", videoStartResult);
        if (!isOperationSuccess(videoStartResult)) {
            result.put("success", false);
            result.put("error", extractOperationError(videoStartResult, "启动视频/流源失败"));
            return result;
        }

        if (startRtsp) {
            // Wait for the main inference loop to pick up video mode and start decoding.
            // Without this delay, the RTSP sender may be created before any frame is
            // available, causing the first push to fail and rtsp_streaming to be reset.
            try { Thread.sleep(2000); } catch (InterruptedException ignored) {}

            Map<String, Object> rtspStartResult = startVideoRtsp();
            result.put("rtspStart", rtspStartResult);
            if (!isOperationSuccess(rtspStartResult)) {
                // Retry once after a short delay
                try { Thread.sleep(2000); } catch (InterruptedException ignored) {}
                rtspStartResult = startVideoRtsp();
                result.put("rtspStart", rtspStartResult);
            }
            if (!isOperationSuccess(rtspStartResult)) {
                result.put("success", false);
                result.put("error", extractOperationError(rtspStartResult, "启动视频推流失败"));
                return result;
            }

            // Verify rtsp_streaming is actually true; retry if not
            for (int attempt = 0; attempt < 5; attempt++) {
                try { Thread.sleep(1000); } catch (InterruptedException ignored) {}
                Map<String, Object> status = getStatus();
                Object streaming = status.get("rtsp_streaming");
                if (Boolean.TRUE.equals(streaming) || "true".equals(String.valueOf(streaming))) {
                    log.info("视频推流已确认就绪 (attempt {})", attempt + 1);
                    break;
                }
                if (attempt < 4) {
                    log.warn("rtsp_streaming 仍为 false，重试开启推流 (attempt {})", attempt + 1);
                    startVideoRtsp();
                }
            }
        }

        if (enableInference) {
            Map<String, Object> inferenceStartResult = startInference(track, trackerBackend);
            result.put("inferenceStart", inferenceStartResult);
            if (!isOperationSuccess(inferenceStartResult)) {
                result.put("success", false);
                result.put("error", extractOperationError(inferenceStartResult, "启动视频推理失败"));
                return result;
            }
        }

        result.put("videoStatus", getVideoStatus());
        result.put("status", getStatus());
        result.put("rtspUrls", getRtspUrls());
        result.put("success", true);
        return result;
    }

    public Map<String, Object> stopVideoSource(boolean stopRtsp, boolean restartCameraRtsp) {
        Map<String, Object> result = new LinkedHashMap<>();
        result.put("stopRtsp", stopRtsp);
        result.put("restartCameraRtsp", restartCameraRtsp);

        Map<String, Object> videoStopResult = stopVideo();
        result.put("videoStop", videoStopResult);
        if (!isOperationSuccess(videoStopResult)) {
            result.put("success", false);
            result.put("error", extractOperationError(videoStopResult, "停止视频源失败"));
            return result;
        }

        if (stopRtsp) {
            Map<String, Object> rtspStopResult = stopRtsp();
            result.put("rtspStop", rtspStopResult);
            if (!isOperationSuccess(rtspStopResult)) {
                result.put("success", false);
                result.put("error", extractOperationError(rtspStopResult, "停止视频推流失败"));
                return result;
            }
        }

        if (restartCameraRtsp) {
            Map<String, Object> cameraRtspResult = startCameraRtsp();
            result.put("cameraRtspRestart", cameraRtspResult);
            if (!isOperationSuccess(cameraRtspResult)) {
                result.put("success", false);
                result.put("error", extractOperationError(cameraRtspResult, "恢复摄像头推流失败"));
                return result;
            }
        }

        result.put("videoStatus", getVideoStatus());
        result.put("status", getStatus());
        result.put("success", true);
        return result;
    }

    // ==================== 状态查询 ====================

    /**
     * 获取全部状态
     */
    public Map<String, Object> getStatus() {
        try {
            String url = getApiUrl("/api/status");
            ResponseEntity<Map> response = rknnServerRestTemplate.getForEntity(url, Map.class);

            if (response.getStatusCode().is2xxSuccessful() && response.getBody() != null) {
                return response.getBody();
            }
            return Map.of("success", false, "error", "获取状态失败");
        } catch (RestClientException e) {
            log.error("获取状态失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    /**
     * 获取视频状态
     */
    public Map<String, Object> getVideoStatus() {
        try {
            String url = getApiUrl("/api/video/status");
            ResponseEntity<Map> response = rknnServerRestTemplate.getForEntity(url, Map.class);

            if (response.getStatusCode().is2xxSuccessful() && response.getBody() != null) {
                return response.getBody();
            }
            return Map.of("success", false, "error", "获取视频状态失败");
        } catch (RestClientException e) {
            log.error("获取视频状态失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    public Map<String, Object> getVideoSourceStatus() {
        Map<String, Object> videoStatus = getVideoStatus();
        Map<String, Object> status = getStatus();

        if (!isOperationSuccess(videoStatus) && !isOperationSuccess(status)) {
            return Map.of(
                    "success", false,
                    "error", extractOperationError(status, extractOperationError(videoStatus, "获取视频源状态失败"))
            );
        }

        Map<String, Object> result = new LinkedHashMap<>();
        Object rawVideoMode = status.get("video_mode");
        if (rawVideoMode == null) {
            rawVideoMode = videoStatus.get("video_mode");
        }
        result.put("videoMode", parseBooleanLike(rawVideoMode));
        result.put("videoPath", readString(videoStatus, "video_path", "videoPath"));
        result.put("videoLoop", parseBooleanLike(videoStatus.get("video_loop")));
        result.put("inferenceEnabled", parseBooleanLike(status.get("inference_enabled")));
        result.put("trackerEnabled", parseBooleanLike(status.get("tracker_enabled")));
        result.put("trackerBackend", readString(status, "tracker_backend", "trackerBackend"));
        result.put("rtspStreaming", parseBooleanLike(status.get("rtsp_streaming")));
        result.put("rtspUrl", readString(status, "rtsp_url_video", "rtspUrlVideo"));
        result.put("running", parseBooleanLike(status.get("running")));
        result.put("success", true);
        return result;
    }

    /**
     * 获取当前帧图片的 URL
     */
    public String getFrameUrl(boolean withTracking) {
        String url = getApiUrl("/api/frame") + (withTracking ? "?track=1" : "");
        return url;
    }

    /**
     * 获取本地 RTSP 流地址
     */
    public Map<String, String> getRtspUrls() {
        return Map.of(
            "cam0", String.format("rtsp://%s:8554/cam0", rknnServerHost),
            "cam1", String.format("rtsp://%s:8554/cam1", rknnServerHost),
            "cam3", String.format("rtsp://%s:8554/cam3", rknnServerHost)
        );
    }

    // ==================== 本地状态管理 ====================

    public boolean isInferenceEnabled() {
        return inferenceEnabled.get();
    }

    public boolean isTrackingEnabled() {
        return trackingEnabled.get();
    }

    public boolean isRtspStreaming() {
        return rtspStreaming.get();
    }

    public boolean isVideoPlaying() {
        return videoPlaying.get();
    }

    /**
     * 获取本地缓存的状态（不查询远程）
     */
    public Map<String, Object> getLocalStatus() {
        return Map.of(
            "serverAvailable", isServerAvailable(),
            "inferenceEnabled", inferenceEnabled.get(),
            "trackingEnabled", trackingEnabled.get(),
            "rtspStreaming", rtspStreaming.get(),
            "videoPlaying", videoPlaying.get(),
            "serverHost", rknnServerHost,
            "serverPort", rknnServerPort,
            "rtspUrls", getRtspUrls()
        );
    }

    // ==================== 阈值控制 ====================

    /**
     * 设置检测阈值
     */
    public Map<String, Object> setThreshold(float value) {
        return setThreshold(value, null);
    }

    /**
     * 设置检测阈值与框数量阈值（boxCount 可选）
     */
    public Map<String, Object> setThreshold(float value, Integer boxCount) {
        try {
            // 限制阈值范围
            if (value < 0) value = 0;
            if (value > 1) value = 1;

            StringBuilder urlBuilder = new StringBuilder(getApiUrl("/api/threshold/set"))
                .append("?value=").append(value);
            if (boxCount != null) {
                int normalizedBoxCount = Math.max(0, boxCount);
                urlBuilder.append("&box_count=").append(normalizedBoxCount);
            }

            String url = urlBuilder.toString();
            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(url, null, Map.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                if (boxCount != null) {
                    log.info("检测阈值已设置为: {}, 框数量阈值: {}", value, Math.max(0, boxCount));
                } else {
                    log.info("检测阈值已设置为: {}", value);
                }
                return response.getBody();
            }
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (RestClientException e) {
            log.error("设置阈值失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    /**
     * 获取当前检测阈值
     */
    public Map<String, Object> getThreshold() {
        try {
            String url = getApiUrl("/api/threshold/get");
            ResponseEntity<Map> response = rknnServerRestTemplate.getForEntity(url, Map.class);

            if (response.getStatusCode().is2xxSuccessful() && response.getBody() != null) {
                return response.getBody();
            }
            return Map.of("success", false, "error", "获取阈值失败");
        } catch (RestClientException e) {
            log.error("获取阈值失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    /**
     * 获取指定摄像头的检测数量
     */
    public Map<String, Object> getDetectionCount(int cam) {
        try {
            String url = getApiUrl("/api/detection/count") + "?cam=" + cam;
            ResponseEntity<Map> response = rknnServerRestTemplate.getForEntity(url, Map.class);

            if (response.getStatusCode().is2xxSuccessful() && response.getBody() != null) {
                return response.getBody();
            }
            return Map.of("success", false, "error", "获取检测数量失败");
        } catch (RestClientException e) {
            log.error("获取检测数量失败: {}", e.getMessage());
            return Map.of("success", false, "error", e.getMessage());
        }
    }

    /**
     * 获取指定摄像头当前帧（JPEG）
     * cameraId: 1 -> cam0, 2 -> cam1
     */
    public byte[] getCurrentFrame(int cameraId, boolean track) {
        int cam = (cameraId == 2) ? 1 : 0;
        // 直接按 cam 获取对应路数的最新帧，避免“先切换再抓帧”带来的竞态串帧。
        String frameUrl = getApiUrl("/api/frame") +
                "?track=" + (track ? "1" : "0") +
                "&cam=" + cam;
        HttpHeaders headers = new HttpHeaders();
        headers.setAccept(List.of(MediaType.IMAGE_JPEG, MediaType.ALL));
        HttpEntity<Void> entity = new HttpEntity<>(headers);
        ResponseEntity<byte[]> response = rknnServerRestTemplate.exchange(frameUrl, HttpMethod.GET, entity, byte[].class);
        if (response.getStatusCode().is2xxSuccessful() && response.getBody() != null && response.getBody().length > 0) {
            return response.getBody();
        }
        throw new RestClientException("视觉服务返回空帧或非 2xx 响应");
    }

    /**
     * 保存禁入区域（四边形四个点）
     */
    public Map<String, Object> saveForbiddenArea(ForbiddenAreaSaveRequest request) {
        validateForbiddenAreaRequest(request);
        if (isForbiddenAreaCleared(request)) {
            forbiddenAreaStore.remove(request.getCameraId());
            log.info("禁入区域已清空: cameraId={}", request.getCameraId());
            return buildEmptyForbiddenAreaResponse(request.getCameraId());
        }

        ForbiddenAreaSaveRequest copy = deepCopyForbiddenArea(request);
        forbiddenAreaStore.put(copy.getCameraId(), copy);
        log.info("禁入区域已保存: cameraId={}, points={}", copy.getCameraId(), copy.getPoints());
        return toForbiddenAreaResponse(copy, true);
    }

    /**
     * 获取禁入区域
     */
    public Map<String, Object> getForbiddenArea(Long cameraId) {
        if (cameraId == null) {
            throw new IllegalArgumentException("cameraId 不能为空");
        }
        ForbiddenAreaSaveRequest saved = forbiddenAreaStore.get(cameraId);
        if (saved == null) {
            return buildEmptyForbiddenAreaResponse(cameraId);
        }
        return toForbiddenAreaResponse(saved, true);
    }

    private void validateForbiddenAreaRequest(ForbiddenAreaSaveRequest request) {
        if (request == null) {
            throw new IllegalArgumentException("请求体不能为空");
        }
        if (request.getCameraId() == null) {
            throw new IllegalArgumentException("cameraId 不能为空");
        }
        long cameraId = request.getCameraId();
        if (cameraId != 1L && cameraId != 2L) {
            throw new IllegalArgumentException("cameraId 仅支持 1 或 2");
        }
        int width = request.getImageWidth() == null ? 0 : request.getImageWidth();
        int height = request.getImageHeight() == null ? 0 : request.getImageHeight();
        if (width < 0 || height < 0) {
            throw new IllegalArgumentException("imageWidth/imageHeight 不能为负数");
        }

        List<ForbiddenAreaPoint> points = request.getPoints();
        if (points == null || points.isEmpty()) {
            return;
        }
        if (points.size() != 4) {
            throw new IllegalArgumentException("points 必须是 4 个端点，或传空数组表示清空");
        }

        for (int i = 0; i < points.size(); i++) {
            ForbiddenAreaPoint point = points.get(i);
            if (point == null || point.getX() == null || point.getY() == null) {
                throw new IllegalArgumentException("第 " + (i + 1) + " 个点坐标无效");
            }
            if (point.getX() < 0 || point.getY() < 0) {
                throw new IllegalArgumentException("第 " + (i + 1) + " 个点坐标不能为负数");
            }
            if (width > 0 && point.getX() >= width) {
                throw new IllegalArgumentException("第 " + (i + 1) + " 个点 x 超出图像宽度");
            }
            if (height > 0 && point.getY() >= height) {
                throw new IllegalArgumentException("第 " + (i + 1) + " 个点 y 超出图像高度");
            }
        }
    }

    private ForbiddenAreaSaveRequest deepCopyForbiddenArea(ForbiddenAreaSaveRequest request) {
        ForbiddenAreaSaveRequest copy = new ForbiddenAreaSaveRequest();
        copy.setCameraId(request.getCameraId());
        copy.setImageWidth(request.getImageWidth());
        copy.setImageHeight(request.getImageHeight());

        List<ForbiddenAreaPoint> copiedPoints = new ArrayList<>();
        List<ForbiddenAreaPoint> sourcePoints = request.getPoints() == null ? List.of() : request.getPoints();
        for (ForbiddenAreaPoint point : sourcePoints) {
            ForbiddenAreaPoint copied = new ForbiddenAreaPoint();
            copied.setX(point.getX());
            copied.setY(point.getY());
            copiedPoints.add(copied);
        }
        copy.setPoints(copiedPoints);
        return copy;
    }

    private Map<String, Object> toForbiddenAreaResponse(ForbiddenAreaSaveRequest request, boolean exists) {
        List<Map<String, Integer>> points = new ArrayList<>();
        List<ForbiddenAreaPoint> sourcePoints = request.getPoints() == null ? List.of() : request.getPoints();
        for (ForbiddenAreaPoint point : sourcePoints) {
            Map<String, Integer> p = new LinkedHashMap<>();
            p.put("x", point.getX());
            p.put("y", point.getY());
            points.add(p);
        }

        Map<String, Object> result = new LinkedHashMap<>();
        result.put("exists", exists);
        result.put("cameraId", request.getCameraId());
        result.put("imageWidth", request.getImageWidth());
        result.put("imageHeight", request.getImageHeight());
        result.put("pointCount", points.size());
        result.put("points", points);
        return result;
    }

    private boolean isForbiddenAreaCleared(ForbiddenAreaSaveRequest request) {
        return request.getPoints() == null || request.getPoints().isEmpty();
    }

    private Map<String, Object> buildEmptyForbiddenAreaResponse(Long cameraId) {
        Map<String, Object> result = new LinkedHashMap<>();
        result.put("exists", false);
        result.put("cameraId", cameraId);
        result.put("pointCount", 0);
        result.put("points", List.of());
        return result;
    }

    private String extractVideoExtension(String fileName) {
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex < 0 || dotIndex == fileName.length() - 1) {
            throw new IllegalArgumentException("视频文件必须包含扩展名");
        }
        return fileName.substring(dotIndex).toLowerCase(Locale.ROOT);
    }

    private String sanitizeVideoFileName(String baseName) {
        String sanitized = baseName.replaceAll("[^a-zA-Z0-9._-]", "_");
        while (sanitized.startsWith(".")) {
            sanitized = sanitized.substring(1);
        }
        while (sanitized.endsWith(".")) {
            sanitized = sanitized.substring(0, sanitized.length() - 1);
        }
        return sanitized;
    }

    private Path allocateVideoUploadTarget(Path targetDir, String baseName, String extension) throws IOException {
        Path candidate = targetDir.resolve(baseName + extension).normalize();
        int counter = 1;
        while (Files.exists(candidate)) {
            candidate = targetDir.resolve(baseName + "_" + counter + extension).normalize();
            counter += 1;
        }
        if (!candidate.startsWith(targetDir)) {
            throw new IOException("视频上传目标路径非法");
        }
        return candidate;
    }

    private String normalizeVideoSourcePath(String sourcePath) {
        String raw = sourcePath == null ? "" : sourcePath.trim();
        if (!StringUtils.hasText(raw)) {
            throw new IllegalArgumentException("sourcePath 不能为空");
        }

        String normalized = raw.toLowerCase(Locale.ROOT);
        for (String prefix : REMOTE_VIDEO_SOURCE_PREFIXES) {
            if (normalized.startsWith(prefix)) {
                return raw;
            }
        }

        Path candidate = Paths.get(raw);
        if (!candidate.isAbsolute()) {
            candidate = VIDEO_UPLOAD_ROOT.resolve(candidate).normalize();
        } else {
            candidate = candidate.toAbsolutePath().normalize();
        }

        if (!candidate.startsWith(VIDEO_UPLOAD_ROOT)) {
            throw new IllegalArgumentException("本地视频源仅支持 uploads/videos 目录下的文件");
        }
        if (!Files.exists(candidate) || !Files.isRegularFile(candidate)) {
            throw new IllegalArgumentException("视频文件不存在: " + candidate);
        }
        return candidate.toString();
    }

    private Map<String, Object> postJsonWithRawHttp(String endpoint, String jsonBody) throws IOException {
        byte[] bodyBytes = jsonBody.getBytes(StandardCharsets.UTF_8);
        String requestText = "POST " + endpoint + " HTTP/1.1\r\n"
                + "Host: " + rknnServerHost + ":" + rknnServerPort + "\r\n"
                + "Content-Type: application/json\r\n"
                + "Content-Length: " + bodyBytes.length + "\r\n"
                + "Connection: close\r\n"
                + "\r\n";

        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(rknnServerHost, rknnServerPort), 5000);
            socket.setSoTimeout(10000);

            try (OutputStream output = socket.getOutputStream();
                 BufferedInputStream input = new BufferedInputStream(socket.getInputStream())) {
                output.write(requestText.getBytes(StandardCharsets.UTF_8));
                output.write(bodyBytes);
                output.flush();

                ByteArrayOutputStream responseBuffer = new ByteArrayOutputStream();
                byte[] chunk = new byte[4096];
                int read;
                while ((read = input.read(chunk)) != -1) {
                    responseBuffer.write(chunk, 0, read);
                }

                String responseText = responseBuffer.toString(StandardCharsets.UTF_8);
                return parseRawHttpJsonResponse(responseText);
            }
        }
    }

    private Map<String, Object> parseRawHttpJsonResponse(String responseText) throws IOException {
        String[] sections = responseText.split("\\r\\n\\r\\n", 2);
        String headerText = sections.length > 0 ? sections[0] : "";
        String bodyText = sections.length > 1 ? sections[1] : "";

        int statusCode = 500;
        String[] headerLines = headerText.split("\\r\\n");
        if (headerLines.length > 0) {
            String[] statusParts = headerLines[0].split(" ");
            if (statusParts.length >= 2) {
                try {
                    statusCode = Integer.parseInt(statusParts[1]);
                } catch (NumberFormatException ignored) {
                    statusCode = 500;
                }
            }
        }

        Map<String, Object> parsedBody;
        if (bodyText == null || bodyText.isBlank()) {
            parsedBody = new LinkedHashMap<>();
        } else {
            parsedBody = OBJECT_MAPPER.readValue(bodyText, new TypeReference<LinkedHashMap<String, Object>>() {});
        }

        if (statusCode >= 200 && statusCode < 300) {
            return parsedBody;
        }

        Map<String, Object> errorResult = new LinkedHashMap<>(parsedBody);
        errorResult.put("success", false);
        if (!errorResult.containsKey("error")) {
            Object message = errorResult.get("message");
            errorResult.put("error", message instanceof String && !((String) message).isBlank()
                    ? message
                    : "HTTP " + statusCode);
        }
        errorResult.put("statusCode", statusCode);
        return errorResult;
    }

    private String escapeJson(String text) {
        if (text == null || text.isEmpty()) {
            return "";
        }
        StringBuilder sb = new StringBuilder(text.length() + 16);
        for (int i = 0; i < text.length(); i++) {
            char ch = text.charAt(i);
            switch (ch) {
                case '\\':
                    sb.append("\\\\");
                    break;
                case '"':
                    sb.append("\\\"");
                    break;
                case '\n':
                    sb.append("\\n");
                    break;
                case '\r':
                    sb.append("\\r");
                    break;
                case '\t':
                    sb.append("\\t");
                    break;
                default:
                    sb.append(ch);
                    break;
            }
        }
        return sb.toString();
    }

    private boolean isOperationSuccess(Map<String, Object> result) {
        if (result == null || result.isEmpty()) {
            return false;
        }
        Object success = result.get("success");
        if (Boolean.FALSE.equals(success)) {
            return false;
        }
        Object status = result.get("status");
        if (status instanceof String statusText) {
            String normalized = statusText.trim().toLowerCase(Locale.ROOT);
            if ("error".equals(normalized) || "failed".equals(normalized) || "fail".equals(normalized)) {
                return false;
            }
        }
        return true;
    }

    private String extractOperationError(Map<String, Object> result, String fallback) {
        if (result == null || result.isEmpty()) {
            return fallback;
        }
        Object error = result.get("error");
        if (error instanceof String errorText && !errorText.isBlank()) {
            return errorText;
        }
        Object message = result.get("message");
        if (message instanceof String messageText && !messageText.isBlank()) {
            return messageText;
        }
        return fallback;
    }

    private String readString(Map<String, Object> source, String... keys) {
        if (source == null || keys == null) {
            return "";
        }
        for (String key : keys) {
            Object value = source.get(key);
            if (value instanceof String text && !text.isBlank()) {
                return text;
            }
        }
        return "";
    }
}
