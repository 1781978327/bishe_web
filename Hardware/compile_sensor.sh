#!/bin/bash
# 传感器硬件读取程序编译脚本

echo "===== 传感器读取程序编译 ====="
cd "$(dirname "$0")"

# 检查是否已安装wiringPi
if ! ldconfig -p | grep -q libwiringPi; then
    echo "警告: wiringPi库未找到，请先安装: sudo apt install wiringpi"
fi

# 检查I2C设备
if [ ! -e "/dev/i2c-1" ]; then
    echo "警告: I2C设备 /dev/i2c-1 不存在"
    echo "请确保设备树已启用I2C1: sudo dtoverlay i2c1"
fi

# 编译
echo "正在编译sensor_reader..."
g++ -std=c++17 -O2 -Wall sensor_reader.cpp -o sensor_reader -lwiringPi

if [ $? -eq 0 ]; then
    echo "编译成功!"
    echo ""
    echo "===== 使用方法 ====="
    echo "运行: sudo ./sensor_reader"
    echo "输出格式: {\"temperature\": XX.X, \"humidity\": XX.X, \"smoke\": XX, \"light\": XX}"
else
    echo "编译失败!"
    exit 1
fi
