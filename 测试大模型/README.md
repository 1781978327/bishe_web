# 测试大模型

这个目录放的是一组独立的本地联调脚本，用来验证第三方视觉大模型网关，以及把本项目的前后端服务数据聚合后做一次多模态风险分析。

当前推荐默认模型是 `qwen3-vl:235b-instruct`。之前的 `doubao-1.5-vision-pro` 在当前网关下已经不可用，不建议再继续作为默认配置。

补充说明：

- 这个目录里的脚本主要是“独立测试工具”
- 项目主后端现在也已经内置了自动 AI 融合分析
- 也就是视频、声音、环境异常一旦写入安全记录，Spring Boot 会自动做一次多源分析并回写结果
- 这里的脚本更多用于单独验证网关和多模态提示词，不是唯一入口

## 目录说明

- `test_866646_api.py`
  - Python 本地测试脚本。
  - 支持 `GET /v1/models`、纯文本 `chat/completions`、带本地图片的多模态请求。
- `VisionImageChat.java`
  - Java 单图测试程序。
  - 把本地图片编码成 `data:image/...;base64,...` 后直接请求视觉模型。
- `MultiServiceRiskAnalyzer.java`
  - 多源融合分析程序。
  - 会抓取两路摄像头当前帧、最近 5 次声音实时窗口、最近 2 次传感器历史，再调用视觉模型输出 JSON 风险结论。
- `new.py`
  - 早期的最小示例脚本。
  - 现在只适合当作参数来源参考，不是推荐入口。
- `R-C.jpg`
  - 本地图像测试样例。
- `HelloJava.java`
  - Java 运行环境自检小程序。

## 运行前提

### 1. 第三方模型网关

当前脚本默认请求：

```text
https://api.866646.xyz/
```

建议通过环境变量传参，而不是把密钥直接写进源码：

```bash
export VISION_API_KEY='你的 token'
export VISION_BASE_URL='https://api.866646.xyz/'
export VISION_MODEL='qwen3-vl:235b-instruct'
```

`MultiServiceRiskAnalyzer.java` 还支持：

```bash
export RISK_MODEL='qwen3-vl:235b-instruct'
```

### 2. 本项目本地服务

如果要跑 `MultiServiceRiskAnalyzer.java`，需要先启动本项目整套本地服务：

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master
./start_all_stack.sh
```

它会访问这些本地接口：

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

如果后端代理不可用，程序还会尝试走直连回退：

- 声音服务 `8089`
- 视觉服务 `8091`

## 快速使用

### Python: 查看网关模型列表

```bash
python3 /home/orangepi/Desktop/web/bishebeifen-master/测试大模型/test_866646_api.py \
  --api-key "$VISION_API_KEY" \
  --base-url "$VISION_BASE_URL" \
  --list-models
```

### Python: 测试单张图片解析

```bash
python3 /home/orangepi/Desktop/web/bishebeifen-master/测试大模型/test_866646_api.py \
  --api-key "$VISION_API_KEY" \
  --base-url "$VISION_BASE_URL" \
  --model 'qwen3-vl:235b-instruct' \
  --image '/home/orangepi/Desktop/web/bishebeifen-master/测试大模型/R-C.jpg' \
  --prompt '请描述这张图片'
```

### Java: 编译并测试单图识别

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/测试大模型
javac VisionImageChat.java
java VisionImageChat
```

### Java: 只看本地多源聚合，不调用大模型

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/测试大模型
javac MultiServiceRiskAnalyzer.java
java MultiServiceRiskAnalyzer --dry-run
```

### Java: 执行完整融合分析

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/测试大模型
javac MultiServiceRiskAnalyzer.java
java MultiServiceRiskAnalyzer
```

## MultiServiceRiskAnalyzer 实际行为

程序会按当前代码执行下面这套流程：

1. 从后端读取最近 `2` 条传感器历史与当前阈值。
2. 从声音服务读取最近 `5` 个实时窗口，窗口里既包含异常也包含非异常状态。
3. 抓取 `cameraId=1` 和 `cameraId=2` 两路当前帧。
4. 如果当前抓不到图片，会临时调用 `/api/rknn/inference/on?track=false` 打开视觉推理，再重试抓图。
5. 把文本监测数据和两张图片一起发给视觉模型。
6. 要求模型只返回严格 JSON 风险分析结果。

当前输出 JSON 结构包括：

- `risk_level`
- `level_name`
- `summary`
- `camera_findings`
- `audio_findings`
- `sensor_findings`
- `reasons`
- `immediate_actions`
- `missing_sources`

## 当前默认模型

当前脚本默认值已经统一为：

```text
qwen3-vl:235b-instruct
```

这个模型已经在当前网关上完成过实际图片解析测试，`POST /v1/chat/completions` 可以正常返回 `HTTP 200`。

如果后面想做简单容错，优先考虑这些候选模型：

- `qwen3-vl:235b-instruct`
- `gemini-2.5-pro`
- `claude-sonnet-4-6`
- `qwen3-vl:235b`

## 与主系统 AI 融合分析的关系

Spring Boot 当前内置的自动 AI 分析，和 `MultiServiceRiskAnalyzer.java` 的思路基本一致，但职责不同：

- `DetectionRecordAiAnalysisService`
  - 主系统正式链路
  - 任一异常写入安全记录后自动触发
  - 结果回写到原始安全记录的 `aiAnalysisStatus / aiAnalysisResult / aiAnalysisTime`
- `MultiServiceRiskAnalyzer.java`
  - 手工测试与调试工具
  - 用来独立验证“多源数据 + 图片 + 第三方模型”这一条链路

当前主系统自动 AI 分析实际汇总：

- 触发记录本身
- 最近 `2` 条传感器历史
- 当前阈值
- 最近 `5` 个声音窗口
- 声音实时状态与实时事件
- 视觉状态与两路检测计数
- 记录自带图片，或尽量抓当前帧补图

当前不是：

- 不会额外录一段新的当前音频文件再送模型
- 不会新建独立 AI 安全记录

## 区域闯入快速模拟

如果你要测试“区域闯入 + 其他数据 + AI 融合分析”，最快可以模拟一条 `env_intrusion`：

1. 先启动整套本地服务，并确保环境监测、声音实时监测、视觉推理都处于工作状态。
2. 抓一张当前帧，构造 `env_intrusion` 上报。

示例：

```bash
curl -s 'http://127.0.0.1:8080/api/rknn/frame/current?cameraId=1&track=false' -o /tmp/env_intrusion_cam1.jpg

python3 - <<'PY'
import base64, json
from pathlib import Path

img = Path('/tmp/env_intrusion_cam1.jpg').read_bytes()
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
Path('/tmp/env_intrusion_sim.json').write_text(json.dumps(payload, ensure_ascii=False), encoding='utf-8')
PY

curl -X POST http://127.0.0.1:8080/api/detection/record/rknn/report \
  -H 'Content-Type: application/json' \
  --data-binary @/tmp/env_intrusion_sim.json
```

这条记录进入后端后，会自动触发和正式链路相同的 AI 融合分析。

## 已知限制

- `recent_video_alerts` 当前仍可能返回 `403`。
  - 原因是它请求的 `/api/detection/record/page?...eventType=video` 在部分环境下需要登录 token。
  - 即使这一项缺失，融合分析主链路仍然可以继续执行。
- 这几个源码文件当前把工作目录写死成了：

```text
/home/orangepi/Desktop/web/测试大模型
```

也就是说，仓库里的这份目录虽然已经存在于：

```text
/home/orangepi/Desktop/web/bishebeifen-master/测试大模型
```

但如果你只保留仓库内这一个目录，而没有外层那份 `/home/orangepi/Desktop/web/测试大模型` 副本，`VisionImageChat.java`、`MultiServiceRiskAnalyzer.java`、`test_866646_api.py` 里的回退读取逻辑会优先指向外层目录。后续如果准备彻底收敛到仓库内路径，建议把这几个文件里的 `WORK_DIR` 一起改掉。

## 排查建议

- 先跑 `HelloJava.java`，确认本机 Java 运行时没问题。
- 先用 `test_866646_api.py --list-models` 确认网关和 token 可用。
- 再用 `VisionImageChat.java` 验证单图调用。
- 最后启动本项目服务，执行 `MultiServiceRiskAnalyzer.java --dry-run` 看本地多源聚合是否完整。
- 如果怀疑代理影响，可以临时取消代理环境变量后再测。
