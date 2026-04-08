package com.example.demo.controller;

import com.example.demo.dto.Result;
import com.example.demo.entity.SensorData;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.web.bind.annotation.*;
import com.example.demo.service.SensorService;

import java.util.Map;

@Slf4j
@RestController
@RequestMapping("/sensor")
@RequiredArgsConstructor
public class SensorController {
    
    private final SensorService sensorService;
    
    // 获取最新传感器数据
    @GetMapping("/latest")
    public Result<SensorData> getLatestSensorData() {
        try {
            SensorData data = sensorService.getLatestSensorData();
            if (data == null) {
                return Result.error(404, "暂无传感器数据");
            }
            return Result.success(data);
        } catch (Exception e) {
            log.error("获取传感器数据失败", e);
            return Result.error(500, "获取传感器数据失败: " + e.getMessage());
        }
    }
    
    // 获取传感器历史数据（分页）
    @GetMapping("/history")
    public Result<Page<SensorData>> getSensorHistory(
            @RequestParam(defaultValue = "0") int page,
            @RequestParam(defaultValue = "20") int size) {
        try {
            Pageable pageable = PageRequest.of(page, size, Sort.by(Sort.Direction.DESC, "createTime"));
            Page<SensorData> history = sensorService.getSensorHistory(pageable);
            return Result.success(history);
        } catch (Exception e) {
            log.error("获取传感器历史数据失败", e);
            return Result.error(500, "获取传感器历史数据失败: " + e.getMessage());
        }
    }
    
    // 更新阈值
    @PutMapping("/threshold")
    public Result<Void> updateThreshold(@RequestBody Map<String, Float> thresholds) {
        log.info("[SensorController] 收到阈值更新请求: {}", thresholds);
        try {
            sensorService.updateThreshold(thresholds);
            return Result.success();
        } catch (Exception e) {
            log.error("更新阈值失败", e);
            return Result.error(400, "更新阈值失败: " + e.getMessage());
        }
    }
    
    // 获取当前阈值
    @GetMapping("/threshold")
    public Result<Map<String, Float>> getThreshold() {
        try {
            Map<String, Float> threshold = sensorService.getThreshold();
            return Result.success(threshold);
        } catch (Exception e) {
            log.error("获取阈值失败", e);
            return Result.error(500, "获取阈值失败: " + e.getMessage());
        }
    }
    
    // 手动刷新传感器数据（从硬件读取）
    @PostMapping("/refresh")
    public Result<SensorData> refreshSensorData() {
        try {
            SensorData data = sensorService.readSensorDataFromHardware();
            return Result.success(data);
        } catch (Exception e) {
            log.error("刷新传感器数据失败", e);
            return Result.error(500, "刷新传感器数据失败: " + e.getMessage());
        }
    }
    
    // 开启/关闭环境监测
    @PutMapping("/monitoring")
    public Result<Void> toggleMonitoring(@RequestParam Boolean enabled) {
        try {
            sensorService.setMonitoringEnabled(enabled);
            return Result.success();
        } catch (Exception e) {
            log.error("切换监测状态失败", e);
            return Result.error(400, "切换监测状态失败: " + e.getMessage());
        }
    }
    
    // 获取监测状态
    @GetMapping("/monitoring")
    public Result<Map<String, Object>> getMonitoringStatus() {
        try {
            Map<String, Object> status = sensorService.getMonitoringStatus();
            return Result.success(status);
        } catch (Exception e) {
            log.error("获取监测状态失败", e);
            return Result.error(500, "获取监测状态失败: " + e.getMessage());
        }
    }
}
