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
    /**
     * 上报来源：monitoring 表示实时/KWS 自动监测，manual 表示手动上传/录制分析。
     * 旧版 C++ 声音服务不会传该字段，后端按 monitoring 处理。
     */
    private String source;
}
