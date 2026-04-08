cmake_minimum_required(VERSION 3.4.1)

# ByteTrack library
add_library(bytetrack STATIC
    src/BYTETracker.cpp
    src/kalmanFilter.cpp
    src/lapjv.cpp
    src/STrack.cpp
)

target_include_directories(bytetrack PUBLIC
    ${CMAKE_SOURCE_DIR}/bytetrack/include
    ${CMAKE_SOURCE_DIR}/include
    ${EIGEN3_INCLUDE_DIR}
)
