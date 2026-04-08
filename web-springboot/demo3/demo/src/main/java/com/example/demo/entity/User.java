package com.example.demo.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.springframework.data.annotation.CreatedDate;
import org.springframework.data.annotation.LastModifiedDate;
import org.springframework.data.jpa.domain.support.AuditingEntityListener;

import java.time.LocalDateTime;

@Data
@EqualsAndHashCode(callSuper = false)
@Entity
@Table(name = "users")
@EntityListeners(AuditingEntityListener.class)
public class User {
    
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    
    @Column(unique = true, nullable = false, length = 50)
    private String username;
    
    @Column(nullable = false)
    private String password;
    
    @Column(name = "real_name", nullable = false, length = 50)
    private String realName;
    
    @Column(length = 20)
    private String phone;
    
    @Column(length = 100)
    private String email;
    
    @Column(nullable = false)
    private Integer role = 0; // 0: 普通用户, 1: 管理员
    
    @Column(nullable = false)
    private Integer status = 1; // 0: 禁用, 1: 启用
    
    @Column(name = "avatar_bucket")
    private String avatarBucket;
    
    @Column(name = "avatar_object_key")
    private String avatarObjectKey;
    
    @Column(name = "avatar_url")
    private String avatarUrl;
    
    @CreatedDate
    @Column(name = "create_time")
    private LocalDateTime createTime;
    
    @LastModifiedDate
    @Column(name = "update_time")
    private LocalDateTime updateTime;
}
