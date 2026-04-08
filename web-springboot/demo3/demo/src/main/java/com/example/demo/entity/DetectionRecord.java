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
    
    @Column(name = "image_bucket")
    private String imageBucket;
    
    @Column(name = "image_object_key")
    private String imageObjectKey;
    
    @Column(nullable = false)
    private Integer level; // 1: 低风险, 2: 中风险, 3: 高风险
    
    @Column(name = "is_violence", nullable = false)
    private Boolean isViolence = false;
    
    @Column(name = "ai_description", columnDefinition = "TEXT")
    private String aiDescription;
    
    @Column(name = "confidence_score")
    private Double confidenceScore;
    
    @Column(name = "person_count")
    private Integer personCount;
    
    @Column(name = "location", length = 200)
    private String location;
    
    @Column(name = "is_processed", nullable = false)
    private Boolean isProcessed = false;
    
    @Column(name = "processed_by")
    private Long processedBy;
    
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

    @CreatedDate
    @Column(name = "create_time")
    private LocalDateTime createTime;
}
