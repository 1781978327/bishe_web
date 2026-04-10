package com.example.demo.repository;

import com.example.demo.entity.RknnModelProfile;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.List;
import java.util.Optional;

@Repository
public interface RknnModelProfileRepository extends JpaRepository<RknnModelProfile, Long> {
    Optional<RknnModelProfile> findByUsernameAndBaseName(String username, String baseName);

    List<RknnModelProfile> findByUsernameOrderByUpdateTimeDesc(String username);

    Optional<RknnModelProfile> findByIdAndUsername(Long id, String username);

    Optional<RknnModelProfile> findFirstByUsernameAndSelectedTrueOrderByUpdateTimeDesc(String username);
}
