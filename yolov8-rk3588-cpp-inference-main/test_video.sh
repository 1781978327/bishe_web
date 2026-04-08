#!/bin/bash
# Quick diagnostic: check FFmpeg/libavcodec can actually decode the video
# Add build/lib first so it overrides system FFmpeg

export LD_LIBRARY_PATH="/home/orangepi/sheji/yolov8-rk3588-cpp-inference-main/build/lib:/home/orangepi/opencv_deps/lib:${LD_LIBRARY_PATH:-}"

echo "=== LD_LIBRARY_PATH ==="
echo "$LD_LIBRARY_PATH" | tr ':' '\n' | head -10

echo ""
echo "=== avformat linked libs ==="
ldd /home/orangepi/sheji/yolov8-rk3588-cpp-inference-main/build/lib/libavformat.so.60

echo ""
echo "=== avcodec linked libs ==="
ldd /home/orangepi/sheji/yolov8-rk3588-cpp-inference-main/build/lib/libavcodec.so.60

echo ""
echo "=== avcodec dlopen deps (what libavcodec itself dlopens) ==="
# Check if libavcodec.so can dlopen h264 decoder at runtime
# Run a tiny FFmpeg command to see actual error
echo "=== ffprobe with LD_LIBRARY_PATH ==="
ffprobe video/person.mp4 2>&1

echo ""
echo "=== OpenCV videoio backends available ==="
./build/rknn_yolov8_demo install/model/RK3588/yolov8s.rknn video/person.mp4 2>&1 | head -5
