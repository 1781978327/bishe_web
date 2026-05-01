# Sound_Monitoring HTTP 服务说明

以下说明已按当前源码核对，真实 HTTP 入口是 `src/main_http.cc`，运行产物是 `rknn_yamnet_demo_http`，默认端口 `8089`。

这个服务当前承担：

- 音频文件异常检测
- 上传音频自动转码后检测
- 实时麦克风监测
- 实时异常事件与实时窗口缓存
- 可选的 Vosk ASR 转写
- 紧急关键词唤醒监测
- 声音异常与紧急关键词事件自动上报 Spring Boot

## 源码结构

```text
Sound_Monitoring/src/
├── main_http.cc                  # 服务入口（init_model + main）
├── CMakeLists.txt                # CMake 构建配置
├── sound_globals.h/.cpp          # 全局变量、常量、事件结构体
├── sound_http_handlers.h/.cpp    # 路由处理函数 + 分发器 + 服务器线程
├── sound_http_utils.h/.cpp       # JSON 转义、查询解析、HTTP 响应、信号处理
├── sound_rt_monitor.h/.cpp       # 实时 YAMNet 推理线程、rt_start/stop
├── sound_offline.h/.cpp          # 离线分析、异常事件管理、multipart 解析
├── sound_denoise.h/.cpp          # FFmpeg 滤波 + SoX 降噪
├── sound_event_audio.h/.cpp      # 事件音频存储、环形缓冲区
├── sound_kws.h/.cpp              # 紧急关键词进程管理、上报线程
├── sound_report.h/.cpp           # Spring Boot 自动上报
├── sound_audio_config.h/.cpp     # YAML 配置解析、Pulse/ALSA 设备管理
├── process.h                     # YAMNet 推理接口
├── asr_vosk.h                    # Vosk ASR 接口
├── audio_capture.h               # 音频采集接口
└── yamnet.h                      # YAMNet 模型与标签
```

## 目录

```text
Sound_Monitoring/
├── src/                 # HTTP 服务源码（模块化）
├── utils/               # 音频/文件工具
├── model/               # YAMNet 模型与标签
├── wake/                # 紧急关键词唤醒程序
├── config/              # runtime_audio.yaml
├── scripts/             # 构建脚本
├── build/               # scripts/build.sh 默认输出目录
└── 3rdparty/            # RKNN、FFTW、libsndfile 等依赖
```

## 编译

推荐方式：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring
./scripts/build.sh
```

默认产物：

- `src/build/rknn_yamnet_demo_http`
- `src/build/lib/`（符号链接到 `build/lib/`）
- `src/build/model/`（符号链接到 `build/model/`）
- `src/build/config/`（符号链接到 `build/config/`）
- `src/build/wake/`（符号链接到 `build/wake/`）
- `build/wake/emergency_monitor`（检测到依赖时会自动构建）

手动 CMake 构建：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/src
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

补充：

- `scripts/build.sh` 默认输出到 `Sound_Monitoring/build`
- 根目录 `start_all_stack.sh` 会优先尝试 `Sound_Monitoring/src/build`，不存在时自动回退到 `Sound_Monitoring/build`
- `src/build/` 下的 `lib/`、`model/`、`config/`、`wake/` 为符号链接，指向 `build/` 中的运行时依赖
- 如果自动构建唤醒程序失败，可以单独执行 `bash ./scripts/build_wake_monitor.sh`

## 启动

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/src/build
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
sudo ./rknn_yamnet_demo_http 8089
```

说明：

- 程序第一个启动参数是监听端口；不传时默认 `8089`
- 默认模型：`./model/yamnet.rknn`
- 默认标签：`./model/yamnet_class_map.txt`
- 默认实时输入：`parec`
- 默认 `AUTO_START_REALTIME=1`，启动后会尝试自动开启实时监测
- 默认 `ENABLE_VOSK_ASR=0`，转写接口在未启用 ASR 时会返回提示
- 默认 `EMERGENCY_KWS_AUTO_START=0`，紧急关键词唤醒不会自动启动

## 运行时配置

### YAML 配置文件查找顺序

程序会按下面顺序查找音频配置：

1. 环境变量 `SOUND_MONITORING_CONFIG`
2. `./config/runtime_audio.yaml`
3. `../config/runtime_audio.yaml`
4. `../../config/runtime_audio.yaml`

默认仓库文件在：

- `Sound_Monitoring/config/runtime_audio.yaml`

执行 `./scripts/build.sh` 后会复制到：

- `Sound_Monitoring/build/config/runtime_audio.yaml`

### 常用环境变量

```bash
export SOUND_MONITORING_CONFIG=/path/to/runtime_audio.yaml
export AUTO_START_REALTIME=0
export ENABLE_VOSK_ASR=1
export VOSK_MODEL_CN=/path/to/vosk-model-small-cn-0.22
export VOSK_MODEL_EN=/path/to/vosk-model-small-en-us-0.15
export VOSK_LIB_PATH=/path/to/libvosk.so

export RT_PRINT_ASR=1
export RT_PRINT_WINDOW=1
export RT_CAPTURE_VOLUME=1.0
export RT_CAPTURE_VOLUME_PERCENT=100
export RT_AMIXER_ENABLED=0
export RT_AMIXER_CARD=5
export RT_AMIXER_CONTROL=Mic
export RT_AMIXER_VOLUME=60%
export RT_AMIXER_AUTO_GAIN_CONTROL=off

export RT_FFMPEG_FILTER_ENABLED=0
export RT_FFMPEG_BIN=ffmpeg
export SOX_DENOISE_ENABLED=0
export SOX_DENOISE_PROFILE=/path/to/noise.prof
export SOX_DENOISE_AMOUNT=0.25
export SOX_BIN=sox

export EMERGENCY_KWS_AUTO_START=0
export EMERGENCY_KWS_REPORT_ENABLED=1
export EMERGENCY_KWS_CMD=./wake/emergency_monitor
export EMERGENCY_KWS_WORKDIR=./wake
export EMERGENCY_KWS_LOG_PATH=./wake/emergency_log.txt
export EMERGENCY_KWS_PID_FILE=/tmp/emergency_monitor.pid
export EMERGENCY_KWS_STDOUT_PATH=/tmp/emergency_monitor.out
export EMERGENCY_KWS_MODEL_DIR=/path/to/sherpa-onnx-kws-model
```

代码事实：

- `RT_PRINT_ASR` 当前默认是开启的
- `RT_PRINT_WINDOW` 当前默认是开启的
- `RT_CAPTURE_VOLUME` 取值 `0.0~2.0`，若传 `60` 这类百分数也会被换算
- `RT_CAPTURE_VOLUME_PERCENT` 也是软件增益入口（`0~200`），会覆盖到 `RT_CAPTURE_VOLUME`
- `RT_AMIXER_AUTO_GAIN_CONTROL` 可在启动实时前下发自动增益控制开关（例如 `off`）
- `EMERGENCY_KWS_PID_FILE` / `EMERGENCY_KWS_STDOUT_PATH` 可重定向唤醒子进程 PID 与输出文件位置
- FFmpeg / SoX 没装好时，服务会跳过相关滤波能力，但不会阻止主程序启动

## HTTP API

### 核心接口

| 方法 | 路径 | 说明 |
|---|---|---|
| `POST` | `/analyze` | 按本地文件绝对路径分析音频 |
| `POST` | `/analyze/upload` | 上传音频文件或原始音频二进制分析 |
| `GET` | `/health` | 健康检查 |
| `GET` | `/config` | 读取当前配置快照 |

说明：

- 当前代码只有 `GET /config`
- 当前代码没有 `PUT /config`

### 实时监测接口

| 方法 | 路径 | 说明 |
|---|---|---|
| `POST` | `/realtime/start` | 启动实时麦克风监测，可选 JSON `{"device":"parec"}` |
| `POST` | `/realtime/stop` | 停止实时监测 |
| `GET` | `/realtime/status` | 返回当前实时状态、设备、采样率、增益与滤波配置 |
| `GET` | `/realtime/events` | 返回异常事件队列 |
| `GET` | `/realtime/windows?limit=5` | 返回最近检测窗口，不限于异常 |
| `GET` | `/realtime/transcript?seconds=120&save_audio=1` | 返回最近音频转写；`save_audio=1` 时会导出 WAV |

### 紧急关键词接口

| 方法 | 路径 | 说明 |
|---|---|---|
| `POST` | `/wake/start` | 启动紧急关键词唤醒 |
| `POST` | `/wake/stop` | 停止紧急关键词唤醒 |
| `GET` | `/wake/status` | 查看唤醒程序、PID、日志路径、自动启动状态 |
| `GET` | `/wake/events?limit=20` | 读取最近唤醒日志 |

## 返回结果示例

### `GET /health`

```json
{
  "status": "ok",
  "service": "sound-server",
  "model": "yamnet"
}
```

### `GET /realtime/status`

返回里常见字段：

- `running`
- `device`
- `sample_rate`
- `capture_volume`
- `mixer_enabled`
- `mixer_card`
- `mixer_control`
- `mixer_volume`
- `ffmpeg_filter_enabled`
- `ffmpeg_bin`
- `ffmpeg_audio_filter`

### `GET /config`

返回里常见字段：

- `model_path`
- `anomaly_threshold`
- `save_anomaly`
- `asr_enabled`
- `asr_model_cn`
- `asr_model_en`
- `runtime_audio_config_path`
- `rt_default_device`
- `sox_denoise_enabled`
- `rt_ffmpeg_filter_enabled`
- `rt_capture_volume`
- `rt_amixer_enabled`
- `wake_running`
- `wake_auto_start`
- `wake_report_enabled`
- `wake_command`
- `wake_workdir`
- `wake_log_path`

## 运行时产物

相对当前工作目录，程序会生成或使用这些目录：

- `./alarm_audio/`
  - 实时异常事件音频
- `./anomaly_audio/`
  - `/analyze` 与 `/analyze/upload` 保存的异常片段
- `./debug_audio/`
  - `/realtime/transcript?save_audio=1` 导出的音频
- `/tmp/yamnet_upload_*`
  - 上传接口原始临时文件
- `/tmp/yamnet_convert_*.wav`
  - 转码后的临时 WAV
- `/tmp/yamnet_sox_*`
  - SoX 处理中间文件

## 与 Spring Boot 的联动

当前源码默认把声音异常上报到：

- `http://localhost:8080/api/detection/record/sound/report`

联动行为：

- 声音异常会通过 HTTP 上报 Spring Boot
- 紧急关键词触发后，如果 `EMERGENCY_KWS_REPORT_ENABLED=1`，也会自动上报
- Spring Boot 收到后会落库 `DetectionRecord`，并进入 AI 融合分析链路

## 自检

```bash
curl http://127.0.0.1:8089/health
curl http://127.0.0.1:8089/config
curl http://127.0.0.1:8089/realtime/status
curl http://127.0.0.1:8089/realtime/windows?limit=5
curl http://127.0.0.1:8089/wake/status
```

文件分析示例：

```bash
curl -X POST http://127.0.0.1:8089/analyze \
  -H "Content-Type: application/json" \
  -d '{"audio_path":"/absolute/path/to/test.wav"}'
```

上传分析示例：

```bash
curl -X POST http://127.0.0.1:8089/analyze/upload \
  -F 'audio=@/absolute/path/to/test.mp3'
```

实时启动示例：

```bash
curl -X POST http://127.0.0.1:8089/realtime/start \
  -H "Content-Type: application/json" \
  -d '{"device":"parec"}'
```
