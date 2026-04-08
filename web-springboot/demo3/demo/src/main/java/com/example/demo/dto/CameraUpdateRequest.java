package com.example.demo.dto;

import jakarta.validation.constraints.Size;
import lombok.Data;

@Data
public class CameraUpdateRequest {

    @Size(max = 100, message = "设备名称长度不能超过100个字符")
    private String name;

    @Size(max = 500, message = "RTSP地址长度不能超过500个字符")
    private String rtspUrl;

    @Size(max = 200, message = "设备位置长度不能超过200个字符")
    private String location;

    private Integer status;

    private Boolean isEnabled;

    private Boolean detectionEnabled;

    @Size(max = 50, message = "分辨率长度不能超过50个字符")
    private String resolution;

    private Integer frameRate;

    private String description;
}

