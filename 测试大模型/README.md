# 测试大模型

这个目录放的是一组独立联调脚本，用来验证第三方视觉大模型网关，以及把本项目多路数据聚合后做一次多模态风险分析。

以下说明已按当前仓库代码核对。它不是系统主运行入口，而是：

- 第三方模型网关连通性测试工具
- 多模态提示词调试工具
- 本项目多源数据聚合的独立验证工具

当前推荐默认模型：

- `qwen3-vl:235b-instruct`

## 与主系统的关系

项目主系统现在已经在 Spring Boot 内置了自动 AI 融合分析：

- 任意一条视频、声音、环境异常记录写入后
- 后端会自动汇总多源上下文
- 分析结果回写到原始安全记录

所以这个目录里的脚本更多是：

- 手工调试工具
- 网关验证工具
- 多模态 Prompt 验证工具

而不是线上主链路唯一入口。

## 目录说明

- `test_866646_api.py`
  - Python 网关测试脚本
  - 支持 `GET /v1/models`
  - 支持纯文本 `chat/completions`
  - 支持带本地图片的多模态请求

- `VisionImageChat.java`
  - Java 单图测试程序
  - 会把本地图片编码成 `data:image/...;base64,...` 再请求模型

- `MultiServiceRiskAnalyzer.java`
  - 多源融合分析程序
  - 会读取本项目后端与下游服务的数据，再调用视觉大模型输出风险 JSON

- `new.py`
  - 早期最小示例
  - 现在只适合拿来参考参数，不是推荐入口

- `R-C.jpg`
  - 单图测试样例

- `HelloJava.java`
  - Java 运行环境自检

## 运行前提

### 1. 第三方模型网关

当前脚本默认请求：

```text
https://api.866646.xyz/
```

建议通过环境变量传参：

```bash
export VISION_API_KEY='你的 token'
export VISION_BASE_URL='https://api.866646.xyz/'
export VISION_MODEL='qwen3-vl:235b-instruct'
export RISK_MODEL='qwen3-vl:235b-instruct'
```

### 2. 本项目本地服务

如果要运行 `MultiServiceRiskAnalyzer.java`，需要先启动本项目本地服务。推荐：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master
./start_all_stack.sh
```

也可以手动启动前端、Spring Boot、传感器 HTTP、声音 HTTP、视觉 HTTP。

## 当前依赖的本地接口

`MultiServiceRiskAnalyzer.java` 当前会读取这些接口：

- `http://127.0.0.1:8080/api/test/health`
- `http://127.0.0.1:8080/api/sensor/history?page=0&size=2`
- `http://127.0.0.1:8080/api/sensor/threshold`
- `http://127.0.0.1:8080/api/sound/realtime/status`
- `http://127.0.0.1:8080/api/sound/realtime/windows?limit=5`
- `http://127.0.0.1:8080/api/sound/realtime/events`
- `http://127.0.0.1:8080/api/rknn/status`
- `http://127.0.0.1:8080/api/rknn/detection/count?cam=0`
- `http://127.0.0.1:8080/api/rknn/detection/count?cam=1`
- `http://127.0.0.1:8080/api/rknn/frame/current?cameraId=1&track=false`
- `http://127.0.0.1:8080/api/rknn/frame/current?cameraId=2&track=false`

如果后端代理不可用，程序还会尝试直接回退到：

- 声音服务 `http://127.0.0.1:8089`
- 视觉服务 `http://127.0.0.1:8091`

## 快速使用

### Python：查看网关模型列表

```bash
python3 /home/orangepi/Desktop/web/bishebeifen-master/测试大模型/test_866646_api.py \
  --api-key "$VISION_API_KEY" \
  --base-url "$VISION_BASE_URL" \
  --list-models
```

### Python：测试单张图片解析

```bash
python3 /home/orangepi/Desktop/web/bishebeifen-master/测试大模型/test_866646_api.py \
  --api-key "$VISION_API_KEY" \
  --base-url "$VISION_BASE_URL" \
  --model 'qwen3-vl:235b-instruct' \
  --image '/home/orangepi/Desktop/web/bishebeifen-master/测试大模型/R-C.jpg' \
  --prompt '请描述这张图片'
```

### Java：编译并测试单图识别

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/测试大模型
javac VisionImageChat.java
java VisionImageChat
```

### Java：只做本地多源聚合，不调用大模型

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/测试大模型
javac MultiServiceRiskAnalyzer.java
java MultiServiceRiskAnalyzer --dry-run
```

### Java：执行完整融合分析

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/测试大模型
javac MultiServiceRiskAnalyzer.java
java MultiServiceRiskAnalyzer
```

## MultiServiceRiskAnalyzer 当前行为

程序会按当前代码执行这套流程：

1. 读取最近 `2` 条传感器历史与当前阈值。
2. 读取最近 `5` 个声音实时窗口，以及当前实时状态和实时事件。
3. 抓取 `cameraId=1` 与 `cameraId=2` 的当前帧。
4. 如果当前抓不到图片，会临时调用视觉推理，再重试抓图。
5. 把文本监测数据和两张图片一起发给视觉大模型。
6. 要求模型只返回严格 JSON 风险结果。

当前输出 JSON 结构主要包括：

- `risk_level`
- `level_name`
- `summary`
- `camera_findings`
- `audio_findings`
- `sensor_findings`
- `reasons`
- `immediate_actions`
- `missing_sources`

## 当前默认模型与候选模型

当前默认值已经统一为：

```text
qwen3-vl:235b-instruct
```

如果要做简单容错，可以优先考虑：

- `qwen3-vl:235b-instruct`
- `gemini-2.5-pro`
- `claude-sonnet-4-6`
- `qwen3-vl:235b`

## 与后端 AI 分析的区别

### Spring Boot 内置链路

- 正式业务链路
- 任一异常记录入库后自动触发
- 结果回写到原始记录的 `aiAnalysisStatus / aiAnalysisResult / aiAnalysisTime`

### `MultiServiceRiskAnalyzer.java`

- 独立测试工具
- 方便验证“多源数据 + 图片 + 第三方模型”这一条链路
- 方便单独调 Prompt、模型与网关参数

## 快速自检

```bash
curl http://127.0.0.1:8080/api/test/health
curl http://127.0.0.1:8080/api/sound/realtime/windows?limit=5
curl http://127.0.0.1:8080/api/rknn/status
```
