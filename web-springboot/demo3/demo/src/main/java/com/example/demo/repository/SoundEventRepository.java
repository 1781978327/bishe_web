package com.example.demo.repository;

import com.example.demo.entity.SoundEvent;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;

@Repository
public interface SoundEventRepository extends JpaRepository<SoundEvent, Long> {
    
    // 查询指定时间范围的事件
    List<SoundEvent> findByStartTimeBetween(LocalDateTime start, LocalDateTime end);
    
    // 分页查询
    Page<SoundEvent> findAllByOrderByCreateTimeDesc(Pageable pageable);
    
    // 按声音类型查询
    List<SoundEvent> findBySoundTypeOrderByStartTimeDesc(String soundType);
}
