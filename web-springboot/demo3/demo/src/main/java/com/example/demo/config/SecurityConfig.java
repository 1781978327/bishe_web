package com.example.demo.config;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Lazy;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.config.annotation.web.configuration.EnableWebSecurity;
import org.springframework.security.config.http.SessionCreationPolicy;
import org.springframework.security.crypto.bcrypt.BCryptPasswordEncoder;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.security.web.SecurityFilterChain;
import org.springframework.web.cors.CorsConfiguration;
import org.springframework.web.cors.CorsConfigurationSource;
import org.springframework.web.cors.UrlBasedCorsConfigurationSource;

import java.util.Arrays;

@Configuration
@EnableWebSecurity
public class SecurityConfig {
    
    @Bean
    public PasswordEncoder passwordEncoder() {
        return new BCryptPasswordEncoder();
    }
    
    @Bean
    public SecurityFilterChain filterChain(HttpSecurity http, @Lazy JwtAuthenticationFilter jwtAuthenticationFilter) throws Exception {
        http
            .cors(cors -> cors.configurationSource(corsConfigurationSource()))
            .csrf(csrf -> csrf.disable())
            .sessionManagement(session -> session.sessionCreationPolicy(SessionCreationPolicy.STATELESS))
            .authorizeHttpRequests(authz -> authz
                // 允许未认证访问的端点
                .requestMatchers("/user/login", "/user/register").permitAll()
                // 允许测试接口
                .requestMatchers("/test/**").permitAll()
                // 允许传感器接口（用于环境监测）
                .requestMatchers("/sensor/**").permitAll()
                // 允许 RKNN 推理接口（无需认证）
                .requestMatchers("/rknn/**").permitAll()
                // 允许声音监测接口（无需认证）
                .requestMatchers("/sound/**").permitAll()
                // 允许声音异常上报接口（无需认证，用于自动上报）
                .requestMatchers("/detection/record/sound/**").permitAll()
                // 允许视觉服务阈值告警上报接口（无需认证）
                .requestMatchers("/detection/record/rknn/**").permitAll()
                // 允许WebSocket连接
                .requestMatchers("/ws/**").permitAll()
                // 允许静态资源
                .requestMatchers("/uploads/**").permitAll()
                // 允许文件服务接口（用于头像/图片上传与访问）
                .requestMatchers("/file/**").permitAll()
                // 允许错误页面
                .requestMatchers("/error").permitAll()
                // 其他请求需要认证
                .anyRequest().authenticated()
            )
            .formLogin(form -> form.disable())
            .httpBasic(basic -> basic.disable())
            .addFilterBefore(jwtAuthenticationFilter, org.springframework.security.web.authentication.UsernamePasswordAuthenticationFilter.class);
        
        return http.build();
    }
    
    @Bean
    public CorsConfigurationSource corsConfigurationSource() {
        CorsConfiguration configuration = new CorsConfiguration();
        // 开发环境：允许前端所在的端口
        configuration.setAllowedOriginPatterns(Arrays.asList(
            "http://localhost:*",
            "http://127.0.0.1:*",
            "http://192.168.*.*:*"
        ));
        configuration.setAllowedMethods(Arrays.asList("GET", "POST", "PUT", "DELETE", "OPTIONS"));
        configuration.setAllowedHeaders(Arrays.asList("*"));
        configuration.setAllowCredentials(true);
        configuration.setMaxAge(3600L); // 预检请求缓存1小时

        UrlBasedCorsConfigurationSource source = new UrlBasedCorsConfigurationSource();
        source.registerCorsConfiguration("/**", configuration);
        return source;
    }
}
