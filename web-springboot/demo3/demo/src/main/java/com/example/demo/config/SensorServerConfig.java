package com.example.demo.config;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.client.RestTemplate;

@Configuration
public class SensorServerConfig {

    @Value("${sensor.server.host:localhost}")
    private String sensorServerHost;

    @Value("${sensor.server.port:8088}")
    private int sensorServerPort;

    @Bean
    public RestTemplate sensorServerRestTemplate() {
        return new RestTemplate();
    }
}
