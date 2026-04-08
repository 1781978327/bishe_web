package com.example.demo.controller;

import com.example.demo.dto.CameraCreateRequest;
import com.example.demo.dto.CameraUpdateRequest;
import com.example.demo.dto.Result;
import com.example.demo.entity.Camera;
import com.example.demo.service.CameraService;
import jakarta.validation.Valid;
import lombok.RequiredArgsConstructor;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.http.ResponseEntity;
import org.springframework.validation.FieldError;
import org.springframework.web.bind.MethodArgumentNotValidException;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.Map;

@RestController
@RequestMapping("/camera")
@RequiredArgsConstructor
public class CameraController {
    
    private final CameraService cameraService;
    
    // 新增监控设备
    @PostMapping
    public Result<Camera> createCamera(@Valid @RequestBody CameraCreateRequest request) {
        try {
            Camera camera = cameraService.createCamera(request);
            return Result.success(camera);
        } catch (Exception e) {
            return Result.error(400, e.getMessage());
        }
    }
    
    // 获取设备列表（分页）
    @GetMapping
    public Result<Page<Camera>> getCameras(
            @RequestParam(defaultValue = "0") int page,
            @RequestParam(defaultValue = "10") int size,
            @RequestParam(defaultValue = "createTime") String sortBy,
            @RequestParam(defaultValue = "desc") String sortDir,
            @RequestParam(required = false) String name,
            @RequestParam(required = false) Integer status,
            @RequestParam(required = false) Boolean enabled) {
        
        Sort.Direction direction = sortDir.equalsIgnoreCase("desc") ? Sort.Direction.DESC : Sort.Direction.ASC;
        Pageable pageable = PageRequest.of(page, size, Sort.by(direction, sortBy));
        
        Page<Camera> cameras = cameraService.getCameras(pageable, name, status, enabled);
        return Result.success(cameras);
    }
    
    // 获取设备详情
    @GetMapping("/{id}")
    public Result<Camera> getCamera(@PathVariable Long id) {
        try {
            Camera camera = cameraService.getCameraById(id);
            return Result.success(camera);
        } catch (RuntimeException e) {
            return Result.error(404, e.getMessage());
        }
    }
    
    // 更新设备信息
    @PutMapping("/{id}")
    public Result<Camera> updateCamera(@PathVariable Long id, @Valid @RequestBody CameraUpdateRequest request) {
        try {
            Camera camera = cameraService.updateCamera(id, request);
            return Result.success(camera);
        } catch (RuntimeException e) {
            return Result.error(404, e.getMessage());
        } catch (Exception e) {
            return Result.error(400, e.getMessage());
        }
    }
    
    // 删除设备
    @DeleteMapping("/{id}")
    public Result<Void> deleteCamera(@PathVariable Long id) {
        try {
            cameraService.deleteCamera(id);
            return Result.success();
        } catch (RuntimeException e) {
            return Result.error(404, e.getMessage());
        }
    }
    
    // 启用/禁用设备
    @PutMapping("/{id}/enable")
    public Result<Void> toggleCameraEnabled(@PathVariable Long id, @RequestParam Boolean enabled) {
        try {
            cameraService.toggleCameraEnabled(id, enabled);
            return Result.success();
        } catch (RuntimeException e) {
            return Result.error(404, e.getMessage());
        }
    }
    
    // 启用/禁用检测功能
    @PutMapping("/{id}/detection")
    public Result<Void> toggleDetectionEnabled(@PathVariable Long id, @RequestParam Boolean enabled) {
        try {
            cameraService.toggleDetectionEnabled(id, enabled);
            return Result.success();
        } catch (RuntimeException e) {
            return Result.error(404, e.getMessage());
        }
    }
    
    // 更新设备状态
    @PutMapping("/{id}/status")
    public Result<Void> updateCameraStatus(@PathVariable Long id, @RequestParam Integer status) {
        try {
            cameraService.updateCameraStatus(id, status);
            return Result.success();
        } catch (RuntimeException e) {
            return Result.error(404, e.getMessage());
        }
    }

    // 心跳：算法端定时调用，标记设备在线
    @PostMapping("/{id}/heartbeat")
    public Result<Void> heartbeat(@PathVariable Long id) {
        try {
            cameraService.heartbeat(id);
            return Result.success();
        } catch (RuntimeException e) {
            return Result.error(404, e.getMessage());
        }
    }
    
    // 获取在线设备列表
    @GetMapping("/online")
    public Result<java.util.List<Camera>> getOnlineCameras() {
        java.util.List<Camera> cameras = cameraService.getOnlineCameras();
        return Result.success(cameras);
    }
    
    // 获取启用检测的设备列表
    @GetMapping("/detection-enabled")
    public Result<java.util.List<Camera>> getDetectionEnabledCameras() {
        java.util.List<Camera> cameras = cameraService.getDetectionEnabledCameras();
        return Result.success(cameras);
    }
    
    // 全局异常处理
    @ExceptionHandler(MethodArgumentNotValidException.class)
    public ResponseEntity<Result<Void>> handleValidationExceptions(MethodArgumentNotValidException ex) {
        Map<String, String> errors = new HashMap<>();
        ex.getBindingResult().getAllErrors().forEach((error) -> {
            String fieldName = ((FieldError) error).getField();
            String errorMessage = error.getDefaultMessage();
            errors.put(fieldName, errorMessage);
        });
        
        String message = errors.values().iterator().next();
        return ResponseEntity.badRequest().body(Result.error(400, message));
    }
}
