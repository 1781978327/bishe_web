# YOLOv8 RK3588 HTTP / RTSP 服务

`src/main_http_ctrl.cc` 是当前仓库里视觉服务的主入口，负责：

- 双路摄像头采集
- RKNN 推理
- ByteTrack / DeepSORT 跟踪
- 单帧 JPEG 抓图
- RTSP 推流
- 视频文件推理与推流
- 摄像头录像
- 框数量告警上报
- 禁入区同步与异常闯入上报

Spring Boot 默认通过 `localhost:8091` 访问它。

## 默认端口与流地址

- HTTP：`8091`
- RTSP：`8554`
- WebRTC(WHEP)：`8889`
- ICE：`8189/udp`

默认推流地址：

- `rtsp://127.0.0.1:8554/cam0`
- `rtsp://127.0.0.1:8554/cam1`
- `rtsp://127.0.0.1:8554/cam2`
- `rtsp://127.0.0.1:8554/cam3`

含义：

- `cam0`：摄像头 1
- `cam1`：摄像头 2
- `cam2`：`cam0 + cam1` 融合拼接流
- `cam3`：视频文件模式输出

## 目录说明

```text
yolov8-rk3588-cpp-3-15/
├── src/                 # HTTP 服务、推理与推流主逻辑
├── include/             # RKNN / RTSP / V4L2 / 公共头文件
├── bytetrack/           # ByteTrack
├── deepsort/            # DeepSORT + ReID
├── model/RK3588/        # YOLO / ReID 模型
├── recordings/camera/   # 摄像头录像默认输出目录
├── video/               # 视频文件输入示例
└── build_release/       # 推荐运行目录
```

常用模型：

- 检测模型：`model/RK3588/yolov8s.rknn`
- 检测模型：`model/RK3588/yolov8n.rknn`
- 标签文件：`model/coco_80_labels_list.txt`
- ReID 模型：`model/RK3588/osnet_x0_25_market.rknn`

## 依赖

- RK3588 / OrangePi 5B
- OpenCV 4
- Eigen3
- RGA
- FFmpeg 60 + RKMPP
- `mediamtx` 或其他 RTSP / WebRTC 服务器

当前代码会优先走：

- 摄像头：`V4L2 DMABUF -> RGA -> RKNN`
- 视频：`FFmpeg RKMPP -> DRM_PRIME -> RGA -> RKNN`
- 推流：`BGR -> DMA staging -> RGA -> NV12 -> MPP encoder -> RTSP`

如果硬件链路不可用，部分路径仍保留软件回退逻辑。

## 编译

推荐使用 `Release` 构建：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15
mkdir -p build_release
cd build_release
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2 rknn_http_ctrl rknn_yolov8_demo
```

开发调试也可以继续使用 `build/`：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/build
cmake ..
make -j2 rknn_http_ctrl rknn_yolov8_demo
```

## 启动服务

推荐从 `build_release` 目录启动：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/build_release
sudo ./rknn_http_ctrl --cam0-source /dev/video0 --cam1-source /dev/video2
```

服务特点：

- HTTP 服务默认监听 `8091`
- 启动时会自动尝试拉起 `mediamtx`
- 模型在第一次调用 `/api/inference/on` 时懒加载
- 默认输入源是 `cam0=/dev/video0`、`cam1=/dev/video2`

如需改默认跟踪器：

```bash
sudo ./rknn_http_ctrl \
  --tracker-backend deepsort \
  --reid-model /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/model/RK3588/osnet_x0_25_market.rknn
```

## 命令行参数

支持的主要参数：

- `--port <http_port>`
- `--rtsp-host <host>`
- `--rtsp-port <port>`
- `--mediamtx-bin <path>`
- `--mediamtx-log <path>`
- `--mediamtx-auto-start <true|false>`
- `--ffmpeg-bin <path>`
- `--record-output-dir <path>`
- `--cam0-source <source>`
- `--cam1-source <source>`
- `--tracker-backend <bytetrack|deepsort>`
- `--reid-model <path>`

## 环境变量

除了命令行，也支持环境变量：

```bash
export RTSP_HOST=127.0.0.1
export RTSP_PORT=8554
export MEDIAMTX_BIN=/path/to/mediamtx
export MEDIAMTX_LOG=/tmp/mediamtx_auto.log
export MEDIAMTX_AUTO_START=true
export FFMPEG_BIN=/usr/local/ffmpeg/bin/ffmpeg
export RECORD_OUTPUT_DIR=/path/to/recordings/camera
export CAM0_SOURCE=/dev/video0
export CAM1_SOURCE=/dev/video2
export TRACKER_BACKEND=bytetrack
export TRACKER_REID_MODEL=/path/to/osnet_x0_25_market.rknn
export REPORT_SERVER_HOST=127.0.0.1
export REPORT_SERVER_PORT=8080
export REPORT_SERVER_PATH=/api/detection/record/rknn/report
export REPORT_CAM0_ID=1
export REPORT_CAM1_ID=2
export BOX_ALERT_COOLDOWN_MS=8000
export FORBIDDEN_AREA_PATH=/api/rknn/forbidden-area
export FORBIDDEN_AREA_SYNC_MS=2000
export FORBIDDEN_AREA_TIMEOUT_MS=600
export INTRUSION_ALERT_COOLDOWN_MS=8000
```

## mediamtx 说明

当前版本支持两种方式：

### 方式 1：让 `rknn_http_ctrl` 自动拉起

- 推荐从 `build_release` 目录启动
- 自动拉起时会优先读取 `build_release/mediamtx.yml`
- 通常会同时开放 `8554 / 8889 / 8189`

### 方式 2：手动启动 `mediamtx`

```bash
./mediamtx
```

检查端口：

```bash
ss -ltnup | grep -E '8554|8889|8189'
```

## HTTP API 一览

### 状态与基础控制

- `GET /api/status`
  - 返回推理、模型、RTSP、录像、禁入区、当前输入源等完整状态
- `GET /`
- `GET /index.html`
  - 返回内置 HTML 控制页
- `POST /api/camera/{0|1|2}`
  - 切换当前显示摄像头

### 推理与跟踪

- `POST /api/inference/on`
  - 开启推理，可通过查询参数指定：
  - `track=0|1`
  - `tracker=bytetrack|deepsort`
  - `model=/abs/path/to/model.rknn`
  - `labels=/abs/path/to/labels.txt`
  - `reid_model=/abs/path/to/osnet.rknn`
  - `tracker_skip=2`
- `POST /api/inference/off`
  - 关闭推理
  - 支持 `unload=1`
  - 也支持顺便指定下次加载的 `model` / `labels`
- `GET /api/tracker`
  - 返回跟踪开关和当前算法
- `POST /api/tracker/1`
  - 打开跟踪
- `POST /api/tracker/0`
  - 关闭跟踪

说明：

- 当前模型已经加载时，如果要切换模型、标签或跟踪算法，需要先调用 `/api/inference/off`
- `/api/inference/on` 默认开启跟踪，除非显式传 `track=0`

### 阈值与检测结果

- `POST /api/threshold/set?value=0.6&box_count=3`
  - 设置置信度阈值和框数量告警阈值
- `GET /api/threshold/get`
  - 获取当前阈值
- `GET /api/detection/count?cam=0`
  - 获取指定摄像头当前检测框数量

### 图像抓取

- `GET /api/frame?cam=0`
  - 获取 JPEG 抓图
- 支持参数：
  - `cam=0|1`
  - `cameraId=1|2`
  - `track=0|1`
  - `redraw=1`

说明：

- 默认不会二次重绘跟踪框，避免叠加双框
- `redraw=1` 主要用于调试

### RTSP 推流

- `POST /api/rtsp/start`
  - 摄像头模式开启 RTSP 推流
  - 会同时尝试启动 `cam0 / cam1 / cam2`
- `POST /api/rtsp/video/start`
  - 视频模式开启 `cam3` 推流
- `POST /api/rtsp/stop`
  - 停止全部 RTSP 推流

### 录像

- `GET /api/record/status`
- `POST /api/record/start?cam=0`
- `POST /api/record/start?cameraId=1`
- `POST /api/record/stop?cam=0`
- `POST /api/record/stop`
  - 不带参数时会尝试停止全部录像

说明：

- 录像仅支持真实摄像头 `cam0 / cam1`
- 默认输出目录：`recordings/camera`

### 视频文件模式

- `GET /api/video/status`
- `POST /api/video/start`
  - 请求体示例：

```json
{
  "path": "/abs/path/to/video.mp4",
  "loop": true
}
```

- `POST /api/video/stop`
  - 停止视频模式并恢复摄像头

## 推荐联调流程

先设变量：

```bash
BASE=http://127.0.0.1:8091
MODEL=/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/model/RK3588/yolov8s.rknn
LABELS=/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/model/coco_80_labels_list.txt
REID=/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/model/RK3588/osnet_x0_25_market.rknn
```

### 摄像头推理 + 跟踪 + 推流

```bash
curl -X POST "$BASE/api/rtsp/stop"
curl -X POST "$BASE/api/inference/off?unload=1"
curl -X POST "$BASE/api/video/stop"

curl -X POST "$BASE/api/camera/0"
curl -X POST -G "$BASE/api/inference/on" \
  --data-urlencode "track=1" \
  --data-urlencode "tracker=bytetrack" \
  --data-urlencode "model=$MODEL" \
  --data-urlencode "labels=$LABELS"

sleep 1
curl "$BASE/api/frame?cam=0" -o cam0.jpg
curl -X POST "$BASE/api/rtsp/start"
curl "$BASE/api/status"
```

### DeepSORT 模式

```bash
curl -X POST -G "$BASE/api/inference/on" \
  --data-urlencode "track=1" \
  --data-urlencode "tracker=deepsort" \
  --data-urlencode "tracker_skip=2" \
  --data-urlencode "model=$MODEL" \
  --data-urlencode "labels=$LABELS" \
  --data-urlencode "reid_model=$REID"
```

### 视频模式

```bash
curl -X POST "$BASE/api/video/start" \
  -H 'Content-Type: application/json' \
  -d '{"path":"/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/video/person.mp4","loop":true}'

curl -X POST "$BASE/api/rtsp/video/start"
curl "$BASE/api/video/status"
```

### 摄像头录像

```bash
curl "$BASE/api/record/status"
curl -X POST "$BASE/api/record/start?cam=0"
curl -X POST "$BASE/api/record/stop?cam=0"
```

## 与 Spring Boot 的联动

当前代码内建了两类后端交互：

### 1. 检测记录上报

默认上报到：

```text
http://127.0.0.1:8080/api/detection/record/rknn/report
```

用于框数量告警等检测记录上报。

### 2. 禁入区同步

默认从下面接口拉取禁入区：

```text
http://127.0.0.1:8080/api/rknn/forbidden-area
```

服务会周期性同步：

- 默认拉取间隔：`2000ms`
- 默认超时：`600ms`

当检测框进入禁入区后，会按冷却时间异步上报告警。

## 常见问题

### `bind 端口 8091 失败`

通常是旧进程未退出：

```bash
printf 'orangepi\n' | sudo -S pkill -x rknn_http_ctrl || true
printf 'orangepi\n' | sudo -S pkill -x mediamtx || true
ss -ltnp | grep -E '8091|8554|8889' || true
```

### 摄像头 busy

```bash
printf 'orangepi\n' | sudo -S fuser -v /dev/video0 /dev/video2 || true
v4l2-ctl --list-devices
ls /dev/video*
```

### 抓图返回 JSON 而不是 JPEG

通常还没有拿到有效帧，优先检查：

```bash
curl "$BASE/api/status"
```

重点看：

- `model_loaded`
- `inference_enabled`
- `input_source_cam0`
- `input_source_cam1`

### 切换模型失败

当前实现里，如果模型已经加载并且推理正在运行，需要先：

```bash
curl -X POST "$BASE/api/inference/off?unload=1"
```

然后再重新调用 `/api/inference/on`。

### 看不到融合流 `cam2`

先确认：

- `POST /api/rtsp/start` 已成功
- `GET /api/status` 里 `rtsp_url_mosaic` 已返回
- RTSP 服务端口 `8554` 正常监听
