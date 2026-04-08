package com.example.demo.controller;

import com.example.demo.dto.Result;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.time.LocalDateTime;
import java.util.HashMap;
import java.util.Map;

@RestController
@RequestMapping("/test")
public class TestController {
    
    @GetMapping("/health")
    public Result<Map<String, Object>> health() {
        Map<String, Object> data = new HashMap<>();
        data.put("status", "ok");
        data.put("message", "嵌入式多目标追踪与智能预警系统的设计与实现后端服务正常运行");
        data.put("timestamp", LocalDateTime.now());
        data.put("version", "1.0.0");
        return Result.success(data);
    }
}
