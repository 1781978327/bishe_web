cmake_minimum_required(VERSION 3.10)

project(rknn_yamnet_demo)

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()

set(rknpu_yamnet_file ${CMAKE_CURRENT_SOURCE_DIR}/yamnet.cc)
if(TARGET_SOC STREQUAL "rk1808" OR TARGET_SOC STREQUAL "rv1109" OR TARGET_SOC STREQUAL "rv1126")
    set(rknpu_yamnet_file ${CMAKE_CURRENT_SOURCE_DIR}/yamnet.cc)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../utils/ utils.out)

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# 实时音频采集版本（不包含 main.cc）
add_executable(${PROJECT_NAME}_live
    main_live.cc
    audio_capture.c
    process.cc
    ${rknpu_yamnet_file}
)

# 离线文件检测版本（不包含 main_live.cc）
add_executable(${PROJECT_NAME}
    main.cc
    process.cc
    ${rknpu_yamnet_file}
)

# HTTP API 服务器版本
add_executable(${PROJECT_NAME}_http
    main_http.cc
    audio_capture.c
    process.cc
    ${rknpu_yamnet_file}
)

# 3rdparty libsndfile.a 未用 -fPIC 编译，链接 PIE 可执行文件会失败，故禁用 PIE
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-no-pie")
    set_target_properties(${PROJECT_NAME}_live PROPERTIES LINK_FLAGS "-no-pie")
    set_target_properties(${PROJECT_NAME}_http PROPERTIES LINK_FLAGS "-no-pie")
endif()

target_link_libraries(${PROJECT_NAME}
    fileutils
    audioutils   
    ${LIBRKNNRT}
    dl
)

# 实时版本需要 ALSA
target_link_libraries(${PROJECT_NAME}_live
    fileutils
    audioutils
    ${LIBRKNNRT}
    dl
    asound
)

target_link_libraries(${PROJECT_NAME}_http
    fileutils
    audioutils
    ${LIBRKNNRT}
    dl
    asound
)

target_include_directories(${PROJECT_NAME}_live PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRKNNRT_INCLUDES}
    ${LIBTIMER_INCLUDES}
)

target_include_directories(${PROJECT_NAME}_http PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRKNNRT_INCLUDES}
    ${LIBTIMER_INCLUDES}
)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_link_libraries(${PROJECT_NAME}
    log
)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
    target_link_libraries(${PROJECT_NAME}_live Threads::Threads)
    target_link_libraries(${PROJECT_NAME}_http Threads::Threads)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRKNNRT_INCLUDES}
    ${LIBTIMER_INCLUDES}
)

install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(TARGETS ${PROJECT_NAME}_http DESTINATION .)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/test.wav DESTINATION ./model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/yamnet_class_map.txt DESTINATION ./model)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model)