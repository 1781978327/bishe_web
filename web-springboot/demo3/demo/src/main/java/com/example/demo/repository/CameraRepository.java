package com.example.demo.repository;

import com.example.demo.entity.Camera;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.JpaSpecificationExecutor;
import org.springframework.stereotype.Repository;

import java.util.List;

@Repository
public interface CameraRepository extends JpaRepository<Camera, Long>, JpaSpecificationExecutor<Camera> {
    List<Camera> findByStatusOrderByCreateTimeDesc(Integer status);

    List<Camera> findByDetectionEnabledTrueAndStatus(Integer status);
}

