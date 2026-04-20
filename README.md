# 嵌入式多目标追踪与智能预警系统

以 HTTP 联调为主线的校园安全监测项目。当前代码里的主链路是：

- 浏览器访问 `web-vue`
- 前端主要请求 Spring Boot `http://<host>:8080/api`
- Spring Boot 再分别对接 3 个本地 HTTP 服务
  - 环境传感器：`8088`
  - 声音异常：`8089`
  - RKNN 视觉：`8091`
- 视觉服务通过 MediaMTX 继续提供 `RTSP / HLS / WebRTC(WHEP)` 流

## 项目结构

```text
bishebeifen-master/
├── README.md
├── web-vue/                     # Vue 3 + Vite 前端
├── web-springboot/              # Spring Boot 后端
├── Hardware/                    # 环境传感器 HTTP 服务
├── Sound_Monitoring/            # 声音异常 HTTP 服务
├── yolov8-rk3588-cpp-3-15/      # RK3588 视觉 HTTP / RTSP 服务
├── docs/                        # 辅助接口文档
├── start_all_stack.sh           # 一键启动脚本
└── stop_all_stack.sh            # 一键停止脚本
```

## 当前 HTTP 拓扑

| 调用方 | 被调用方 | 用途 |
|---|---|---|
| 浏览器 | `http://<host>:3000` | 访问 Vue 前端 |
| 前端 | `http://<host>:8080/api` | 统一业务入口 |
| Spring Boot | `http://localhost:8088` | 读取传感器 `/sensor` |
| Spring Boot | `http://localhost:8089` | 声音检测、实时状态、实时事件 |
| Spring Boot | `http://localhost:8091` | 视觉推理、录像、抓帧、阈值、状态 |
| 浏览器 | `rtsp://<host>:8554/*` | RTSP 拉流 |
| 浏览器 | `http://<host>:8888/*` | HLS 播放 |
| 浏览器 | `http://<host>:8889/*/whep` | WebRTC(WHEP) 播放 |

## 端口约定

| 服务 | 端口 | 说明 |
|---|---:|---|
| `web-vue` | `3000` | 前端开发服务 |
| `web-springboot` | `8080` | 后端，带 `/api` 上下文 |
| `sensor_reader_http` | `8088` | 传感器 HTTP 服务 |
| `rknn_yamnet_demo_http` | `8089` | 声音 HTTP 服务 |
| `rknn_http_ctrl` | `8091` | 视觉 HTTP 服务 |
| MediaMTX RTSP | `8554` | RTSP |
| MediaMTX HLS | `8888` | HLS |
| MediaMTX WebRTC | `8889` | WHEP |
| MediaMTX ICE | `8189/udp` | WebRTC ICE |

## 启动顺序

### 0. 前置依赖

- `MySQL 8`
- `JDK 17`
- `Node.js 18+`
- 板端依赖：`wiringPi`、`libmicrohttpd-dev`、OpenCV、FFmpeg、RGA、RKNN 运行库等

### 1. 启动环境传感器 HTTP 服务

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
sudo ./sensor_reader_http
```

自检：

```bash
curl http://127.0.0.1:8088/health
curl http://127.0.0.1:8088/sensor
```

### 2. 启动声音 HTTP 服务

推荐先编译：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring
./scripts/build.sh
```

再运行：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/build
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
sudo ./rknn_yamnet_demo_http 8089
```

说明：

- `scripts/build.sh` 默认产物在 `Sound_Monitoring/build`
- 根目录 `start_all_stack.sh` 优先尝试 `Sound_Monitoring/src/build`，如目录不同可显式设置 `SOUND_DIR`
- 当前实时输入默认改成 `parec`，会直接跟随系统默认麦克风
- 声音服务实时音频配置默认从 `Sound_Monitoring/config/runtime_audio.yaml` 读取；构建后会复制到 `Sound_Monitoring/build/config/runtime_audio.yaml`
- 当前默认配置是不改硬件增益、不做 FFmpeg/SoX 额外滤波，只使用 `parec` 原始输入

自检：

```bash
curl http://127.0.0.1:8089/health
curl http://127.0.0.1:8089/realtime/status
```

### 3. 启动视觉 HTTP 服务

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/build_release
sudo ./rknn_http_ctrl --cam0-source /dev/video0 --cam1-source /dev/video2
```

说明：

- 推荐从 `build_release` 启动，自动拉起的 `mediamtx` 会优先读取 `build_release/mediamtx.yml`
- 视觉服务直连端口是 `8091`

自检：

```bash
curl http://127.0.0.1:8091/api/status
```

### 4. 启动 Spring Boot

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/web-springboot/demo3/demo
./gradlew bootRun
```

后端入口：

- `http://127.0.0.1:8080/api`

自检：

```bash
curl http://127.0.0.1:8080/api/test/health
curl http://127.0.0.1:8080/api/rknn/status
```

### 5. 启动前端

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/web-vue
npm install
npm run dev -- --host 0.0.0.0 --port 3000
```

访问：

- 本机：`http://127.0.0.1:3000`
- 局域网：`http://<当前机器IP>:3000`

### 6. 可选：一键脚本

在三类下游二进制都已准备好的前提下，可以直接使用：

```bash
./start_all_stack.sh
./stop_all_stack.sh
```

## 联调自检

```bash
ss -ltnup | grep -E '3000|8080|8088|8089|8091|8554|8888|8889|8189'
curl http://127.0.0.1:8088/sensor
curl http://127.0.0.1:8089/health
curl http://127.0.0.1:8091/api/status
curl http://127.0.0.1:8080/api/test/health
```

打开视觉推理与推流：

```bash
curl -X POST "http://127.0.0.1:8080/api/rknn/inference/on?track=true&tracker=bytetrack"
curl -X POST "http://127.0.0.1:8080/api/rknn/rtsp/camera/start"
```

## 主要业务入口

### 后端主要入口

- `POST /api/user/login`
- `POST /api/user/register`
- `POST /api/user/logout`
- `PUT /api/user`
- `GET/PUT/POST /api/sensor/*`
- `GET/POST /api/sound/*`
- `GET/POST /api/rknn/*`
- `GET/POST /api/model/*`
- `GET/POST/DELETE /api/file/*`
- `GET/PUT/DELETE /api/detection/record/*`

### 视觉链路

- 视觉服务 RTSP
  - `rtsp://<host>:8554/cam0`
  - `rtsp://<host>:8554/cam1`
  - `rtsp://<host>:8554/cam2`
  - `rtsp://<host>:8554/cam3`
- 前端默认会把 RTSP 地址自动转换成 WHEP 地址
  - `http://<host>:8889/cam0/whep`
  - `http://<host>:8889/cam1/whep`
  - `http://<host>:8889/cam2/whep`

### 模型管理

当前后端内置模型：

- `person_2700_i8`
- `yolov8s`
- `yolov8n`

上传模型仍走 `.rknn + 同名 .txt` 配对方式，选择模型时由后端把绝对路径下发给视觉服务。

## 前端现状说明

- 当前主路由：`/login`、`/register`、`/dashboard`、`/profile`、`/monitor`、`/detection/record`、`/model/upload`、`/admin`、`/admin/users`
- 监控页默认播放协议是 `WebRTC`，不是 `HLS`
- `camera/index.vue` 组件仍存在，但当前 `/camera` 路由会直接重定向到 `/dashboard`
- `src/api/user.ts` 里还保留了 `/user/page`、`/user/{id}`、`/user/password` 等历史封装；当前后端并没有实现这些接口，现网主链路只包括登录、注册、退出和个人信息更新

## 代码里的几个真实细节

- `SensorService` 在 `8088` 传感器服务不可用时，会退回到模拟数据
- `/api/rknn/forbidden-area` 当前存储在后端内存中，Spring Boot 重启后会清空
- `/api/rknn/status` 会把视觉服务返回里的 `localhost/127.0.0.1` RTSP 地址改写成当前请求主机，便于前端直接播放
- 录像真正落盘在视觉服务目录，Spring Boot 只负责代理状态、列表和下载

## AI 融合分析

当前后端已经接入“异常落库后自动触发 AI 融合分析”的链路。

### 触发方式

以下三类异常写入 `detection_records` 后，会自动异步触发 AI：

- 视频类异常上报
- 声音类异常上报
- 环境类异常记录

AI 不是新建一条独立记录，而是把分析结果回写到原来的安全记录里。

### 当前聚合的数据

后端会按当前代码汇总：

- 触发异常记录本身
- 最近 `2` 条传感器历史
- 当前传感器阈值
- 最近 `5` 个声音实时窗口
- 声音实时状态与实时异常事件
- 视觉状态与两路检测计数
- 记录自带图片；如果记录没有图片，则尽量抓当前帧补图

说明：

- 当前不会为了 AI 额外录制一段“新的当前音频文件”
- 声音上下文主要来自声音服务实时窗口与事件接口

### 配置

启动 Spring Boot 前建议先设置：

```bash
export VISION_API_KEY='你的 key'
export VISION_BASE_URL='https://api.866646.xyz/'
export RISK_MODEL='qwen3-vl:235b-instruct'
```

如果你平时通过根目录脚本启动，当前也可以直接把这些变量写到本地文件：

```bash
/home/orangepi/Desktop/web/bishebeifen-master/.runtime/ai.env
```

例如：

```bash
VISION_API_KEY='你的 key'
VISION_BASE_URL='https://api.866646.xyz/'
RISK_MODEL='qwen3-vl:235b-instruct'
```

`start_all_stack.sh` 会自动加载这个文件，所以日常只需要执行：

```bash
./start_all_stack.sh
```

如果你不是用 `start_all_stack.sh`，而是手动启动或重启 Spring Boot，需要先把这个文件加载进当前 shell，再启动后端：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master
set -a
source .runtime/ai.env
set +a
cd web-springboot/demo3/demo
./gradlew bootRun
```

如果只改了 `.runtime/ai.env` 里的 `VISION_API_KEY` / `VISION_BASE_URL` / `RISK_MODEL`，但后端没有重启，那么运行中的 Spring 进程不会自动拿到新值，AI 融合分析仍然可能显示“未配置 VISION_API_KEY”。

对应的后端配置项在 `application.properties`：

- `ai.analysis.enabled`
- `ai.analysis.base-url`
- `ai.analysis.api-key`
- `ai.analysis.model`
- `ai.analysis.sensor-history-size`
- `ai.analysis.sound-window-limit`

注意：

- 这些环境变量需要在后端启动前就存在
- 如果你改了 `VISION_API_KEY` 但后端已经在跑，最好重启后端；如果是用 `./gradlew bootRun`，必要时先执行一次 `./gradlew --stop`

### 前端显示

前端“安全记录”详情页现在会显示：

- `aiAnalysisStatus`
- `aiAnalysisResult`
- `aiAnalysisTime`

也就是同一条安全记录里同时保留：

- 原始异常信息
- 图片或音频信息
- AI 融合分析结论

### 快速模拟区域闯入

如果暂时不方便做真实禁入区闯入，可以直接构造一条 `env_intrusion` 上报：

```bash
python3 - <<'PY'
import base64, json
from pathlib import Path

img = Path('/home/orangepi/Desktop/web/bishebeifen-master/测试大模型/R-C.jpg').read_bytes()
payload = {
    "cameraId": 1,
    "detectionResult": json.dumps({
        "type": "env_intrusion",
        "cam": 0,
        "hitCount": 1,
        "zonePoints": [
            {"x": 160, "y": 120},
            {"x": 480, "y": 120},
            {"x": 480, "y": 400},
            {"x": 160, "y": 400}
        ],
        "objects": [
            {
                "label": "person",
                "cls": 0,
                "score": 0.93,
                "center": {"x": 320, "y": 260},
                "box": {"x1": 240, "y1": 120, "x2": 400, "y2": 420}
            }
        ]
    }, ensure_ascii=False),
    "imageBase64": base64.b64encode(img).decode("utf-8")
}
Path('/tmp/env_intrusion_test.json').write_text(json.dumps(payload, ensure_ascii=False), encoding='utf-8')
PY

curl -X POST http://127.0.0.1:8080/api/detection/record/rknn/report \
  -H 'Content-Type: application/json' \
  --data-binary @/tmp/env_intrusion_test.json
```

然后去前端“安全记录”里查看这条记录的图片、原始 `env_intrusion` 数据和 AI 融合分析结果。

## 模块 README

- `web-vue/README`
- `web-springboot/readme`
- `web-springboot/demo3/demo/README.md`
- `Hardware/README.md`
- `Sound_Monitoring/README.md`
- `yolov8-rk3588-cpp-3-15/README.md`
