# Hardware 传感器 HTTP 服务

以下说明已按当前源码核对，真实 HTTP 入口是 `sensor_reader_http.cpp`，运行产物是 `sensor_reader_http`，固定监听 `8088`。

它负责把板端环境数据以 JSON 方式暴露给 Spring Boot：

- `DHT11`：温度、湿度
- `ADS1115 + MQ-2`：烟雾近似值
- `ADS1115 + GL5528`：光照近似值

Spring Boot 默认通过 `http://localhost:8088` 调用本服务。

## 源码结构

```text
Hardware/
├── sensor_reader_http.cpp       # 服务入口（main + signal）
├── CMakeLists.txt               # CMake 构建配置
├── src/
│   ├── dht11_module.h/.cpp      # DHT11 温湿度读取（GPIO bit-bang）
│   ├── ads1115_module.h/.cpp    # ADS1115 I2C、MQ-2 烟雾、光照转换
│   └── sensor_http_server.h/.cpp # libmicrohttpd 服务、JSON、轮询线程
├── dht11/                       # DHT11 独立测试程序
├── ads1115/                     # ADS1115 独立测试程序
└── build/                       # CMake 构建输出目录
```

## 硬件映射

- `DHT11` 数据脚：`wiringPi pin 2`，对应物理引脚 `7`
- `ADS1115` 设备：`/dev/i2c-1`
- `ADS1115` 地址：`0x48`
- `MQ-2`：`AIN0`
- `GL5528`：`AIN1`

## 依赖

```bash
sudo apt install wiringpi libmicrohttpd-dev
```

同时需要确认：

- 系统已启用 `I2C1`
- 板端存在 `/dev/i2c-1`
- 运行用户具有 GPIO / I2C 权限，通常直接使用 `sudo`

## 编译

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

产物位于 `build/sensor_reader_http`。

手动编译（兼容旧方式）：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
g++ -std=c++17 -O2 -Wall sensor_reader_http.cpp \
  src/dht11_module.cpp src/ads1115_module.cpp src/sensor_http_server.cpp \
  -Isrc -o sensor_reader_http -lwiringPi -lmicrohttpd -pthread
```

## 启动

前台运行：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
sudo ./sensor_reader_http
```

后台守护：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
sudo ./sensor_reader_http --daemon
```

## HTTP API

### `GET /`

返回服务简介：

```json
{
  "service": "sensor-reader",
  "version": "1.0",
  "endpoints": ["/health", "/sensor"]
}
```

### `GET /health`

返回健康状态与当前缓存快照的时间戳：

```json
{
  "status": "ok",
  "timestamp": 1710000000
}
```

### `GET /sensor`

每次请求都会立即重新读取一次硬件，再刷新全局快照：

```json
{
  "temperature": 28.5,
  "humidity": 65.0,
  "smoke": 45,
  "light": 320
}
```

## 代码事实

- 仅支持 `GET`
- 未知路径返回 `404`
- 非 `GET` 请求返回 `405`
- 响应头固定带 `Access-Control-Allow-Origin: *`
- 服务启动时会先预热一次传感器读取
- 后台线程会按约 `1Hz` 刷新一次缓存
- `/sensor` 不仅读缓存，而是会主动再采一轮最新数据
- DHT11 本次读取失败，但上一次温湿度有效时，代码会继续沿用上一份温湿度
- 当前代码里，如果湿度低于 `30%`，会被随机修正到 `30.0~31.0`
- 烟雾值来自 `MQ-2` 电压换算的近似 `ppm`
- 光照值当前是 `voltage * 200` 的简单线性换算

## 与 Spring Boot 的联动

后端配置位于：

- `web-springboot/demo3/demo/src/main/resources/application.properties`

默认值：

```properties
sensor.server.host=localhost
sensor.server.port=8088
sensor.poll.interval-ms=1000
```

代码行为：

- Spring Boot 优先请求 `http://localhost:8088/sensor`
- 如果本服务不可用，`SensorService` 会退回到模拟数据
- 传感器阈值超限后，后端会写入 `DetectionRecord`

## 自检

```bash
curl http://127.0.0.1:8088/
curl http://127.0.0.1:8088/health
curl http://127.0.0.1:8088/sensor
ss -ltnp | grep 8088
```

## 常见问题

### `/dev/i2c-1` 不存在

通常说明 I2C 设备树未开启。

### 温湿度一直是 `-1`

优先检查：

- DHT11 接线
- `wiringPi pin 2` 是否对应正确
- 供电与地线是否稳定

### 烟雾或光照一直是 `-1`

优先检查：

- `ADS1115` 是否被系统识别
- 地址是否真的是 `0x48`
- `AIN0 / AIN1` 接线是否正确
