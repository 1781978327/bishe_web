#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${PROJECT_DIR}"

export LD_LIBRARY_PATH="${PROJECT_DIR}/build/lib:/home/orangepi/opencv_deps/lib:${LD_LIBRARY_PATH:-}"

echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
echo "avcodec: $(ldd ./build/rknn_yolov8_demo 2>/dev/null | grep avcodec || echo 'n/a')"
echo ""

# ============================================================
# 参数说明
# ============================================================
# 参数1: 模式
#   single      - 单摄像头（默认）
#   dual        - 双摄像头（需要两个视频设备）
# 参数2: 跟踪模式 [deepsort|bytetrack|none]
# 参数3: 隔帧数 (仅deepsort有效) [0|1|2]
# 参数4: 视频源
#   single:  video.mp4 | camera | /dev/video0 | rtsp://...
#   dual:    camera,camera2 | /dev/video0,/dev/video2 | (逗号分隔)
# 参数5: 输出目标 [空=仅显示 | rtsp://...=推流]
#   single:  rtsp://ip:8554/stream
#   dual:    rtsp://ip:8554/cam0,rtsp://ip:8554/cam1
MODE="${1:-single}"
TRACKER="${2:-deepsort}"
SKIP_FRAMES="${3:-0}"

# ============================================================
# mediamtx 管理
# ============================================================
start_mediamtx() {
    if pgrep -x mediamtx > /dev/null 2>&1; then
        echo "[mediamtx] already running (pid=$(pgrep -x mediamtx))"
    else
        echo "[mediamtx] starting..."
        ./mediamtx > /tmp/mediamtx.log 2>&1 &
        sleep 2
        if pgrep -x mediamtx > /dev/null 2>&1; then
            echo "[mediamtx] started (pid=$(pgrep -x mediamtx))"
            cat /tmp/mediamtx.log | tail -5
        else
            echo "[mediamtx] FAILED - check /tmp/mediamtx.log"
            cat /tmp/mediamtx.log
            exit 1
        fi
    fi
}

stop_mediamtx() {
    if pgrep -x mediamtx > /dev/null 2>&1; then
        echo "[mediamtx] stopping..."
        pkill -x mediamtx
        sleep 1
    fi
}

# 退出时清理
trap 'echo "cleanup..."; stop_mediamtx; exit 0' INT TERM EXIT

MEDIAMTX_IP="${MEDIAMTX_IP:-192.168.1.129}"

YOLO_MODEL="${PROJECT_DIR}/install/model/RK3588/yolov8s.rknn"
REID_MODEL="${PROJECT_DIR}/install/model/RK3588/osnet_x0_25_market.rknn"

case "$MODE" in
  single)
    INPUT_SRC="${4:-camera}"
    OUTPUT_DST="${5:-}"

    if [[ "$INPUT_SRC" == "camera" ]]; then
        INPUT_SRC="/dev/video0"
    fi

    echo "=== 单摄像头模式 ==="
    echo "跟踪: $TRACKER, 隔帧=$SKIP_FRAMES"
    echo "输入: $INPUT_SRC"
    echo "输出: ${OUTPUT_DST:-仅本地显示}"

    if [[ "$OUTPUT_DST" == rtsp://* ]]; then
        start_mediamtx
    fi

    ./build/rknn_yolov8_demo "$YOLO_MODEL" "$INPUT_SRC" "$OUTPUT_DST" "$REID_MODEL" \
        --tracker "$TRACKER" --skip "$SKIP_FRAMES"
    ;;

  dual)
    CAM0="${4:-camera}"
    CAM1="${5:-camera2}"
    OUTPUT_DST="${6:-}"

    if [[ "$CAM0" == "camera" ]]; then  CAM0="/dev/video0"; fi
    if [[ "$CAM1" == "camera2" ]] || [[ "$CAM1" == "camera" ]]; then CAM1="/dev/video2"; fi

    # 两个摄像头均设为 1280x720 MJPG
    echo "[摄像头初始化]"
    v4l2-ctl -d 0 --set-fmt-video=width=1280,height=720,pixelformat=MJPG 2>/dev/null && echo "  Cam0 -> 1280x720 MJPG" || echo "  Cam0 格式设置失败"
    v4l2-ctl -d 2 --set-fmt-video=width=1280,height=720,pixelformat=MJPG 2>/dev/null && echo "  Cam1 -> 1280x720 MJPG" || echo "  Cam2 格式设置失败"

    # 自动生成双路 RTSP URL（逗号分隔，video_path 用逗号分隔）
    if [[ "$OUTPUT_DST" == rtsp://* ]] && [[ "$OUTPUT_DST" != *,* ]]; then
        # 用户只给了一个 URL，自动派生第二个
        CAM0_URL="${OUTPUT_DST}"
        CAM1_URL="${OUTPUT_DST/cam0/cam1}"
        if [[ "$CAM1_URL" == "$CAM0_URL" ]]; then
            CAM1_URL="${OUTPUT_DST%/stream}/stream2"
        fi
        OUTPUT_DST="${CAM0_URL},${CAM1_URL}"
    fi

    echo "=== 双摄像头模式 ==="
    echo "跟踪: $TRACKER, 隔帧=$SKIP_FRAMES"
    echo "Cam0: $CAM0"
    echo "Cam1: $CAM1"
    echo "推流: ${OUTPUT_DST:-仅本地显示}"

    if [[ "$OUTPUT_DST" == rtsp://* ]]; then
        start_mediamtx
    fi

    # main.cc: video_path 用逗号分隔；output_path 用逗号分隔
    ./build/rknn_yolov8_demo "$YOLO_MODEL" "${CAM0},${CAM1}" "$OUTPUT_DST" "$REID_MODEL" \
        --tracker "$TRACKER" --skip "$SKIP_FRAMES"
    ;;

  *)
    echo "用法: bash $0 [single|dual] [deepsort|bytetrack|none] [skip] [input] [output]"
    echo ""
    echo "  single - 单摄像头模式（默认）"
    echo "  dual   - 双摄像头模式"
    echo ""
    echo "  tracker: deepsort (默认,需要ReID) | bytetrack (无需ReID) | none"
    echo "  skip:    0=每帧跟踪 1=每2帧1次 2=每3帧1次"
    echo ""
    echo "示例:"
    echo "  # 单摄像头本地"
    echo "  bash $0 single deepsort 0 camera"
    echo ""
    echo "  # 单摄像头推RTSP"
    echo "  bash $0 single deepsort 0 camera rtsp://192.168.1.129:8554/stream"
    echo ""
    echo "  # 双摄像头本地显示"
    echo "  bash $0 dual deepsort 0 camera camera2"
    echo ""
    echo "  # 双摄像头推双路RTSP"
    echo "  bash $0 dual deepsort 0 camera camera2 rtsp://192.168.1.129:8554/cam0"
    echo ""
    echo "  # 手动指定设备"
    echo "  bash $0 dual deepsort 0 /dev/video0,/dev/video2 rtsp://192.168.1.129:8554/cam0,rtsp://192.168.1.129:8554/cam1"
    echo ""
    exit 1
    ;;
esac
