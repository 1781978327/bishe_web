# 后端 API 接口文档

> 嵌入式多目标追踪与智能预警系统的设计与实现

**版本**: v1.0.0
**Base URL**: `http://localhost:8080/api`

---

## 一、统一响应格式

所有接口均遵循以下统一响应结构：

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": { ... }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `code` | int | 状态码（200=成功，400=参数错误，401=未授权，404=未找到，500=服务器错误） |
| `msg` | string | 响应消息 |
| `data` | object | 响应数据，分页数据时结构见"分页响应" |

### 分页响应

分页接口（如列表查询）返回结构如下：

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "content": [ ... ],   // 当前页数据数组
    "totalElements": 100, // 总记录数
    "totalPages": 5,      // 总页数
    "size": 20,           // 每页条数
    "number": 0           // 当前页码（从0开始）
  }
}
```

---

## 二、用户管理接口 `/user`

### 2.1 用户登录

```
POST /user/login
Content-Type: application/json
```

**请求参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `username` | string | 是 | 用户名 |
| `password` | string | 是 | 密码（明文传输） |

**请求示例：**

```json
{
  "username": "admin",
  "password": "123456"
}
```

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "token": "eyJhbGciOiJIUzI1NiJ9...",
    "user": {
      "id": 1,
      "username": "admin",
      "realName": "管理员",
      "role": 1,
      "avatar": "..."
    }
  }
}
```

> 登录成功后需在后续请求 Header 中携带：`Authorization: Bearer {token}`

---

### 2.2 用户注册

```
POST /user/register
Content-Type: application/json
```

**请求参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `username` | string | 是 | 用户名（唯一） |
| `password` | string | 是 | 密码 |
| `realName` | string | 是 | 真实姓名 |
| `phone` | string | 是 | 手机号 |
| `email` | string | 是 | 电子邮箱 |

---

### 2.3 退出登录

```
POST /user/logout
```

> JWT 无状态，后端无需处理，前端清除本地 token 即可。

---

### 2.4 更新用户信息

```
PUT /user
Authorization: Bearer {token}
Content-Type: application/json
```

**请求参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `id` | long | 是 | 用户ID |
| `realName` | string | 否 | 真实姓名 |
| `phone` | string | 否 | 手机号 |
| `email` | string | 否 | 邮箱 |
| `avatarUrl` | string | 否 | 头像URL |

---

## 三、监控设备管理接口 `/camera`

> Base URL: `http://localhost:8080/api/camera`
> 所有接口均需登录认证。

### 3.1 新增监控设备

```
POST /camera
Authorization: Bearer {token}
Content-Type: application/json
```

**请求参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `name` | string | 是 | 设备名称 |
| `location` | string | 是 | 安装位置 |
| `rtspUrl` | string | 是 | RTSP 流地址 |
| `status` | int | 否 | 在线状态，默认 `0` |
| `isEnabled` | boolean | 否 | 是否启用，默认 `true` |
| `detectionEnabled` | boolean | 否 | 是否启用检测，默认 `true` |

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "id": 1,
    "name": "教学楼入口",
    "location": "教学楼1层",
    "rtspUrl": "rtsp://192.168.1.100:554/stream1",
    "status": 1,
    "isEnabled": true,
    "detectionEnabled": true,
    "createTime": "2026-04-04T10:00:00"
  }
}
```

---

### 3.2 获取设备列表（分页）

```
GET /camera
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `page` | int | 否 | 页码，从 0 开始，默认 0 |
| `size` | int | 否 | 每页条数，默认 10 |
| `sortBy` | string | 否 | 排序字段，默认 `createTime` |
| `sortDir` | string | 否 | 排序方向，`asc` 或 `desc`，默认 `desc` |
| `name` | string | 否 | 按设备名称模糊查询 |
| `status` | int | 否 | 按在线状态筛选（1=在线，0=离线） |
| `enabled` | boolean | 否 | 按是否启用筛选 |

---

### 3.3 获取设备详情

```
GET /camera/{id}
Authorization: Bearer {token}
```

---

### 3.4 更新设备信息

```
PUT /camera/{id}
Authorization: Bearer {token}
Content-Type: application/json
```

**请求参数：** 同 `3.1 新增设备`，所有字段均可选。

---

### 3.5 删除设备

```
DELETE /camera/{id}
Authorization: Bearer {token}
```

---

### 3.6 启用/禁用设备

```
PUT /camera/{id}/enable
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `enabled` | boolean | 是 | true=启用，false=禁用 |

---

### 3.7 启用/禁用检测功能

```
PUT /camera/{id}/detection
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `enabled` | boolean | 是 | true=开启检测，false=关闭检测 |

---

### 3.8 更新设备状态

```
PUT /camera/{id}/status
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `status` | int | 是 | 设备状态（1=在线，0=离线） |

---

### 3.9 心跳上报（算法端调用）

```
POST /camera/{id}/heartbeat
Authorization: Bearer {token}
```

> 算法端定时调用，标记设备在线状态，避免超时掉线。

---

### 3.10 获取在线设备列表

```
GET /camera/online
Authorization: Bearer {token}
```

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": [
    { "id": 1, "name": "教学楼入口", "location": "教学楼1层", "status": 1 }
  ]
}
```

---

### 3.11 获取启用检测的设备列表

```
GET /camera/detection-enabled
Authorization: Bearer {token}
```

---

## 四、检测记录接口 `/detection/record`

> Base URL: `http://localhost:8080/api/detection/record`
> 所有接口均需登录认证。

### 4.1 添加检测记录（算法端上报）

```
POST /detection/record
Authorization: Bearer {token}
Content-Type: application/json
```

**请求参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `cameraId` | long | 是 | 摄像头ID |
| `imageBase64` | string | 是 | 检测图片（Base64，含 `data:image/jpeg;base64,` 前缀） |
| `detectionTime` | string | 否 | 检测时间（ISO 8601 格式，不传则取当前时间） |
| `detectionResult` | string | 是 | 检测结果描述 |
| `audioUrl` | string | 否 | 音频URL（声音异常时使用） |
| `audioDuration` | float | 否 | 音频时长（秒，声音异常时使用） |
| `soundKeywords` | string | 否 | 声音关键词，声音异常时使用 |

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": true
}
```

---

### 4.2 分页查询检测记录

```
GET /detection/record/page
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `current` | int | 是 | 当前页码（从 1 开始） |
| `size` | int | 是 | 每页条数 |
| `cameraId` | long | 否 | 按摄像头ID筛选 |
| `processed` | int | 否 | 按处理状态筛选（0=未处理，1=已处理） |
| `eventType` | string | 否 | 按事件大类筛选：`video`（监控异常）/ `sound`（声音异常）/ `env`（环境异常） |

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "records": [
      {
        "id": 180,
        "cameraId": 0,
        "cameraName": "环境监测",
        "imageUrl": null,
        "detectionTime": "2026-04-04T12:37:55",
        "detectionResult": "env_温度超过阈值(35.0°C) 湿度超过阈值(80.0%)",
        "processed": 0,
        "processContent": "温度: 40.0°C, 湿度: 90.0%, 烟雾: 150.0ppm, 光照: 600.0lux",
        "processNotes": "温度: 40.0°C, 湿度: 90.0%, 烟雾: 150.0ppm, 光照: 600.0lux | 阈值-温度: 35.0°C, 阈值-湿度: 80.0%",
        "processImageUrl": null,
        "processTime": null,
        "aiDescription": "env_温度超过阈值(35.0°C) 湿度超过阈值(80.0%)",
        "audioUrl": null,
        "audioDuration": null,
        "soundKeywords": null
      }
    ],
    "total": 100,
    "size": 10,
    "current": 1,
    "pages": 10
  }
}
```

**字段说明：**

| 字段 | 说明 |
|------|------|
| `id` | 记录ID |
| `cameraId` | 摄像头ID（环境监测为 0，声音监测为 -1） |
| `cameraName` | 摄像头名称 |
| `imageUrl` | 检测图片URL（环境/声音异常时为 null） |
| `detectionTime` | 检测时间 |
| `detectionResult` / `aiDescription` | 检测结果描述 |
| `processed` | 处理状态（0=未处理，1=已处理） |
| `processContent` / `processNotes` | 处理/详情内容（环境监测时存储详细数据+阈值） |
| `processTime` | 处理时间 |
| `audioUrl` | 音频URL（声音异常时） |
| `audioDuration` | 音频时长（声音异常时） |
| `soundKeywords` | 声音关键词（声音异常时） |

---

### 4.3 获取检测记录详情

```
GET /detection/record/{id}
Authorization: Bearer {token}
```

---

### 4.4 处理检测记录

```
PUT /detection/record/process
Authorization: Bearer {token}
Content-Type: application/json
```

**请求参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `id` | long | 是 | 记录ID |
| `processed` | int | 是 | 处理状态（0=未处理，1=已处理） |
| `processContent` | string | 否 | 处理说明 |
| `processImageBase64` | string | 否 | 处理现场图片（Base64） |

---

### 4.5 批量更新处理状态

```
PUT /detection/record/{id}/process
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `processed` | int | 是 | 处理状态（0 或 1） |

---

### 4.6 声音异常上报

```
POST /detection/record/sound/report
Content-Type: application/json
```

**请求参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `cameraId` | long | 否 | 摄像头ID，默认 -1 |
| `cameraName` | string | 否 | 摄像头名称，默认 "声音监测" |
| `detectionTime` | string | 否 | 检测时间（ISO 8601） |
| `detectionResult` | string | 是 | 检测结果，如异常类型字符串 |
| `audioUrl` | string | 否 | 音频URL |
| `audioDuration` | float | 否 | 音频时长（秒） |
| `soundKeywords` | string | 否 | 声音关键词 |

---

## 五、环境监测接口 `/sensor`

> Base URL: `http://localhost:8080/api/sensor`
> 当前代码中这些接口已放开，无需登录认证。

### 5.1 获取最新传感器数据

```
GET /sensor/latest
Authorization: Bearer {token}
```

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "id": 100,
    "temperature": 28.5,
    "humidity": 65.0,
    "smoke": 50.0,
    "light": 450.0,
    "alertMessage": null,
    "createTime": "2026-04-04T12:00:00"
  }
}
```

---

### 5.2 获取传感器历史数据（分页）

```
GET /sensor/history
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `page` | int | 否 | 页码，从 0 开始，默认 0 |
| `size` | int | 否 | 每页条数，默认 20 |

---

### 5.3 获取当前阈值

```
GET /sensor/threshold
Authorization: Bearer {token}
```

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "temperature": 35.0,
    "humidity": 80.0,
    "smoke": 100.0,
    "light": 500.0
  }
}
```

---

### 5.4 更新阈值

```
PUT /sensor/threshold
Authorization: Bearer {token}
Content-Type: application/json
```

**请求参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `temperature` | float | 否 | 温度阈值（°C） |
| `humidity` | float | 否 | 湿度阈值（%） |
| `smoke` | float | 否 | 烟雾阈值（ppm） |
| `light` | float | 否 | 光照阈值（lux） |

> 所有阈值参数均独立生效，支持只传其中一项或多项。

---

### 5.5 手动刷新传感器数据

```
POST /sensor/refresh
Authorization: Bearer {token}
```

> 强制从硬件传感器读取一次数据并存储到数据库。

---

### 5.6 开启/关闭环境监测

```
PUT /sensor/monitoring
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `enabled` | boolean | 是 | true=开启，false=关闭 |

---

### 5.7 获取环境监测状态

```
GET /sensor/monitoring
Authorization: Bearer {token}
```

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "enabled": true,
    "lastUpdateTime": "2026-04-04T12:00:00",
    "alertCount": 5
  }
}
```

---

## 六、声音监测接口 `/sound`

> Base URL: `http://localhost:8080/api/sound`
> 所有接口均需登录认证。

### 6.1 开启声音监测

```
POST /sound/start
Authorization: Bearer {token}
```

---

### 6.2 停止声音监测

```
POST /sound/stop
Authorization: Bearer {token}
```

---

### 6.3 获取监测状态

```
GET /sound/status
Authorization: Bearer {token}
```

补充实时接口：

- `GET /sound/realtime/status` 获取实时监测状态
- `GET /sound/realtime/events` 获取异常事件队列
- `GET /sound/realtime/windows?limit=5` 获取最近 5 个实时检测窗口状态，不只包含异常窗口
- `POST /sound/realtime/start` 启动实时监测
- `POST /sound/realtime/stop` 停止实时监测

---

### 6.4 获取最新检测事件

```
GET /sound/latest
Authorization: Bearer {token}
```

---

### 6.5 获取历史事件（分页）

```
GET /sound/history
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `page` | int | 否 | 页码，默认 0 |
| `size` | int | 否 | 每页条数，默认 20 |

---

### 6.6 上传音频文件并检测（前端专用）

```
POST /sound/upload
Authorization: Bearer {token}
Content-Type: multipart/form-data
```

**Form 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `file` | File | 是 | 音频文件（支持 WAV、MP3、M4A、OGG、FLAC） |

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "audioPath": "/home/orangepi/Desktop/web/Sound_Monitoring/uploads/1743740000000_test.wav",
    "anomalyCount": 1,
    "result": {
      "id": 10,
      "soundType": "异常类型",
      "confidence": 0.92,
      "keywords": "尖叫,大声呼救",
      "duration": 3.5,
      "audioPath": "..."
    }
  }
}
```

---

### 6.7 录制并检测

```
POST /sound/record
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `duration` | int | 否 | 录制时长（秒），默认 5 |

> 使用 `arecord` 从音频设备 `hw:4,0` 录制指定时长音频，然后自动进行异常检测。

---

### 6.8 获取音频文件

```
GET /sound/audio
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `path` | string | 是 | 音频文件完整路径 |

---

## 七、算法推理接口 `/rknn`

> Base URL: `http://localhost:8080/api/rknn`
> 所有接口均需登录认证。

### 7.1 开启推理

```
POST /rknn/inference/on
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `track` | boolean | 否 | 是否开启目标跟踪，默认 false |

---

### 7.2 关闭推理

```
POST /rknn/inference/off
Authorization: Bearer {token}
```

---

### 7.3 获取全部状态

```
GET /rknn/status
Authorization: Bearer {token}
```

---

### 7.4 获取本地状态（不查远程）

```
GET /rknn/local/status
Authorization: Bearer {token}
```

---

### 7.5 获取 RTSP 流地址

```
GET /rknn/rtsp/urls
Authorization: Bearer {token}
```

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "camera0": "rtsp://localhost:8554/stream0",
    "camera1": "rtsp://localhost:8554/stream1"
  }
}
```

---

### 7.6 获取当前帧图片 URL

```
GET /rknn/frame/url
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `track` | boolean | 否 | 是否带跟踪标注，默认 false |

---

### 7.7 检查算法服务健康状态

```
GET /rknn/health
Authorization: Bearer {token}
```

> 返回 200 表示可用，返回 503 表示不可用。

---

### 7.8 设置检测置信度阈值

```
POST /rknn/threshold/set
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `value` | float | 是 | 置信度阈值，范围 0.0~1.0 |

---

### 7.9 获取检测置信度阈值

```
GET /rknn/threshold/get
Authorization: Bearer {token}
```

---

### 7.10 播放视频文件

```
POST /rknn/video/start
Authorization: Bearer {token}
```

**Query 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `path` | string | 是 | 视频文件路径 |
| `loop` | boolean | 否 | 是否循环播放，默认 true |

---

### 7.11 停止视频播放

```
POST /rknn/video/stop
Authorization: Bearer {token}
```

> 停止视频后恢复摄像头实时流。

---

### 7.12 获取视频播放状态

```
GET /rknn/video/status
Authorization: Bearer {token}
```

---

## 八、文件管理接口 `/file`

> Base URL: `http://localhost:8080/api/file`
> 部分接口需登录认证。

### 8.1 上传文件

```
POST /file/upload/{bucket}
Authorization: Bearer {token}
Content-Type: multipart/form-data
```

**Path 参数：**

| 参数名 | 说明 |
|--------|------|
| `bucket` | 存储桶名称（如 `models`、`avatars`、`images`） |

**Form 参数：**

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `file` | File | 是 | 待上传文件 |
| `username` | string | 否 | 用户名（用于区分文件归属） |
| `is_cache` | string | 否 | 是否为缓存文件（`true` / `false`） |

> `models` 桶限制单个文件不超过 200MB。

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "bucket": "models",
    "objectKey": "default/yolov8n.onnx",
    "url": "/api/file/models/default/yolov8n.onnx",
    "isCache": false
  }
}
```

---

### 8.2 获取文件

```
GET /file/{bucket}/**
```

> 无需认证。文件访问路径对应上传时生成的 `url` 字段。

---

### 8.3 删除文件

```
DELETE /file/{bucket}/**
Authorization: Bearer {token}
```

---

## 九、系统接口

### 9.1 健康检查

```
GET /api/test/health
```

**响应示例：**

```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "status": "ok",
    "message": "嵌入式多目标追踪与智能预警系统的设计与实现后端服务正常运行",
    "timestamp": "2026-04-04T12:00:00",
    "version": "1.0.0"
  }
}
```

---

## 十、数据库表结构

### 10.1 detection_records（检测记录表）

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | BIGINT | 主键，自增 |
| `camera_id` | BIGINT | 摄像头ID（环境=0，声音=-1） |
| `camera_name` | VARCHAR(100) | 摄像头名称 |
| `detection_time` | DATETIME(6) | 检测时间 |
| `ai_description` | TEXT | AI 检测结果描述 |
| `image_url` | VARCHAR(500) | 检测图片URL |
| `is_processed` | BIT(1) | 是否已处理 |
| `process_notes` | TEXT | 处理备注/环境详细数据 |
| `process_image_url` | VARCHAR(500) | 处理现场图片URL |
| `processed_time` | DATETIME(6) | 处理时间 |
| `audio_url` | VARCHAR(500) | 音频URL（声音异常） |
| `audio_duration` | FLOAT | 音频时长（声音异常） |
| `sound_keywords` | VARCHAR(200) | 声音关键词（声音异常） |
| `create_time` | DATETIME(6) | 记录创建时间 |

---

### 10.2 sensor_data（传感器数据表）

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | BIGINT | 主键，自增 |
| `temperature` | FLOAT | 温度（°C） |
| `humidity` | FLOAT | 湿度（%） |
| `smoke` | FLOAT | 烟雾浓度（ppm） |
| `light` | FLOAT | 光照强度（lux） |
| `alert_message` | VARCHAR(500) | 报警信息 |
| `create_time` | DATETIME(6) | 记录时间 |

---

### 10.3 cameras（监控设备表）

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | BIGINT | 主键，自增 |
| `name` | VARCHAR(100) | 设备名称 |
| `location` | VARCHAR(200) | 安装位置 |
| `rtsp_url` | VARCHAR(500) | RTSP 流地址 |
| `status` | INT | 在线状态（1=在线，0=离线） |
| `is_enabled` | BIT(1) | 是否启用 |
| `detection_enabled` | BIT(1) | 是否启用检测 |
| `create_time` | DATETIME(6) | 创建时间 |
| `update_time` | DATETIME(6) | 更新时间 |

---

### 10.4 rknn_model_profiles（模型资料表）

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | BIGINT | 主键，自增 |
| `username` | VARCHAR(64) | 所属用户名 |
| `base_name` | VARCHAR(200) | 模型基名 |
| `model_object_key` | VARCHAR(500) | `.rknn` 文件对象键 |
| `label_object_key` | VARCHAR(500) | `.txt` 标签文件对象键 |
| `selected` | BIT(1) | 是否被选中 |
| `create_time` | DATETIME(6) | 创建时间 |
| `update_time` | DATETIME(6) | 更新时间 |

---

### 10.5 sound_event（声音事件表）

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | BIGINT | 主键，自增 |
| `sound_type` | VARCHAR(255) | 声音类型 |
| `confidence` | FLOAT | 置信度 |
| `keywords` | VARCHAR(500) | 关键词 |
| `duration` | FLOAT | 持续时长（秒） |
| `audio_path` | VARCHAR(500) | 音频文件路径 |
| `start_time` | DATETIME(6) | 开始时间 |
| `end_time` | DATETIME(6) | 结束时间 |
| `create_time` | DATETIME(6) | 创建时间 |

---

### 10.6 users（用户表）

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | BIGINT | 主键，自增 |
| `username` | VARCHAR(50) | 用户名（唯一） |
| `password` | VARCHAR(255) | 密码（BCrypt加密） |
| `real_name` | VARCHAR(50) | 真实姓名 |
| `phone` | VARCHAR(20) | 手机号 |
| `email` | VARCHAR(100) | 电子邮箱 |
| `role` | INT | 角色（1=管理员，0=普通用户） |
| `status` | INT | 状态（1=启用，0=禁用） |
| `avatar_url` | VARCHAR(255) | 头像URL |
| `create_time` | DATETIME(6) | 创建时间 |
| `update_time` | DATETIME(6) | 更新时间 |

---

### 10.7 sensor_threshold（传感器阈值表）

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | BIGINT | 主键，自增 |
| `temperature_threshold` | FLOAT | 温度阈值（°C） |
| `humidity_threshold` | FLOAT | 湿度阈值（%） |
| `smoke_threshold` | FLOAT | 烟雾阈值（ppm） |
| `light_threshold` | FLOAT | 光照阈值（lux） |

---

## 十一、三大安全事件类型

本系统将所有安全记录统一分为三大类：

| 事件类型 | 判断条件 | camera_id | 示例 |
|---------|---------|-----------|------|
| **环境异常** | `aiDescription` 以 `env_` 开头 | 0 | 温度超过阈值、湿度超标、烟雾超标、光照异常 |
| **声音异常** | `audioUrl` 非空 | -1 | 声音监测设备检测到的各类异常 |
| **视频异常** | 其他情况 | 正数 | 摄像头检测到的各类目标 |

---

## 十二、补充接口（2026-04 更新）

### 12.1 录像代理接口 `/rknn`

#### 获取录像状态

```
GET /rknn/record/status
```

返回视觉服务当前录像目录、`ffmpeg` 路径，以及 `cam0/cam1` 的录像状态。

#### 开始录像

```
POST /rknn/record/start?cameraId=1
POST /rknn/record/start?cameraId=2
```

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `cameraId` | int | 是 | `1 -> cam0`，`2 -> cam1` |
| `name` | string | 否 | 自定义输出文件名 |

#### 停止录像

```
POST /rknn/record/stop?cameraId=1
```

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `cameraId` | int | 否 | 不传时停止全部录像 |

#### 获取录像文件列表

```
GET /rknn/record/files?cameraId=1
```

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `cameraId` | int | 否 | `1 -> cam0`，`2 -> cam1`，不传则返回全部 |

#### 下载录像文件

```
GET /rknn/record/file?name=cam0_20260410_183910.mp4
```

说明：

- 仅允许下载录像目录中的 `.mp4` 文件
- 文件名不允许携带路径分隔符

### 12.2 检测记录删除接口 `/detection/record`

#### 删除单条记录

```
DELETE /detection/record/{id}
```

#### 批量删除记录

```
DELETE /detection/record/batch
Content-Type: application/json
```

请求体示例：

```json
[101, 102, 103]
```

#### 清空全部记录

```
DELETE /detection/record/clear-all
```

---

*文档生成时间：2026年4月*
