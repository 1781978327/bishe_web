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
@Table(name = "sensor_data")
@EntityListeners(AuditingEntityListener.class)
public class SensorData {
    
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    
    @Column(name = "temperature")
    private Float temperature;  // 温度 (°C)
    
    @Column(name = "humidity")
    private Float humidity;  // 湿度 (%)
    
    @Column(name = "smoke")
    private Float smoke;  // 烟雾浓度 (ppm)
    
    @Column(name = "light")
    private Float light;  // 光照强度 (lux)
    
    @Column(name = "alert_message", length = 500)
    private String alertMessage;  // 报警信息
    
    @CreatedDate
    @Column(name = "create_time")
    private LocalDateTime createTime;
}
