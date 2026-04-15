# 声音异常检测 HTTP 服务

`Sound_Monitoring/src/main_http.cc` 是当前仓库里的声音 HTTP 服务主入口，实际联调用的是 `rknn_yamnet_demo_http`，默认监听 `8089`。

## 服务能力

- 基于 `YAMNet RKNN` 的音频异常检测
- 上传音频文件后自动转为 `16kHz / 单声道 / PCM16 WAV`
- 实时麦克风检测
- 可选 `Vosk ASR` 转写
- 实时异常事件自动保存音频
- 实时异常事件自动上报 Spring Boot

Spring Boot 默认通过 `localhost:8089` 访问本服务，对应配置见：

- `web-springboot/demo3/demo/src/main/resources/application.properties`

## 目录说明

```text
Sound_Monitoring/
├── src/                 # 主程序与 HTTP 服务入口
├── utils/               # 音频 / 文件 / 图像工具
├── 3rdparty/            # RKNN / FFTW / libsndfile 等依赖
├── model/               # YAMNet 模型与标签
├── scripts/             # 构建脚本
├── build/               # 默认构建输出目录
└── uploads/             # 前后端联调时的上传目录
```

## 编译

推荐直接使用仓库自带脚本：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring
./scripts/build.sh
```

构建完成后，关键产物会在 `build/` 下：

- `build/rknn_yamnet_demo_http`
- `build/lib/`
- `build/model/`

如果你手动构建，也建议仍然从 `Sound_Monitoring/build` 目录进行：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/build
cmake ../src
make -j$(nproc)
```

说明：

- `scripts/build.sh` 除了编译，还会把运行所需的 `lib/` 和 `model/` 拷贝到 `build/`
- 如果你不走脚本，需要自己确认 `build/lib/` 与 `build/model/` 已准备好

## 启动 HTTP 服务

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/build
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
./rknn_yamnet_demo_http 8089
```

如果需要访问摄像头麦克风设备，通常建议：

```bash
sudo env LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./rknn_yamnet_demo_http 8089
```

默认行为：

- 默认端口：`8089`
- 默认模型：`./model/yamnet.rknn`
- 默认标签：`./model/yamnet_class_map.txt`
- 默认实时设备：`plughw:CARD=Camera_1,DEV=0`
- 如果默认设备打不开，会自动回退到：`plughw:CARD=Camera,DEV=0`
- 服务启动后会默认尝试自动开启实时监测

## 环境变量

### 实时监测 / ASR

```bash
export AUTO_START_REALTIME=0
export RT_PRINT_ASR=0
export VOSK_MODEL_CN=/path/to/vosk-model-small-cn-0.22
export VOSK_MODEL_EN=/path/to/vosk-model-small-en-us-0.15
export VOSK_LIB_PATH=/path/to/libvosk.so
```

含义：

- `AUTO_START_REALTIME=0`
  - 禁止服务启动时自动调用实时麦克风监测
- `RT_PRINT_ASR=0`
  - 关闭实时模式下每隔约 3 秒输出一次转写日志
- `VOSK_MODEL_CN` / `VOSK_MODEL_EN`
  - 配置中文 / 英文 ASR 模型目录
- `VOSK_LIB_PATH`
  - 可选，显式指定 `libvosk.so`

如果未配置 `VOSK_MODEL_CN` / `VOSK_MODEL_EN`，服务会尝试从下面两个相对路径自动探测：

- `./model/vosk-model-small-cn-0.22`
- `../model/vosk-model-small-cn-0.22`
- `./model/vosk-model-small-en-us-0.15`
- `../model/vosk-model-small-en-us-0.15`

## 运行时文件路径

服务运行时会生成几类文件，路径均相对于当前工作目录：

- `./alarm_audio/`
  - 实时异常事件保存的音频
  - `GET /realtime/transcript?...&save_audio=1` 导出的转写音频
- `./anomaly_audio/`
  - HTTP 分析接口截取出的异常音频片段
- `/tmp/yamnet_upload_*`
  - 上传接口写入的临时原始文件
- `/tmp/yamnet_convert_*.wav`
  - 非 WAV 格式转换后的临时文件

## HTTP API

### `POST /analyze`

按本地路径分析音频：

```bash
curl -X POST http://127.0.0.1:8089/analyze \
  -H "Content-Type: application/json" \
  -d '{"audio_path":"/absolute/path/to/audio.wav"}'
```

返回结果包含：

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

上传二进制音频并分析，支持 `multipart/form-data` 或原始二进制：

```bash
curl -X POST http://127.0.0.1:8089/analyze/upload \
  -F 'audio=@/path/to/audio.mp3'
```

或：

```bash
curl -X POST http://127.0.0.1:8089/analyze/upload \
  -H "Content-Type: audio/mpeg" \
  --data-binary @audio.mp3
```

说明：

- `multipart` 模式要求字段名为 `audio`
- 服务内部会先尝试用 `ffmpeg` 转成标准 WAV
- 当前实现已去掉原先固定 `16KB` 请求缓冲限制，上传大小主要受机器可用内存影响

### `POST /realtime/start`

启动实时麦克风检测：

```bash
curl -X POST http://127.0.0.1:8089/realtime/start \
  -H "Content-Type: application/json" \
  -d '{"device":"plughw:CARD=Camera_1,DEV=0"}'
```

### `POST /realtime/stop`

停止实时检测：

```bash
curl -X POST http://127.0.0.1:8089/realtime/stop
```

### `GET /realtime/status`

返回实时检测状态：

```json
{
  "running": true,
  "device": "plughw:CARD=Camera_1,DEV=0",
  "sample_rate": 16000
}
```

### `GET /realtime/events`

返回当前事件队列中的异常事件：

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

获取最近一段麦克风缓存的转写结果：

```bash
curl "http://127.0.0.1:8089/realtime/transcript?seconds=120"
curl "http://127.0.0.1:8089/realtime/transcript?seconds=30&save_audio=1"
```

说明：

- `seconds` 最大 `120`
- `save_audio=1` 时会把参与转写的音频保存到 `./alarm_audio/`
- 如果 ASR 没启用，会返回 `success: false` 和 `ASR disabled`

### `GET /health`

健康检查：

```json
{
  "status": "ok",
  "service": "sound-server",
  "model": "yamnet"
}
```

### `GET /config`

返回当前只读配置：

```json
{
  "model_path": "./model/yamnet.rknn",
  "anomaly_threshold": 0.10,
  "save_anomaly": 0,
  "keywords_count": 521,
  "asr_enabled": true,
  "asr_model_cn": "...",
  "asr_model_en": "..."
}
```

说明：

- 代码注释里提到过 `PUT /config`
- 但当前实现只注册了 `GET /config`，没有写配置接口

## 与 Spring Boot 的联动

实时异常事件在音频成功保存后，会自动上报到：

```text
http://localhost:8080/api/detection/record/sound/report
```

这个地址目前是写死在代码里的，定义在：

- `src/main_http.cc`

上报内容包含：

- `cameraId = -1`
- `cameraName = "声音监测"`
- `detectionTime`
- `detectionResult`
- `audioUrl`
- `audioDuration`
- `soundKeywords`

## 联调建议

### 1. 先看服务是否起来

```bash
curl http://127.0.0.1:8089/health
curl http://127.0.0.1:8089/config
curl http://127.0.0.1:8089/realtime/status
```

### 2. 再测上传分析

```bash
curl -X POST http://127.0.0.1:8089/analyze/upload \
  -F 'audio=@/home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/model/test.wav'
```

### 3. 最后检查 Spring Boot 是否能收到上报

```bash
curl http://127.0.0.1:8080/api/detection/record/page?current=1&size=10
```

## 备注

- 当前 HTTP 服务默认模型名是 `model/yamnet.rknn`
- 如果你只想启动 HTTP 服务、不希望进程启动时自动拉起实时麦克风监测，可以在启动前加上 `AUTO_START_REALTIME=0`
