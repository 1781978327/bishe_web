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

        checkHttpServer();
    }

    private void checkHttpServer() {
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
            } else {
                log.warn("  ✗ HTTP API Server returned: {}", responseCode);
                httpServerAvailable.set(false);
            }
            conn.disconnect();
        } catch (Exception e) {
            log.warn("  ✗ HTTP API Server not available: {}", e.getMessage());
            httpServerAvailable.set(false);
        }
    }

    public boolean isHttpServerAvailable() {
        return httpServerAvailable.get();
    }

    public Map<String, Object> getMonitoringStatus() {
        Map<String, Object> status = new HashMap<>();
        status.put("httpServerAvailable", httpServerAvailable.get());
        status.put("httpServerUrl", String.format("http://%s:%d", soundServerHost, soundServerPort));
        status.put("enabled", monitoringEnabled.get());
        status.put("latestEvent", latestEvent);
        return status;
    }

    @SuppressWarnings("unchecked")
    public SoundEvent analyzeAudio(String audioFilePath) {
        if (!httpServerAvailable.get()) {
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
        log.info("[SoundService] 启动实时监测模式");
        if (!httpServerAvailable.get()) {
            log.error("HTTP server not available");
            return false;
        }

        HttpURLConnection conn = null;
        try {
            URL url = new URL(String.format("http://%s:%d/realtime/start", soundServerHost, soundServerPort));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
            conn.setConnectTimeout(5000);
            conn.setReadTimeout(10000);

            log.info("[SoundService] 发送请求到 C++ 实时监测服务: POST {}", url);

            int responseCode = conn.getResponseCode();
            log.info("[SoundService] 实时监测响应码: {}", responseCode);

            // 读取响应
            StringBuilder response = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(conn.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    response.append(line);
                }
            }
            log.info("[SoundService] 实时监测响应: {}", response.toString());

            if (responseCode == 200) {
                log.info("[SoundService] ✓ 实时监测已启动");
                monitoringEnabled.set(true);
                return true;
            } else if (responseCode == 409 || response.toString().contains("Already running")) {
                // 已经是运行状态，视为成功（幂等性）
                log.info("[SoundService] ✓ 实时监测已在运行中");
                monitoringEnabled.set(true);
                return true;
            } else {
                log.error("[SoundService] ✗ 启动实时监测失败: {}", response.toString());
                return false;
            }
        } catch (Exception e) {
            log.error("[SoundService] 调用实时监测启动接口失败: {}", e.getMessage());
            return false;
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
    }

    public boolean stopMonitoring() {
        log.info("[SoundService] 停止实时监测模式");

        HttpURLConnection conn = null;
        try {
            URL url = new URL(String.format("http://%s:%d/realtime/stop", soundServerHost, soundServerPort));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
            conn.setConnectTimeout(5000);
            conn.setReadTimeout(10000);

            log.info("[SoundService] 发送请求到 C++ 实时监测服务: POST {}", url);

            int responseCode = conn.getResponseCode();
            log.info("[SoundService] 停止监测响应码: {}", responseCode);

            if (responseCode == 200) {
                log.info("[SoundService] ✓ 实时监测已停止");
                monitoringEnabled.set(false);
                return true;
            } else {
                return false;
            }
        } catch (Exception e) {
            log.error("[SoundService] 调用实时监测停止接口失败: {}", e.getMessage());
            return false;
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
    }

    /**
     * 获取实时监测状态
     */
    public Map<String, Object> getRealtimeStatus() {
        Map<String, Object> status = new HashMap<>();

        HttpURLConnection conn = null;
        try {
            URL url = new URL(String.format("http://%s:%d/realtime/status", soundServerHost, soundServerPort));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(5000);

            int responseCode = conn.getResponseCode();
            if (responseCode == 200) {
                StringBuilder response = new StringBuilder();
                try (BufferedReader reader = new BufferedReader(new InputStreamReader(conn.getInputStream()))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        response.append(line);
                    }
                }
                com.fasterxml.jackson.databind.ObjectMapper mapper = new com.fasterxml.jackson.databind.ObjectMapper();
                Map<String, Object> result = mapper.readValue(response.toString(), Map.class);
                status.put("realtime", result);
                status.put("success", true);
            } else {
                status.put("success", false);
                status.put("error", "获取状态失败");
            }
        } catch (Exception e) {
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

        HttpURLConnection conn = null;
        try {
            URL url = new URL(String.format("http://%s:%d/realtime/events", soundServerHost, soundServerPort));
            conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(5000);

            int responseCode = conn.getResponseCode();
            if (responseCode == 200) {
                StringBuilder response = new StringBuilder();
                try (BufferedReader reader = new BufferedReader(new InputStreamReader(conn.getInputStream()))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        response.append(line);
                    }
                }
                com.fasterxml.jackson.databind.ObjectMapper mapper = new com.fasterxml.jackson.databind.ObjectMapper();
                Map<String, Object> eventsResult = mapper.readValue(response.toString(), Map.class);
                result.put("events", eventsResult.get("events"));
                result.put("count", eventsResult.get("count"));
                result.put("success", true);
            } else {
                result.put("success", false);
                result.put("error", "获取事件失败");
            }
        } catch (Exception e) {
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

            String url = String.format("http://localhost:%s/api/detection/record/sound/report", springbootServerPort);
            HttpHeaders headers = new HttpHeaders();
            headers.setContentType(MediaType.APPLICATION_JSON);

            HttpEntity<SoundAnomalyReportRequest> entity = new HttpEntity<>(request, headers);
            ResponseEntity<String> response = restTemplate.postForEntity(url, entity, String.class);

            if (response.getStatusCode().is2xxSuccessful()) {
                log.info("✓ 声音异常已上报到安全记录");
            } else {
                log.warn("声音异常上报失败: {}", response.getBody());
            }
        } catch (Exception e) {
            log.error("上报声音异常到安全记录失败: {}", e.getMessage());
            e.printStackTrace();
        }
    }
}
