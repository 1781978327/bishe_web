package com.example.demo.config;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.client.RestTemplate;

@Configuration
public class RknnServerConfig {

    @Value("${rknn.server.host:localhost}")
    private String rknnServerHost;

    @Value("${rknn.server.port:8088}")
    private int rknnServerPort;

    public String getRknnServerHost() {
        return rknnServerHost;
    }

    public int getRknnServerPort() {
        return rknnServerPort;
    }

    public String getRknnServerBaseUrl() {
        return String.format("http://%s:%d", rknnServerHost, rknnServerPort);
    }

    @Bean
    public RestTemplate rknnServerRestTemplate() {
        return new RestTemplate();
    }
}
