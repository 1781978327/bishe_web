package com.example.demo.dto;

import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;
import lombok.Data;

@Data
public class CameraCreateRequest {
    
    @NotBlank(message = "设备名称不能为空")
    @Size(max = 100, message = "设备名称长度不能超过100个字符")
    private String name;
    
    @Size(max = 500, message = "RTSP地址长度不能超过500个字符")
    private String rtspUrl;
    
    @Size(max = 200, message = "设备位置长度不能超过200个字符")
    private String location;
    
    private Integer status = 0; // 默认离线
    
    private Boolean isEnabled = true; // 默认启用
    
    private Boolean detectionEnabled = true; // 默认开启检测
}
