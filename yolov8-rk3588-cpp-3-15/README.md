# RK3588 视觉 HTTP / RTSP 服务说明

当前视觉主入口是 `src/main_http_ctrl.cc`，运行产物为 `rknn_http_ctrl`。

它负责：

- 双路摄像头采集
- RKNN 推理
- `ByteTrack / DeepSORT` 跟踪
- JPEG 抓帧
- RTSP/HLS/WebRTC 推流
- 摄像头录像
- 视频文件模式
- 检测数量阈值上报
- 禁入区同步与异常闯入上报

Spring Boot 默认通过 `http://localhost:8091` 访问本服务。

## 端口与流地址

| 类型 | 端口 | 说明 |
|---|---:|---|
| HTTP | `8091` | 控制接口 |
| RTSP | `8554` | MediaMTX RTSP |
| HLS | `8888` | MediaMTX HLS |
| WebRTC | `8889` | WHEP |
| ICE | `8189/udp` | WebRTC ICE |

默认流：

- `rtsp://127.0.0.1:8554/cam0`
- `rtsp://127.0.0.1:8554/cam1`
- `rtsp://127.0.0.1:8554/cam2`
- `rtsp://127.0.0.1:8554/cam3`

含义：

- `cam0`：摄像头 1
- `cam1`：摄像头 2
- `cam2`：`cam0 + cam1` 的融合拼接流
- `cam3`：视频文件模式输出

## 目录

```text
yolov8-rk3588-cpp-3-15/
├── src/                 # HTTP 服务与主逻辑
├── include/             # 公共头文件
├── bytetrack/           # ByteTrack
├── deepsort/            # DeepSORT + ReID
├── model/RK3588/        # RKNN 模型
├── recordings/camera/   # 录像默认输出目录
├── video/               # 视频样例
└── build_release/       # 推荐运行目录
```

## 常用模型

- `model/RK3588/coco_person_i8.rknn`
- `model/RK3588/person_2700_i8.rknn`
- `model/RK3588/yolov8s.rknn`
- `model/RK3588/yolov8n.rknn`
- `model/RK3588/osnet_x0_25_market.rknn`
- `model/coco_80_labels_list.txt`
- `model/coco_person_i8.txt`
- `model/person_2700_i8.txt`

## 编译

推荐 `Release`：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15
mkdir -p build_release
cd build_release
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2 rknn_http_ctrl rknn_yolov8_demo
```

## 启动

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/build_release
sudo ./rknn_http_ctrl --cam0-source /dev/video0 --cam1-source /dev/video2
```

说明：

- 推荐从 `build_release` 启动
- 启动时会尝试自动拉起 `mediamtx`
- 模型在第一次调用 `/api/inference/on` 时懒加载

## 主要参数

命令行参数：

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

环境变量：

```bash
export RTSP_HOST=127.0.0.1
export RTSP_PORT=8554
export MEDIAMTX_BIN=/path/to/mediamtx
export MEDIAMTX_LOG=/tmp/mediamtx_auto.log
export MEDIAMTX_AUTO_START=true
export FFMPEG_BIN=/usr/bin/ffmpeg
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

## MediaMTX

两种方式：

### 自动拉起

- 推荐
- 会优先读取当前运行目录下的 `mediamtx.yml`

### 手动启动

```bash
./mediamtx
```

检查：

```bash
ss -ltnup | grep -E '8554|8888|8889|8189'
```

## HTTP API

### 状态

- `GET /api/status`
- `GET /`
- `GET /index.html`
- `POST /api/camera/{0|1|2}`

`/api/status` 当前会返回的关键字段包括：

- `running`
- `inference_enabled`
- `model_loaded`
- `model_loading`
- `model_path`
- `label_path`
- `rtsp_url_cam0`
- `rtsp_url_cam1`
- `rtsp_url_mosaic`
- `rtsp_url_video`
- `record_output_dir`
- `recording_cam0`
- `recording_cam1`
- `tracker_enabled`
- `tracker_backend`
- `detection_count_cam0`
- `detection_count_cam1`
- `threshold`
- `box_count_alert_threshold`
- `forbidden_area_cam0_loaded`
- `forbidden_area_cam1_loaded`
- `video_mode`
- `rtsp_streaming`

### 推理与跟踪

- `POST /api/inference/on`
- `POST /api/inference/off`
- `GET /api/tracker`
- `POST /api/tracker/1`
- `POST /api/tracker/0`

`/api/inference/on` 常用参数：

- `track=0|1`
- `tracker=bytetrack|deepsort`
- `model=/abs/path/to/model.rknn`
- `labels=/abs/path/to/labels.txt`
- `reid_model=/abs/path/to/osnet.rknn`
- `tracker_skip=2`

说明：

- 如果模型已经加载，切换模型或跟踪算法前应先调用 `/api/inference/off`
- 不显式传 `track=0` 时，推理通常会带跟踪

### 阈值与计数

- `POST /api/threshold/set?value=0.6&box_count=3`
- `GET /api/threshold/get`
- `GET /api/detection/count?cam=0|1`

### 抓帧

- `GET /api/frame?cam=0`

支持参数：

- `cam=0|1`
- `cameraId=1|2`
- `track=0|1`
- `redraw=1`

### 推流

- `POST /api/rtsp/start`
- `POST /api/rtsp/video/start`
- `POST /api/rtsp/stop`

说明：

- `/api/rtsp/start` 会尝试同时推 `cam0`、`cam1`、`cam2`
- `cam2` 主要用于展示，不参与当前录像入口

### 录像

- `GET /api/record/status`
- `POST /api/record/start?cam=0|1`
- `POST /api/record/stop?cam=0|1`
- `POST /api/record/stop`

说明：

- 直连视觉服务时，录像参数使用 `cam`
- `cam0`、`cam1` 才能录像
- 默认输出目录是 `recordings/camera`

### 视频模式

- `GET /api/video/status`
- `POST /api/video/start`
- `POST /api/video/stop`

请求体示例：

```json
{
  "path": "/abs/path/to/video.mp4",
  "loop": true
}
```

## 推荐联调顺序

```bash
BASE=http://127.0.0.1:8091
curl "$BASE/api/status"
curl -X POST "$BASE/api/inference/off?unload=1"
curl -X POST "$BASE/api/inference/on?track=1&tracker=bytetrack"
curl -X POST "$BASE/api/rtsp/start"
curl "$BASE/api/frame?cam=0" -o cam0.jpg
```

录像测试：

```bash
curl "$BASE/api/record/status"
curl -X POST "$BASE/api/record/start?cam=0"
curl -X POST "$BASE/api/record/stop?cam=0"
```

## 与 Spring Boot 的联动

当前代码内建两类上游交互：

### 1. 检测记录上报

默认上报到：

```text
http://127.0.0.1:8080/api/detection/record/rknn/report
```

### 2. 禁入区同步

默认从：

```text
http://127.0.0.1:8080/api/rknn/forbidden-area
```

拉取摄像头 1、2 的四边形区域配置。

## 自检

```bash
curl http://127.0.0.1:8091/api/status
curl http://127.0.0.1:8091/api/threshold/get
curl http://127.0.0.1:8091/api/detection/count?cam=0
ss -ltnup | grep -E '8091|8554|8888|8889|8189'
```
