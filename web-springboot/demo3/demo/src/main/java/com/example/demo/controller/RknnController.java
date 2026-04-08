package com.example.demo.controller;

import com.example.demo.dto.ForbiddenAreaSaveRequest;
import com.example.demo.dto.Result;
import com.example.demo.service.RknnService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.http.CacheControl;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.nio.charset.StandardCharsets;
import java.util.Map;

@Slf4j
@RestController
@RequestMapping("/rknn")
@RequiredArgsConstructor
public class RknnController {

    private final RknnService rknnService;

    // 仅保留前端当前实际使用的最小接口集合

    /**
     * 开启推理
     * @param track 是否开启目标跟踪
     */
    @PostMapping("/inference/on")
    public Result<Map<String, Object>> startInference(@RequestParam(defaultValue = "false") boolean track) {
        try {
            log.info("开启推理, track={}", track);
            Map<String, Object> result = rknnService.startInference(track);
            return Result.success(result);
        } catch (Exception e) {
            log.error("开启推理失败", e);
            return Result.error(500, "开启推理失败: " + e.getMessage());
        }
    }

    /**
     * 关闭推理
     */
    @PostMapping("/inference/off")
    public Result<Map<String, Object>> stopInference() {
        try {
            log.info("关闭推理");
            Map<String, Object> result = rknnService.stopInference();
            return Result.success(result);
        } catch (Exception e) {
            log.error("关闭推理失败", e);
            return Result.error(500, "关闭推理失败: " + e.getMessage());
        }
    }

    /**
     * 开启摄像头推流（两路 1280x720）
     */
    @PostMapping("/rtsp/camera/start")
    public Result<Map<String, Object>> startCameraRtsp() {
        try {
            log.info("开启摄像头推流");
            Map<String, Object> result = rknnService.startCameraRtsp();
            return Result.success(result);
        } catch (Exception e) {
            log.error("开启摄像头推流失败", e);
            return Result.error(500, "开启摄像头推流失败: " + e.getMessage());
        }
    }

    /**
     * 设置检测阈值
     * @param value 置信度阈值 (0.0-1.0)
     * @param boxCount 框数量阈值（可选，0 表示不限制）
     */
    @PostMapping("/threshold/set")
    public Result<Map<String, Object>> setThreshold(
            @RequestParam float value,
            @RequestParam(required = false) Integer boxCount) {
        try {
            log.info("设置检测阈值: {}, boxCount: {}", value, boxCount);
            Map<String, Object> result = rknnService.setThreshold(value, boxCount);
            return Result.success(result);
        } catch (Exception e) {
            log.error("设置阈值失败", e);
            return Result.error(500, "设置阈值失败: " + e.getMessage());
        }
    }

    /**
     * 获取指定摄像头当前帧（JPEG）
     * @param cameraId 1 表示 cam0，2 表示 cam1
     * @param track 是否按跟踪模式取帧
     */
    @GetMapping("/frame/current")
    public ResponseEntity<byte[]> getCurrentFrame(
            @RequestParam(defaultValue = "1") int cameraId,
            @RequestParam(defaultValue = "false") boolean track) {
        try {
            byte[] frame = rknnService.getCurrentFrame(cameraId, track);
            HttpHeaders headers = new HttpHeaders();
            headers.setContentType(MediaType.IMAGE_JPEG);
            headers.setCacheControl(CacheControl.noStore().mustRevalidate());
            return ResponseEntity.ok()
                    .headers(headers)
                    .body(frame);
        } catch (Exception e) {
            log.error("获取当前帧失败 cameraId={}, track={}", cameraId, track, e);
            return ResponseEntity.status(503)
                    .contentType(MediaType.TEXT_PLAIN)
                    .body(("获取当前帧失败: " + e.getMessage()).getBytes(StandardCharsets.UTF_8));
        }
    }

    /**
     * 保存禁入区域（四边形四点）
     */
    @PostMapping("/forbidden-area")
    public Result<Map<String, Object>> saveForbiddenArea(@RequestBody ForbiddenAreaSaveRequest request) {
        try {
            Map<String, Object> result = rknnService.saveForbiddenArea(request);
            return Result.success(result);
        } catch (IllegalArgumentException e) {
            return Result.error(400, e.getMessage());
        } catch (Exception e) {
            log.error("保存禁入区域失败", e);
            return Result.error(500, "保存禁入区域失败: " + e.getMessage());
        }
    }

    /**
     * 查询禁入区域
     */
    @GetMapping("/forbidden-area")
    public Result<Map<String, Object>> getForbiddenArea(@RequestParam Long cameraId) {
        try {
            Map<String, Object> result = rknnService.getForbiddenArea(cameraId);
            return Result.success(result);
        } catch (IllegalArgumentException e) {
            return Result.error(400, e.getMessage());
        } catch (Exception e) {
            log.error("查询禁入区域失败", e);
            return Result.error(500, "查询禁入区域失败: " + e.getMessage());
        }
    }

    /**
     * 获取指定摄像头的检测数量
     * @param cam 摄像头编号 (0 或 1，默认 0)
     */
    @GetMapping("/detection/count")
    public Result<Map<String, Object>> getDetectionCount(@RequestParam(defaultValue = "0") int cam) {
        try {
            log.info("获取摄像头 {} 的检测数量", cam);
            Map<String, Object> result = rknnService.getDetectionCount(cam);
            return Result.success(result);
        } catch (Exception e) {
            log.error("获取检测数量失败", e);
            return Result.error(500, "获取检测数量失败: " + e.getMessage());
        }
    }
}
