package com.example.demo.controller;

import com.example.demo.dto.Result;
import com.example.demo.entity.SoundEvent;
import com.example.demo.service.SoundService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.core.io.Resource;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

@Slf4j
@RestController
@RequestMapping("/sound")
@RequiredArgsConstructor
public class SoundController {
    
    private final SoundService soundService;
    
    // 音频文件上传目录
    private static final String UPLOAD_DIR = "/home/orangepi/Desktop/web/Sound_Monitoring/uploads";
    
    /**
     * 开启声音监测
     */
    @PostMapping("/start")
    public Result<Map<String, Object>> startMonitoring() {
        try {
            boolean success = soundService.startMonitoring();
            if (success) {
                return Result.success(Map.of("message", "声音监测已启动", "enabled", true));
            } else {
                return Result.error(500, "启动声音监测失败");
            }
        } catch (Exception e) {
            log.error("启动声音监测失败", e);
            return Result.error(500, "启动失败: " + e.getMessage());
        }
    }
    
    /**
     * 停止声音监测
     */
    @PostMapping("/stop")
    public Result<Map<String, Object>> stopMonitoring() {
        try {
            boolean success = soundService.stopMonitoring();
            if (success) {
                return Result.success(Map.of("message", "声音监测已停止", "enabled", false));
            } else {
                return Result.error(500, "停止声音监测失败");
            }
        } catch (Exception e) {
            log.error("停止声音监测失败", e);
            return Result.error(500, "停止失败: " + e.getMessage());
        }
    }
    
    /**
     * 获取监测状态
     */
    @GetMapping("/status")
    public Result<Map<String, Object>> getStatus() {
        try {
            return Result.success(soundService.getMonitoringStatus());
        } catch (Exception e) {
            log.error("获取状态失败", e);
            return Result.error(500, "获取状态失败: " + e.getMessage());
        }
    }
    
    /**
     * 获取最新检测事件
     */
    @GetMapping("/latest")
    public Result<SoundEvent> getLatestEvent() {
        try {
            SoundEvent event = soundService.getLatestEvent();
            if (event != null) {
                return Result.success(event);
            } else {
                return Result.error(404, "暂无检测事件");
            }
        } catch (Exception e) {
            log.error("获取最新事件失败", e);
            return Result.error(500, "获取失败: " + e.getMessage());
        }
    }
    
    /**
     * 获取历史事件（分页）
     */
    @GetMapping("/history")
    public Result<Page<SoundEvent>> getHistory(
            @RequestParam(defaultValue = "0") int page,
            @RequestParam(defaultValue = "20") int size) {
        try {
            Pageable pageable = PageRequest.of(page, size, Sort.by(Sort.Direction.DESC, "createTime"));
            Page<SoundEvent> history = soundService.getEventHistory(pageable);
            return Result.success(history);
        } catch (Exception e) {
            log.error("获取历史事件失败", e);
            return Result.error(500, "获取失败: " + e.getMessage());
        }
    }

    /**
     * 获取实时监测状态
     */
    @GetMapping("/realtime/status")
    public Result<Map<String, Object>> getRealtimeStatus() {
        try {
            return Result.success(soundService.getRealtimeStatus());
        } catch (Exception e) {
            log.error("获取实时状态失败", e);
            return Result.error(500, "获取失败: " + e.getMessage());
        }
    }

    /**
     * 获取实时异常事件
     */
    @GetMapping("/realtime/events")
    public Result<Map<String, Object>> getRealtimeEvents() {
        try {
            return Result.success(soundService.getRealtimeEvents());
        } catch (Exception e) {
            log.error("获取实时事件失败", e);
            return Result.error(500, "获取失败: " + e.getMessage());
        }
    }

    /**
     * 获取最近实时窗口状态
     */
    @GetMapping("/realtime/windows")
    public Result<Map<String, Object>> getRealtimeWindows(@RequestParam(defaultValue = "5") int limit) {
        try {
            return Result.success(soundService.getRealtimeWindows(limit));
        } catch (Exception e) {
            log.error("获取实时窗口状态失败", e);
            return Result.error(500, "获取失败: " + e.getMessage());
        }
    }

    /**
     * 开启实时监测
     */
    @PostMapping("/realtime/start")
    public Result<Map<String, Object>> startRealtime() {
        try {
            boolean success = soundService.startMonitoring();
            if (success) {
                return Result.success(Map.of("message", "实时监测已启动", "running", true));
            } else {
                return Result.error(500, "启动实时监测失败");
            }
        } catch (Exception e) {
            log.error("启动实时监测失败", e);
            return Result.error(500, "启动失败: " + e.getMessage());
        }
    }

    /**
     * 停止实时监测
     */
    @PostMapping("/realtime/stop")
    public Result<Map<String, Object>> stopRealtime() {
        try {
            boolean success = soundService.stopMonitoring();
            if (success) {
                return Result.success(Map.of("message", "实时监测已停止", "running", false));
            } else {
                return Result.error(500, "停止实时监测失败");
            }
        } catch (Exception e) {
            log.error("停止实时监测失败", e);
            return Result.error(500, "停止失败: " + e.getMessage());
        }
    }
    
    /**
     * 上传音频文件并检测
     */
    @PostMapping("/detect")
    public Result<SoundEvent> detectAudio(@RequestParam("file") MultipartFile file) {
        try {
            // 创建上传目录
            File uploadDir = new File(UPLOAD_DIR);
            if (!uploadDir.exists()) {
                uploadDir.mkdirs();
            }

            // 保存文件
            String filename = System.currentTimeMillis() + "_" + file.getOriginalFilename();
            Path filePath = Path.of(UPLOAD_DIR, filename);
            Files.copy(file.getInputStream(), filePath, StandardCopyOption.REPLACE_EXISTING);

            // 检测
            SoundEvent result = soundService.detectFromFile(filePath.toString());
            if (result != null) {
                result.setAudioPath(filePath.toString());
                return Result.success(result);
            } else {
                return Result.error(500, "检测失败");
            }

        } catch (IOException e) {
            log.error("上传或检测失败", e);
            return Result.error(500, "处理失败: " + e.getMessage());
        }
    }

    /**
     * 上传音频文件并检测（前端专用接口）
     * 返回格式兼容前端 el-upload 组件
     */
    @PostMapping("/upload")
    public Result<Map<String, Object>> uploadAudio(@RequestParam("file") MultipartFile file) {
        try {
            // 验证文件类型
            String filename = file.getOriginalFilename();
            if (filename == null || !filename.matches(".*\\.(wav|mp3|m4a|ogg|flac)$")) {
                return Result.error(400, "不支持的文件格式，请上传 WAV、MP3、M4A、OGG 或 FLAC 格式");
            }

            // 验证文件大小（50MB）
            if (file.getSize() > 50 * 1024 * 1024) {
                return Result.error(400, "文件大小不能超过 50MB");
            }

            // 创建上传目录
            File uploadDir = new File(UPLOAD_DIR);
            if (!uploadDir.exists()) {
                uploadDir.mkdirs();
            }

            // 保存文件
            String savedFilename = System.currentTimeMillis() + "_" + filename;
            Path filePath = Path.of(UPLOAD_DIR, savedFilename);
            Files.copy(file.getInputStream(), filePath, StandardCopyOption.REPLACE_EXISTING);

            log.info("音频文件上传成功: {}", filePath);

            // 调用检测服务
            log.info("开始调用检测服务: {}", filePath.toString());
            SoundEvent result = soundService.detectFromFile(filePath.toString());
            log.info("检测服务返回结果: {}", result);

            // 返回结果
            if (result != null) {
                Map<String, Object> successData = new HashMap<>();
                successData.put("audioPath", filePath.toString());
                successData.put("anomalyCount", 1);
                successData.put("result", result);
                return Result.success(successData);
            } else {
                // 检测服务不可用时返回成功但无检测结果
                Map<String, Object> noResultData = new HashMap<>();
                noResultData.put("audioPath", filePath.toString());
                noResultData.put("anomalyCount", 0);
                noResultData.put("result", null);
                noResultData.put("message", "文件已上传，检测服务暂不可用");
                return Result.success(noResultData);
            }

        } catch (IOException e) {
            log.error("上传音频文件失败", e);
            e.printStackTrace();
            return Result.error(500, "上传失败: " + e.getClass().getName() + ": " + e.getMessage());
        } catch (Exception e) {
            log.error("处理音频文件时发生异常", e);
            e.printStackTrace();
            return Result.error(500, "处理失败: " + e.getClass().getName() + ": " + e.getMessage());
        }
    }
    
    /**
     * 录制并检测 - 录制指定时长的音频
     * @param duration 录制时长（秒）
     */
    @PostMapping("/record")
    public Result<Map<String, Object>> recordAndDetect(@RequestParam(defaultValue = "5") int duration) {
        try {
            // 录制音频
            String outputFile = UPLOAD_DIR + "/record_" + System.currentTimeMillis() + ".wav";
            File uploadDir = new File(UPLOAD_DIR);
            if (!uploadDir.exists()) {
                uploadDir.mkdirs();
            }
            
            ProcessBuilder pb = new ProcessBuilder(
                "arecord",
                "-D", "hw:4,0",
                "-f", "S16_LE",
                "-r", "16000",
                "-c", "1",
                "-d", String.valueOf(duration),
                outputFile
            );
            Process process = pb.start();
            int exitCode = process.waitFor();
            
            if (exitCode == 0) {
                // 检测
                SoundEvent result = soundService.detectFromFile(outputFile);
                return Result.success(Map.of(
                    "message", "录制并检测完成",
                    "audioPath", outputFile,
                    "result", result != null ? result : "no anomaly"
                ));
            } else {
                return Result.error(500, "录制失败");
            }
            
        } catch (Exception e) {
            log.error("录制并检测失败", e);
            return Result.error(500, "处理失败: " + e.getMessage());
        }
    }

    /**
     * 获取音频文件
     * 用于播放安全记录中的异常音频
     */
    @GetMapping("/audio")
    public ResponseEntity<Resource> getAudio(@RequestParam String path) {
        try {
            File file = resolveAudioFile(path);
            if (!file.exists()) {
                return ResponseEntity.notFound().build();
            }
            
            String contentType = "audio/mpeg";
            if (path.endsWith(".wav")) {
                contentType = "audio/wav";
            } else if (path.endsWith(".ogg")) {
                contentType = "audio/ogg";
            } else if (path.endsWith(".m4a")) {
                contentType = "audio/mp4";
            }
            
            return ResponseEntity.ok()
                .contentType(MediaType.parseMediaType(contentType))
                .header(HttpHeaders.CONTENT_DISPOSITION, "inline; filename=\"" + file.getName() + "\"")
                .body(new org.springframework.core.io.FileSystemResource(file));
        } catch (Exception e) {
            log.error("获取音频文件失败", e);
            return ResponseEntity.internalServerError().build();
        }
    }

    private File resolveAudioFile(String rawPath) {
        if (rawPath == null || rawPath.isBlank()) {
            return new File("");
        }

        Path requested = Paths.get(rawPath.trim()).normalize();
        if (requested.isAbsolute()) {
            return requested.toFile();
        }

        String normalized = rawPath.trim().replace("\\", "/");
        if (normalized.startsWith("./")) {
            normalized = normalized.substring(2);
        }

        Path userDir = Paths.get(System.getProperty("user.dir", ".")).toAbsolutePath().normalize();
        List<Path> searchRoots = new ArrayList<>();
        Path cursor = userDir;
        for (int i = 0; i < 8 && cursor != null; i++) {
            searchRoots.add(cursor);
            searchRoots.add(cursor.resolve("Sound_Monitoring").resolve("src").resolve("build"));
            searchRoots.add(cursor.resolve("Sound_Monitoring").resolve("build"));
            cursor = cursor.getParent();
        }

        Set<Path> candidates = new LinkedHashSet<>();
        for (Path root : searchRoots) {
            candidates.add(root.resolve(normalized).normalize());
        }

        for (Path candidate : candidates) {
            if (Files.exists(candidate) && Files.isRegularFile(candidate)) {
                return candidate.toFile();
            }
        }

        return requested.toFile();
    }
}
