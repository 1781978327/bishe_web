package com.example.demo.repository;

import com.example.demo.entity.DetectionRecord;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;

@Repository
public interface DetectionRecordRepository extends JpaRepository<DetectionRecord, Long> {
    
    Page<DetectionRecord> findByIsProcessedOrderByDetectionTimeDesc(Boolean isProcessed, Pageable pageable);
    
    Page<DetectionRecord> findByIsViolenceOrderByDetectionTimeDesc(Boolean isViolence, Pageable pageable);
    
    Page<DetectionRecord> findByCameraIdOrderByDetectionTimeDesc(Long cameraId, Pageable pageable);
    
    @Query("SELECT d FROM DetectionRecord d WHERE d.detectionTime BETWEEN :startTime AND :endTime ORDER BY d.detectionTime DESC")
    Page<DetectionRecord> findByDetectionTimeBetween(@Param("startTime") LocalDateTime startTime, 
                                                     @Param("endTime") LocalDateTime endTime, 
                                                     Pageable pageable);
    
    List<DetectionRecord> findByIsProcessedFalseOrderByDetectionTimeDesc();
    
    Long countByIsProcessedFalse();
    
    Long countByIsViolenceTrue();

    // 按事件类型关键词查询（支持前缀匹配）
    @Query("SELECT d FROM DetectionRecord d WHERE d.aiDescription LIKE %:keyword% ORDER BY d.detectionTime DESC")
    Page<DetectionRecord> findByEventTypeKeyword(@Param("keyword") String keyword, Pageable pageable);

    // 按视频类事件查询 (fall, fight, knife, env_intrusion)
    @Query("SELECT d FROM DetectionRecord d WHERE " +
           "(d.aiDescription LIKE '%fall%' OR d.aiDescription LIKE '%fight%' OR d.aiDescription LIKE '%knife%' OR d.aiDescription LIKE '%env_intrusion%') " +
           "ORDER BY d.detectionTime DESC")
    Page<DetectionRecord> findByVideoEvents(Pageable pageable);

    // 按声音类事件查询 (sound_*)
    @Query("SELECT d FROM DetectionRecord d WHERE d.aiDescription LIKE '%sound_%' ORDER BY d.detectionTime DESC")
    Page<DetectionRecord> findBySoundEvents(Pageable pageable);

    // 按环境类事件查询 (env_*，不含 env_intrusion)
    @Query("SELECT d FROM DetectionRecord d WHERE d.aiDescription LIKE '%env_%' AND d.aiDescription NOT LIKE '%env_intrusion%' ORDER BY d.detectionTime DESC")
    Page<DetectionRecord> findByEnvEvents(Pageable pageable);
}
