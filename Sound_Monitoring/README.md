# Sound_Monitoring HTTP 服务说明

当前仓库里声音主链路使用 `src/main_http.cc`，运行产物是 `rknn_yamnet_demo_http`，默认端口 `8089`。

这个服务主要提供：

- 音频文件异常检测
- 上传音频自动转码后检测
- 实时麦克风监测
- 实时异常事件缓存
- 可选 Vosk ASR 转写
- 自动上报 Spring Boot 安全记录

## 目录

```text
Sound_Monitoring/
├── src/                 # 主程序与 HTTP 入口
├── utils/               # 音频/图像/文件工具
├── model/               # YAMNet 模型与标签
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

也可以手动构建：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/build
cmake ../src
make -j$(nproc)
```

说明：

- `scripts/build.sh` 产物默认在 `Sound_Monitoring/build`
- 根目录 `start_all_stack.sh` 优先尝试 `Sound_Monitoring/src/build`，若目录不同可手动设置 `SOUND_DIR`

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
- 默认实时设备：`plughw:CARD=Camera_1,DEV=0`
- 默认设备失败时会回退到：`plughw:CARD=Camera,DEV=0`
- 如未关闭自动启动，会在服务启动后尝试自动开启实时监测

## 环境变量

```bash
export AUTO_START_REALTIME=0
export RT_PRINT_ASR=0
export VOSK_MODEL_CN=/path/to/vosk-model-small-cn-0.22
export VOSK_MODEL_EN=/path/to/vosk-model-small-en-us-0.15
export VOSK_LIB_PATH=/path/to/libvosk.so
```

说明：

- `AUTO_START_REALTIME=0`：禁止启动时自动开启实时监测
- `RT_PRINT_ASR=0`：关闭实时模式的周期性转写日志
- `VOSK_MODEL_CN`、`VOSK_MODEL_EN`：显式指定中文/英文模型目录
- `VOSK_LIB_PATH`：显式指定 `libvosk.so`

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
  -d '{"device":"plughw:CARD=Camera_1,DEV=0"}'
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
  "device": "plughw:CARD=Camera_1,DEV=0",
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
curl "http://127.0.0.1:8089/realtime/transcript?seconds=120"
curl "http://127.0.0.1:8089/realtime/transcript?seconds=30&save_audio=1"
```

说明：

- `seconds` 上限是 `120`
- `save_audio=1` 时，音频会保存到 `./debug_audio/`
- 如果未启用 ASR，会返回 `success=false` 和 `ASR disabled`

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
  "asr_enabled": true,
  "asr_model_cn": "...",
  "asr_model_en": "..."
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

上传测试：

```bash
curl -X POST http://127.0.0.1:8089/analyze/upload \
  -F 'audio=@/home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring/model/test.wav'
```
