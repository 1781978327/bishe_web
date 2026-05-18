#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNTIME_DIR="$ROOT_DIR/.runtime"
PID_DIR="$RUNTIME_DIR/pids"
LOG_DIR="$RUNTIME_DIR/logs"
AI_ENV_FILE="${AI_ENV_FILE:-$RUNTIME_DIR/ai.env}"

FRONTEND_DIR="$ROOT_DIR/web-vue"
BACKEND_DIR="$ROOT_DIR/web-springboot/demo3/demo"
SENSOR_DIR="$ROOT_DIR/Hardware"
SOUND_DIR="${SOUND_DIR:-$ROOT_DIR/Sound_Monitoring/src/build}"
if [[ ! -d "$SOUND_DIR" ]]; then
  LEGACY_SOUND_DIR="$ROOT_DIR/Sound_Monitoring/build"
  if [[ -d "$LEGACY_SOUND_DIR" ]]; then
    SOUND_DIR="$LEGACY_SOUND_DIR"
  fi
fi
VISION_DIR="$ROOT_DIR/yolov8-rk3588-cpp-3-15/build_release"
VISION_MEDIAMTX_TEMPLATE="$ROOT_DIR/yolov8-rk3588-cpp-3-15/mediamtx.yml"
VISION_MEDIAMTX_CONFIG="$VISION_DIR/mediamtx.yml"
SUDO_PASSWORD="orangepi"
DEFAULT_CAM0_SOURCE=""
DEFAULT_CAM1_SOURCE=""

mkdir -p "$PID_DIR" "$LOG_DIR"

load_optional_env_file() {
  local env_file="$1"
  if [[ ! -f "$env_file" ]]; then
    return 0
  fi

  echo "加载本地环境配置: $env_file"
  set -a
  # shellcheck disable=SC1090
  source "$env_file"
  set +a
}

load_optional_env_file "$AI_ENV_FILE"

DRY_RUN=0
if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=1
fi

require_file() {
  local path="$1"
  if [[ ! -e "$path" ]]; then
    echo "缺少文件或目录: $path" >&2
    exit 1
  fi
}

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "缺少命令: $cmd" >&2
    exit 1
  fi
}

require_file "$FRONTEND_DIR/package.json"
require_file "$BACKEND_DIR/gradlew"
require_file "$SENSOR_DIR/sensor_reader_http"
require_file "$SOUND_DIR/rknn_yamnet_demo_http"
require_file "$VISION_DIR/rknn_http_ctrl"
require_file "$VISION_MEDIAMTX_TEMPLATE"

require_cmd bash
require_cmd nohup
require_cmd npm
require_cmd sudo
require_cmd curl

if (( DRY_RUN == 0 )) && [[ "${EUID}" -ne 0 ]]; then
  echo "正在自动输入 sudo 密码，用于启动三个硬件/算法服务..."
  printf '%s\n' "$SUDO_PASSWORD" | sudo -S -p '' -v
fi

declare -A SERVICE_PID=()
declare -A SERVICE_STATE=()
declare -A SERVICE_LOG=()

is_running_pid() {
  local pid="${1:-}"
  [[ -n "$pid" ]] && [[ "$pid" =~ ^[0-9]+$ ]] && [[ -d "/proc/$pid" ]]
}

existing_pid_from_file() {
  local pidfile="$1"
  if [[ -f "$pidfile" ]]; then
    local pid
    pid="$(tr -d '[:space:]' < "$pidfile" 2>/dev/null || true)"
    if is_running_pid "$pid"; then
      printf '%s' "$pid"
      return 0
    fi
    rm -f "$pidfile"
  fi
  return 1
}

wait_for_pidfile() {
  local pidfile="$1"
  local tries=50
  local pid=""
  for ((i=0; i<tries; i++)); do
    if [[ -f "$pidfile" ]]; then
      pid="$(tr -d '[:space:]' < "$pidfile" 2>/dev/null || true)"
      if [[ -n "$pid" ]]; then
        printf '%s' "$pid"
        return 0
      fi
    fi
    sleep 0.2
  done
  return 1
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

stop_service_if_running() {
  local name="$1"
  local mode="$2"
  local pidfile="$PID_DIR/${name}.pid"
  local pid=""

  if (( DRY_RUN )); then
    if [[ -f "$pidfile" ]]; then
      pid="$(tr -d '[:space:]' < "$pidfile" 2>/dev/null || true)"
    fi
    if is_running_pid "$pid"; then
      echo "[dry-run][$name] 检测到运行中进程，跳过停止 (PID=$pid)"
    fi
    return 0
  fi

  if [[ -f "$pidfile" ]]; then
    pid="$(tr -d '[:space:]' < "$pidfile" 2>/dev/null || true)"
  fi

  if ! is_running_pid "$pid"; then
    rm -f "$pidfile"
    return 0
  fi

  echo "[$name] 发现旧进程，先重启以应用最新配置 (PID=$pid)"
  run_kill TERM "$pid" "$mode"
  for ((i=0; i<20; i++)); do
    if ! is_running_pid "$pid"; then
      rm -f "$pidfile"
      return 0
    fi
    sleep 0.2
  done

  echo "[$name] 旧进程未退出，发送 SIGKILL"
  run_kill KILL "$pid" "$mode"
  for ((i=0; i<10; i++)); do
    if ! is_running_pid "$pid"; then
      rm -f "$pidfile"
      return 0
    fi
    sleep 0.2
  done

  echo "[$name] 警告：旧进程可能仍未退出 (PID=$pid)" >&2
  rm -f "$pidfile"
}

cleanup_vision_residuals() {
  if (( DRY_RUN )); then
    echo "[dry-run][vision_http] 跳过残留进程清理"
    return 0
  fi
  echo "[vision_http] 清理残留的视觉服务进程..."
  sudo -n pkill -f 'rknn_http_ctrl' 2>/dev/null || true
  sudo -n pkill -f 'mediamtx' 2>/dev/null || true
  sleep 1
}

cleanup_sensor_residuals() {
  if (( DRY_RUN )); then
    echo "[dry-run][sensor_http] 跳过残留进程清理"
    return 0
  fi
  echo "[sensor_http] 清理残留的传感器服务进程..."
  sudo -n pkill -f 'sensor_reader_http' 2>/dev/null || true
  sleep 1
}

cleanup_sound_residuals() {
  if (( DRY_RUN )); then
    echo "[dry-run][sound_http] 跳过残留进程清理"
    return 0
  fi
  echo "[sound_http] 清理残留的声音服务进程..."
  sudo -n pkill -f 'rknn_yamnet_demo_http' 2>/dev/null || true
  sudo -n pkill -f '/Sound_Monitoring/.*/wake/emergency_monitor' 2>/dev/null || true
  sudo -n pkill -f '(^|[[:space:]/])emergency_monitor([[:space:]]|$)' 2>/dev/null || true
  sudo -n rm -f /tmp/emergency_monitor.pid 2>/dev/null || true
  sleep 1
}

resolve_camera_source() {
  local env_value="$1"
  local preferred_path="$2"
  local fallback_a="$3"
  local fallback_b="$4"

  if [[ -n "$env_value" ]]; then
    printf '%s' "$env_value"
    return 0
  fi

  if [[ -e "$preferred_path" ]]; then
    printf '%s' "$preferred_path"
    return 0
  fi

  if [[ -e "$fallback_a" ]]; then
    printf '%s' "$fallback_a"
    return 0
  fi

  printf '%s' "$fallback_b"
}

discover_primary_camera_source() {
  local candidate

  shopt -s nullglob
  for candidate in /dev/v4l/by-path/*video-index0; do
    if [[ -e "$candidate" ]]; then
      printf '%s' "$candidate"
      shopt -u nullglob
      return 0
    fi
  done
  shopt -u nullglob

  printf '%s' ""
}

discover_secondary_camera_source() {
  local primary_source="$1"
  local candidate

  shopt -s nullglob
  for candidate in /dev/v4l/by-path/*video-index0; do
    if [[ "$candidate" != "$primary_source" ]] && [[ -e "$candidate" ]]; then
      printf '%s' "$candidate"
      shopt -u nullglob
      return 0
    fi
  done
  shopt -u nullglob

  if [[ -e "/dev/video2" ]]; then
    printf '%s' "/dev/video2"
    return 0
  fi

  if [[ -e "/dev/video3" ]]; then
    printf '%s' "/dev/video3"
    return 0
  fi

  printf '%s' "/dev/video2"
}

ensure_vision_mediamtx_config() {
  mkdir -p "$VISION_DIR"
  cp "$VISION_MEDIAMTX_TEMPLATE" "$VISION_MEDIAMTX_CONFIG"
  echo "[vision_http] 已同步 mediamtx 配置: $VISION_MEDIAMTX_CONFIG"
}

start_service() {
  local name="$1"
  local mode="$2"
  local workdir="$3"
  local cmd="$4"
  local pidfile="$PID_DIR/${name}.pid"
  local logfile="$LOG_DIR/${name}.log"

  SERVICE_LOG["$name"]="$logfile"

  local existing_pid=""
  if existing_pid="$(existing_pid_from_file "$pidfile")"; then
    SERVICE_PID["$name"]="$existing_pid"
    SERVICE_STATE["$name"]="already-running"
    echo "[$name] 已在运行，PID=$existing_pid"
    return 0
  fi

  local launch_script
  launch_script="cd '$workdir' && nohup bash -lc 'exec $cmd' > '$logfile' 2>&1 < /dev/null & echo \$! > '$pidfile'"

  if (( DRY_RUN )); then
    echo "[dry-run][$name][$mode] $launch_script"
    SERVICE_STATE["$name"]="dry-run"
    return 0
  fi

  rm -f "$pidfile"
  if [[ "$mode" == "sudo" ]]; then
    sudo -n bash -lc "$launch_script"
  else
    bash -lc "$launch_script"
  fi

  local pid=""
  if ! pid="$(wait_for_pidfile "$pidfile")"; then
    echo "[$name] 启动失败：未生成 PID 文件 $pidfile" >&2
    echo "[$name] 日志：$logfile" >&2
    exit 1
  fi

  if ! is_running_pid "$pid"; then
    echo "[$name] 启动失败：进程未存活，PID=$pid" >&2
    echo "[$name] 日志：$logfile" >&2
    tail -n 40 "$logfile" 2>/dev/null || true
    exit 1
  fi

  SERVICE_PID["$name"]="$pid"
  SERVICE_STATE["$name"]="started"
  echo "[$name] 已启动，PID=$pid"
}

fail_service_start() {
  local name="$1"
  local message="$2"
  local logfile="${SERVICE_LOG[$name]:-}"
  echo "[$name] 启动失败：$message" >&2
  if [[ -n "$logfile" ]]; then
    echo "[$name] 日志：$logfile" >&2
    tail -n 40 "$logfile" 2>/dev/null || true
  fi
  exit 1
}

wait_for_http_ready() {
  local name="$1"
  local url="$2"
  local expected_text="${3:-}"
  local status_regex="${4:-^200$}"
  local tries="${5:-40}"

  if (( DRY_RUN )); then
    return 0
  fi

  local response=""
  local code=""
  local body=""
  for ((i=0; i<tries; i++)); do
    response="$(curl -sS --max-time 2 -w $'\n%{http_code}' "$url" 2>/dev/null || true)"
    code="${response##*$'\n'}"
    body="${response%$'\n'*}"
    if [[ "$code" =~ $status_regex ]]; then
      if [[ -z "$expected_text" || "$body" == *"$expected_text"* ]]; then
        SERVICE_STATE["$name"]="ready"
        return 0
      fi
    fi
    sleep 0.5
  done

  fail_service_start "$name" "健康检查未通过: $url"
}

stop_service_if_running "frontend" "user"
stop_service_if_running "backend" "user"
stop_service_if_running "sensor_http" "sudo"
stop_service_if_running "sound_http" "sudo"
stop_service_if_running "vision_http" "sudo"

cleanup_sensor_residuals
cleanup_sound_residuals
cleanup_vision_residuals

start_service "frontend" "user" "$FRONTEND_DIR" "npm run dev -- --host 0.0.0.0"
wait_for_http_ready "frontend" "http://127.0.0.1:3000" "" '^200$' 40
start_service "backend" "user" "$BACKEND_DIR" "./gradlew bootRun"
wait_for_http_ready "backend" "http://127.0.0.1:8080/api/test/health" "" '^200$' 80
start_service "sensor_http" "sudo" "$SENSOR_DIR" "./sensor_reader_http"
wait_for_http_ready "sensor_http" "http://127.0.0.1:8088/health" '"status": "ok"' '^200$' 40
SOUND_HTTP_CMD="env LD_LIBRARY_PATH=./lib:\$LD_LIBRARY_PATH \
EMERGENCY_KWS_AUTO_START=${EMERGENCY_KWS_AUTO_START:-0} \
EMERGENCY_KWS_REPORT_ENABLED=${EMERGENCY_KWS_REPORT_ENABLED:-1} \
EMERGENCY_KWS_CMD=${EMERGENCY_KWS_CMD:-} \
EMERGENCY_KWS_WORKDIR=${EMERGENCY_KWS_WORKDIR:-} \
EMERGENCY_KWS_LOG_PATH=${EMERGENCY_KWS_LOG_PATH:-} \
EMERGENCY_KWS_MODEL_DIR=${EMERGENCY_KWS_MODEL_DIR:-} \
./rknn_yamnet_demo_http 8089"
start_service "sound_http" "sudo" "$SOUND_DIR" "$SOUND_HTTP_CMD"
wait_for_http_ready "sound_http" "http://127.0.0.1:8089/health" '"status": "ok"' '^200$' 40

CAM0_SOURCE="$(resolve_camera_source "${CAM0_SOURCE:-}" "$(discover_primary_camera_source)" "${DEFAULT_CAM0_SOURCE:-/dev/video0}" "/dev/video0")"
if [[ -n "${CAM1_SOURCE:-}" ]]; then
  CAM1_SOURCE="$CAM1_SOURCE"
else
  CAM1_SOURCE="$(resolve_camera_source "" "$(discover_secondary_camera_source "$CAM0_SOURCE")" "${DEFAULT_CAM1_SOURCE:-/dev/video2}" "/dev/video2")"
fi

echo "视觉服务摄像头源："
echo "  cam0 -> $CAM0_SOURCE"
echo "  cam1 -> $CAM1_SOURCE"

ensure_vision_mediamtx_config
start_service "vision_http" "sudo" "$VISION_DIR" "./rknn_http_ctrl --cam0-source $CAM0_SOURCE --cam1-source $CAM1_SOURCE"
wait_for_http_ready "vision_http" "http://127.0.0.1:8091/api/status" '"running"' '^200$' 40

echo
echo "================ PID Summary ================"
for name in frontend backend sensor_http sound_http vision_http; do
  printf '%-12s state=%-16s pid=%-8s log=%s\n' \
    "$name" \
    "${SERVICE_STATE[$name]:-unknown}" \
    "${SERVICE_PID[$name]:--}" \
    "${SERVICE_LOG[$name]:--}"
done
echo "============================================="

echo
echo "端口参考："
echo "  frontend    -> 3000"
echo "  backend     -> 8080"
echo "  sensor_http -> 8088"
echo "  sound_http  -> 8089"
echo "  vision_http -> 8091"

if (( DRY_RUN == 0 )); then
  echo
  echo "可用下面命令快速确认："
  echo "  ss -ltnp | grep -E '3000|8080|8088|8089|8091'"
fi
