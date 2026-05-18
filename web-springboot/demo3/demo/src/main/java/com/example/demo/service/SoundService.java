package com.example.demo.service;

import com.example.demo.dto.SoundAnomalyReportRequest;
import com.example.demo.entity.SoundEvent;
import com.example.demo.repository.SoundEventRepository;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.*;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestTemplate;

import jakarta.annotation.PostConstruct;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;

@Slf4j
@Service
public class SoundService {

    @Autowired
    private SoundEventRepository soundEventRepository;

    @Autowired
    private RestTemplate restTemplate;

    @Value("${sound.server.host:localhost}")
    private String soundServerHost;

    @Value("${sound.server.port:8089}")
    private int soundServerPort;

    @Value("${springboot.server.port:8080}")
    private String springbootServerPort;

    private static final String UPLOAD_DIR = "/home/orangepi/Desktop/web/Sound_Monitoring/uploads";

    private final AtomicBoolean httpServerAvailable = new AtomicBoolean(false);
    private final AtomicBoolean monitoringEnabled = new AtomicBoolean(false);
    private final AtomicBoolean acceptingMonitoringReports = new AtomicBoolean(false);
    private final AtomicBoolean monitoringStopRequested = new AtomicBoolean(false);
    private volatile SoundEvent latestEvent;

    @PostConstruct
    public void init() {
        log.info("SoundService initialized");
        log.info("  Sound Server: http://{}:{}", soundServerHost, soundServerPort);
        log.info("  Upload Dir: {}", UPLOAD_DIR);

        try {
            Files.createDirectories(Paths.get(UPLOAD_DIR));
        } catch (IOException e) {
            log.warn("Failed to create upload directory: {}", e.getMessage());
        }

        refreshHttpServerAvailability();
        syncMonitoringEnabledFromSoundStatus();
        acceptingMonitoringReports.set(monitoringEnabled.get());
    }

    private boolean refreshHttpServerAvailability() {
        try {
            URL url = new URL(String.format("http://%s:%d/health", soundServerHost, soundServerPort));
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(3000);
            conn.setReadTimeout(3000);
            
            int responseCode = conn.getResponseCode();
            if (responseCode == 200) {
                log.info("  ✓ HTTP API Server available");
                httpServerAvailable.set(true);
                conn.disconnect();
                return true;
            } else {
                log.warn("  ✗ HTTP API Server returned: {}", responseCode);
                httpServerAvailable.set(false);
                conn.disconnect();
                return false;
            }
        } catch (Exception e) {
            log.warn("  ✗ HTTP API Server not available: {}", e.getMessage());
            httpServerAvailable.set(false);
            return false;
        }
    }

    public boolean isHttpServerAvailable() {
        return httpServerAvailable.get();
    }

    public Map<String, Object> getMonitoringStatus() {
        refreshHttpServerAvailability();
        Map<String, Object> realtimeStatus = fetchSoundServerStatus("/realtime/status");
        Map<String, Object> wakeStatus = fetchSoundServerStatus("/wake/status");
        boolean realtimeRunning = isRunningStatus(realtimeStatus);
        boolean wakeRunning = isRunningStatus(wakeStatus);
        boolean enabled = realtimeRunning || wakeRunning;
        monitoringEnabled.set(enabled);
        if (enabled && !monitoringStopRequested.get()) {
            acceptingMonitoringReports.set(true);
        } else if (!enabled) {
            acceptingMonitoringReports.set(false);
        }

        Map<String, Object> status = new HashMap<>();
        status.put("httpServerAvailable", httpServerAvailable.get());
        status.put("httpServerUrl", String.format("http://%s:%d", soundServerHost, soundServerPort));
        status.put("enabled", enabled);
        status.put("acceptingMonitoringReports", acceptingMonitoringReports.get());
        status.put("realtimeRunning", realtimeRunning);
        status.put("wakeRunning", wakeRunning);
        status.put("realtime", realtimeStatus);
        status.put("wake", wakeStatus);
        status.put("latestEvent", latestEvent);
        return status;
    }

    public boolean isAcceptingMonitoringReports() {
        if (!monitoringStopRequested.get() && !acceptingMonitoringReports.get()) {
            syncMonitoringEnabledFromSoundStatus();
            if (monitoringEnabled.get()) {
                acceptingMonitoringReports.set(true);
            }
        }
        return acceptingMonitoringReports.get();
    }

    @SuppressWarnings("unchecked")
    public SoundEvent analyzeAudio(String audioFilePath) {
        if (!refreshHttpServerAvailability()) {
            log.error("HTTP server not available, cannot analyze audio");
            return null;
        }

        HttpURLConnection conn = null;
        BufferedReader reader = null;
        
        try {
            URL url = new URL(String.format("http://%s:%d/analyze", soundServerHost, soundServerPort));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
            conn.setRequestProperty("Accept", "application/json");
            conn.setDoOutput(true);
            conn.setConnectTimeout(30000);
            conn.setReadTimeout(60000);
            
            // 构建 JSON 请求体
            String jsonBody = "{\"audio_path\":\"" + audioFilePath.replace("\\", "\\\\") + "\"}";
            log.info("[SoundService] 发送请求 - URL: {}", url);
            log.info("[SoundService] 发送请求 - Body: {}", jsonBody);
            
            // 发送请求
            try (OutputStream os = conn.getOutputStream()) {
                byte[] input = jsonBody.getBytes(StandardCharsets.UTF_8);
                os.write(input, 0, input.length);
            }
            
            // 获取响应
            int responseCode = conn.getResponseCode();
            log.info("[SoundService] 响应状态码: {}", responseCode);
            
            StringBuilder response = new StringBuilder();
            InputStream errorStream = conn.getErrorStream();
            if (errorStream != null) {
                reader = new BufferedReader(new InputStreamReader(errorStream, StandardCharsets.UTF_8));
                String line;
                while ((line = reader.readLine()) != null) {
                    response.append(line);
                }
            } else {
                try (InputStream is = conn.getInputStream()) {
                    reader = new BufferedReader(new InputStreamReader(is, StandardCharsets.UTF_8));
                    String line;
                    while ((line = reader.readLine()) != null) {
                        response.append(line);
                    }
                }
            }
            
            String responseBody = response.toString();
            log.info("[SoundService] 响应内容: {}", responseBody);
            
            if (responseCode == 200) {
                // 解析 JSON 响应
                com.fasterxml.jackson.databind.ObjectMapper mapper = new com.fasterxml.jackson.databind.ObjectMapper();
                Map<String, Object> result = mapper.readValue(responseBody, Map.class);
                
                if (Boolean.TRUE.equals(result.get("success"))) {
                    Integer eventCount = (Integer) result.get("anomaly_count");
                    log.info("[SoundService] 异常数量: {}", eventCount);
                    
                    if (eventCount != null && eventCount > 0) {
                        List<Map<String, Object>> events = (List<Map<String, Object>>) result.get("events");
                        if (events != null && !events.isEmpty()) {
                            Map<String, Object> evt = events.get(0);
                            SoundEvent event = new SoundEvent();
                            event.setSoundType("anomaly");

                            Number startNum = (Number) evt.get("start");
                            Number endNum = (Number) evt.get("end");
                            event.setStartTime(LocalDateTime.now());
                            event.setEndTime(LocalDateTime.now().plusSeconds(startNum != null ? endNum.intValue() : 0));

                            Number confNum = (Number) evt.get("confidence");
                            event.setConfidence(confNum != null ? confNum.floatValue() : 0.8f);

                            Number durNum = (Number) evt.get("duration");
                            event.setDuration(durNum != null ? durNum.floatValue() : null);

                            List<String> keywords = (List<String>) evt.get("keywords");
                            if (keywords != null && !keywords.isEmpty()) {
                                event.setKeywords(String.join(", ", keywords));
                            }

                            event.setAudioPath(audioFilePath);
                            event.setCreateTime(LocalDateTime.now());

                            latestEvent = event;
                            log.info("HTTP API 返回异常事件: 置信度={}, 关键词={}",
                                event.getConfidence(), event.getKeywords());

                            // 保存到 SoundEvent
                            SoundEvent savedEvent = soundEventRepository.save(event);

                            // 自动上报到安全记录
                            reportToDetectionRecord(savedEvent);

                            return savedEvent;
                        }
                    }
                    log.info("HTTP API 分析完成，未检测到异常");
                    return null;
                } else {
                    log.error("HTTP API 返回失败: {}", result.get("message"));
                    return null;
                }
            } else {
                log.error("HTTP API 调用失败: {}", responseBody);
                return null;
            }
            
        } catch (Exception e) {
            log.error("调用声音分析 HTTP API 失败: {}", e.getMessage());
            e.printStackTrace();
            return null;
        } finally {
            if (reader != null) {
                try { reader.close(); } catch (IOException ignored) {}
            }
            if (conn != null) {
                conn.disconnect();
            }
        }
    }

    public boolean startMonitoring() {
        log.info("[SoundService] 启动声音监测模式（实时异常 + 紧急关键词）");
        monitoringStopRequested.set(false);
        acceptingMonitoringReports.set(false);
        if (!refreshHttpServerAvailability()) {
            log.error("HTTP server not available");
            return false;
        }

        boolean realtimeOk = postSoundServerControl("/realtime/start", "实时监测启动", true);
        boolean wakeOk = postSoundServerControl("/wake/start", "紧急关键词监听启动", true);

        syncMonitoringEnabledFromSoundStatus();
        if (realtimeOk && wakeOk) {
            log.info("[SoundService] ✓ 声音监测已启动");
            monitoringEnabled.set(true);
            acceptingMonitoringReports.set(true);
            return true;
        }

        log.error("[SoundService] ✗ 声音监测启动不完整: realtimeOk={}, wakeOk={}", realtimeOk, wakeOk);
        monitoringStopRequested.set(true);
        acceptingMonitoringReports.set(false);
        postSoundServerControl("/realtime/stop", "回滚实时监测启动", false);
        postSoundServerControl("/wake/stop", "回滚紧急关键词监听启动", false);
        syncMonitoringEnabledFromSoundStatus();
        return false;
    }

    public boolean stopMonitoring() {
        log.info("[SoundService] 停止声音监测模式（实时异常 + 紧急关键词）");
        monitoringStopRequested.set(true);
        acceptingMonitoringReports.set(false);

        if (!refreshHttpServerAvailability()) {
            log.warn("[SoundService] HTTP server not available, mark sound monitoring disabled locally");
            monitoringEnabled.set(false);
            return true;
        }

        boolean realtimeOk = postSoundServerControl("/realtime/stop", "实时监测停止", false);
        boolean wakeOk = postSoundServerControl("/wake/stop", "紧急关键词监听停止", false);

        syncMonitoringEnabledFromSoundStatus();
        if (realtimeOk && wakeOk && !monitoringEnabled.get()) {
            log.info("[SoundService] ✓ 声音监测已停止");
            return true;
        }

        log.error("[SoundService] ✗ 声音监测停止不完整: realtimeOk={}, wakeOk={}, enabled={}",
            realtimeOk, wakeOk, monitoringEnabled.get());
        return false;
    }

    /**
     * 获取实时监测状态
     */
    public Map<String, Object> getRealtimeStatus() {
        Map<String, Object> status = new HashMap<>();
        refreshHttpServerAvailability();

        HttpURLConnection conn = null;
        try {
            URL url = new URL(String.format("http://%s:%d/realtime/status", soundServerHost, soundServerPort));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(5000);

            int responseCode = conn.getResponseCode();
            if (responseCode == 200) {
                httpServerAvailable.set(true);
                String response = readConnectionBody(conn);
                com.fasterxml.jackson.databind.ObjectMapper mapper = new com.fasterxml.jackson.databind.ObjectMapper();
                Map<String, Object> result = mapper.readValue(response, Map.class);
                status.put("realtime", result);
                status.put("success", true);
                Object running = result.get("running");
                if (running instanceof Boolean runningFlag) {
                    monitoringEnabled.set(runningFlag);
                }
            } else {
                httpServerAvailable.set(false);
                status.put("success", false);
                status.put("error", "获取状态失败");
            }
        } catch (Exception e) {
            httpServerAvailable.set(false);
            status.put("success", false);
            status.put("error", e.getMessage());
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
        return status;
    }

    /**
     * 获取实时异常事件
     */
    public Map<String, Object> getRealtimeEvents() {
        Map<String, Object> result = new HashMap<>();
        if (!refreshHttpServerAvailability()) {
            result.put("success", false);
            result.put("error", "声音服务不可用");
            return result;
        }

        HttpURLConnection conn = null;
        try {
            URL url = new URL(String.format("http://%s:%d/realtime/events", soundServerHost, soundServerPort));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(5000);

            int responseCode = conn.getResponseCode();
            if (responseCode == 200) {
                httpServerAvailable.set(true);
                String response = readConnectionBody(conn);
                com.fasterxml.jackson.databind.ObjectMapper mapper = new com.fasterxml.jackson.databind.ObjectMapper();
                Map<String, Object> eventsResult = mapper.readValue(response, Map.class);
                result.put("events", eventsResult.get("events"));
                result.put("count", eventsResult.get("count"));
                result.put("success", true);
            } else {
                httpServerAvailable.set(false);
                result.put("success", false);
                result.put("error", "获取事件失败");
            }
        } catch (Exception e) {
            httpServerAvailable.set(false);
            result.put("success", false);
            result.put("error", e.getMessage());
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
        return result;
    }

    /**
     * 获取最近实时窗口状态（不限于异常）
     */
    public Map<String, Object> getRealtimeWindows(int limit) {
        Map<String, Object> result = new HashMap<>();
        int normalizedLimit = Math.max(1, Math.min(limit, 50));
        if (!refreshHttpServerAvailability()) {
            result.put("success", false);
            result.put("error", "声音服务不可用");
            return result;
        }

        HttpURLConnection conn = null;
        try {
            URL url = new URL(String.format("http://%s:%d/realtime/windows?limit=%d",
                soundServerHost, soundServerPort, normalizedLimit));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(5000);
            conn.setReadTimeout(5000);

            int responseCode = conn.getResponseCode();
            if (responseCode == 200) {
                httpServerAvailable.set(true);
                String response = readConnectionBody(conn);
                com.fasterxml.jackson.databind.ObjectMapper mapper = new com.fasterxml.jackson.databind.ObjectMapper();
                Map<String, Object> windowsResult = mapper.readValue(response, Map.class);
                result.putAll(windowsResult);
                result.put("success", true);
            } else {
                httpServerAvailable.set(false);
                result.put("success", false);
                result.put("error", "获取实时窗口状态失败");
            }
        } catch (Exception e) {
            httpServerAvailable.set(false);
            result.put("success", false);
            result.put("error", e.getMessage());
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
        return result;
    }

    public SoundEvent getLatestEvent() {
        return latestEvent;
    }

    public Page<SoundEvent> getEventHistory(Pageable pageable) {
        return soundEventRepository.findAll(pageable);
    }

    public SoundEvent detectFromFile(String filePath) {
        return analyzeAudio(filePath);
    }

    /**
     * 上报声音异常到安全记录
     */
    private void reportToDetectionRecord(SoundEvent event) {
        try {
            SoundAnomalyReportRequest request = new SoundAnomalyReportRequest();
            request.setCameraId(-1L); // 声音监测设备使用 -1
            request.setCameraName("声音监测");
            request.setDetectionTime(event.getStartTime() != null ?
                event.getStartTime().format(DateTimeFormatter.ISO_LOCAL_DATE_TIME) : null);
            request.setDetectionResult(String.format("声音异常 - %s (置信度: %.2f%%)",
                event.getKeywords() != null ? event.getKeywords() : "未知",
                (event.getConfidence() != null ? event.getConfidence() : 0) * 100));
            request.setAudioUrl(event.getAudioPath());
            request.setAudioDuration(event.getDuration());
            request.setSoundKeywords(event.getKeywords());
            request.setSource("manual");

            String url = String.format("http://localhost:%s/api/detection/record/sound/report", springbootServerPort);
            HttpHeaders headers = new HttpHeaders();
            headers.setContentType(MediaType.APPLICATION_JSON);

            HttpEntity<SoundAnomalyReportRequest> entity = new HttpEntity<>(request, headers);
            ResponseEntity<String> response = restTemplate.postForEntity(url, entity, String.class);

            if (response.getStatusCode().is2xxSuccessful() && isSuccessResult(response.getBody())) {
                log.info("✓ 声音异常已上报到安全记录");
            } else {
                log.warn("声音异常上报失败: status={}, body={}",
                    response.getStatusCode(), response.getBody());
            }
        } catch (Exception e) {
            log.error("上报声音异常到安全记录失败: {}", e.getMessage());
            e.printStackTrace();
        }
    }

    private boolean isSuccessResult(String body) {
        if (body == null || body.isBlank()) {
            return false;
        }

        try {
            com.fasterxml.jackson.databind.ObjectMapper mapper = new com.fasterxml.jackson.databind.ObjectMapper();
            Map<String, Object> result = mapper.readValue(body, Map.class);
            Object code = result.get("code");
            if (code instanceof Number number) {
                return number.intValue() == 200;
            }
            return false;
        } catch (Exception e) {
            log.warn("解析安全记录上报响应失败: body={}", body, e);
            return false;
        }
    }

    private void syncMonitoringEnabledFromRealtimeStatus() {
        Map<String, Object> status = fetchSoundServerStatus("/realtime/status");
        monitoringEnabled.set(isRunningStatus(status));
    }

    private void syncMonitoringEnabledFromSoundStatus() {
        Map<String, Object> realtimeStatus = fetchSoundServerStatus("/realtime/status");
        Map<String, Object> wakeStatus = fetchSoundServerStatus("/wake/status");
        monitoringEnabled.set(isRunningStatus(realtimeStatus) || isRunningStatus(wakeStatus));
    }

    private boolean postSoundServerControl(String path, String actionName, boolean allowAlreadyRunning) {
        HttpURLConnection conn = null;
        try {
            URL url = new URL(String.format("http://%s:%d%s", soundServerHost, soundServerPort, path));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
            conn.setConnectTimeout(5000);
            conn.setReadTimeout(10000);

            log.info("[SoundService] 发送请求到 C++ 声音服务: POST {}", url);

            int responseCode = conn.getResponseCode();
            String responseBody = readConnectionBody(conn);
            log.info("[SoundService] {}响应码: {}, 响应: {}", actionName, responseCode, responseBody);

            httpServerAvailable.set(true);
            if (responseCode >= 200 && responseCode < 300) {
                return true;
            }

            if (allowAlreadyRunning && (responseCode == 409 || responseBody.contains("Already running"))) {
                return true;
            }

            log.error("[SoundService] {}失败: {}", actionName, responseBody);
            return false;
        } catch (Exception e) {
            log.error("[SoundService] 调用{}接口失败: {}", actionName, e.getMessage());
            return false;
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
    }

    @SuppressWarnings("unchecked")
    private Map<String, Object> fetchSoundServerStatus(String path) {
        Map<String, Object> status = new HashMap<>();
        HttpURLConnection conn = null;
        try {
            URL url = new URL(String.format("http://%s:%d%s", soundServerHost, soundServerPort, path));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(3000);
            conn.setReadTimeout(3000);

            int responseCode = conn.getResponseCode();
            String response = readConnectionBody(conn);
            status.put("httpStatus", responseCode);

            if (responseCode == 200 && response != null && !response.isBlank()) {
                com.fasterxml.jackson.databind.ObjectMapper mapper = new com.fasterxml.jackson.databind.ObjectMapper();
                status.putAll(mapper.readValue(response, Map.class));
                httpServerAvailable.set(true);
            } else {
                status.put("success", false);
                status.put("error", response == null || response.isBlank() ? "empty response" : response);
                if (responseCode != 200) {
                    httpServerAvailable.set(false);
                }
            }
        } catch (Exception e) {
            status.put("success", false);
            status.put("error", e.getMessage());
            httpServerAvailable.set(false);
            log.debug("[SoundService] 获取声音服务状态失败 {}: {}", path, e.getMessage());
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
        return status;
    }

    private boolean isRunningStatus(Map<String, Object> status) {
        Object running = status.get("running");
        return running instanceof Boolean runningFlag && runningFlag;
    }

    private String readConnectionBody(HttpURLConnection conn) throws IOException {
        InputStream stream = null;
        try {
            stream = conn.getErrorStream();
            if (stream == null) {
                stream = conn.getInputStream();
            }
            if (stream == null) {
                return "";
            }

            try (BufferedReader reader = new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8))) {
                StringBuilder response = new StringBuilder();
                String line;
                while ((line = reader.readLine()) != null) {
                    response.append(line);
                }
                return response.toString();
            }
        } catch (IOException e) {
            if (stream != null) {
                throw e;
            }
            return "";
        }
    }
}
