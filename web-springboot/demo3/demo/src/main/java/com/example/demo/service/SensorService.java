package com.example.demo.service;

import com.example.demo.entity.DetectionRecord;
import com.example.demo.entity.SensorData;
import com.example.demo.entity.SensorThreshold;
import com.example.demo.repository.DetectionRecordRepository;
import com.example.demo.repository.SensorDataRepository;
import com.example.demo.repository.SensorThresholdRepository;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.http.*;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.client.RestClientException;
import org.springframework.web.client.RestTemplate;

import jakarta.annotation.PostConstruct;
import java.time.LocalDateTime;
import java.util.*;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

@Slf4j
@Service
public class SensorService {

    @Autowired
    private SensorDataRepository sensorDataRepository;

    @Autowired
    private SensorThresholdRepository sensorThresholdRepository;

    @Autowired
    private DetectionRecordRepository detectionRecordRepository;

    @Autowired
    private DetectionRecordAiAnalysisService detectionRecordAiAnalysisService;

    @Autowired(required = false)
    private RestTemplate sensorServerRestTemplate;

    // ============ 传感器 HTTP 服务器配置 ============
    @Value("${sensor.server.host:localhost}")
    private String sensorServerHost;

    @Value("${sensor.server.port:8088}")
    private int sensorServerPort;

    @Value("${sensor.poll.interval-ms:1000}")
    private long sensorPollIntervalMs;

    // 监测开关
    private final AtomicBoolean monitoringEnabled = new AtomicBoolean(false);

    // 阈值配置
    private final Map<String, Float> thresholds = new ConcurrentHashMap<>();

    // 环境监测专用摄像头ID（使用一个虚拟摄像头ID=0）
    private static final Long ENV_MONITOR_CAMERA_ID = 0L;
    private static final String ENV_MONITOR_CAMERA_NAME = "环境监测";

    // 最新传感器数据（缓存）
    private volatile SensorData latestSensorData;

    // 上次报警时间（用于避免重复记录）
    private volatile LocalDateTime lastAlertTime;

    // 报警间隔（毫秒），避免短时间内重复记录
    private static final long ALERT_INTERVAL_MS = 60000; // 1分钟内不重复记录
    private static final int SMOKE_ALERT_CONFIRM_SAMPLES = 3;
    private int smokeAlertOverThresholdCount = 0;

    @PostConstruct
    public void init() {
        // 从数据库加载阈值，如果不存在则使用默认值
        Optional<SensorThreshold> savedThreshold = sensorThresholdRepository.findTopByOrderByIdDesc();
        if (savedThreshold.isPresent()) {
            SensorThreshold st = savedThreshold.get();
            thresholds.put("temperature", st.getTemperatureThreshold());
            thresholds.put("humidity", st.getHumidityThreshold());
            thresholds.put("smoke", st.getSmokeThreshold());
            thresholds.put("light", st.getLightThreshold());
            log.info("从数据库加载阈值: {}", thresholds);
        } else {
            // 初始化默认阈值
            thresholds.put("temperature", 35.0f);
            thresholds.put("humidity", 60.0f);
            thresholds.put("smoke", 60.0f);
            thresholds.put("light", 600.0f);
            // 保存默认阈值到数据库
            saveThresholdToDb();
            log.info("使用默认阈值: {}", thresholds);
        }

        log.info("SensorService initialized");
        log.info("  Sensor Server: http://{}:{}", sensorServerHost, sensorServerPort);
        log.info("  Sensor Poll Interval: {} ms", sensorPollIntervalMs);

        // 检查 HTTP 服务器是否可用
        if (isHttpServerAvailable()) {
            log.info("  ✓ HTTP API 服务器可用");
        } else {
            log.warn("  ✗ HTTP API 服务器不可用，将使用模拟数据");
        }

        // 注意：默认不启动环境监测，由前端手动开启
    }

    private void saveThresholdToDb() {
        SensorThreshold st = new SensorThreshold();
        st.setTemperatureThreshold(thresholds.get("temperature"));
        st.setHumidityThreshold(thresholds.get("humidity"));
        st.setSmokeThreshold(thresholds.get("smoke"));
        st.setLightThreshold(thresholds.get("light"));
        sensorThresholdRepository.save(st);
    }

    /**
     * 检查 HTTP API 服务器是否可用
     */
    private boolean isHttpServerAvailable() {
        if (sensorServerRestTemplate == null) return false;
        try {
            String url = String.format("http://%s:%d/health", sensorServerHost, sensorServerPort);
            ResponseEntity<Map> response = sensorServerRestTemplate.getForEntity(url, Map.class);
            return response.getStatusCode().is2xxSuccessful();
        } catch (RestClientException e) {
            log.debug("HTTP server not available: {}", e.getMessage());
            return false;
        }
    }

    /**
     * 从硬件读取传感器数据
     * 优先使用 HTTP API，失败则使用模拟数据
     */
    public SensorData readSensorDataFromHardware() {
        SensorData data = new SensorData();

        boolean success = false;

        // 优先使用 HTTP API
        if (isHttpServerAvailable()) {
            success = readViaHttpApi(data);
        }

        // 如果 HTTP 失败，使用模拟数据
        if (!success) {
            log.info("使用模拟传感器数据");
            data.setTemperature(25.0f + (float)(Math.random() * 5));
            data.setHumidity(60.0f + (float)(Math.random() * 10));
            data.setSmoke(50.0f + (float)(Math.random() * 30));
            data.setLight(300.0f + (float)(Math.random() * 200));
        }

        // 检查是否超过阈值
        checkAlert(data);

        // 保存数据
        latestSensorData = sensorDataRepository.save(data);

        return latestSensorData;
    }

    /**
     * 通过 HTTP API 读取传感器数据
     */
    @SuppressWarnings("unchecked")
    private boolean readViaHttpApi(SensorData data) {
        try {
            String url = String.format("http://%s:%d/sensor", sensorServerHost, sensorServerPort);

            log.info("Calling Sensor HTTP API: GET {}", url);
            ResponseEntity<Map> response = sensorServerRestTemplate.getForEntity(url, Map.class);

            if (response.getStatusCode().is2xxSuccessful() && response.getBody() != null) {
                Map<String, Object> result = response.getBody();

                // 解析响应数据
                Object temp = result.get("temperature");
                Object hum = result.get("humidity");
                Object smoke = result.get("smoke");
                Object light = result.get("light");

                if (temp != null) {
                    data.setTemperature(Float.parseFloat(temp.toString()));
                }
                if (hum != null) {
                    data.setHumidity(Float.parseFloat(hum.toString()));
                }
                if (smoke != null) {
                    data.setSmoke(Float.parseFloat(smoke.toString()));
                }
                if (light != null) {
                    data.setLight(Float.parseFloat(light.toString()));
                }

                log.info("HTTP API 返回: 温度={}°C, 湿度={}%, 烟雾={}ppm, 光照={}lux",
                    data.getTemperature(), data.getHumidity(), data.getSmoke(), data.getLight());

                return true;
            }

            log.error("HTTP API 调用失败: {}", response.getStatusCode());
            return false;

        } catch (RestClientException e) {
            log.error("调用传感器 HTTP API 失败: {}", e.getMessage());
            return false;
        } catch (Exception e) {
            log.error("解析传感器响应失败: {}", e.getMessage());
            return false;
        }
    }

    /**
     * 启动环境监测
     */
    public void startMonitoring() {
        monitoringEnabled.set(true);
        log.info("环境监测已开启");

        // 立即读取一次数据
        try {
            readSensorDataFromHardware();
        } catch (Exception e) {
            log.error("启动时读取传感器数据失败", e);
        }
    }

    /**
     * 停止环境监测
     */
    public void stopMonitoring() {
        monitoringEnabled.set(false);
        log.info("环境监测已关闭");
    }

    /**
     * 设置监测状态
     */
    public void setMonitoringEnabled(boolean enabled) {
        if (enabled) {
            startMonitoring();
        } else {
            stopMonitoring();
        }
    }

    /**
     * 获取监测状态
     */
    public Map<String, Object> getMonitoringStatus() {
        Map<String, Object> status = new ConcurrentHashMap<>();
        status.put("enabled", monitoringEnabled.get());
        status.put("thresholds", thresholds);
        status.put("latestData", latestSensorData);
        status.put("httpServerAvailable", isHttpServerAvailable());
        return status;
    }

    /**
     * 检查是否超过阈值并设置报警，同时创建报警记录
     */
    private void checkAlert(SensorData data) {
        StringBuilder alertMsg = new StringBuilder();
        boolean isAlert = false;

        if (data.getTemperature() != null &&
            data.getTemperature() > thresholds.get("temperature")) {
            isAlert = true;
            alertMsg.append("温度超过阈值(").append(thresholds.get("temperature")).append("°C) ");
        }

        if (data.getHumidity() != null &&
            data.getHumidity() > thresholds.get("humidity")) {
            isAlert = true;
            alertMsg.append("湿度超过阈值(").append(thresholds.get("humidity")).append("%) ");
        }

        if (isSmokeAlertConfirmed(data.getSmoke())) {
            isAlert = true;
            alertMsg.append("烟雾浓度超过阈值(").append(thresholds.get("smoke")).append("ppm) ");
        }

        if (data.getLight() != null &&
            data.getLight() > thresholds.get("light")) {
            isAlert = true;
            alertMsg.append("光照强度超过阈值(").append(thresholds.get("light")).append("lux) ");
        }

        data.setAlertMessage(alertMsg.length() > 0 ? alertMsg.toString() : null);

        if (isAlert) {
            log.warn("环境报警: {}", alertMsg);

            // 如果监测开启且超过阈值，创建报警记录
            if (monitoringEnabled.get()) {
                createAlertRecord(data, alertMsg.toString());
            }
        }
    }

    /**
     * MQ-2 经过 ADS1115 换算后偶尔会出现单点尖峰，烟雾报警需要连续多次确认。
     */
    private synchronized boolean isSmokeAlertConfirmed(Float smokeValue) {
        Float smokeThreshold = thresholds.get("smoke");
        if (smokeValue == null || smokeThreshold == null || smokeValue <= smokeThreshold) {
            smokeAlertOverThresholdCount = 0;
            return false;
        }

        smokeAlertOverThresholdCount++;
        if (smokeAlertOverThresholdCount < SMOKE_ALERT_CONFIRM_SAMPLES) {
            log.warn("烟雾浓度单次超阈值，等待连续确认({}/{}): {}ppm > {}ppm",
                smokeAlertOverThresholdCount, SMOKE_ALERT_CONFIRM_SAMPLES, smokeValue, smokeThreshold);
            return false;
        }

        return true;
    }

    /**
     * 创建环境报警记录
     */
    private void createAlertRecord(SensorData data, String alertMessage) {
        LocalDateTime now = LocalDateTime.now();

        // 检查是否在报警间隔内
        if (lastAlertTime != null &&
            java.time.Duration.between(lastAlertTime, now).toMillis() < ALERT_INTERVAL_MS) {
            log.debug("距离上次报警不足{}秒，跳过记录", ALERT_INTERVAL_MS / 1000);
            return;
        }

        try {
            DetectionRecord record = new DetectionRecord();
            record.setCameraId(ENV_MONITOR_CAMERA_ID);
            record.setCameraName(ENV_MONITOR_CAMERA_NAME);
            record.setDetectionTime(now);
            record.setAiDescription("env_" + alertMessage);
            record.setIsProcessed(false);
            detectionRecordAiAnalysisService.preparePendingAnalysis(record);

            // 添加详细的环境数据（包含阈值信息）
            StringBuilder detail = new StringBuilder();
            detail.append("温度: ").append(data.getTemperature()).append("°C");
            detail.append(", 湿度: ").append(data.getHumidity()).append("%");
            detail.append(", 烟雾: ").append(data.getSmoke()).append("ppm");
            detail.append(", 光照: ").append(data.getLight()).append("lux");
            detail.append(" | 阈值-温度: ").append(thresholds.get("temperature")).append("°C");
            detail.append(", 阈值-湿度: ").append(thresholds.get("humidity")).append("%");
            detail.append(", 阈值-烟雾: ").append(thresholds.get("smoke")).append("ppm");
            detail.append(", 阈值-光照: ").append(thresholds.get("light")).append("lux");
            record.setProcessNotes(detail.toString());

            DetectionRecord saved = detectionRecordRepository.save(record);
            detectionRecordAiAnalysisService.requestAnalysis(saved.getId());
            lastAlertTime = now;
            log.info("已创建环境报警记录: {}", alertMessage);
        } catch (Exception e) {
            log.error("创建环境报警记录失败", e);
        }
    }

    /**
     * 获取最新传感器数据
     */
    public SensorData getLatestSensorData() {
        if (latestSensorData != null) {
            return latestSensorData;
        }
        return sensorDataRepository.findTopByOrderByCreateTimeDesc();
    }

    /**
     * 获取传感器历史数据
     */
    public Page<SensorData> getSensorHistory(Pageable pageable) {
        return sensorDataRepository.findAllByOrderByCreateTimeDesc(pageable);
    }

    /**
     * 更新阈值
     */
    @Transactional
    public void updateThreshold(Map<String, Float> newThresholds) {
        if (newThresholds.containsKey("temperature")) {
            thresholds.put("temperature", newThresholds.get("temperature"));
        }
        if (newThresholds.containsKey("humidity")) {
            thresholds.put("humidity", newThresholds.get("humidity"));
        }
        if (newThresholds.containsKey("smoke")) {
            thresholds.put("smoke", newThresholds.get("smoke"));
        }
        if (newThresholds.containsKey("light")) {
            thresholds.put("light", newThresholds.get("light"));
        }
        // 持久化到数据库
        saveThresholdToDb();
        log.info("阈值已更新: {}", thresholds);
    }

    /**
     * 获取当前阈值
     */
    public Map<String, Float> getThreshold() {
        return new ConcurrentHashMap<>(thresholds);
    }

    /**
     * 定时任务：按配置周期读取一次传感器数据
     * 只有在监测开启时才执行
     */
    @Scheduled(fixedRateString = "${sensor.poll.interval-ms:1000}")
    public void scheduledSensorRead() {
        if (monitoringEnabled.get()) {
            try {
                readSensorDataFromHardware();
            } catch (Exception e) {
                log.error("定时读取传感器数据失败", e);
            }
        }
    }
}
