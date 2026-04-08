# 嵌入式多目标追踪与智能预警系统

基于 Vue + Spring Boot 的校园安全监控系统，支持视频监控、声音异常检测、环境异常检测三大功能模块。

## 技术栈

### 前端
- **Vue 3** + TypeScript
- **Element Plus** UI 组件库
- **Vite** 构建工具
- **Pinia** 状态管理
- **ECharts** 数据可视化
- **flv.js / hls.js** 视频流播放

### 后端
- **Spring Boot 3.2** + Java 17
- **Spring Data JPA** + MySQL
- **JWT** 身份认证
- **WebSocket** 实时通信

## 项目结构

```
web/
├── web-vue/          # 前端项目 (Vue 3 + Vite)
├── web-springboot/   # 后端项目 (Spring Boot)
└── yolov8-rk3588-cpp-inference-main/  # YOLOv8 模型推理
```

## 环境要求

### 前端环境
- Node.js >= 18.x
- npm >= 9.x

### 后端环境
- JDK 17+
- MySQL 8.0+
- Gradle 8.x

## 快速开始

### 1. 前端启动

```bash
# 进入前端目录
cd web-vue

# 安装依赖
npm install

# 启动开发服务器
npm run dev
```

前端启动后访问: http://localhost:5173

### 2. 后端启动

```bash
# 进入后端目录
cd web-springboot/demo3/demo

# 启动 Spring Boot 应用
./gradlew bootRun
```
跟踪
限流
区域闯入
后端启动后访问: http://localhost:8080

## 可用脚本

| 命令 | 说明 |
|------|------|
| `npm run dev` | 启动开发服务器 |
| `npm run build` | 构建生产版本 |
| `npm run preview` | 预览生产构建 |

## 功能模块

### 1. 监控异常
区域闯入
限流

### 2. 声音异常
- 哭泣检测
- 尖叫检测
- 玻璃破碎检测

### 3. 环境异常
- 烟雾检测
- 火灾检测
- 入侵检测

## API 文档

| 服务 | 地址 |
|------|------|
| 后端 API | http://localhost:8080/api |
| 前端 | http://localhost:5173 |

## 默认账户

- 用户名: `admin`
- 密码: `123456`

## 许可证

MIT License
