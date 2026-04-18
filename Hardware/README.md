# Hardware 传感器 HTTP 服务说明

当前环境监测主链路使用 `sensor_reader_http.cpp`，运行产物是 `sensor_reader_http`，固定端口 `8088`。

服务负责读取：

- `DHT11`：温度、湿度
- `MQ-2`：烟雾
- `GL5528`：光照
- `ADS1115`：ADC 采样

Spring Boot 默认通过 `http://localhost:8088` 访问它。

## 硬件映射

- `DHT11` 数据脚：`wiringPi pin 2`，物理引脚 `7`
- `ADS1115`：`/dev/i2c-1`
- `ADS1115` 地址：`0x48`
- `MQ-2`：`AIN0`
- `GL5528`：`AIN1`

## 依赖

```bash
sudo apt install wiringpi libmicrohttpd-dev
```

同时需要确认：

- 已启用 `I2C1`
- 系统存在 `/dev/i2c-1`
- 运行用户有 GPIO/I2C 权限，通常直接用 `sudo`

## 编译

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
g++ -std=c++17 -O2 -Wall sensor_reader_http.cpp -o sensor_reader_http -lwiringPi -lmicrohttpd
```

说明：

- 仓库里已经存在一个已编译的 `sensor_reader_http`
- 但能否直接运行仍取决于当前板卡环境和库版本

## 启动

前台：

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

返回健康状态和最近一次读取时间：

```json
{
  "status": "ok",
  "timestamp": 1710000000
}
```

### `GET /sensor`

每次请求都会立即读取一次最新数据：

```json
{
  "temperature": 28.5,
  "humidity": 65.0,
  "smoke": 45,
  "light": 320
}
```

代码事实：

- `/sensor` 内部会先执行一次 `readAllSensors()`
- 主循环也会每秒刷新一次全局缓存
- 读取失败时，相关字段可能返回 `-1`

### 其他行为

- 仅支持 `GET`
- 非 `GET` 返回 `405`
- 未知路径返回 `404`
- 响应头放开了 `Access-Control-Allow-Origin: *`

## 运行细节

- 启动时会先预热一次硬件读取
- DHT11 当前按照约 `1Hz` 节奏轮询
- `MQ-2` 通过电压估算近似 `ppm`
- 光照当前使用简单线性换算 `voltage * 200`

## 与 Spring Boot 的联动

后端配置位于：

- `web-springboot/demo3/demo/src/main/resources/application.properties`

默认值：

```properties
sensor.server.host=localhost
sensor.server.port=8088
sensor.poll.interval-ms=1000
```

后端会优先请求本服务；若本服务不可用，`SensorService` 会回退到模拟数据。

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

- `ADS1115` 是否被识别
- 地址是否真的是 `0x48`
- `AIN0 / AIN1` 接线是否正确
