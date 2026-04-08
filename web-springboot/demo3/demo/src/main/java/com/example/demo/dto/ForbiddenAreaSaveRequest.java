package com.example.demo.dto;

import lombok.Data;

import java.util.List;

@Data
public class ForbiddenAreaSaveRequest {
    private Long cameraId;
    private Integer imageWidth;
    private Integer imageHeight;
    private List<ForbiddenAreaPoint> points;
}

