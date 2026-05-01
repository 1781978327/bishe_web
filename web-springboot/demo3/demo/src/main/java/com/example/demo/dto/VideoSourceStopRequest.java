package com.example.demo.dto;

import lombok.Data;

@Data
public class VideoSourceStopRequest {
    private Boolean stopRtsp;
    private Boolean restartCameraRtsp;
}
