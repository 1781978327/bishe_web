#!/bin/bash
# Build C++ emergency keyword monitor for Sound_Monitoring.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_FILE="${ROOT_DIR}/wake/emergency_monitor.cpp"
OUT_DIR="${1:-${ROOT_DIR}/build/wake}"
OUT_BIN="${OUT_DIR}/emergency_monitor"

if [ ! -f "${SRC_FILE}" ]; then
    echo "[WAKE] Source file not found: ${SRC_FILE}"
    exit 1
fi

declare -a install_candidates=()
if [ -n "${SHERPA_ONNX_INSTALL_DIR:-}" ]; then
    install_candidates+=("${SHERPA_ONNX_INSTALL_DIR}")
fi
install_candidates+=(
    "${ROOT_DIR}/wake/sherpa-onnx/build/install"
    "${ROOT_DIR}/../语音唤醒/sherpa-onnx/build/install"
    "${ROOT_DIR}/../../语音唤醒/sherpa-onnx/build/install"
)

SHERPA_INSTALL=""
for candidate in "${install_candidates[@]}"; do
    if [ -f "${candidate}/include/sherpa-onnx/c-api/c-api.h" ] &&
       [ -f "${candidate}/lib/libsherpa-onnx-c-api.so" ] &&
       [ -f "${candidate}/lib/libonnxruntime.so" ]; then
        SHERPA_INSTALL="${candidate}"
        break
    fi
done

if [ -z "${SHERPA_INSTALL}" ]; then
    echo "[WAKE] Failed to find sherpa-onnx install artifacts."
    echo "[WAKE] Please set SHERPA_ONNX_INSTALL_DIR=/path/to/sherpa-onnx/build/install"
    exit 1
fi

mkdir -p "${OUT_DIR}"

echo "[WAKE] Building emergency monitor..."
echo "[WAKE] Using sherpa install: ${SHERPA_INSTALL}"

g++ -std=c++17 -Wall -O2 \
    -I"${SHERPA_INSTALL}/include" \
    -o "${OUT_BIN}" \
    "${SRC_FILE}" \
    -L"${SHERPA_INSTALL}/lib" \
    -Wl,-rpath,"${SHERPA_INSTALL}/lib" \
    -lsherpa-onnx-c-api \
    -lonnxruntime \
    -lasound \
    -lpthread

touch "${OUT_DIR}/emergency_log.txt"

echo "[WAKE] Build finished: ${OUT_BIN}"
echo "[WAKE] Tip: set EMERGENCY_KWS_MODEL_DIR if your model is in a custom directory."
