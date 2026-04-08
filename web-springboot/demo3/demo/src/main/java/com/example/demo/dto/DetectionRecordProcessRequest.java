package com.example.demo.dto;

import jakarta.validation.constraints.NotNull;
import lombok.Data;

@Data
public class DetectionRecordProcessRequest {
    @NotNull(message = "id不能为空")
    private Long id;

    @NotNull(message = "processed不能为空")
    private Integer processed;

    private String processContent;

    private String processImageBase64;
}

