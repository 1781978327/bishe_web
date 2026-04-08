package com.example.demo.repository;

import com.example.demo.entity.SensorData;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

@Repository
public interface SensorDataRepository extends JpaRepository<SensorData, Long> {
    
    // 获取最新一条传感器数据
    SensorData findTopByOrderByCreateTimeDesc();
    
    // 获取传感器历史数据（分页）
    Page<SensorData> findAllByOrderByCreateTimeDesc(Pageable pageable);
}
