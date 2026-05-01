cmake_minimum_required(VERSION 3.10)
project(sensor_reader_http CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MICROHTTPD REQUIRED libmicrohttpd)

add_executable(sensor_reader_http
    sensor_reader_http.cpp
    src/dht11_module.cpp
    src/ads1115_module.cpp
    src/sensor_http_server.cpp
)

target_include_directories(sensor_reader_http PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MICROHTTPD_INCLUDE_DIRS}
)

target_link_libraries(sensor_reader_http
    wiringPi
    ${MICROHTTPD_LIBRARIES}
    pthread
)

install(TARGETS sensor_reader_http DESTINATION .)
