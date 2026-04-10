package com.example.demo.service;

import com.example.demo.entity.RknnModelProfile;
import com.example.demo.repository.RknnModelProfileRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.util.StringUtils;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

@Slf4j
@Service
@RequiredArgsConstructor
public class ModelProfileService {

    private static final Path UPLOAD_ROOT = Paths.get("./uploads").toAbsolutePath().normalize();
    private static final String MODEL_BUCKET = "models";

    private final RknnModelProfileRepository modelProfileRepository;
    private final RknnService rknnService;

    @Transactional
    public void registerUpload(String username, String originalFilename, String objectKey) {
        if (!StringUtils.hasText(originalFilename) || !StringUtils.hasText(objectKey)) {
            return;
        }

        String safeUsername = normalizeUsername(username);
        String ext = getExtension(originalFilename);
        if (!".rknn".equals(ext) && !".txt".equals(ext) && !".yaml".equals(ext)) {
            return;
        }

        String baseName = getBaseName(originalFilename);
        if (!StringUtils.hasText(baseName)) {
            return;
        }

        String normalizedKey = objectKey.replace("\\", "/");
        RknnModelProfile profile = modelProfileRepository.findByUsernameAndBaseName(safeUsername, baseName)
                .orElseGet(() -> {
                    RknnModelProfile p = new RknnModelProfile();
                    p.setUsername(safeUsername);
                    p.setBaseName(baseName);
                    p.setSelected(false);
                    return p;
                });

        if (".rknn".equals(ext)) {
            profile.setModelObjectKey(normalizedKey);
        } else if (".txt".equals(ext)) {
            profile.setLabelObjectKey(normalizedKey);
        } else {
            profile.setYamlObjectKey(normalizedKey);
        }

        modelProfileRepository.save(profile);
        log.info("模型资源关联已更新: user={}, base={}, ext={}, key={}", safeUsername, baseName, ext, normalizedKey);
    }

    @Transactional(readOnly = true)
    public List<Map<String, Object>> listByUsername(String username) {
        String safeUsername = normalizeUsername(username);
        List<RknnModelProfile> profiles = modelProfileRepository.findByUsernameOrderByUpdateTimeDesc(safeUsername);
        List<Map<String, Object>> result = new ArrayList<>(profiles.size());
        for (RknnModelProfile profile : profiles) {
            result.add(toView(profile));
        }
        return result;
    }

    @Transactional(readOnly = true)
    public Map<String, Object> getCurrentSelected(String username) {
        String safeUsername = normalizeUsername(username);
        Optional<RknnModelProfile> selected = modelProfileRepository
                .findFirstByUsernameAndSelectedTrueOrderByUpdateTimeDesc(safeUsername);
        return selected.map(this::toView).orElse(null);
    }

    @Transactional
    public Map<String, Object> selectAndApply(String username, Long id) {
        if (id == null) {
            throw new IllegalArgumentException("模型ID不能为空");
        }
        String safeUsername = normalizeUsername(username);
        RknnModelProfile profile = modelProfileRepository.findByIdAndUsername(id, safeUsername)
                .orElseThrow(() -> new IllegalArgumentException("模型不存在或无权限访问"));

        if (!StringUtils.hasText(profile.getModelObjectKey())) {
            throw new IllegalStateException("该模型缺少 .rknn 文件，请重新上传");
        }
        if (!StringUtils.hasText(profile.getLabelObjectKey())) {
            throw new IllegalStateException("该模型缺少 .txt 标签文件，请重新上传");
        }

        String modelPath = resolveLocalPath(profile.getModelObjectKey());
        String labelPath = resolveLocalPath(profile.getLabelObjectKey());

        if (!Files.isReadable(Paths.get(modelPath))) {
            throw new IllegalStateException("模型文件不存在或不可读: " + modelPath);
        }
        if (!Files.isReadable(Paths.get(labelPath))) {
            throw new IllegalStateException("标签文件不存在或不可读: " + labelPath);
        }

        Map<String, Object> applyResult = rknnService.applyModelAndLabel(modelPath, labelPath);
        if (isDownstreamError(applyResult)) {
            throw new IllegalStateException(extractErrorMessage(applyResult, "下发模型到视觉服务失败"));
        }

        List<RknnModelProfile> all = modelProfileRepository.findByUsernameOrderByUpdateTimeDesc(safeUsername);
        for (RknnModelProfile item : all) {
            boolean isCurrent = item.getId().equals(profile.getId());
            item.setSelected(isCurrent);
            if (isCurrent) {
                item.setLastSelectedTime(LocalDateTime.now());
            }
        }
        modelProfileRepository.saveAll(all);

        Map<String, Object> response = new LinkedHashMap<>();
        response.put("selected", toView(profile));
        response.put("applyResult", applyResult);
        return response;
    }

    private Map<String, Object> toView(RknnModelProfile profile) {
        Map<String, Object> view = new LinkedHashMap<>();
        view.put("id", profile.getId());
        view.put("username", profile.getUsername());
        view.put("baseName", profile.getBaseName());
        view.put("modelObjectKey", profile.getModelObjectKey());
        view.put("labelObjectKey", profile.getLabelObjectKey());
        view.put("yamlObjectKey", profile.getYamlObjectKey());
        view.put("modelUrl", buildFileUrl(profile.getModelObjectKey()));
        view.put("labelUrl", buildFileUrl(profile.getLabelObjectKey()));
        view.put("yamlUrl", buildFileUrl(profile.getYamlObjectKey()));
        view.put("selected", Boolean.TRUE.equals(profile.getSelected()));
        view.put("ready", StringUtils.hasText(profile.getModelObjectKey()) && StringUtils.hasText(profile.getLabelObjectKey()));
        view.put("createTime", profile.getCreateTime());
        view.put("updateTime", profile.getUpdateTime());
        view.put("lastSelectedTime", profile.getLastSelectedTime());
        return view;
    }

    private String buildFileUrl(String objectKey) {
        if (!StringUtils.hasText(objectKey)) {
            return null;
        }
        return "/api/file/" + MODEL_BUCKET + "/" + objectKey.replace("\\", "/");
    }

    private String resolveLocalPath(String objectKey) {
        Path resolved = UPLOAD_ROOT.resolve(MODEL_BUCKET).resolve(objectKey).normalize();
        if (!resolved.startsWith(UPLOAD_ROOT)) {
            throw new IllegalArgumentException("非法模型路径");
        }
        return resolved.toString();
    }

    private String normalizeUsername(String username) {
        if (!StringUtils.hasText(username)) {
            return "default";
        }
        return username.trim().replaceAll("[^a-zA-Z0-9_]", "_");
    }

    private String getBaseName(String filename) {
        int idx = filename.lastIndexOf('.');
        if (idx <= 0) {
            return filename;
        }
        return filename.substring(0, idx);
    }

    private String getExtension(String filename) {
        int idx = filename.lastIndexOf('.');
        if (idx < 0 || idx == filename.length() - 1) {
            return "";
        }
        return filename.substring(idx).toLowerCase();
    }

    private boolean isDownstreamError(Map<String, Object> result) {
        if (result == null) {
            return true;
        }
        Object success = result.get("success");
        return Boolean.FALSE.equals(success);
    }

    private String extractErrorMessage(Map<String, Object> result, String fallback) {
        if (result == null) {
            return fallback;
        }
        Object error = result.get("error");
        if (error != null && StringUtils.hasText(error.toString())) {
            return error.toString();
        }
        Object message = result.get("message");
        if (message != null && StringUtils.hasText(message.toString())) {
            return message.toString();
        }
        return fallback;
    }
}
