# Install script for directory: /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-inference-main

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-inference-main/install")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE EXECUTABLE FILES "/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-inference-main/Hardware/rknn_yolov8_demo")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/./rknn_yolov8_demo" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/./rknn_yolov8_demo")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/./rknn_yolov8_demo")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE PROGRAM FILES "/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-inference-main/include/3rdparty/rga/RK3588/lib/Linux/aarch64/librga.so")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE PROGRAM FILES
    "/usr/local/ffmpeg/lib/libavformat.so.60"
    "/usr/local/ffmpeg/lib/libavcodec.so.60"
    "/usr/local/ffmpeg/lib/libavutil.so.58"
    "/usr/local/ffmpeg/lib/libswscale.so.7"
    "/usr/local/ffmpeg/lib/libswresample.so.4"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/./" TYPE DIRECTORY FILES "/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-inference-main/model")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-inference-main/Hardware/deepsort/cmake_install.cmake")
  include("/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-inference-main/Hardware/bytetrack/cmake_install.cmake")

endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-inference-main/Hardware/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
