package com.example.demo.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.springframework.data.annotation.CreatedDate;
import org.springframework.data.annotation.LastModifiedDate;
import org.springframework.data.jpa.domain.support.AuditingEntityListener;

import java.time.LocalDateTime;

@Data
@EqualsAndHashCode(callSuper = false)
@Entity
@Table(
        name = "rknn_model_profiles",
        uniqueConstraints = {
                @UniqueConstraint(name = "uk_rknn_model_user_base", columnNames = {"username", "base_name"})
        }
)
@EntityListeners(AuditingEntityListener.class)
public class RknnModelProfile {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false, length = 64)
    private String username;

    @Column(name = "base_name", nullable = false, length = 200)
    private String baseName;

    @Column(name = "model_object_key", length = 500)
    private String modelObjectKey;

    @Column(name = "label_object_key", length = 500)
    private String labelObjectKey;

    @Column(nullable = false)
    private Boolean selected = false;

    @CreatedDate
    @Column(name = "create_time")
    private LocalDateTime createTime;

    @LastModifiedDate
    @Column(name = "update_time")
    private LocalDateTime updateTime;
}
