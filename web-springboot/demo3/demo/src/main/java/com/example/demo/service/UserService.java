package com.example.demo.service;

import com.example.demo.dto.LoginRequest;
import com.example.demo.dto.LoginResponse;
import com.example.demo.dto.RegisterRequest;
import com.example.demo.dto.UpdateUserRequest;
import com.example.demo.entity.User;
import com.example.demo.repository.UserRepository;
import com.example.demo.utils.JwtUtil;
import lombok.RequiredArgsConstructor;
import org.springframework.security.core.authority.SimpleGrantedAuthority;
import org.springframework.security.core.userdetails.UserDetails;
import org.springframework.security.core.userdetails.UserDetailsService;
import org.springframework.security.core.userdetails.UsernameNotFoundException;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Service;

import java.util.Collections;
import java.util.Optional;

@Service
@RequiredArgsConstructor
public class UserService implements UserDetailsService {
    
    private final UserRepository userRepository;
    private final PasswordEncoder passwordEncoder;
    private final JwtUtil jwtUtil;
    
    public LoginResponse login(LoginRequest loginRequest) {
        String username = loginRequest.getUsername();
        String password = loginRequest.getPassword();
        
        // 查找用户
        Optional<User> userOpt = userRepository.findByUsername(username);
        if (userOpt.isEmpty()) {
            throw new RuntimeException("用户名或密码错误");
        }
        
        User user = userOpt.get();
        
        // 检查用户状态
        if (user.getStatus() == 0) {
            throw new RuntimeException("账户已被禁用");
        }
        
        // 验证密码
        if (!passwordEncoder.matches(password, user.getPassword())) {
            throw new RuntimeException("用户名或密码错误");
        }
        
        // 生成JWT token
        String token = jwtUtil.generateToken(user.getUsername(), user.getId(), user.getRole());
        
        // 构建响应
        LoginResponse response = new LoginResponse();
        response.setUserInfo(user);
        response.setToken(token);
        
        return response;
    }
    
    public void register(RegisterRequest registerRequest) {
        String username = registerRequest.getUsername();
        
        // 检查用户名是否已存在
        if (userRepository.existsByUsername(username)) {
            throw new RuntimeException("用户名已存在");
        }
        
        // 创建新用户
        User user = new User();
        user.setUsername(username);
        user.setPassword(passwordEncoder.encode(registerRequest.getPassword()));
        user.setRealName(registerRequest.getRealName());
        user.setPhone(registerRequest.getPhone());
        user.setEmail(registerRequest.getEmail());
        user.setRole(0); // 默认普通用户
        user.setStatus(1); // 默认启用
        
        userRepository.save(user);
    }
    
    public User getUserById(Long id) {
        return userRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("用户不存在"));
    }
    
    public User getUserByUsername(String username) {
        return userRepository.findByUsername(username)
                .orElseThrow(() -> new RuntimeException("用户不存在"));
    }

    public User updateUser(UpdateUserRequest request) {
        if (request == null || request.getId() == null) {
            throw new RuntimeException("用户ID不能为空");
        }

        User user = userRepository.findById(request.getId())
            .orElseThrow(() -> new RuntimeException("用户不存在"));

        if (request.getRealName() != null) {
            user.setRealName(request.getRealName());
        }
        if (request.getPhone() != null) {
            user.setPhone(request.getPhone());
        }
        if (request.getEmail() != null) {
            user.setEmail(request.getEmail());
        }
        if (request.getAvatarBucket() != null) {
            user.setAvatarBucket(request.getAvatarBucket());
        }
        if (request.getAvatarObjectKey() != null) {
            user.setAvatarObjectKey(request.getAvatarObjectKey());
        }

        if (user.getAvatarBucket() != null && user.getAvatarObjectKey() != null) {
            user.setAvatarUrl("/api/file/" + user.getAvatarBucket() + "/" + user.getAvatarObjectKey());
        }

        return userRepository.save(user);
    }
    
    @Override
    public UserDetails loadUserByUsername(String username) throws UsernameNotFoundException {
        User user = userRepository.findByUsername(username)
                .orElseThrow(() -> new UsernameNotFoundException("用户不存在: " + username));
        
        // 检查用户状态
        if (user.getStatus() == 0) {
            throw new UsernameNotFoundException("账户已被禁用: " + username);
        }
        
        // 构建权限列表
        String role = user.getRole() == 1 ? "ROLE_ADMIN" : "ROLE_USER";
        
        return org.springframework.security.core.userdetails.User.builder()
                .username(user.getUsername())
                .password(user.getPassword())
                .authorities(Collections.singletonList(new SimpleGrantedAuthority(role)))
                .build();
    }
}
