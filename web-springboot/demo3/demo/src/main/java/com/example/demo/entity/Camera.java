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
@Table(name = "cameras")
@EntityListeners(AuditingEntityListener.class)
public class Camera {
    
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    
    @Column(nullable = false, length = 100)
    private String name;
    
    @Column(name = "rtsp_url", length = 500)
    private String rtspUrl;
    
    @Column(name = "location", length = 200)
    private String location;
    
    @Column(nullable = false)
    private Integer status = 1; // 0: 离线, 1: 在线
    
    @Column(name = "is_enabled", nullable = false)
    private Boolean isEnabled = true;
    
    @Column(name = "detection_enabled", nullable = false)
    private Boolean detectionEnabled = true;
    
    @Column(name = "resolution", length = 50)
    private String resolution;
    
    @Column(name = "frame_rate")
    private Integer frameRate;
    
    @Column(name = "last_online_time")
    private LocalDateTime lastOnlineTime;
    
    @Column(name = "description", columnDefinition = "TEXT")
    private String description;
    
    @CreatedDate
    @Column(name = "create_time")
    private LocalDateTime createTime;
    
    @LastModifiedDate
    @Column(name = "update_time")
    private LocalDateTime updateTime;
}
