package com.example.demo.dto;

import lombok.Data;

@Data
public class VideoSourceStartRequest {
    private String sourcePath;
    private String path;
    private Boolean loop;
    private Boolean startRtsp;
    private Boolean enableInference;
    private Boolean track;
    private String tracker;
}
