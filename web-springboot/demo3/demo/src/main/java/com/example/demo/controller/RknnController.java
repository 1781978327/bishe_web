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
     * @param tracker 跟踪算法，可选：bytetrack / deepsort
     */
    @PostMapping("/inference/on")
    public Result<Map<String, Object>> startInference(
            @RequestParam(defaultValue = "false") boolean track,
            @RequestParam(required = false) String tracker) {
        try {
            log.info("开启推理, track={}, tracker={}", track, tracker);
            Map<String, Object> result = rknnService.startInference(track, tracker);
            if (isDownstreamError(result)) {
                int statusCode = extractStatusCode(result.get("error"), 500);
                String message = extractErrorMessage(result, "开启推理失败");
                return Result.error(statusCode, message);
            }
            return Result.success(result);
        } catch (IllegalArgumentException e) {
            return Result.error(400, e.getMessage());
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
     * 设置目标跟踪开关（推理开启时即时生效）
     * @param enabled true 开启跟踪，false 关闭跟踪
     */
    @PostMapping("/tracker/set")
    public Result<Map<String, Object>> setTrackerEnabled(@RequestParam boolean enabled) {
        try {
            log.info("设置目标跟踪: enabled={}", enabled);
            Map<String, Object> result = rknnService.setTrackerEnabled(enabled);
            if (isDownstreamError(result)) {
                int statusCode = extractStatusCode(result.get("error"), 500);
                String message = extractErrorMessage(result, "设置目标跟踪失败");
                return Result.error(statusCode, message);
            }
            return Result.success(result);
        } catch (Exception e) {
            log.error("设置目标跟踪失败", e);
            return Result.error(500, "设置目标跟踪失败: " + e.getMessage());
        }
    }

    private boolean isDownstreamError(Map<String, Object> result) {
        if (result == null) {
            return true;
        }
        Object success = result.get("success");
        return Boolean.FALSE.equals(success);
    }

    private String extractErrorMessage(Map<String, Object> result, String fallback) {
        if (result != null) {
            Object error = result.get("error");
            if (error != null && !error.toString().trim().isEmpty()) {
                return error.toString();
            }
            Object message = result.get("message");
            if (message != null && !message.toString().trim().isEmpty()) {
                return message.toString();
            }
        }
        return fallback;
    }

    private int extractStatusCode(Object rawError, int fallback) {
        if (rawError == null) {
            return fallback;
        }
        String text = rawError.toString();
        for (int i = 0; i + 2 < text.length(); i++) {
            char c0 = text.charAt(i);
            char c1 = text.charAt(i + 1);
            char c2 = text.charAt(i + 2);
            if (Character.isDigit(c0) && Character.isDigit(c1) && Character.isDigit(c2)) {
                int code = Integer.parseInt(text.substring(i, i + 3));
                if (code >= 100 && code <= 599) {
                    return code;
                }
            }
        }
        return fallback;
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
     * 获取视觉模块状态
     */
    @GetMapping("/status")
    public Result<Map<String, Object>> getStatus() {
        try {
            Map<String, Object> result = rknnService.getStatus();
            return Result.success(result);
        } catch (Exception e) {
            log.error("获取视觉模块状态失败", e);
            return Result.error(500, "获取视觉模块状态失败: " + e.getMessage());
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
