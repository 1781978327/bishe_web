package com.example.demo.dto;

import lombok.Data;

@Data
public class SoundAnomalyReportRequest {
    private Long cameraId;
    private String cameraName;
    private String detectionTime;
    private String detectionResult;
    private String audioUrl;
    private Float audioDuration;
    private String soundKeywords;
}
