package com.example.demo.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.springframework.data.annotation.CreatedDate;
import org.springframework.data.jpa.domain.support.AuditingEntityListener;

import java.time.LocalDateTime;

@Data
@EqualsAndHashCode(callSuper = false)
@Entity
@Table(name = "detection_records")
@EntityListeners(AuditingEntityListener.class)
public class DetectionRecord {
    
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    
    @Column(name = "camera_id", nullable = false)
    private Long cameraId;
    
    @Column(name = "camera_name", nullable = false, length = 100)
    private String cameraName;
    
    @Column(name = "detection_time", nullable = false)
    private LocalDateTime detectionTime;
    
    @Column(name = "image_url", length = 500)
    private String imageUrl;
    
    @Column(name = "ai_description", columnDefinition = "TEXT")
    private String aiDescription;

    @Column(name = "ai_analysis_status", length = 32)
    private String aiAnalysisStatus;

    @Column(name = "ai_analysis_result", columnDefinition = "TEXT")
    private String aiAnalysisResult;

    @Column(name = "ai_analysis_time")
    private LocalDateTime aiAnalysisTime;
    
    @Column(name = "is_processed", nullable = false)
    private Boolean isProcessed = false;
    
    @Column(name = "processed_time")
    private LocalDateTime processedTime;
    
    @Column(name = "process_notes", columnDefinition = "TEXT")
    private String processNotes;

    @Column(name = "process_image_url", length = 500)
    private String processImageUrl;

    // 声音异常专用字段
    @Column(name = "audio_url", length = 500)
    private String audioUrl;

    @Column(name = "audio_duration")
    private Float audioDuration;

    @Column(name = "sound_keywords", length = 200)
    private String soundKeywords;

    // 兼容历史表结构中的旧字段，避免插入时触发 NOT NULL 约束异常
    @Column(name = "confidence_score")
    private Double confidenceScore;

    @Column(name = "is_violence", nullable = false)
    private Boolean isViolence = false;

    @Column(name = "level", nullable = false)
    private Integer level = 0;

    @Column(name = "location", length = 200)
    private String location;

    @Column(name = "person_count")
    private Integer personCount;

    @Column(name = "processed_by")
    private Long processedBy;

    @CreatedDate
    @Column(name = "create_time")
    private LocalDateTime createTime;
}
