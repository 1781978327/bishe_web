# 环境传感器 HTTP 服务

`Hardware/sensor_reader_http.cpp` 是当前仓库里用于环境监测的独立 HTTP 服务，负责读取：

- `DHT11` 温度 / 湿度
- `MQ-2` 烟雾
- `GL5528` 光照
- `ADS1115` ADC

Spring Boot 默认通过 `localhost:8088` 访问它，对应配置见 `web-springboot/demo3/demo/src/main/resources/application.properties`。

## 硬件映射

- `DHT11` 数据脚：`wiringPi pin 2`，对应物理引脚 `7`
- `ADS1115` 设备：`/dev/i2c-1`
- `ADS1115` 地址：`0x48`
- `MQ-2` 通道：`AIN0`
- `光照` 通道：`AIN1`

## 系统依赖

```bash
sudo apt install wiringpi libmicrohttpd-dev
```

同时需要确认：

- 已启用 `I2C1`
- 系统存在 `/dev/i2c-1`
- 运行用户有访问 GPIO / I2C 的权限，通常直接用 `sudo`

## 编译

当前服务入口是 `sensor_reader_http.cpp`，推荐直接编译这个文件：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
g++ -std=c++17 -O2 -Wall sensor_reader_http.cpp -o sensor_reader_http -lwiringPi -lmicrohttpd
```

说明：

- 仓库内已经存在一个已编译好的 `sensor_reader_http`，但是否可直接运行仍取决于当前机器环境

## 启动

前台运行：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
sudo ./sensor_reader_http
```

后台守护运行：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
sudo ./sensor_reader_http --daemon
```

服务固定监听：

- `8088`

## HTTP API

### `GET /`

返回服务简介和可用端点：

```json
{
  "service": "sensor-reader",
  "version": "1.0",
  "endpoints": ["/health", "/sensor"]
}
```

### `GET /health`

健康检查，返回最近一次读取时间：

```json
{
  "status": "ok",
  "timestamp": 1710000000
}
```

### `GET /sensor`

读取一份最新传感器数据并立即返回：

```json
{
  "temperature": 28.5,
  "humidity": 65.0,
  "smoke": 45,
  "light": 320
}
```

说明：

- 每次访问 `/sensor` 都会先触发一次实时读取
- 服务主循环也会每 `1` 秒刷新一次全局缓存
- 读取失败时，相关字段会返回 `-1`

### 其他行为

- 仅支持 `GET`
- 非 `GET` 请求返回 `405`
- 未知路径返回 `404`
- 响应头中已放开 `Access-Control-Allow-Origin: *`

## 运行细节

- 启动时会先执行一次 `readAllSensors()` 预热硬件
- 主循环默认每 `1` 秒更新一次传感器缓存
- DHT11 不适合高频轮询，当前代码刻意保持约 `1Hz`
- `MQ-2` 使用电压换算为近似 `ppm`
- 光照传感器当前使用简单线性换算 `voltage * 200`

## 快速自检

```bash
curl http://127.0.0.1:8088/
curl http://127.0.0.1:8088/health
curl http://127.0.0.1:8088/sensor
```

检查端口：

```bash
ss -ltnp | grep 8088
```

## 常见问题

### `/dev/i2c-1` 不存在

通常说明 I2C 设备树没有开启，先确认板卡配置。

### 温湿度总是 `-1`

优先检查：

- `DHT11` 接线
- `wiringPi pin 2` 是否对应正确
- 供电和地线是否稳定

### 烟雾或光照一直是 `-1`

优先检查：

- `ADS1115` 是否被系统识别
- 地址是否为 `0x48`
- `AIN0 / AIN1` 接线是否正确

### Spring Boot 读不到数据

确认下面三项是否一致：

- 服务已启动并监听 `8088`
- `application.properties` 中 `sensor.server.host/port` 未改错
- 本机执行 `curl http://127.0.0.1:8088/sensor` 能拿到 JSON
