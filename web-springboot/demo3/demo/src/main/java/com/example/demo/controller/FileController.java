package com.example.demo.controller;

import com.example.demo.dto.Result;
import org.springframework.core.io.FileSystemResource;
import org.springframework.core.io.Resource;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.util.StringUtils;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;
import org.springframework.web.servlet.HandlerMapping;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.Map;
import jakarta.servlet.http.HttpServletRequest;

@RestController
@RequestMapping("/file")
public class FileController {

    private static final Path UPLOAD_ROOT = Paths.get("./uploads").toAbsolutePath().normalize();
    private static final long MAX_MODEL_FILE_SIZE = 200L * 1024 * 1024;

    @PostMapping("/upload/{bucket}")
    public Result<Map<String, Object>> upload(@PathVariable("bucket") String bucket,
                                              @RequestParam("file") MultipartFile file,
                                              @RequestParam(value = "username", required = false) String username,
                                              @RequestParam(value = "is_cache", required = false) String isCache) throws IOException {
        if (file == null || file.isEmpty()) {
            return Result.error("文件不能为空");
        }
        if (!StringUtils.hasText(bucket)) {
            return Result.error("bucket不能为空");
        }
        if ("models".equalsIgnoreCase(bucket) && file.getSize() > MAX_MODEL_FILE_SIZE) {
            return Result.error(413, "模型文件不能超过200MB");
        }

        String originalFilename = file.getOriginalFilename();
        String ext = "";
        if (StringUtils.hasText(originalFilename) && originalFilename.contains(".")) {
            ext = originalFilename.substring(originalFilename.lastIndexOf('.'));
            if (ext.length() > 10) {
                ext = "";
            }
        }

        // 保持原始文件名，如果已存在则加序号区分
        // 路径格式: bucket/username/文件名
        String baseName = originalFilename;
        if (StringUtils.hasText(ext)) {
            baseName = originalFilename.substring(0, originalFilename.length() - ext.length());
        }

        // 如果没有传username，使用 "default"
        String folder = StringUtils.hasText(username) ? username : "default";
        // 清理文件夹名，只允许字母数字下划线
        folder = folder.replaceAll("[^a-zA-Z0-9_]", "_");

        String objectKey = folder + "/" + originalFilename;
        Path targetDir = UPLOAD_ROOT.resolve(bucket).resolve(folder).normalize();
        Path target = targetDir.resolve(originalFilename).normalize();

        // 如果文件已存在，加序号
        int counter = 1;
        while (Files.exists(target)) {
            String newFilename = baseName + "_" + counter + ext;
            objectKey = folder + "/" + newFilename;
            target = targetDir.resolve(newFilename).normalize();
            counter++;
        }

        Files.createDirectories(targetDir);

        if (!target.startsWith(UPLOAD_ROOT)) {
            return Result.error("非法路径");
        }

        file.transferTo(target.toFile());

        Map<String, Object> data = new HashMap<>();
        data.put("bucket", bucket);
        data.put("objectKey", objectKey.replace("\\", "/"));
        data.put("url", "/api/file/" + bucket + "/" + objectKey.replace("\\", "/"));
        data.put("isCache", StringUtils.hasText(isCache));
        return Result.success(data);
    }

    @GetMapping("/{bucket}/**")
    public ResponseEntity<Resource> get(@PathVariable("bucket") String bucket,
                                        HttpServletRequest request) throws IOException {
        String objectKey = extractObjectKey(bucket, request);
        if (!StringUtils.hasText(bucket) || !StringUtils.hasText(objectKey)) {
            return ResponseEntity.badRequest().build();
        }

        Path filePath = UPLOAD_ROOT.resolve(bucket).resolve(objectKey).normalize();
        if (!filePath.startsWith(UPLOAD_ROOT)) {
            return ResponseEntity.badRequest().build();
        }
        if (!Files.exists(filePath) || !Files.isRegularFile(filePath)) {
            return ResponseEntity.notFound().build();
        }

        String contentType = Files.probeContentType(filePath);
        if (!StringUtils.hasText(contentType)) {
            contentType = MediaType.APPLICATION_OCTET_STREAM_VALUE;
        }

        Resource resource = new FileSystemResource(filePath.toFile());
        return ResponseEntity.ok()
            .header(HttpHeaders.CACHE_CONTROL, "no-cache")
            .contentType(MediaType.parseMediaType(contentType))
            .body(resource);
    }

    @DeleteMapping("/{bucket}/**")
    public Result<Boolean> delete(@PathVariable("bucket") String bucket,
                                  HttpServletRequest request) throws IOException {
        String objectKey = extractObjectKey(bucket, request);
        if (!StringUtils.hasText(bucket) || !StringUtils.hasText(objectKey)) {
            return Result.error("参数不能为空");
        }

        Path filePath = UPLOAD_ROOT.resolve(bucket).resolve(objectKey).normalize();
        if (!filePath.startsWith(UPLOAD_ROOT)) {
            return Result.error("非法路径");
        }
        if (!Files.exists(filePath) || !Files.isRegularFile(filePath)) {
            return Result.error("文件不存在");
        }

        Files.delete(filePath);
        return Result.success(true);
    }

    private String extractObjectKey(String bucket, HttpServletRequest request) {
        Object pathWithinMappingAttr = request.getAttribute(HandlerMapping.PATH_WITHIN_HANDLER_MAPPING_ATTRIBUTE);
        if (!(pathWithinMappingAttr instanceof String pathWithinMapping)) {
            return null;
        }
        String prefix = "/file/" + bucket + "/";
        if (!pathWithinMapping.startsWith(prefix)) {
            return null;
        }
        String objectKey = pathWithinMapping.substring(prefix.length());
        return objectKey.replace("\\", "/");
    }
}

