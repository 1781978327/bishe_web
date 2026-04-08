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
    
    @Column(name = "temperature_threshold")
    private Float temperatureThreshold = 35.0f;  // 温度阈值
    
    @Column(name = "humidity_threshold")
    private Float humidityThreshold = 80.0f;  // 湿度阈值
    
    @Column(name = "smoke_threshold")
    private Float smokeThreshold = 100.0f;  // 烟雾浓度阈值
    
    @Column(name = "light_threshold")
    private Float lightThreshold = 500.0f;  // 光照强度阈值
    
    @Column(name = "is_alert")
    private Boolean isAlert = false;  // 是否报警
    
    @Column(name = "alert_message", length = 500)
    private String alertMessage;  // 报警信息
    
    @CreatedDate
    @Column(name = "create_time")
    private LocalDateTime createTime;
}
