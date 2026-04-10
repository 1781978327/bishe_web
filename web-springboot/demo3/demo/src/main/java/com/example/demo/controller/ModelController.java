package com.example.demo.controller;

import com.example.demo.dto.Result;
import com.example.demo.service.ModelProfileService;
import lombok.RequiredArgsConstructor;
import org.springframework.security.core.Authentication;
import org.springframework.security.core.userdetails.UserDetails;
import org.springframework.util.StringUtils;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/model")
@RequiredArgsConstructor
public class ModelController {

    private final ModelProfileService modelProfileService;

    @GetMapping("/list")
    public Result<List<Map<String, Object>>> list(Authentication authentication) {
        try {
            String username = resolveUsername(authentication);
            return Result.success(modelProfileService.listByUsername(username));
        } catch (Exception e) {
            return Result.error(500, "获取模型列表失败: " + e.getMessage());
        }
    }

    @GetMapping("/current")
    public Result<Map<String, Object>> current(Authentication authentication) {
        try {
            String username = resolveUsername(authentication);
            return Result.success(modelProfileService.getCurrentSelected(username));
        } catch (Exception e) {
            return Result.error(500, "获取当前模型失败: " + e.getMessage());
        }
    }

    @PostMapping("/select")
    public Result<Map<String, Object>> select(@RequestParam Long id, Authentication authentication) {
        try {
            String username = resolveUsername(authentication);
            return Result.success(modelProfileService.selectAndApply(username, id));
        } catch (IllegalArgumentException e) {
            return Result.error(400, e.getMessage());
        } catch (IllegalStateException e) {
            return Result.error(409, e.getMessage());
        } catch (Exception e) {
            return Result.error(500, "切换模型失败: " + e.getMessage());
        }
    }

    private String resolveUsername(Authentication authentication) {
        if (authentication == null || !authentication.isAuthenticated()) {
            throw new IllegalStateException("未登录");
        }
        Object principal = authentication.getPrincipal();
        if (principal instanceof UserDetails userDetails) {
            return userDetails.getUsername();
        }
        if (principal instanceof String str && StringUtils.hasText(str) && !"anonymousUser".equals(str)) {
            return str;
        }
        throw new IllegalStateException("无法识别当前用户");
    }
}
