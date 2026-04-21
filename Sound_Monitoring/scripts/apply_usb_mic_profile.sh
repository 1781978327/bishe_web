#!/usr/bin/env bash
set -euo pipefail

SOURCE="alsa_input.usb-Jieli_Technology_UACDemoV1.0_4150344C3631350D-00.mono-fallback"
CARD="5"

command -v pactl >/dev/null 2>&1 || { echo "缺少命令: pactl" >&2; exit 1; }
command -v amixer >/dev/null 2>&1 || { echo "缺少命令: amixer" >&2; exit 1; }

echo "[audio] 设置默认麦克风为 USB 麦: $SOURCE"
pactl set-default-source "$SOURCE"

echo "[audio] 设置 Pulse 输入音量到 100%"
pactl set-source-volume "$SOURCE" 100%

echo "[audio] 设置硬件 Mic 到 100%"
amixer -c "$CARD" set Mic 100% >/dev/null

echo "[audio] 开启硬件 Auto Gain Control"
amixer -c "$CARD" set 'Auto Gain Control' on >/dev/null

echo "[audio] 当前状态:"
pactl info | sed -n '/默认信源/p;/Default Source/p'
echo "---"
pactl list sources | sed -n "/名称：$SOURCE/,/格式：/p" | sed -n '/音量：/p;/基础音量：/p'
echo "---"
amixer -c "$CARD" get Mic
echo "---"
amixer -c "$CARD" get 'Auto Gain Control'
