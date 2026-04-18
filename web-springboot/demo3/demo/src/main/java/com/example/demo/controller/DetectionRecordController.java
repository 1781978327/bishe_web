package com.example.demo.controller;

import com.example.demo.dto.DetectionRecordAddRequest;
import com.example.demo.dto.DetectionRecordProcessRequest;
import com.example.demo.dto.DetectionRecordVO;
import com.example.demo.dto.PageVO;
import com.example.demo.dto.Result;
import com.example.demo.dto.SoundAnomalyReportRequest;
import com.example.demo.entity.DetectionRecord;
import com.example.demo.repository.CameraRepository;
import com.example.demo.repository.DetectionRecordRepository;
import jakarta.validation.Valid;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.time.format.DateTimeParseException;
import java.util.Base64;
import java.util.List;
import java.util.UUID;

@Slf4j
@RestController
@RequestMapping("/detection/record")
@RequiredArgsConstructor
public class DetectionRecordController {

    private final DetectionRecordRepository detectionRecordRepository;
    private final CameraRepository cameraRepository;

    @PostMapping
    public Result<Boolean> add(@Valid @RequestBody DetectionRecordAddRequest request) {
        DetectionRecord record = new DetectionRecord();
        record.setCameraId(request.getCameraId());
        record.setCameraName(cameraRepository.findById(request.getCameraId())
                .map(c -> c.getName() == null ? "" : c.getName())
                .orElse(""));

        record.setDetectionTime(parseTimeOrNow(request.getDetectionTime()));
        record.setAiDescription(request.getDetectionResult());
        record.setIsViolence(true);
        record.setIsProcessed(false);
        record.setLevel(extractLevel(request.getDetectionResult()));
        record.setImageUrl(saveBase64ImageAndGetUrl(request.getImageBase64()));

        // 设置声音异常专用字段
        if (request.getAudioUrl() != null) {
            record.setAudioUrl(request.getAudioUrl());
        }
        if (request.getAudioDuration() != null) {
            record.setAudioDuration(request.getAudioDuration());
        }
        if (request.getSoundKeywords() != null) {
            record.setSoundKeywords(request.getSoundKeywords());
        }

        detectionRecordRepository.save(record);
        return Result.success(true);
    }

    @GetMapping("/page")
    public Result<PageVO<DetectionRecordVO>> page(
            @RequestParam int current,
            @RequestParam int size,
            @RequestParam(required = false) Long cameraId,
            @RequestParam(required = false) Integer processed,
            @RequestParam(required = false) String eventType
    ) {
        Pageable pageable = PageRequest.of(Math.max(current - 1, 0), Math.max(size, 1),
                Sort.by(Sort.Direction.DESC, "detectionTime"));

        Page<DetectionRecord> page;
        if (cameraId != null) {
            page = detectionRecordRepository.findByCameraIdOrderByDetectionTimeDesc(cameraId, pageable);
        } else if (processed != null) {
            page = detectionRecordRepository.findByIsProcessedOrderByDetectionTimeDesc(processed == 1, pageable);
        } else if (eventType != null && !eventType.isBlank()) {
            // 根据事件类型筛选
            page = filterByEventType(eventType, pageable);
        } else {
            page = detectionRecordRepository.findAll(pageable);
        }

        PageVO<DetectionRecordVO> vo = new PageVO<>();
        vo.setCurrent(current);
        vo.setSize(size);
        vo.setTotal(page.getTotalElements());
        vo.setPages(page.getTotalPages());
        vo.setRecords(page.getContent().stream().map(this::toVO).toList());
        return Result.success(vo);
    }

    /**
     * 根据事件类型筛选记录（三大类）
     * video: 监控异常 (fall, fight, knife, env_intrusion)
     * sound: 声音异常（兼容 sound_* 与 “声音异常 - xxx”）
     * env: 环境异常 (env_*，不含 env_intrusion)
     */
    private Page<DetectionRecord> filterByEventType(String eventType, Pageable pageable) {
        return switch (eventType) {
            case "video" -> detectionRecordRepository.findByVideoEvents(pageable);
            case "sound" -> detectionRecordRepository.findBySoundEvents(pageable);
            case "env" -> detectionRecordRepository.findByEnvEvents(pageable);
            default -> detectionRecordRepository.findAll(pageable);
        };
    }

    @GetMapping("/{id}")
    public Result<DetectionRecordVO> getById(@PathVariable Long id) {
        DetectionRecord record = detectionRecordRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("记录不存在"));
        return Result.success(toVO(record));
    }

    @PutMapping("/process")
    public Result<Boolean> process(@Valid @RequestBody DetectionRecordProcessRequest request) {
        DetectionRecord record = detectionRecordRepository.findById(request.getId())
                .orElseThrow(() -> new RuntimeException("记录不存在"));

        boolean processed = request.getProcessed() != null && request.getProcessed() == 1;
        record.setIsProcessed(processed);
        record.setProcessNotes(request.getProcessContent());
        record.setProcessedTime(LocalDateTime.now());
        if (request.getProcessImageBase64() != null && !request.getProcessImageBase64().isBlank()) {
            record.setProcessImageUrl(saveBase64ImageAndGetUrl(request.getProcessImageBase64(), "process"));
        }

        detectionRecordRepository.save(record);
        return Result.success(true);
    }

    @PutMapping("/{id}/process")
    public Result<Boolean> updateProcessed(@PathVariable Long id, @RequestParam Integer processed) {
        DetectionRecord record = detectionRecordRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("记录不存在"));
        record.setIsProcessed(processed != null && processed == 1);
        record.setProcessedTime(LocalDateTime.now());
        detectionRecordRepository.save(record);
        return Result.success(true);
    }

    @DeleteMapping("/{id}")
    public Result<Boolean> delete(@PathVariable Long id) {
        if (!detectionRecordRepository.existsById(id)) {
            return Result.error(404, "记录不存在");
        }
        detectionRecordRepository.deleteById(id);
        return Result.success(true);
    }

    @DeleteMapping("/batch")
    public Result<Boolean> batchDelete(@RequestBody List<Long> ids) {
        if (ids == null || ids.isEmpty()) {
            return Result.error(400, "请选择要删除的记录");
        }
        detectionRecordRepository.deleteAllById(ids);
        return Result.success(true);
    }

    @DeleteMapping("/clear-all")
    public Result<Boolean> clearAll() {
        detectionRecordRepository.deleteAllInBatch();
        return Result.success(true);
    }

    private DetectionRecordVO toVO(DetectionRecord r) {
        DetectionRecordVO vo = new DetectionRecordVO();
        vo.setId(r.getId());
        vo.setCameraId(r.getCameraId());
        vo.setCameraName(r.getCameraName());
        vo.setImageUrl(r.getImageUrl());
        vo.setDetectionTime(r.getDetectionTime() == null ? null : r.getDetectionTime().toString());
        vo.setDetectionResult(r.getAiDescription());
        vo.setProcessed(r.getIsProcessed() != null && r.getIsProcessed() ? 1 : 0);
        vo.setProcessContent(r.getProcessNotes());
        vo.setProcessImageUrl(r.getProcessImageUrl());
        vo.setProcessTime(r.getProcessedTime() == null ? null : r.getProcessedTime().toString());
        vo.setCreateTime(r.getCreateTime() == null ? null : r.getCreateTime().toString());
        // 环境监测专用字段
        vo.setAiDescription(r.getAiDescription());
        vo.setProcessNotes(r.getProcessNotes());

        // 声音异常专用字段
        vo.setAudioUrl(r.getAudioUrl());
        vo.setAudioDuration(r.getAudioDuration());
        vo.setSoundKeywords(r.getSoundKeywords());

        // 调试日志
        System.out.println("[DEBUG toVO] id=" + r.getId() + ", aiDescription=" + r.getAiDescription() + ", processNotes=" + r.getProcessNotes());
        System.out.println("[DEBUG toVO] vo.aiDescription=" + vo.getAiDescription() + ", vo.processNotes=" + vo.getProcessNotes());

        return vo;
    }

    private LocalDateTime parseTimeOrNow(String iso) {
        if (iso == null || iso.isBlank()) {
            return LocalDateTime.now();
        }
        try {
            return LocalDateTime.parse(iso);
        } catch (DateTimeParseException e) {
            return LocalDateTime.now();
        }
    }

    private int extractLevel(String detectionResult) {
        // 不再依赖 riskLevel/maxRiskLevel，由算法事件类型决定等级：
        // - fight/knife: 3
        // - fall: 2
        // - sound_scream/sound_glass: 3
        // - sound_fight: 4
        // - sound_other: 2
        // - 其他: 1
        if (detectionResult == null) {
            return 1;
        }
        String s = detectionResult.toLowerCase();
        if (s.contains("\"type\":\"sound_fight\"") || s.contains("sound_fight")) {
            return 4;
        }
        if (s.contains("\"type\":\"sound_scream\"") || s.contains("sound_scream") ||
                s.contains("\"type\":\"sound_glass\"") || s.contains("sound_glass")) {
            return 3;
        }
        if (s.contains("\"type\":\"sound_other\"") || s.contains("sound_other")) {
            return 2;
        }
        if (s.contains("\"type\":\"fight\"") || s.contains("\"type\":\"knife\"") ||
                s.contains("fight") || s.contains("knife")) {
            return 3;
        }
        if (s.contains("\"type\":\"fall\"") || s.contains("fall")) {
            return 2;
        }
        return 1;
    }

    private String saveBase64ImageAndGetUrl(String imageBase64) {
        return saveBase64ImageAndGetUrl(imageBase64, "detection");
    }

    private String saveBase64ImageAndGetUrl(String imageBase64, String category) {
        if (imageBase64 == null || imageBase64.isBlank()) {
            return "";
        }

        try {
            String b64 = imageBase64;
            // 兼容 data:image/jpeg;base64,xxxx
            int commaIdx = b64.indexOf(',');
            if (b64.startsWith("data:image") && commaIdx > 0) {
                b64 = b64.substring(commaIdx + 1);
            }
            byte[] bytes = Base64.getDecoder().decode(b64);

            String dateDir = java.time.LocalDate.now().toString().replace("-", "");
            Path dir = Paths.get("uploads", category, dateDir).toAbsolutePath().normalize();
            Files.createDirectories(dir);

            String filename = UUID.randomUUID() + ".jpg";
            Path filePath = dir.resolve(filename);
            Files.write(filePath, bytes);

            // 统一走 FileController 读取，避免静态资源映射差异导致 404
            return "/api/file/" + category + "/" + dateDir + "/" + filename;
        } catch (IllegalArgumentException | IOException e) {
            return "";
        }
    }

    /**
     * 视觉服务阈值告警上报接口（无需认证）
     * 典型来源：RKNN C++ 服务在“检测框数量超过阈值”时主动上报。
     */
    @PostMapping("/rknn/report")
    public Result<Boolean> reportRknnAlert(@RequestBody DetectionRecordAddRequest request) {
        try {
            if (request.getCameraId() == null) {
                return Result.error(400, "cameraId不能为空");
            }
            if (request.getDetectionResult() == null || request.getDetectionResult().isBlank()) {
                return Result.error(400, "detectionResult不能为空");
            }

            DetectionRecord record = new DetectionRecord();
            record.setCameraId(request.getCameraId());
            record.setCameraName(cameraRepository.findById(request.getCameraId())
                    .map(c -> c.getName() == null ? "" : c.getName())
                    .orElse("摄像头" + request.getCameraId()));
            record.setDetectionTime(parseTimeOrNow(request.getDetectionTime()));
            record.setAiDescription(request.getDetectionResult());
            record.setIsViolence(true);
            record.setIsProcessed(false);
            record.setLevel(extractLevel(request.getDetectionResult()));
            record.setImageUrl(saveBase64ImageAndGetUrl(request.getImageBase64()));

            detectionRecordRepository.save(record);
            log.info("RKNN 阈值告警已上报到安全记录: cameraId={}, result={}",
                    request.getCameraId(), request.getDetectionResult());
            return Result.success(true);
        } catch (Exception e) {
            log.error("RKNN 阈值告警上报失败", e);
            return Result.error(500, "上报失败: " + e.getMessage());
        }
    }

    /**
     * 声音异常上报专用接口
     * 用于 SoundService 检测到声音异常时自动上报到安全记录
     */
    @PostMapping("/sound/report")
    public Result<Boolean> reportSoundAnomaly(@RequestBody SoundAnomalyReportRequest request) {
        try {
            DetectionRecord record = new DetectionRecord();
            
            // 声音监测使用 cameraId = -1 表示声音监测设备
            record.setCameraId(request.getCameraId() != null ? request.getCameraId() : -1L);
            record.setCameraName(request.getCameraName() != null ? request.getCameraName() : "声音监测");
            record.setDetectionTime(request.getDetectionTime() != null ? parseTimeOrNow(request.getDetectionTime()) : LocalDateTime.now());
            record.setAiDescription(request.getDetectionResult());
            record.setIsViolence(true);
            record.setIsProcessed(false);
            record.setLevel(extractLevel(request.getDetectionResult()));

            // 声音异常专用字段
            if (request.getAudioUrl() != null) {
                record.setAudioUrl(request.getAudioUrl());
            }
            if (request.getAudioDuration() != null) {
                record.setAudioDuration(request.getAudioDuration());
            }
            if (request.getSoundKeywords() != null) {
                record.setSoundKeywords(request.getSoundKeywords());
            }

            detectionRecordRepository.save(record);
            log.info("声音异常已上报到安全记录: {}", request.getDetectionResult());
            return Result.success(true);
        } catch (Exception e) {
            log.error("声音异常上报失败", e);
            return Result.error(500, "上报失败: " + e.getMessage());
        }
    }
}
