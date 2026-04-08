package com.example.demo.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
@Entity
@Table(name = "sensor_threshold")
public class SensorThreshold {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(name = "temperature_threshold", nullable = false)
    private Float temperatureThreshold = 35.0f;  // 温度阈值

    @Column(name = "humidity_threshold", nullable = false)
    private Float humidityThreshold = 80.0f;  // 湿度阈值

    @Column(name = "smoke_threshold", nullable = false)
    private Float smokeThreshold = 100.0f;  // 烟雾浓度阈值

    @Column(name = "light_threshold", nullable = false)
    private Float lightThreshold = 500.0f;  // 光照强度阈值（正常应该是500左右，低于这个值说明太暗）
}
