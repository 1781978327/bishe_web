package com.example.demo.entity;

import jakarta.persistence.*;
import lombok.Data;
import java.time.LocalDateTime;

@Data
@Entity
@Table(name = "sound_event")
public class SoundEvent {
    
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    
    // 声音类型: alarm, glass_break, gunshot, scream, siren, other
    @Column(nullable = false)
    private String soundType;
    
    // 置信度
    @Column(nullable = false)
    private Float confidence;
    
    // 持续时间(秒)
    private Float duration;
    
    // 开始时间
    @Column(nullable = false)
    private LocalDateTime startTime;
    
    // 结束时间
    private LocalDateTime endTime;
    
    // 关键词列表 (JSON格式存储)
    @Column(length = 500)
    private String keywords;
    
    // 原始音频文件路径
    @Column(length = 500)
    private String audioPath;
    
    // 创建时间
    private LocalDateTime createTime;
    
    @PrePersist
    protected void onCreate() {
        createTime = LocalDateTime.now();
    }
}
