package com.example.demo.service;

import com.example.demo.dto.ForbiddenAreaPoint;
import com.example.demo.dto.ForbiddenAreaSaveRequest;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.*;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClientException;
import org.springframework.web.client.RestTemplate;
import org.springframework.web.util.UriComponentsBuilder;

import jakarta.annotation.PostConstruct;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.atomic.AtomicBoolean;

@Slf4j
@Service
public class RknnService {

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

    // ==================== 视频文件控制 ====================

    /**
     * 播放视频文件
     */
    public Map<String, Object> startVideo(String videoPath, boolean loop) {
        try {
            String url = getApiUrl("/api/video/start");
            HttpHeaders headers = new HttpHeaders();
            headers.setContentType(MediaType.APPLICATION_JSON);
            String body = String.format("{\"path\": \"%s\", \"loop\": %s}", videoPath, loop);
            HttpEntity<String> request = new HttpEntity<>(body, headers);

            ResponseEntity<Map> response = rknnServerRestTemplate.postForEntity(url, request, Map.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                videoPlaying.set(true);
                log.info("视频播放已开启: {}, loop={}", videoPath, loop);
                return response.getBody();
            }
            log.error("播放视频失败: {}", response.getStatusCode());
            return Map.of("success", false, "error", "HTTP " + response.getStatusCode());
        } catch (RestClientException e) {
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
}
