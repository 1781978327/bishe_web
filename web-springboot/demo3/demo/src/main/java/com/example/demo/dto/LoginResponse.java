package com.example.demo.dto;

import com.example.demo.entity.User;
import lombok.Data;

@Data
public class LoginResponse {
    private User userInfo;
    private String token;
}
