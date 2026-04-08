package com.example.demo.controller;

import com.example.demo.dto.LoginRequest;
import com.example.demo.dto.LoginResponse;
import com.example.demo.dto.RegisterRequest;
import com.example.demo.dto.Result;
import com.example.demo.dto.UpdateUserRequest;
import com.example.demo.entity.User;
import com.example.demo.service.UserService;
import jakarta.validation.Valid;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/user")
@RequiredArgsConstructor
public class UserController {

    private final UserService userService;

    @PostMapping("/login")
    public Result<LoginResponse> login(@Valid @RequestBody LoginRequest request) {
        try {
            return Result.success(userService.login(request));
        } catch (Exception e) {
            return Result.error(400, e.getMessage());
        }
    }

    @PostMapping("/register")
    public Result<Void> register(@Valid @RequestBody RegisterRequest request) {
        try {
            userService.register(request);
            return Result.success();
        } catch (Exception e) {
            return Result.error(400, e.getMessage());
        }
    }

    @PostMapping("/logout")
    public Result<Void> logout() {
        // JWT 无状态：前端清 token 即可，这里仅用于避免 404
        return Result.success();
    }

    @PutMapping
    public Result<User> update(@RequestBody UpdateUserRequest request) {
        try {
            return Result.success(userService.updateUser(request));
        } catch (Exception e) {
            return Result.error(400, e.getMessage());
        }
    }
}

