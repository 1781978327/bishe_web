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
    private static final String YOLO_PROJECT_DIR = "yolov8-rk3588-cpp-3-15";
    private static final String BUILTIN_SOURCE = "builtin";
    private static final String UPLOAD_SOURCE = "upload";
    private static final List<BuiltinModelDefinition> BUILTIN_MODEL_DEFINITIONS = List.of(
            new BuiltinModelDefinition(-3L, "builtin:person_2700_i8", "person_2700_i8", "model/RK3588/person_2700_i8.rknn", "model/person_2700_i8.txt"),
            new BuiltinModelDefinition(-1L, "builtin:yolov8s", "yolov8s", "model/RK3588/yolov8s.rknn", "model/coco_80_labels_list.txt"),
            new BuiltinModelDefinition(-2L, "builtin:yolov8n", "yolov8n", "model/RK3588/yolov8n.rknn", "model/coco_80_labels_list.txt")
    );

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
        RuntimeSelection runtimeSelection = resolveRuntimeSelection();

        List<Map<String, Object>> result = new ArrayList<>();
        for (BuiltinModel builtinModel : resolveBuiltinModels()) {
            result.add(toBuiltinView(safeUsername, builtinModel, runtimeSelection, null));
        }

        List<RknnModelProfile> profiles = modelProfileRepository.findByUsernameOrderByUpdateTimeDesc(safeUsername);
        for (RknnModelProfile profile : profiles) {
            result.add(toUploadedView(profile, runtimeSelection, null));
        }
        return result;
    }

    @Transactional(readOnly = true)
    public Map<String, Object> getCurrentSelected(String username) {
        String safeUsername = normalizeUsername(username);
        RuntimeSelection runtimeSelection = resolveRuntimeSelection();

        if (runtimeSelection.available()) {
            for (BuiltinModel builtinModel : resolveBuiltinModels()) {
                if (matchesRuntimeSelection(runtimeSelection, builtinModel.modelPath(), builtinModel.labelPath())) {
                    return toBuiltinView(safeUsername, builtinModel, runtimeSelection, true);
                }
            }

            List<RknnModelProfile> profiles = modelProfileRepository.findByUsernameOrderByUpdateTimeDesc(safeUsername);
            for (RknnModelProfile profile : profiles) {
                String modelPath = resolveLocalPathOrNull(profile.getModelObjectKey());
                String labelPath = resolveLocalPathOrNull(profile.getLabelObjectKey());
                if (matchesRuntimeSelection(runtimeSelection, modelPath, labelPath)) {
                    return toUploadedView(profile, runtimeSelection, true);
                }
            }
        }

        Optional<RknnModelProfile> selected = modelProfileRepository
                .findFirstByUsernameAndSelectedTrueOrderByUpdateTimeDesc(safeUsername);
        return selected.map(profile -> toUploadedView(profile, runtimeSelection, true)).orElse(null);
    }

    @Transactional
    public Map<String, Object> selectAndApply(String username, Long id) {
        if (id == null) {
            throw new IllegalArgumentException("模型ID不能为空");
        }
        String safeUsername = normalizeUsername(username);

        BuiltinModel builtinModel = findBuiltinModelById(id);
        if (builtinModel != null) {
            return selectBuiltinAndApply(safeUsername, builtinModel);
        }

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

        ensureReadableFile(modelPath, "模型文件不存在或不可读: ");
        ensureReadableFile(labelPath, "标签文件不存在或不可读: ");

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
        response.put("selected", toUploadedView(profile, resolveRuntimeSelection(), true));
        response.put("applyResult", applyResult);
        return response;
    }

    private Map<String, Object> selectBuiltinAndApply(String username, BuiltinModel builtinModel) {
        ensureReadableFile(builtinModel.modelPath(), "内置模型不存在或不可读: ");
        ensureReadableFile(builtinModel.labelPath(), "内置标签文件不存在或不可读: ");

        Map<String, Object> applyResult = rknnService.applyModelAndLabel(builtinModel.modelPath(), builtinModel.labelPath());
        if (isDownstreamError(applyResult)) {
            throw new IllegalStateException(extractErrorMessage(applyResult, "下发内置模型到视觉服务失败"));
        }

        clearSelectedUploads(username);

        Map<String, Object> response = new LinkedHashMap<>();
        response.put("selected", toBuiltinView(username, builtinModel, resolveRuntimeSelection(), true));
        response.put("applyResult", applyResult);
        return response;
    }

    private void clearSelectedUploads(String username) {
        List<RknnModelProfile> all = modelProfileRepository.findByUsernameOrderByUpdateTimeDesc(username);
        boolean changed = false;
        for (RknnModelProfile item : all) {
            if (Boolean.TRUE.equals(item.getSelected())) {
                item.setSelected(false);
                changed = true;
            }
        }
        if (changed) {
            modelProfileRepository.saveAll(all);
        }
    }

    private Map<String, Object> toBuiltinView(String username,
                                              BuiltinModel builtinModel,
                                              RuntimeSelection runtimeSelection,
                                              Boolean selectedOverride) {
        boolean selected = selectedOverride != null
                ? selectedOverride
                : matchesRuntimeSelection(runtimeSelection, builtinModel.modelPath(), builtinModel.labelPath());

        Map<String, Object> view = new LinkedHashMap<>();
        view.put("id", builtinModel.id());
        view.put("username", username);
        view.put("baseName", builtinModel.baseName());
        view.put("builtin", true);
        view.put("source", BUILTIN_SOURCE);
        view.put("builtinKey", builtinModel.key());
        view.put("modelObjectKey", builtinModel.modelPath());
        view.put("labelObjectKey", builtinModel.labelPath());
        view.put("yamlObjectKey", null);
        view.put("modelUrl", null);
        view.put("labelUrl", null);
        view.put("yamlUrl", null);
        view.put("modelPath", builtinModel.modelPath());
        view.put("labelPath", builtinModel.labelPath());
        view.put("selected", selected);
        view.put("ready", builtinModel.ready());
        view.put("createTime", null);
        view.put("updateTime", null);
        view.put("lastSelectedTime", null);
        return view;
    }

    private Map<String, Object> toUploadedView(RknnModelProfile profile,
                                               RuntimeSelection runtimeSelection,
                                               Boolean selectedOverride) {
        String modelPath = resolveLocalPathOrNull(profile.getModelObjectKey());
        String labelPath = resolveLocalPathOrNull(profile.getLabelObjectKey());
        boolean ready = isReadableFile(modelPath) && isReadableFile(labelPath);
        boolean selected = selectedOverride != null
                ? selectedOverride
                : matchesRuntimeSelection(runtimeSelection, modelPath, labelPath) ||
                (!runtimeSelection.available() && Boolean.TRUE.equals(profile.getSelected()));

        Map<String, Object> view = new LinkedHashMap<>();
        view.put("id", profile.getId());
        view.put("username", profile.getUsername());
        view.put("baseName", profile.getBaseName());
        view.put("builtin", false);
        view.put("source", UPLOAD_SOURCE);
        view.put("modelObjectKey", profile.getModelObjectKey());
        view.put("labelObjectKey", profile.getLabelObjectKey());
        view.put("yamlObjectKey", profile.getYamlObjectKey());
        view.put("modelUrl", buildFileUrl(profile.getModelObjectKey()));
        view.put("labelUrl", buildFileUrl(profile.getLabelObjectKey()));
        view.put("yamlUrl", buildFileUrl(profile.getYamlObjectKey()));
        view.put("modelPath", modelPath);
        view.put("labelPath", labelPath);
        view.put("selected", selected);
        view.put("ready", ready);
        view.put("createTime", profile.getCreateTime());
        view.put("updateTime", profile.getUpdateTime());
        view.put("lastSelectedTime", profile.getLastSelectedTime());
        return view;
    }

    private BuiltinModel findBuiltinModelById(Long id) {
        for (BuiltinModel builtinModel : resolveBuiltinModels()) {
            if (builtinModel.id() == id) {
                return builtinModel;
            }
        }
        return null;
    }

    private List<BuiltinModel> resolveBuiltinModels() {
        Path repoRoot = detectRepoRoot();
        if (repoRoot == null) {
            log.warn("未找到项目根目录，内置模型列表不可用");
            return List.of();
        }

        Path yoloRoot = repoRoot.resolve(YOLO_PROJECT_DIR).normalize();
        List<BuiltinModel> result = new ArrayList<>(BUILTIN_MODEL_DEFINITIONS.size());
        for (BuiltinModelDefinition definition : BUILTIN_MODEL_DEFINITIONS) {
            String modelPath = yoloRoot.resolve(definition.modelRelativePath()).normalize().toString();
            String labelPath = yoloRoot.resolve(definition.labelRelativePath()).normalize().toString();
            boolean ready = isReadableFile(modelPath) && isReadableFile(labelPath);
            result.add(new BuiltinModel(definition.id(), definition.key(), definition.baseName(), modelPath, labelPath, ready));
        }
        return result;
    }

    private Path detectRepoRoot() {
        List<Path> seeds = new ArrayList<>();
        seeds.add(Paths.get("").toAbsolutePath().normalize());
        try {
            Path codeSource = Paths.get(ModelProfileService.class.getProtectionDomain()
                    .getCodeSource().getLocation().toURI()).toAbsolutePath().normalize();
            seeds.add(codeSource);
        } catch (Exception ignore) {
            // 忽略 code source 解析失败，继续使用当前工作目录探测
        }

        for (Path seed : seeds) {
            Path start = Files.isRegularFile(seed) ? seed.getParent() : seed;
            for (Path probe = start; probe != null; probe = probe.getParent()) {
                if (Files.isDirectory(probe.resolve(YOLO_PROJECT_DIR)) &&
                        Files.isDirectory(probe.resolve("web-springboot"))) {
                    return probe;
                }
            }
        }
        return null;
    }

    private RuntimeSelection resolveRuntimeSelection() {
        Map<String, Object> status = rknnService.getStatus();
        String modelPath = normalizePath(asString(status.get("model_path")));
        String labelPath = normalizePath(asString(status.get("label_path")));
        boolean available = StringUtils.hasText(modelPath);
        return new RuntimeSelection(modelPath, labelPath, available);
    }

    private boolean matchesRuntimeSelection(RuntimeSelection runtimeSelection, String modelPath, String labelPath) {
        if (runtimeSelection == null || !runtimeSelection.available()) {
            return false;
        }
        String normalizedModel = normalizePath(modelPath);
        if (!StringUtils.hasText(normalizedModel) || !normalizedModel.equals(runtimeSelection.modelPath())) {
            return false;
        }

        String normalizedLabel = normalizePath(labelPath);
        if (!StringUtils.hasText(runtimeSelection.labelPath()) || !StringUtils.hasText(normalizedLabel)) {
            return true;
        }
        return normalizedLabel.equals(runtimeSelection.labelPath());
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

    private String resolveLocalPathOrNull(String objectKey) {
        if (!StringUtils.hasText(objectKey)) {
            return null;
        }
        try {
            return resolveLocalPath(objectKey);
        } catch (IllegalArgumentException e) {
            return null;
        }
    }

    private void ensureReadableFile(String filePath, String errorPrefix) {
        if (!isReadableFile(filePath)) {
            throw new IllegalStateException(errorPrefix + filePath);
        }
    }

    private boolean isReadableFile(String filePath) {
        if (!StringUtils.hasText(filePath)) {
            return false;
        }
        try {
            return Files.isReadable(Paths.get(filePath));
        } catch (Exception e) {
            return false;
        }
    }

    private String normalizePath(String filePath) {
        if (!StringUtils.hasText(filePath)) {
            return "";
        }
        try {
            return Paths.get(filePath).toAbsolutePath().normalize().toString();
        } catch (Exception e) {
            return filePath.trim();
        }
    }

    private String asString(Object value) {
        return value == null ? "" : value.toString().trim();
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

    private record BuiltinModelDefinition(long id,
                                          String key,
                                          String baseName,
                                          String modelRelativePath,
                                          String labelRelativePath) {
    }

    private record BuiltinModel(long id,
                                String key,
                                String baseName,
                                String modelPath,
                                String labelPath,
                                boolean ready) {
    }

    private record RuntimeSelection(String modelPath, String labelPath, boolean available) {
    }
}
