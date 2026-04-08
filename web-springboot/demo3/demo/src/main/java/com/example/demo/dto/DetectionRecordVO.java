package com.example.demo.dto;

import lombok.Data;

@Data
public class DetectionRecordVO {
    private Long id;
    private Long cameraId;
    private String cameraName;
    private String imageUrl;
    private String detectionTime;
    private String detectionResult;
    private Integer processed;
    private String processContent;
    private String processImageUrl;
    private String processTime;
    private String createTime;
    private String aiDescription;
    private String processNotes;
    // 声音异常专用字段
    private String audioUrl;
    private Float audioDuration;
    private String soundKeywords;
}

