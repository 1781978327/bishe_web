# 嵌入式多目标追踪与智能预警系统

本项目是一个基于RK3588平台的嵌入式多目标追踪与智能预警系统，融合了环境监测、声音检测、视觉识别等多种技术，构建了一个全方位的智能监控预警平台。

## 系统架构

整个系统采用分层架构设计，主要包括以下三层：

### 1. 硬件抽象层 (Hardware)
- **传感器数据采集**：DHT11(温湿度)、MQ-2(烟雾)、GL5528(光照)配合ADS1115 ADC转换器
- **HTTP服务接口**：`sensor_reader_http` (端口 8088)，提供环境数据的REST API访问
- **底层通信**：通过I2C和GPIO接口与传感器交互

### 2. 智能感知层 (Intelligent Perception)
- **声音监测**：基于YAMNet模型的声音异常检测，`rknn_yamnet_demo_http` (端口 8089)
- **视觉识别**：基于YOLOv8的目标检测与跟踪，`rknn_http_ctrl` (端口 8091)，支持ByteTrack/DeepSORT跟踪算法
- **流媒体服务**：集成MediaMTX，提供RTSP/HLS/WebRTC流媒体服务

### 3. 应用服务层 (Application Services)
- **后端服务**：Spring Boot应用 (端口 8080)，统一管理各子系统
- **前端界面**：Vue 3单页应用 (端口 3000)，提供可视化监控界面
- **数据存储**：集成MySQL/H2数据库，存储检测记录和系统配置

## 系统功能特性

### 环境监测功能
- 实时温湿度监测
- 烟雾浓度检测
- 光照强度监测
- 自定义阈值报警
- 历史数据记录

### 声音检测功能
- 音频文件异常检测
- 实时麦克风监测
- Vosk ASR语音转写（可选）
- 紧急关键词唤醒监测
- 声音事件自动上报

### 视觉识别功能
- 实时目标检测（人、车、物体等）
- 目标跟踪与轨迹分析
- 双路摄像头同步处理
- 检测数量阈值报警
- 禁入区域监测
- JPEG抓帧与录像功能

### 综合管理功能
- 统一Web界面管理
- 实时数据可视化
- 历史记录查询
- 用户权限管理
- 系统配置管理

## 项目结构

```
bishebeifen-master/
├── README.md                      # 项目主文档
├── start_all_stack.sh            # 一键启动脚本
├── stop_all_stack.sh             # 一键停止脚本
├── .runtime/                     # 运行时配置目录
├── Hardware/                     # 硬件传感器HTTP服务
│   ├── sensor_reader_http.cpp    # 传感器数据采集服务
│   ├── README.md                 # 硬件模块文档
│   └── ...
├── Sound_Monitoring/             # 声音检测HTTP服务
│   ├── src/main_http.cc          # 声音检测服务主程序
│   ├── README.md                 # 声音模块文档
│   └── ...
├── yolov8-rk3588-cpp-3-15/       # 视觉识别HTTP服务
│   ├── src/main_http_ctrl.cc     # 视觉控制服务主程序
│   ├── README.md                 # 视觉模块文档
│   └── ...
├── web-springboot/               # Spring Boot后端服务
│   ├── demo3/demo/               # 主应用模块
│   └── ...
├── web-vue/                      # Vue 3前端界面
│   ├── src/views/Dashboard.vue   # 主仪表盘界面
│   └── ...
├── docs/                         # 文档资料
└── 测试大模型/                   # 第三方大模型集成测试
```

## 服务端口分配

| 服务名称 | 端口 | 协议 | 功能描述 |
|---------|------|------|----------|
| Vue前端 | 3000 | HTTP | 用户界面访问 |
| Spring Boot后端 | 8080 | HTTP | API网关与业务逻辑 |
| 传感器HTTP服务 | 8088 | HTTP | 环境数据采集 |
| 声音检测HTTP服务 | 8089 | HTTP | 音频分析与检测 |
| 视觉识别HTTP服务 | 8091 | HTTP | 目标检测与跟踪 |
| RTSP流媒体 | 8554 | RTSP | 视频流传输 |
| HLS流媒体 | 8888 | HTTP | HTTP Live Streaming |
| WebRTC流媒体 | 8889 | HTTP | WebRTC WHEP接口 |
| WebRTC ICE | 8189 | UDP | WebRTC ICE传输 |

## 快速启动指南

### 前提条件
- 确保RK3588开发板已正确配置
- 硬件传感器已正确连接
- 安装必要依赖：Node.js, Java 17, Maven/Gradle

### 一键启动（推荐）

```bash
cd /home/orangepi/Desktop/web/bishebeifen-master
./start_all_stack.sh
```

### 手动启动

#### 1. 启动硬件传感器服务
```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Hardware
sudo ./sensor_reader_http
```

#### 2. 启动声音检测服务
```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/Sound_Monitoring
./scripts/build.sh  # 首次运行需编译
cd build
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
sudo ./rknn_yamnet_demo_http 8089
```

#### 3. 启动视觉识别服务
```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/yolov8-rk3588-cpp-3-15/build_release
sudo ./rknn_http_ctrl --cam0-source /dev/video0 --cam1-source /dev/video2
```

#### 4. 启动后端服务
```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/web-springboot/demo3/demo
./gradlew bootRun
```

#### 5. 启动前端服务
```bash
cd /home/orangepi/Desktop/web/bishebeifen-master/web-vue
npm install
npm run dev -- --host 0.0.0.0 --port 3000
```

## 系统访问

- **前端界面**：`http://<IP>:3000`
- **后端API**：`http://<IP>:8080/api`
- **传感器服务**：`http://<IP>:8088`
- **声音服务**：`http://<IP>:8089`
- **视觉服务**：`http://<IP>:8091`
- **RTSP流**：`rtsp://<IP>:8554/cam0`

## API接口概览

### 环境监测接口
- `GET /api/sensor/latest` - 获取最新环境数据
- `PUT /api/sensor/threshold` - 更新阈值设置
- `PUT /api/sensor/monitoring` - 控制监测开关

### 声音检测接口
- `POST /api/sound/upload` - 上传音频文件检测
- `POST /api/sound/start` - 开启实时监测
- `GET /api/sound/status` - 获取监测状态

### 视觉识别接口
- `POST /api/rknn/inference/on` - 开启目标检测
- `GET /api/rknn/frame/current` - 获取当前帧图像
- `GET /api/rknn/detection/count` - 获取检测数量
- `POST /api/rknn/forbidden-area` - 设置禁入区域

## 系统监控与维护

### 服务健康检查
```bash
# 检查所有服务端口
ss -ltnup | grep -E '3000|8080|8088|8089|8091|8554|8888|8889|8189'

# 检查各服务状态
curl http://127.0.0.1:8088/health
curl http://127.0.0.1:8089/health
curl http://127.0.0.1:8091/api/status
curl http://127.0.0.1:8080/api/test/health
```

### 开启视觉推理与流媒体
```bash
# 开启目标检测
curl -X POST "http://127.0.0.1:8080/api/rknn/inference/on?track=true&tracker=bytetrack"

# 开启RTSP推流
curl -X POST "http://127.0.0.1:8080/api/rknn/rtsp/camera/start"
```

### 一键停止服务
```bash
./stop_all_stack.sh
```

## 开发说明

### 代码组织
- **前端**：Vue 3 + TypeScript + Element Plus，使用组件化开发
- **后端**：Spring Boot + Spring Security + JPA，RESTful API设计
- **底层服务**：C++编写；传感器服务基于microhttpd，声音与视觉服务使用自建socket HTTP服务

### 主要业务流程
1. 硬件传感器持续采集环境数据并通过HTTP API暴露
2. 后端定时拉取传感器数据并进行阈值比较
3. 视觉模块进行实时目标检测和跟踪
4. 声音模块进行音频异常检测
5. 所有异常事件统一上报并记录
6. 前端实时展示所有监控数据和报警信息

## 技术栈

- **硬件平台**：RK3588开发板
- **前端**：Vue 3, TypeScript, Element Plus, Vite
- **后端**：Spring Boot 3.2, Java 17, MySQL/H2, JWT, Spring Security
- **视觉**：YOLOv8, RKNN, ByteTrack, OpenCV
- **声音**：YAMNet, Vosk ASR
- **通信**：HTTP/REST, RTSP, HLS, WebRTC
- **容器/部署**：MediaMTX流媒体服务器

## 常见问题

### 硬件相关
- 检查I2C和GPIO权限，确保以sudo运行
- 确认传感器连接正确，地址匹配

### 服务启动
- 确保按照依赖顺序启动服务
- 检查端口占用情况
- 验证环境变量配置

### 性能优化
- RK3588平台可充分利用NPU加速
- 根据实际需求调整检测频率和阈值
- 合理配置流媒体服务参数

## 贡献

欢迎提交Issue和Pull Request。对于重大变更，请先开Issue讨论您想要改变的内容。

## 许可证

本项目为毕业设计项目，仅供学习交流使用。

## 相关文档

- `Hardware/README.md` - 硬件模块详细说明
- `Sound_Monitoring/README.md` - 声音模块详细说明  
- `yolov8-rk3588-cpp-3-15/README.md` - 视觉模块详细说明
- `web-springboot/readme` - 后端服务说明
- `web-vue/README` - 前端开发说明
