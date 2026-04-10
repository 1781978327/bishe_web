# 嵌入式多目标追踪与智能预警系统

基于 `Vue 3 + Spring Boot + RK3588 RKNN` 的一体化校园安全监测系统，当前整合了三条主线能力：

- 视觉监控：双路摄像头推理、目标跟踪、禁入区、RTSP / WebRTC 播放
- 声音异常：声音分析服务与异常记录联动
- 环境监测：温湿度、烟雾、光照等传感器读取与展示

## 项目结构

```text
bishebeifen-master/
├── web-vue/                     # 前端项目（Vue 3 + Vite）
├── web-springboot/              # 后端项目（Spring Boot）
├── yolov8-rk3588-cpp-3-15/      # RK3588 视觉推理与流媒体服务
├── Sound_Monitoring/            # 声音监测服务
├── docs/                        # 接口文档
└── 环境监测系统说明文档.md       # 传感器系统说明
```

## 技术栈

### 前端
- Vue 3
- TypeScript
- Element Plus
- Vite
- Pinia
- ECharts
- HLS / WebRTC 播放

### 后端
- Java 17
- Spring Boot 3.2
- Spring Security + JWT
- Spring Data JPA
- MySQL 8
- WebSocket

### 视觉服务
- RK3588 NPU / RKNN
- OpenCV
- FFmpeg + RKMPP
- RGA
- MediaMTX

## 端口约定

| 服务 | 端口 | 说明 |
|------|------|------|
| 前端 `web-vue` | `3000` | Vite 开发服务 |
| 后端 `web-springboot` | `8080` | Spring Boot，带 `/api` 上下文 |
| 视觉服务 `rknn_http_ctrl` | `8091` | RKNN HTTP 控制服务 |
| RTSP | `8554` | MediaMTX RTSP |
| HLS | `8888` | MediaMTX HLS |
| WebRTC(WHEP) | `8889` | MediaMTX WebRTC |
| WebRTC ICE | `8189/udp` | MediaMTX ICE |

## 快速开始

推荐按下面顺序启动。

### 1. 启动视觉服务

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/build_release
sudo ./rknn_http_ctrl --cam0-source /dev/video0 --cam1-source /dev/video2
```

说明：

- 推荐从 `build_release` 目录启动，这样自动拉起的 `mediamtx` 会正确读取 `build_release/mediamtx.yml`
- 默认会自动启动 MediaMTX，并开放 `8554 / 8889 / 8189`

### 2. 启动后端

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/web-springboot/demo3/demo
./gradlew bootRun
```

说明：

- `./gradlew` 已默认指向项目内预装的 `gradle-8.14.1`
- 后端基地址为 `http://127.0.0.1:8080/api`

### 3. 启动前端

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/web-vue
npm run dev -- --host 0.0.0.0
```

访问地址：

- 本机：`http://127.0.0.1:3000`
- 局域网：`http://<当前机器IP>:3000`

## 联调自检

### 1. 检查端口

```bash
ss -ltnup | grep -E '3000|8080|8091|8554|8889|8189'
```

### 2. 检查视觉服务状态

```bash
curl http://127.0.0.1:8091/api/status
curl http://127.0.0.1:8080/api/rknn/status
```

### 3. 开推理与推流

```bash
curl -X POST "http://127.0.0.1:8080/api/rknn/inference/on?track=true&tracker=bytetrack"
curl -X POST "http://127.0.0.1:8080/api/rknn/rtsp/camera/start"
```

## 模型说明

当前后端模型管理同时支持两类来源：

### 1. 内置模型

不依赖上传即可直接切换：

- `yolov8s`
- `yolov8n`

两者默认共用：

- `yolov8-rk3588-cpp-3-15/model/coco_80_labels_list.txt`

### 2. 用户上传模型

上传模型仍按数据库档案管理：

- `.rknn` 检测模型
- 对应 `.txt` 类名文件

后端切换模型时，会把实际的 `model=...` 和 `labels=...` 路径下发给视觉服务。

## 流媒体与 IP 说明

### WebRTC/WHEP 默认行为

前端现在默认按“当前访问页面的主机地址”生成 WHEP 地址。

例如你访问：

- `http://10.137.128.69:3000`

前端会优先尝试：

- `http://10.137.128.69:8889/cam0/whep`
- `http://10.137.128.69:8889/cam1/whep`

这意味着：

- 同一局域网内更换设备 IP 后，通常不需要再手改前端地址
- 只有当 WebRTC 服务明确部署在另一台机器时，才建议配置 `VITE_WEBRTC_BASE_URL`

### RTSP / WebRTC 地址

- RTSP:
  - `rtsp://<当前机器IP>:8554/cam0`
  - `rtsp://<当前机器IP>:8554/cam1`
- WebRTC(WHEP):
  - `http://<当前机器IP>:8889/cam0/whep`
  - `http://<当前机器IP>:8889/cam1/whep`

## 常见问题

### 1. 摄像头 busy / 端口 8091 绑定失败

通常是旧的视觉服务没退出干净。

```bash
printf 'orangepi\n' | sudo -S pkill -x rknn_http_ctrl || true
printf 'orangepi\n' | sudo -S pkill -x mediamtx || true
printf 'orangepi\n' | sudo -S fuser -v /dev/video0 /dev/video2 || true
ss -ltnp | grep -E '8091|8554|8889' || true
```

### 2. Dashboard 禁入区取帧返回 503

接口：

- `/api/rknn/frame/current?cameraId=1&track=0`

这个接口在视觉服务刚启动、还未产生可用帧，或者摄像头/推理尚未就绪时，可能短暂返回 `503`。通常等服务稳定后重试即可。

### 3. WebRTC 仍然连旧 IP

优先检查：

- `web-vue/.env.local`

如果里面写了固定的：

- `VITE_WEBRTC_BASE_URL=http://旧IP:8889`

前端就会强制走这个旧地址。当前建议默认不写这一项。

## 默认账户

- 管理员：
  - 用户名：`admin`
  - 密码：`Lml123`

## 更多说明

- 前端联调说明：[/home/orangepi/Desktop/web/bishebeifen-master/web-vue/README](/home/orangepi/Desktop/web/bishebeifen-master/web-vue/README)
- 后端启动说明：[/home/orangepi/Desktop/web/bishebeifen-master/web-springboot/readme](/home/orangepi/Desktop/web/bishebeifen-master/web-springboot/readme)
- 视觉服务运行说明：[/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/运行说明.md](/home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/运行说明.md)
