set -e

GCC_COMPILER=aarch64-linux-gnu
export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

# RKMPP FFmpeg（高优先级）+ OpenCV 依赖库路径
export LD_LIBRARY_PATH="/home/orangepi/opencv_deps/lib:${LD_LIBRARY_PATH:-}"

BUILD_DIR=${PWD}/build
FFMPEG_ROOT=/home/orangepi/Desktop/yolov5_Deepsort_rknn-deepsort/ffmpeg-rockchip/install

if [ ! -d "${BUILD_DIR}" ]; then
  mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}
cmake .. \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DFFMPEG_RKMPP_ROOT=${FFMPEG_ROOT}

make -j$(nproc)
make install
cd "${OLDPWD:-.}"

echo ""
echo "========================================"
echo "Build complete!"
echo "========================================"
ls -la install/rknn_yolov8_demo

echo ""
echo "Verify demo linkage:"
ldd install/rknn_yolov8_demo | grep -E "avcodec|avformat|avutil|rockchip_mpp|rga|opencv"
