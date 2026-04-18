# Spring Boot 后端服务说明

当前后端负责把前端请求统一收口到 `/api`，并协调 3 个下游 HTTP 服务：

- 传感器服务：`http://localhost:8088`
- 声音服务：`http://localhost:8089`
- 视觉服务：`http://localhost:8091`

## 运行环境

- `JDK 17`
- `MySQL 8`
- `Gradle 8`

默认配置在 `src/main/resources/application.properties`：

```properties
server.port=8080
server.servlet.context-path=/api
spring.datasource.url=jdbc:mysql://localhost:3306/campus_violence...
sensor.server.host=localhost
sensor.server.port=8088
sound.server.host=localhost
sound.server.port=8089
rknn.server.host=localhost
rknn.server.port=8091
ai.analysis.base-url=${VISION_BASE_URL:https://api.866646.xyz}
ai.analysis.api-key=${VISION_API_KEY:}
ai.analysis.model=${RISK_MODEL:qwen3-vl:235b-instruct}
```

## 启动

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/web-springboot/demo3/demo
./gradlew bootRun
```

服务入口：

- `http://127.0.0.1:8080/api`

## 统一响应格式

除文件下载和图片/音频二进制响应外，大多数接口返回：

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {}
}
```

## 鉴权规则

`SecurityConfig` 当前放行：

- `/user/login`
- `/user/register`
- `/test/**`
- `/sensor/**`
- `/rknn/**`
- `/sound/**`
- `/file/**`
- `/detection/record/sound/**`
- `/detection/record/rknn/**`
- `/ws/**`
- `/uploads/**`

其他接口默认需要 JWT。

## 当前真实接口清单

### 1. 测试与健康检查

- `GET /test/health`

### 2. 用户

- `POST /user/login`
- `POST /user/register`
- `POST /user/logout`
- `PUT /user`

说明：

- 当前后端没有实现 `/user/page`、`/user/{id}`、`/user/password`、`/user/{id}/reset-password`
- 前端里仍有这些历史封装，联调时不要把它们当成现有能力

### 3. 摄像头

- `POST /camera`
- `GET /camera`
- `GET /camera/{id}`
- `PUT /camera/{id}`
- `DELETE /camera/{id}`
- `PUT /camera/{id}/enable?enabled=true|false`
- `PUT /camera/{id}/detection?enabled=true|false`
- `PUT /camera/{id}/status?status=0|1`
- `POST /camera/{id}/heartbeat`
- `GET /camera/online`
- `GET /camera/detection-enabled`

### 4. 检测记录

- `POST /detection/record`
- `GET /detection/record/page`
- `GET /detection/record/{id}`
- `PUT /detection/record/process`
- `PUT /detection/record/{id}/process`
- `DELETE /detection/record/{id}`
- `DELETE /detection/record/batch`
- `DELETE /detection/record/clear-all`

公开上报接口：

- `POST /detection/record/rknn/report`
- `POST /detection/record/sound/report`

说明：

- `eventType` 当前支持 `video`、`sound`、`env`
- 视觉上报和声音上报会自动写入安全记录
- 当前安全记录还会额外回写
  - `aiAnalysisStatus`
  - `aiAnalysisResult`
  - `aiAnalysisTime`

当前 AI 融合分析行为：

- 视频、声音、环境三类异常写入 `detection_records` 后自动异步触发
- 使用 `DetectionRecordAiAnalysisService`
- 汇总触发记录、最近 `2` 条传感器历史、当前阈值、最近 `5` 个声音窗口、声音实时状态、视觉状态、两路检测计数，以及可用图片
- 分析结果回写到原记录，不会新建单独的 AI 记录

### 5. 环境监测

- `GET /sensor/latest`
- `GET /sensor/history?page=0&size=20`
- `PUT /sensor/threshold`
- `GET /sensor/threshold`
- `POST /sensor/refresh`
- `PUT /sensor/monitoring?enabled=true|false`
- `GET /sensor/monitoring`

代码事实：

- `SensorService` 优先请求 `http://localhost:8088/sensor`
- 如果传感器 HTTP 服务不可用，会退回到模拟数据
- 阈值超限后会生成 `DetectionRecord`

### 6. 声音监测

- `POST /sound/start`
- `POST /sound/stop`
- `GET /sound/status`
- `GET /sound/latest`
- `GET /sound/history?page=0&size=20`
- `GET /sound/realtime/status`
- `GET /sound/realtime/events`
- `POST /sound/realtime/start`
- `POST /sound/realtime/stop`
- `POST /sound/detect`
- `POST /sound/upload`
- `POST /sound/record?duration=5`
- `GET /sound/audio?path=...`

代码事实：

- `/sound/start` 与 `/sound/realtime/start` 最终都会调用声音服务的 `/realtime/start`
- `/sound/upload` 在检测服务不可用时，仍会返回上传成功，但 `anomalyCount=0`
- 声音异常会再上报到 `/detection/record/sound/report`

### 7. RKNN 视觉代理

- `POST /rknn/inference/on?track=true|false&tracker=bytetrack|deepsort`
- `POST /rknn/inference/off`
- `POST /rknn/tracker/set?enabled=true|false`
- `POST /rknn/rtsp/camera/start`
- `GET /rknn/record/status`
- `POST /rknn/record/start?cameraId=1|2`
- `POST /rknn/record/stop?cameraId=1|2`
- `GET /rknn/record/files?cameraId=1|2`
- `GET /rknn/record/file?name=...`
- `GET /rknn/status`
- `POST /rknn/threshold/set?value=0.6&boxCount=3`
- `GET /rknn/frame/current?cameraId=1|2&track=true|false`
- `POST /rknn/forbidden-area`
- `GET /rknn/forbidden-area?cameraId=1|2`
- `GET /rknn/detection/count?cam=0|1`

代码事实：

- `cameraId=1` 会被映射到视觉服务 `cam=0`
- `cameraId=2` 会被映射到视觉服务 `cam=1`
- `/rknn/status` 会把返回里的 `rtsp_url_cam0`、`rtsp_url_cam1`、`rtsp_url_mosaic`、`rtsp_url_video` 自动改写成当前请求主机
- `/rknn/forbidden-area` 当前存储在 `RknnService` 内存里，重启后丢失

禁入区闯入说明：

- 视觉 C++ 服务会定期从 `/api/rknn/forbidden-area?cameraId=1|2` 拉取四边形区域
- 首次检测到有人进入禁区时，会自动向 `/api/detection/record/rknn/report` 上报 `env_intrusion`
- 上报体里会附带当前截图 `imageBase64`
- 后端收到后会自动进入 AI 融合分析链路

### 8. 模型管理

- `GET /model/list`
- `GET /model/current`
- `POST /model/select?id=...`

当前内置模型：

- `person_2700_i8`
- `yolov8s`
- `yolov8n`

上传模型规则：

- 通过 `/file/upload/models`
- 需要 `.rknn` 与同名 `.txt`
- 后端切换模型时会下发绝对路径到视觉服务

### 9. 文件服务

- `POST /file/upload/{bucket}`
- `GET /file/{bucket}/**`
- `DELETE /file/{bucket}/**`

当前常见 bucket：

- `avatars`
- `models`
- `detection`
- `process`

上传路径规则：

- 物理目录：`./uploads/{bucket}/{username}/filename`
- `models` bucket 上传成功后会自动调用 `ModelProfileService.registerUpload`

## 自检命令

```bash
curl http://127.0.0.1:8080/api/test/health
curl http://127.0.0.1:8080/api/sensor/latest
curl http://127.0.0.1:8080/api/sound/status
curl http://127.0.0.1:8080/api/rknn/status
```

登录示例：

```bash
curl -X POST http://127.0.0.1:8080/api/user/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"your_username","password":"your_password"}'
```

区域闯入快速模拟：

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

## 项目结构

```text
src/main/java/com/example/demo/
├── config/          # 安全、异常、静态资源、RestTemplate 配置
├── controller/      # HTTP 接口入口
├── service/         # 业务聚合与下游 HTTP 调用
├── repository/      # JPA 仓库
├── entity/          # 数据实体
├── dto/             # 请求/响应 DTO
└── utils/           # JWT 等工具类
```
