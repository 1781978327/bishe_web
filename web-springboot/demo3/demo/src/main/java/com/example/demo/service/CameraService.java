package com.example.demo.service;

import com.example.demo.dto.CameraCreateRequest;
import com.example.demo.dto.CameraUpdateRequest;
import com.example.demo.entity.Camera;
import com.example.demo.repository.CameraRepository;
import lombok.RequiredArgsConstructor;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.domain.Specification;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import jakarta.persistence.criteria.Predicate;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.List;

@Service
@RequiredArgsConstructor
@Transactional
public class CameraService {
    
    private final CameraRepository cameraRepository;
    
    // 新增监控设备
    public Camera createCamera(CameraCreateRequest request) {
        Camera camera = new Camera();
        camera.setName(request.getName());
        camera.setRtspUrl(request.getRtspUrl());
        camera.setLocation(request.getLocation());
        camera.setStatus(request.getStatus() != null ? request.getStatus() : 0); // 默认离线
        camera.setIsEnabled(request.getIsEnabled() != null ? request.getIsEnabled() : true);
        camera.setDetectionEnabled(request.getDetectionEnabled() != null ? request.getDetectionEnabled() : true);
        camera.setResolution(request.getResolution());
        camera.setFrameRate(request.getFrameRate());
        camera.setDescription(request.getDescription());
        
        // 只有状态为在线时才设置最后在线时间
        if (camera.getStatus() == 1) {
            camera.setLastOnlineTime(LocalDateTime.now());
        }
        
        return cameraRepository.save(camera);
    }
    
    // 获取设备列表（分页，支持条件查询）
    @Transactional(readOnly = true)
    public Page<Camera> getCameras(Pageable pageable, String name, Integer status, Boolean enabled) {
        Specification<Camera> spec = (root, query, criteriaBuilder) -> {
            List<Predicate> predicates = new ArrayList<>();
            
            if (name != null && !name.trim().isEmpty()) {
                predicates.add(criteriaBuilder.like(
                    criteriaBuilder.lower(root.get("name")), 
                    "%" + name.toLowerCase() + "%"
                ));
            }
            
            if (status != null) {
                predicates.add(criteriaBuilder.equal(root.get("status"), status));
            }
            
            if (enabled != null) {
                predicates.add(criteriaBuilder.equal(root.get("isEnabled"), enabled));
            }
            
            return criteriaBuilder.and(predicates.toArray(new Predicate[0]));
        };
        
        return cameraRepository.findAll(spec, pageable);
    }
    
    // 根据ID获取设备
    @Transactional(readOnly = true)
    public Camera getCameraById(Long id) {
        return cameraRepository.findById(id)
            .orElseThrow(() -> new RuntimeException("设备不存在"));
    }
    
    // 更新设备信息
    public Camera updateCamera(Long id, CameraUpdateRequest request) {
        Camera camera = getCameraById(id);
        
        if (request.getName() != null) {
            camera.setName(request.getName());
        }
        if (request.getRtspUrl() != null) {
            camera.setRtspUrl(request.getRtspUrl());
        }
        if (request.getLocation() != null) {
            camera.setLocation(request.getLocation());
        }
        if (request.getStatus() != null) {
            camera.setStatus(request.getStatus());
            if (request.getStatus() == 1) {
                camera.setLastOnlineTime(LocalDateTime.now());
            }
        }
        if (request.getIsEnabled() != null) {
            camera.setIsEnabled(request.getIsEnabled());
        }
        if (request.getDetectionEnabled() != null) {
            camera.setDetectionEnabled(request.getDetectionEnabled());
        }
        if (request.getResolution() != null) {
            camera.setResolution(request.getResolution());
        }
        if (request.getFrameRate() != null) {
            camera.setFrameRate(request.getFrameRate());
        }
        if (request.getDescription() != null) {
            camera.setDescription(request.getDescription());
        }
        
        return cameraRepository.save(camera);
    }
    
    // 删除设备
    public void deleteCamera(Long id) {
        if (!cameraRepository.existsById(id)) {
            throw new RuntimeException("设备不存在");
        }
        cameraRepository.deleteById(id);
    }
    
    // 启用/禁用设备
    public void toggleCameraEnabled(Long id, Boolean enabled) {
        Camera camera = getCameraById(id);
        camera.setIsEnabled(enabled);
        cameraRepository.save(camera);
    }
    
    // 启用/禁用检测功能
    public void toggleDetectionEnabled(Long id, Boolean enabled) {
        Camera camera = getCameraById(id);
        camera.setDetectionEnabled(enabled);
        cameraRepository.save(camera);
    }
    
    // 更新设备状态
    public void updateCameraStatus(Long id, Integer status) {
        Camera camera = getCameraById(id);
        camera.setStatus(status);
        if (status == 1) {
            camera.setLastOnlineTime(LocalDateTime.now());
        }
        cameraRepository.save(camera);
    }

    // 心跳：算法端定时上报，标记为在线并刷新最后在线时间
    public void heartbeat(Long id) {
        Camera camera = getCameraById(id);
        camera.setStatus(1);
        camera.setLastOnlineTime(LocalDateTime.now());
        cameraRepository.save(camera);
    }
    
    // 获取在线设备列表
    @Transactional(readOnly = true)
    public List<Camera> getOnlineCameras() {
        return cameraRepository.findByStatusOrderByCreateTimeDesc(1);
    }
    
    // 获取启用检测的设备列表
    @Transactional(readOnly = true)
    public List<Camera> getDetectionEnabledCameras() {
        return cameraRepository.findByDetectionEnabledTrueAndStatus(1);
    }
}
