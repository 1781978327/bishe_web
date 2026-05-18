#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNTIME_DIR="$ROOT_DIR/.runtime"
PID_DIR="$RUNTIME_DIR/pids"
LOG_DIR="$RUNTIME_DIR/logs"

SUDO_PASSWORD="orangepi"

# 自动输入 sudo 密码
echo "$SUDO_PASSWORD" | sudo -S -p '' -v >/dev/null 2>&1

echo "============================================="
echo "           正在停止所有服务..."
echo "============================================="

# 要停止的服务列表（和启动脚本一一对应）
SERVICES=(
  "frontend"
  "backend"
  "sensor_http"
  "sound_http"
  "vision_http"
)

is_running_pid() {
  local pid="${1:-}"
  [[ -n "$pid" ]] && [[ "$pid" =~ ^[0-9]+$ ]] && [[ -d "/proc/$pid" ]]
}

run_kill() {
  local signal="$1"
  local pid="$2"
  local mode="$3"

  if [[ "$mode" == "sudo" ]]; then
    sudo -n kill "-$signal" "$pid" 2>/dev/null || true
  else
    kill "-$signal" "$pid" 2>/dev/null || true
  fi
}

wait_for_exit() {
  local pid="$1"
  local tries="${2:-20}"
  for ((i=0; i<tries; i++)); do
    if ! is_running_pid "$pid"; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

service_mode() {
  local name="$1"
  case "$name" in
    sensor_http|sound_http|vision_http)
      printf '%s' "sudo"
      ;;
    *)
      printf '%s' "user"
      ;;
  esac
}

# 停止每个服务
for name in "${SERVICES[@]}"; do
  pidfile="$PID_DIR/${name}.pid"
  mode="$(service_mode "$name")"

  if [[ -f "$pidfile" ]]; then
    pid=$(cat "$pidfile" 2>/dev/null || true)
    if is_running_pid "$pid"; then
      echo "停止 $name (PID=$pid)..."
      run_kill TERM "$pid" "$mode"
      if wait_for_exit "$pid" 20; then
        echo "  -> 已正常停止"
      else
        echo "  -> 进程未退出，发送 SIGKILL"
        run_kill KILL "$pid" "$mode"
        if wait_for_exit "$pid" 10; then
          echo "  -> 已强制停止"
        else
          echo "  -> 警告：进程可能仍在运行 (PID=$pid)"
        fi
      fi
    else
      echo "PID 文件存在但进程已不在运行: $name (PID=${pid:-unknown})"
    fi
    rm -f "$pidfile"
  else
    echo "未运行: $name"
  fi
done

# 额外精确清理（防止 PID 文件丢失或 wrapper 残留）
echo "精确清理本项目残留进程..."
pkill -f '/web-vue/node_modules/.bin/vite --host 0.0.0.0' 2>/dev/null || true
pkill -f 'sh -c vite --host 0.0.0.0' 2>/dev/null || true
pkill -f 'gradle.*bootRun' 2>/dev/null || true
pkill -f 'com.example.demo.DemoApplication' 2>/dev/null || true
sudo -n pkill -f '/Hardware.*/sensor_reader_http' 2>/dev/null || true
sudo -n pkill -f '(^|[[:space:]/])sensor_reader_http([[:space:]]|$)' 2>/dev/null || true
sudo -n pkill -f '/Sound_Monitoring/build.*/rknn_yamnet_demo_http 8089' 2>/dev/null || true
sudo -n pkill -f '/Sound_Monitoring/src/build.*/rknn_yamnet_demo_http 8089' 2>/dev/null || true
sudo -n pkill -f '(^|[[:space:]/])rknn_yamnet_demo_http([[:space:]]|$)' 2>/dev/null || true
sudo -n pkill -f '/Sound_Monitoring/.*/wake/emergency_monitor' 2>/dev/null || true
sudo -n pkill -f '(^|[[:space:]/])emergency_monitor([[:space:]]|$)' 2>/dev/null || true
sudo -n rm -f /tmp/emergency_monitor.pid 2>/dev/null || true
sudo -n pkill -f '/yolov8-rk3588-cpp-3-15/build_release.*/rknn_http_ctrl' 2>/dev/null || true
sudo -n pkill -f '/yolov8-rk3588-cpp-3-15/src/mediamtx' 2>/dev/null || true
sudo -n pkill -f 'rknn_http_ctrl' 2>/dev/null || true
sudo -n pkill -f 'mediamtx' 2>/dev/null || true

mkdir -p "$LOG_DIR"

echo
echo "✅ 所有服务已完全停止！"
echo
echo "现在可以重新启动："
echo "  ./start_all_stack.sh"
echo
