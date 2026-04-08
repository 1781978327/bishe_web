package com.example.demo.dto;

import lombok.Data;

@Data
public class UpdateUserRequest {
    private Long id;
    private String realName;
    private String phone;
    private String email;

    private String avatarBucket;
    private String avatarObjectKey;
}

