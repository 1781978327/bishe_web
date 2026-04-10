#!/bin/bash
# 以root权限启动后端服务
# 这样可以访问GPIO和I2C硬件

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "============================================"
echo "  以root权限启动传感器后端服务"
echo "============================================"

# 检查是否以root运行
if [ "$EUID" -ne 0 ]; then
    echo "请输入密码以root权限运行..."
    exec sudo "$0" "$@"
fi

echo "以root用户运行，开始启动后端..."
cd "$SCRIPT_DIR"

# 默认使用项目内预装的 Gradle
WEB_SPRINGBOOT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOCAL_GRADLE="$WEB_SPRINGBOOT_ROOT/gradle-8.14.1/bin/gradle"

if [ ! -x "$LOCAL_GRADLE" ]; then
    echo "未找到项目内预装 Gradle: $LOCAL_GRADLE"
    exit 1
fi

exec "$LOCAL_GRADLE" bootRun
