#!/bin/bash
# YAMNet 声音监测 - 编译脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
SRC_DIR="$SCRIPT_DIR/src"
LIB_DIR="$SCRIPT_DIR/lib"

echo "=========================================="
echo "  YAMNet Sound Monitoring - Build Script"
echo "=========================================="

# 创建build目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 设置GCC编译器路径
export GCC_COMPILER=/usr/bin/aarch64-linux-gnu-gcc

# 检查编译器
if [ ! -f "$GCC_COMPILER" ]; then
    echo "[WARN] GCC compiler not found at $GCC_COMPILER"
    echo "[INFO] Trying system gcc..."
    GCC_COMPILER=gcc
fi

# CMake配置
echo "[INFO] Running CMake..."
cmake "$SRC_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTARGET_SOC=rk3588 \
    -DCMAKE_C_COMPILER=$GCC_COMPILER \
    -DCMAKE_CXX_COMPILER=/usr/bin/aarch64-linux-gnu-g++ \
    -DRKNPU2_DIR="$SCRIPT_DIR/3rdparty/rknpu2/Linux/aarch64" \
    -DRKNPU1_DIR="$SCRIPT_DIR/3rdparty/rknpu1/Linux/aarch64" \
    -DLIBRGA_DIR="$SCRIPT_DIR/3rdparty/librga/Linux/aarch64" \
    -DFFTW_DIR="$SCRIPT_DIR/3rdparty/fftw/Linux/aarch64" \
    -DKALDI_DIR="$SCRIPT_DIR/3rdparty/kaldi_native_fbank/Linux/aarch64" \
    -DLIBSNDFILE_DIR="$SCRIPT_DIR/3rdparty/libsndfile/Linux/aarch64"

# 编译
echo "[INFO] Building..."
make -j$(nproc)

# 复制库文件到build目录
cp "$SCRIPT_DIR/3rdparty/rknpu2/librknnrt.so" "$BUILD_DIR/lib/"
cp "$SCRIPT_DIR/3rdparty/rknpu1/librknn_api.so" "$BUILD_DIR/lib/"
mkdir -p "$BUILD_DIR/model"
cp "$SCRIPT_DIR/model/"* "$BUILD_DIR/model/"
mkdir -p "$BUILD_DIR/config"
cp "$SCRIPT_DIR/config/runtime_audio.yaml" "$BUILD_DIR/config/"

echo ""
echo "=========================================="
echo "  Build completed successfully!"
echo "=========================================="
echo ""
echo "To run offline detection:"
echo "  cd $BUILD_DIR"
echo "  export LD_LIBRARY_PATH=./lib:\$LD_LIBRARY_PATH"
echo "  ./rknn_yamnet_demo model/yamnet音频.rknn model/test.wav"
echo ""
echo "To run live detection:"
echo "  ./rknn_yamnet_demo_live -m model/yamnet音频.rknn -d parec"
echo ""
