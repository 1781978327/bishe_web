package com.example.demo.dto;

import jakarta.validation.constraints.NotNull;
import lombok.Data;

@Data
public class DetectionRecordAddRequest {
    @NotNull(message = "cameraId不能为空")
    private Long cameraId;

    private String imageBase64;

    private String detectionTime;

    @NotNull(message = "detectionResult不能为空")
    private String detectionResult;

    // 声音异常专用字段
    private String audioUrl;
    private Float audioDuration;
    private String soundKeywords;
}

