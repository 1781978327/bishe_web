# Spring Boot 后端服务说明

这个目录是后端实际运行目录。以下内容已按当前源码核对，真实启动方式是：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/web-springboot/demo3/demo
./gradlew bootRun
```

服务入口：

- `http://127.0.0.1:8080/api`

健康检查：

```bash
curl http://127.0.0.1:8080/api/test/health
```

说明：

- 当前后端没有 `/api/status`
- 启动脚本与联调时，建议统一用 `GET /api/test/health`

## 运行环境

- `JDK 17`
- `MySQL 8`
- `Spring Boot 3.2.0`
- Gradle Wrapper（仓库内自带）

## 当前默认配置

关键配置位于：

- `src/main/resources/application.properties`

当前默认值中最重要的部分：

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

ai.analysis.base-url=${VISION_BASE_URL:https://maas-coding-api.cn-huabei-1.xf-yun.com/v2}
ai.analysis.api-key=${VISION_API_KEY:}
ai.analysis.model=${RISK_MODEL:astron-code-latest}
ai.analysis.enable-images=${AI_ANALYSIS_ENABLE_IMAGES:true}
```

## 统一响应格式

除文件下载和图片/音频二进制接口外，大多数接口返回：

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {}
}
```

## 主要接口

### 1. 健康检查

- `GET /test/health`

### 2. 用户

- `POST /user/login`
- `POST /user/register`
- `POST /user/logout`
- `PUT /user`

说明：

- 前端遗留封装里还有 `/user/page`、`/user/{id}`、`/user/password`
- 这些接口当前后端并未实现

### 3. 环境监测

- `GET /sensor/latest`
- `GET /sensor/history?page=0&size=20`
- `PUT /sensor/threshold`
- `GET /sensor/threshold`
- `POST /sensor/refresh`
- `PUT /sensor/monitoring?enabled=true|false`
- `GET /sensor/monitoring`

### 4. 声音监测

- `POST /sound/start`
- `POST /sound/stop`
- `GET /sound/status`
- `GET /sound/latest`
- `GET /sound/history?page=0&size=20`
- `GET /sound/realtime/status`
- `GET /sound/realtime/events`
- `GET /sound/realtime/windows?limit=5`
- `POST /sound/realtime/start`
- `POST /sound/realtime/stop`
- `POST /sound/detect`
- `POST /sound/upload`
- `POST /sound/record?duration=5`
- `GET /sound/audio?path=...`

代码事实：

- `/sound/start` 和 `/sound/realtime/start` 最终都会调用声音服务的 `/realtime/start`
- `/sound/upload` 在声音服务不可用时，会返回上传成功但 `anomalyCount=0`
- 当前上传目录是硬编码路径：`/home/orangepi/Desktop/web/Sound_Monitoring/uploads`（不随仓库目录自动变化）

### 5. RKNN 视觉代理

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

- `cameraId=1` 映射到视觉服务 `cam=0`
- `cameraId=2` 映射到视觉服务 `cam=1`
- `/rknn/status` 会把视觉服务返回中的 `localhost/127.0.0.1` 流地址改写成当前请求主机
- `/rknn/forbidden-area` 当前只存在内存中，重启后会丢失

### 6. 模型管理

- `GET /model/list`
- `GET /model/current`
- `POST /model/select?id=...`

当前内置模型：

- `best-coco-person-moto`
- `coco_person_i8`
- `person_2700_i8`
- `yolov8s`
- `yolov8n`

### 7. 文件服务

- `POST /file/upload/{bucket}`
- `GET /file/{bucket}/**`
- `DELETE /file/{bucket}/**`

### 8. 摄像头与检测记录

- `POST /camera`
- `GET /camera`
- `GET /camera/{id}`
- `PUT /camera/{id}`
- `DELETE /camera/{id}`
- `PUT /camera/{id}/enable`
- `PUT /camera/{id}/detection`
- `PUT /camera/{id}/status`
- `POST /camera/{id}/heartbeat`
- `GET /camera/online`
- `GET /camera/detection-enabled`

- `POST /detection/record`
- `GET /detection/record/page`
- `GET /detection/record/{id}`
- `PUT /detection/record/process`
- `PUT /detection/record/{id}/process`
- `DELETE /detection/record/{id}`
- `DELETE /detection/record/batch`
- `DELETE /detection/record/clear-all`
- `POST /detection/record/rknn/report`
- `POST /detection/record/sound/report`

## 与三个 HTTP 服务的关系

后端当前协调的三条下游链路是：

- 传感器：`http://localhost:8088`
- 声音：`http://localhost:8089`
- 视觉：`http://localhost:8091`

其中：

- 传感器超阈值会生成环境异常记录
- 声音异常和紧急关键词事件会生成声音异常记录
- 视觉数量阈值告警与禁区闯入会上报视频异常记录

## AI 融合分析

当前后端已经接入自动 AI 融合分析：

- 任意一条视频、声音、环境异常记录落库后
- 后端会异步汇总多源上下文
- 结果回写到同一条安全记录

当前实际聚合内容：

- 触发记录本身
- 最近 `2` 条传感器历史
- 当前传感器阈值
- 最近 `5` 个声音窗口
- 声音实时状态与实时事件
- 视觉状态与两路检测计数
- 记录自带图片，或尽量抓取当前帧补图

## 启动前建议设置

```bash
export VISION_API_KEY='你的 key'
export VISION_BASE_URL='https://maas-coding-api.cn-huabei-1.xf-yun.com/v2'
export RISK_MODEL='astron-code-latest'
export AI_ANALYSIS_ENABLE_IMAGES='1'
```

如果主要使用根目录一键脚本，也可以把这些变量写到：

- `/home/orangepi/Desktop/web/bishebeifen-master/.runtime/ai.env`

## 自检命令

```bash
curl http://127.0.0.1:8080/api/test/health
curl http://127.0.0.1:8080/api/sensor/latest
curl http://127.0.0.1:8080/api/sound/status
curl http://127.0.0.1:8080/api/sound/realtime/windows?limit=5
curl http://127.0.0.1:8080/api/rknn/status
```
