#!/bin/bash
# YAMNet 声音监测 - 离线检测运行脚本

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

cd "$BUILD_DIR"

# 设置库路径
export LD_LIBRARY_PATH="$BUILD_DIR/lib:$LD_LIBRARY_PATH"

# 默认参数
MODEL="${1:-model/yamnet音频.rknn}"
AUDIO="${2:-model/test.wav}"

echo "=========================================="
echo "  YAMNet Sound Monitoring - Offline"
echo "=========================================="
echo "  Model:  $MODEL"
echo "  Audio:  $AUDIO"
echo "=========================================="
echo ""

./rknn_yamnet_demo "$MODEL" "$AUDIO"
