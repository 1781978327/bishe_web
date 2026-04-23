# Sound_Monitoring HTTP 服务说明

cat /sys/bus/usb/devices/7-1/power/control
cat /sys/bus/usb/devices/7-1/power/runtime_status
cat /sys/bus/usb/devices/7-1/power/autosuspend
当前usb麦克风已经设置取消省电模式

当前仓库里声音主链路使用 `src/main_http.cc`，运行产物是 `rknn_yamnet_demo_http`，默认端口 `8089`。

这个服务主要提供：

- 音频文件异常检测
- 上传音频自动转码后检测
- 实时麦克风监测
- 实时异常事件缓存
- 默认禁用、按需开启的 Vosk ASR 转写
- 内置 C++ 紧急关键词唤醒（`wake/emergency_monitor.cpp`）
- 紧急关键词触发后自动保存最近 6 秒实时音频、上报 Spring Boot 安全记录并触发 AI 融合分析
- 自动上报 Spring Boot 安全记录

## 目录

```text
Sound_Monitoring/
├── src/                 # 主程序与 HTTP 入口
├── utils/               # 音频/图像/文件工具
├── model/               # YAMNet 模型与标签
├── wake/                # C++ 紧急关键词唤醒源码
├── 3rdparty/            # RKNN、FFTW、libsndfile 等依赖
├── scripts/             # 构建脚本
├── build/               # scripts/build.sh 默认输出目录
└── uploads/             # Spring Boot 上传音频的落盘目录
```

## 编译

推荐：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring
./scripts/build.sh
```

默认产物：

- `build/rknn_yamnet_demo_http`
- `build/lib/`
- `build/model/`
- `build/wake/emergency_monitor`（若检测到 sherpa-onnx C API 安装产物会自动构建）

也可以手动构建：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/build
cmake ../src
make -j$(nproc)
```

说明：

- `scripts/build.sh` 产物默认在 `Sound_Monitoring/build`
- 根目录 `start_all_stack.sh` 优先尝试 `Sound_Monitoring/src/build`，若目录不同可手动设置 `SOUND_DIR`
- 唤醒程序会优先输出到 `build/wake/emergency_monitor`
- 如果自动构建失败，可单独执行：`bash ./scripts/build_wake_monitor.sh`

## 启动

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/build
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
sudo ./rknn_yamnet_demo_http 8089
```

默认行为：

- 端口：`8089`
- 默认模型：`./model/yamnet.rknn`
- 默认标签：`./model/yamnet_class_map.txt`
- 默认实时设备：`parec`
- `parec` 会直接跟随 Pulse 当前默认麦克风
- 如果默认信源异常，会回退到：`parec:alsa_input.usb-Web_Camera_Web_Camera_202409021440-02.mono-fallback.2`
- 如未关闭自动启动，会在服务启动后尝试自动开启实时监测
- 默认不做额外滤波，直接使用 `parec` 输入
- 实时录音默认不主动改硬件 Mic 增益，直接沿用系统当前设置
- 软件增益默认保持 `1.0`

## YAML 音频配置

HTTP 服务启动时会优先读取音频配置文件：

- 仓库内默认文件：`Sound_Monitoring/config/runtime_audio.yaml`
- 执行 `./scripts/build.sh` 后会复制到：`Sound_Monitoring/build/config/runtime_audio.yaml`
- 如果你从 `src/build` 启动，程序也会自动尝试向上查找这个文件
- 也可以通过环境变量 `SOUND_MONITORING_CONFIG=/path/to/runtime_audio.yaml` 指定

当前默认 YAML 就是“只用 `parec`、不改硬件、不走 FFmpeg、不走 SoX”：

```yaml
realtime:
  # 上报 Spring Boot 的最小置信度阈值（默认 0.15）
  device: "parec"
  fallback_device: "parec:alsa_input.usb-Web_Camera_Web_Camera_202409021440-02.mono-fallback.2"
  auto_start: true
  capture_volume: 1.0

  mixer:
    enabled: false

  filter:
    ffmpeg:
      enabled: false
    sox:
      enabled: false
```

最常改的就是这些键：

- `realtime.device`：实时输入设备，设成 `parec` 就是跟随系统默认麦克风
- `realtime.mixer.enabled`：是否启动前执行 `amixer`
- `realtime.filter.ffmpeg.enabled`：是否开启 FFmpeg 实时滤波
- `realtime.filter.sox.enabled`：是否开启 SoX 降噪
- `realtime.capture_volume`：软件录音增益，`1.0` 表示不额外放大

## 为什么这里默认使用 parec

在这台机器上，实时录音默认改成 `parec`，而不是直接用 `arecord` 访问 ALSA 原始设备，主要是因为实际测试下来 `parec` 这条链路更稳定，声音更大、底噪更小。

- `parec` 直接跟随 PulseAudio 当前默认麦克风，切换系统默认输入后，服务会自动录到同一个麦克风，不容易录错设备。
- 当前系统默认麦克风已经设置为第二个摄像头麦克风，这个麦在现场测试里比其他输入更干净，信噪比更好。
- `parec` 走的是 Pulse 当前已经生效的输入路由、采样率和源音量配置，更接近桌面系统里“正在实际使用”的那条录音链路。
- `arecord` 更底层，通常需要手动指定 `hw:x,y` 或 `plughw:*`。一旦卡号、设备号或通道选错，就可能录到更吵的那个输入，或者录到没有经过当前默认路由的原始设备。
- 目前这套配置里，硬件 Mic 增益设为 `60%`，软件录音增益保持 `1.0`，人声足够大，同时没有把底噪一起过度放大。

需要注意：

- 这里效果变好，并不是因为 `parec` 自带了神奇降噪。
- 更关键的原因是：选对了麦克风、走对了默认输入链路，并且把硬件增益和软件增益配到了一个更合适的组合。

## 环境变量

```bash
export SOUND_MONITORING_CONFIG=/path/to/runtime_audio.yaml
export AUTO_START_REALTIME=0
export ENABLE_VOSK_ASR=0
export EMERGENCY_KWS_AUTO_START=0
export EMERGENCY_KWS_REPORT_ENABLED=1
export EMERGENCY_KWS_CMD=./wake/emergency_monitor
export EMERGENCY_KWS_WORKDIR=./wake
export EMERGENCY_KWS_LOG_PATH=./wake/emergency_log.txt
export EMERGENCY_KWS_MODEL_DIR=/path/to/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01
export RT_PRINT_ASR=0
export VOSK_MODEL_CN=/path/to/vosk-model-small-cn-0.22
export VOSK_MODEL_EN=/path/to/vosk-model-small-en-us-0.15
export VOSK_LIB_PATH=/path/to/libvosk.so
export RT_AMIXER_ENABLED=0
export RT_AMIXER_CARD=5
export RT_AMIXER_CONTROL=Mic
export RT_AMIXER_VOLUME=60%
export RT_CAPTURE_VOLUME=1.0
export RT_REPORT_MIN_CONFIDENCE=0.15  # 上报 Spring Boot 的置信度阈值
export RT_FFMPEG_FILTER_ENABLED=0
export SOX_DENOISE_ENABLED=0
export SOX_DENOISE_PROFILE=/home/orangepi/Desktop/web/bishebeifen-master/speech_camera2_80.prof
export SOX_DENOISE_AMOUNT=0.25
export SOX_BIN=sox
```

说明：

- `SOUND_MONITORING_CONFIG`：显式指定 `runtime_audio.yaml` 路径
- `AUTO_START_REALTIME=0`：禁止启动时自动开启实时监测
- `ENABLE_VOSK_ASR=0|1`：是否启用 Vosk 转写，默认 `0`
- `EMERGENCY_KWS_AUTO_START=0|1`：是否自动启动紧急关键词唤醒，默认 `0`
- `EMERGENCY_KWS_REPORT_ENABLED=0|1`：紧急关键词触发后是否自动保存最近 6 秒实时音频、上报安全记录并触发后端 AI 融合分析，默认 `1`
- `EMERGENCY_KWS_CMD`：紧急关键词 C++ 可执行文件路径
- `EMERGENCY_KWS_WORKDIR`：紧急关键词程序工作目录
- `EMERGENCY_KWS_LOG_PATH`：紧急关键词日志文件路径
- `EMERGENCY_KWS_MODEL_DIR`：唤醒模型目录（不设时程序会按内置候选路径自动查找）
- 如果不显式设置 `EMERGENCY_KWS_CMD`，服务会优先尝试 `./wake/emergency_monitor`，再回退历史路径
- `RT_PRINT_ASR=0`：关闭实时模式的周期性转写日志
- `RT_AMIXER_ENABLED=0|1`：是否在启动实时监测前执行 `amixer`
- `RT_AMIXER_CARD=5`、`RT_AMIXER_CONTROL=Mic`、`RT_AMIXER_VOLUME=60%`：例如对应 `amixer -c 5 sset Mic 60%`
- `RT_CAPTURE_VOLUME=1.0`：软件增益，默认不额外放大或缩小
- `RT_FFMPEG_FILTER_ENABLED=0|1`：显式关闭或开启 FFmpeg 实时滤波
- `VOSK_MODEL_CN`、`VOSK_MODEL_EN`：在 `ENABLE_VOSK_ASR=1` 时指定中文/英文模型目录
- `VOSK_LIB_PATH`：显式指定 `libvosk.so`
- `SOX_DENOISE_ENABLED=0|1`：显式关闭或开启 SoX 降噪
- `SOX_DENOISE_PROFILE`：噪声 profile 文件路径
- `SOX_DENOISE_AMOUNT`：`noisered` 强度，当前默认 `0.25`
- `SOX_BIN`：`sox` 可执行文件路径

当前代码行为：

- 默认情况下只用 `parec` 采音，不额外做 FFmpeg 或 SoX 滤波
- 如果在 YAML 或环境变量里开启 `RT_FFMPEG_FILTER_ENABLED=1`，实时窗口和实时事件音频会先走 FFmpeg 滤波
- 如果开启 `SOX_DENOISE_ENABLED=1`，`POST /analyze`、`POST /analyze/upload` 以及实时链路里的 SoX fallback 才会启用
- 如果滤波工具没装好或者 profile 不存在，会自动跳过，不影响服务启动

## 运行时文件

相对当前工作目录会生成：

- `./alarm_audio/`
  - 实时异常事件保存音频
- `./anomaly_audio/`
  - `/analyze` 或 `/analyze/upload` 检出的异常片段
- `./debug_audio/`
  - `/realtime/transcript?save_audio=1` 导出的转写音频
- `/tmp/yamnet_upload_*`
  - 上传接口原始临时文件
- `/tmp/yamnet_convert_*.wav`
  - 转码后临时 WAV
- `/tmp/yamnet_sox_in_*.wav`
  - SoX 降噪临时输入 WAV
- `/tmp/yamnet_sox_out_*.wav`
  - SoX 降噪临时输出 WAV

## HTTP API

### `POST /analyze`

按本地路径分析音频：

```bash
curl -X POST http://127.0.0.1:8089/analyze \
  -H "Content-Type: application/json" \
  -d '{"audio_path":"/absolute/path/to/audio.wav"}'
```

返回里常见字段：

- `success`
- `audio_path`
- `duration`
- `total_chunks`
- `anomaly_count`
- `anomaly_saved`
- `transcript`
- `asr_lang`
- `asr_confidence`
- `events`

### `POST /analyze/upload`

支持 `multipart/form-data` 或原始二进制：

```bash
curl -X POST http://127.0.0.1:8089/analyze/upload \
  -F 'audio=@/path/to/audio.mp3'
```

或：

```bash
curl -X POST http://127.0.0.1:8089/analyze/upload \
  -H "Content-Type: audio/mpeg" \
  --data-binary @/path/to/audio.mp3
```

说明：

- `multipart` 字段名必须是 `audio`
- 服务内部会尝试用 `ffmpeg` 转为标准 WAV

### `POST /realtime/start`

启动实时麦克风监测：

```bash
curl -X POST http://127.0.0.1:8089/realtime/start \
  -H "Content-Type: application/json" \
  -d '{"device":"parec"}'
```

### `POST /realtime/stop`

```bash
curl -X POST http://127.0.0.1:8089/realtime/stop
```

### `GET /realtime/status`

返回：

```json
{
  "running": true,
  "device": "parec",
  "sample_rate": 16000
}
```

### `GET /realtime/windows?limit=5`

返回最近 N 个实时检测窗口状态，不只包含异常窗口。

```json
{
  "success": true,
  "count": 15,
  "returned": 5,
  "limit": 5,
  "windows": [
    {
      "id": 15,
      "start": 21.0,
      "end": 24.0,
      "duration": 3.0,
      "anomaly": false,
      "matched_keyword": "",
      "matched_score": 0.0,
      "top_summary": "White noise(0.45), Noise(0.27), Mechanical fan(0.22)",
      "timestamp": "2026-04-18 10:52:42"
    }
  ]
}
```

### `GET /realtime/events`

返回事件队列：

```json
{
  "success": true,
  "count": 2,
  "events": [
    {
      "id": 1,
      "start": 0.0,
      "end": 3.0,
      "duration": 3.0,
      "confidence": 0.82,
      "timestamp": "2026-04-15 12:00:00",
      "keywords": ["Scream", "Crying"]
    }
  ]
}
```

### `GET /realtime/transcript`

```bash
export ENABLE_VOSK_ASR=1
curl "http://127.0.0.1:8089/realtime/transcript?seconds=120"
curl "http://127.0.0.1:8089/realtime/transcript?seconds=30&save_audio=1"
```

说明：

- `seconds` 上限是 `120`
- `save_audio=1` 时，音频会保存到 `./debug_audio/`
- 如果未启用 ASR，会返回 `success=false` 和 `ASR disabled`
- 要启用转写，需要先设置 `ENABLE_VOSK_ASR=1`

### `POST /wake/start`

启动紧急关键词唤醒监测（启动 C++ `emergency_monitor` 进程）：

```bash
curl -X POST http://127.0.0.1:8089/wake/start
```

### `POST /wake/stop`

```bash
curl -X POST http://127.0.0.1:8089/wake/stop
```

### `GET /wake/status`

```bash
curl http://127.0.0.1:8089/wake/status
```

### `GET /wake/events`

读取关键词唤醒日志（默认 20 条）：

```bash
curl "http://127.0.0.1:8089/wake/events?limit=20"
```

### `GET /health`

```json
{
  "status": "ok",
  "service": "sound-server",
  "model": "yamnet"
}
```

### `GET /config`

返回只读配置：

```json
{
  "model_path": "./model/yamnet.rknn",
  "anomaly_threshold": 0.10,
  "save_anomaly": 0,
  "keywords_count": 521,
  "asr_enabled": false,
  "asr_model_cn": "...",
  "asr_model_en": "...",
  "sox_denoise_enabled": true,
  "sox_bin": "sox",
  "sox_denoise_profile": "/home/orangepi/Desktop/web/bishebeifen-master/speech_camera2_80.prof",
  "sox_denoise_amount": 0.25,
  "rt_capture_volume": 1.00,
  "rt_amixer_enabled": true,
  "rt_amixer_card": "1",
  "rt_amixer_control": "Mic",
  "rt_amixer_volume": "80%",
  "wake_enabled": true,
  "wake_running": false,
  "wake_pid": -1,
  "wake_auto_start": false,
  "wake_command": "./wake/emergency_monitor",
  "wake_workdir": "./wake",
  "wake_log_path": "./wake/emergency_log.txt"
}
```

说明：

- 代码注释里仍提到 `PUT /config`
- 但当前真正注册的只有 `GET /config`

## 与 Spring Boot 的联动

Spring Boot 默认通过 `http://localhost:8089` 调用本服务，对应配置在：

- `web-springboot/demo3/demo/src/main/resources/application.properties`

另外，声音服务检测到异常后，会主动上报：

```text
http://localhost:8080/api/detection/record/sound/report
```

上报内容包含：

- `cameraId`
- `cameraName`
- `detectionTime`
- `detectionResult`
- `audioUrl`
- `audioDuration`
- `soundKeywords`

## 自检

```bash
curl http://127.0.0.1:8089/health
curl http://127.0.0.1:8089/config
curl http://127.0.0.1:8089/realtime/status
curl http://127.0.0.1:8089/realtime/windows?limit=5
```

如果你是通过根目录脚本启动：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master
./start_all_stack.sh
```

当前脚本会优先把下面这个 profile 传给声音服务：

```text
/home/orangepi/Desktop/web/bishebeifen-master/speech_camera2_80.prof
```

如果这个文件不存在，则会回退尝试：

```text
/home/orangepi/Desktop/web/bishebeifen-master/noise.prof
```

上传测试：

```bash
curl -X POST http://127.0.0.1:8089/analyze/upload \
  -F 'audio=@/home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/model/test.wav'
```
