#!/bin/bash
# YAMNet 声音监测 - 实时麦克风检测运行脚本

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

cd "$BUILD_DIR"

# 设置库路径
export LD_LIBRARY_PATH="$BUILD_DIR/lib:$LD_LIBRARY_PATH"

# 默认参数
MODEL="${1:-model/yamnet音频.rknn}"
DEVICE="${2:-hw:4,0}"

echo "=========================================="
echo "  YAMNet Sound Monitoring - Live"
echo "=========================================="
echo "  Model:   $MODEL"
echo "  Device:  $DEVICE"
echo "  Rate:    16000 Hz"
echo "  Channel: Mono"
echo "=========================================="
echo ""
echo "Press Ctrl+C to stop..."
echo ""

./rknn_yamnet_demo_live -m "$MODEL" -d "$DEVICE"
