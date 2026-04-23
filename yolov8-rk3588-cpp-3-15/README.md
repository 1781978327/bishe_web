# RK3588 视觉 HTTP / RTSP 服务说明

以下说明已按当前源码核对，真实 HTTP 入口是 `src/main_http_ctrl.cc`，运行产物是 `rknn_http_ctrl`，默认 HTTP 端口 `8091`。

它当前负责：

- 双路摄像头采集
- RKNN 目标检测
- `ByteTrack / DeepSORT` 跟踪
- JPEG 抓帧
- 摄像头录像
- 视频文件模式
- 检测数量阈值上报
- 禁入区同步与闯入上报
- RTSP / HLS / WebRTC 推流（依赖 MediaMTX）

Spring Boot 默认通过 `http://localhost:8091` 访问本服务。

## 目录

```text
yolov8-rk3588-cpp-3-15/
├── src/                 # HTTP 服务与主逻辑
├── include/             # 公共头文件
├── bytetrack/           # ByteTrack
├── deepsort/            # DeepSORT + ReID
├── model/RK3588/        # RKNN 模型
├── recordings/camera/   # 录像默认输出目录
├── build_release/       # 推荐运行目录
└── mediamtx.yml         # MediaMTX 模板配置
```

## 端口与流地址

| 类型 | 端口 | 说明 |
|---|---:|---|
| HTTP | `8091` | 控制接口 |
| RTSP | `8554` | MediaMTX RTSP |
| HLS | `8888` | MediaMTX HLS |
| WebRTC | `8889` | WHEP |
| ICE | `8189/udp` | WebRTC ICE |

默认流地址：

- `rtsp://127.0.0.1:8554/cam0`
- `rtsp://127.0.0.1:8554/cam1`
- `rtsp://127.0.0.1:8554/cam2`
- `rtsp://127.0.0.1:8554/cam3`

含义：

- `cam0`：摄像头 1
- `cam1`：摄像头 2
- `cam2`：双路拼接流
- `cam3`：视频文件模式流

## 编译

推荐 `Release`：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15
mkdir -p build_release
cd build_release
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2 rknn_http_ctrl rknn_yolov8_demo
```

说明：

- `RTSP / HLS / WebRTC` 能力依赖 CMake 是否启用了 `USE_RTSP_MPP`
- 当前 `CMakeLists.txt` 会优先查找 FFmpeg RKMPP 相关库
- 如果没有编译出 `USE_RTSP_MPP`，核心 HTTP 控制接口仍可用，但 `/api/rtsp/*` 能力不会完整启用

## 启动

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/build_release
sudo ./rknn_http_ctrl --cam0-source /dev/video0 --cam1-source /dev/video2
```

说明：

- 推荐从 `build_release` 启动
- 程序会在第一次调用 `POST /api/inference/on` 时懒加载模型
- 输入源支持设备路径、数字索引、RTSP 地址或视频文件路径
- 根目录 `start_all_stack.sh` 会先把仓库根目录的 `mediamtx.yml` 复制到 `build_release/mediamtx.yml`

## 常用命令行参数

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

## 常用环境变量

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

## HTTP API

### 状态与基础页面

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/api/status` | 返回当前运行状态与主要参数 |
| `GET` | `/` | 内置 HTML 调试页 |
| `GET` | `/index.html` | 同上 |
| `POST` | `/api/camera/{0|1|2}` | 切换当前摄像头视图 |

`/api/status` 当前常见字段包括：

- `running`
- `inference_enabled`
- `model_loaded`
- `model_loading`
- `model_switching`
- `model_path`
- `label_path`
- `rtsp_url_cam0`
- `rtsp_url_cam1`
- `rtsp_url_mosaic`
- `rtsp_url_video`
- `input_source_cam0`
- `input_source_cam1`
- `mediamtx_auto_start`
- `mediamtx_running`
- `record_output_dir`
- `recording_cam0`
- `recording_cam1`
- `tracker_enabled`
- `tracker_backend`
- `tracker_reid_model`
- `tracker_skip_frames`
- `detection_count_cam0`
- `detection_count_cam1`
- `threshold`
- `box_count_alert_threshold`
- `forbidden_area_cam0_loaded`
- `forbidden_area_cam1_loaded`
- `video_mode`
- `rtsp_streaming`

### 推理与跟踪

| 方法 | 路径 | 说明 |
|---|---|---|
| `POST` | `/api/inference/on` | 开启推理，可附加模型、标签、跟踪参数 |
| `POST` | `/api/inference/off` | 关闭推理 |
| `GET` | `/api/tracker` | 查询跟踪状态 |
| `POST` | `/api/tracker/1` | 开启跟踪 |
| `POST` | `/api/tracker/0` | 关闭跟踪 |

`POST /api/inference/on` 常用参数：

- `track=0|1`
- `tracker=bytetrack|deepsort`
- `model=/abs/path/to/model.rknn`
- `labels=/abs/path/to/labels.txt`
- `reid_model=/abs/path/to/osnet.rknn`
- `tracker_skip=2`

说明：

- 不显式传 `track=0` 时，当前逻辑默认会启用跟踪
- 想切换模型、标签、跟踪算法时，最好先调用 `POST /api/inference/off`
- 使用 `deepsort` 时，如果没有可用的 ReID 模型，会直接返回错误

### 阈值、计数、抓帧

| 方法 | 路径 | 说明 |
|---|---|---|
| `POST` | `/api/threshold/set?value=0.6&box_count=3` | 设置置信度阈值和数量阈值 |
| `GET` | `/api/threshold/get` | 读取当前阈值 |
| `GET` | `/api/detection/count?cam=0|1` | 读取指定摄像头检测数量 |
| `GET` | `/api/frame?cam=0` | 获取当前 JPEG 帧 |

`/api/frame` 支持：

- `cam=0|1`
- `cameraId=1|2`
- `track=0|1`
- `redraw=1`

说明：

- `cameraId=1` 会映射成 `cam=0`
- `cameraId=2` 会映射成 `cam=1`
- 默认不会二次重绘跟踪框，避免出现“一人双框”

### RTSP / HLS / WebRTC 推流

| 方法 | 路径 | 说明 |
|---|---|---|
| `POST` | `/api/rtsp/start` | 启动双摄像头与拼接流 |
| `POST` | `/api/rtsp/video/start` | 启动视频文件模式推流 |
| `POST` | `/api/rtsp/stop` | 停止推流 |

补充：

- 这些接口依赖 `USE_RTSP_MPP`
- `POST /api/rtsp/start` 会尝试同时启动 `cam0`、`cam1`、`cam2`
- 当模型未加载时，也可以推裸流

### 录像

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/api/record/status` | 查询录像状态 |
| `POST` | `/api/record/start?cam=0|1` | 开始指定摄像头录像 |
| `POST` | `/api/record/stop?cam=0|1` | 停止指定摄像头录像 |
| `POST` | `/api/record/stop` | 停止所有摄像头录像 |

代码事实：

- 当前只支持 `cam0` 与 `cam1` 录像
- `/api/record/start` 与 `/api/record/stop` 除了 `cam` 外，也兼容 `camera=cam0|cam1`、`cameraId=1|2`
- 录像默认输出到 `recordings/camera`
- `/api/record/status` 会返回录像文件路径、日志路径、启动时间与错误信息

### 视频文件模式

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/api/video/status` | 查询当前视频模式状态 |
| `POST` | `/api/video/start` | 启动视频文件模式 |
| `POST` | `/api/video/stop` | 停止视频文件模式并恢复摄像头 |

`POST /api/video/start` 请求体示例：

```json
{
  "path": "/absolute/path/to/demo.mp4",
  "loop": true
}
```

## 告警上报与禁入区

当前源码里还有两条自动联动链路：

- 检测数量超过阈值时，会按 `REPORT_SERVER_PATH` 上报到 Spring Boot
- 禁入区轮询 `FORBIDDEN_AREA_PATH`，首次检测到闯入时会上报 `env_intrusion`

补充：

- 上报体里会附带 `cameraId`
- 禁入区闯入上报会尽量附带 `imageBase64`
- Spring Boot 收到后会落库并进入 AI 融合分析

## 自检

```bash
BASE=http://127.0.0.1:8091
curl "$BASE/api/status"
curl "$BASE/api/threshold/get"
curl "$BASE/api/detection/count?cam=0"
curl "$BASE/api/frame?cameraId=1"
```

打开推理：

```bash
curl -X POST "$BASE/api/inference/on?track=1&tracker=bytetrack"
```

打开推流：

```bash
curl -X POST "$BASE/api/rtsp/start"
```

查看 MediaMTX 端口：

```bash
ss -ltnup | grep -E '8554|8888|8889|8189'
```
