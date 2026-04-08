package com.example.demo.config;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.client.RestTemplate;

@Configuration
public class SoundServerConfig {

    @Value("${sound.server.host:localhost}")
    private String soundServerHost;

    @Value("${sound.server.port:8089}")
    private int soundServerPort;

    @Bean
    public RestTemplate soundServerRestTemplate() {
        return new RestTemplate();
    }
}
