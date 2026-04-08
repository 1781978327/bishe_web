# 嵌入式多目标追踪与智能预警系统 - 后端服务

## 项目简介
这是嵌入式多目标追踪与智能预警系统的后端服务，基于Spring Boot 3.x构建。

## 技术栈
- Java 17
- Spring Boot 3.2.0
- Spring Security
- Spring Data JPA
- MySQL 8.0
- JWT
- WebSocket
- Lombok

## 功能模块
- 用户管理（注册、登录、权限控制）
- 检测记录管理
- 摄像头设备管理
- WebSocket实时通信
- JWT认证

## 快速开始

### 1. 环境准备
- JDK 17+
- MySQL 8.0+
- Gradle 7.0+

### 2. 数据库配置
1. 创建数据库：
```sql
CREATE DATABASE campus_violence CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
```

2. 修改 `application.yml` 中的数据库连接信息：
```yaml
spring:
  datasource:
    url: jdbc:mysql://localhost:3306/campus_violence?useUnicode=true&characterEncoding=utf8&useSSL=false&serverTimezone=Asia/Shanghai
    username: root
    password: 你的密码
```

### 3. 运行项目
```bash
./gradlew bootRun
```

或者使用IDE运行 `DemoApplication.java`

### 4. 访问接口
- 健康检查：http://localhost:8080/api/test/health
- 用户注册：POST http://localhost:8080/api/user/register
- 用户登录：POST http://localhost:8080/api/user/login

## API文档

### 用户注册
```
POST /api/user/register
Content-Type: application/json

{
  "username": "testuser",
  "password": "123456",
  "realName": "测试用户",
  "phone": "13800138000",
  "email": "test@example.com"
}
```

### 用户登录
```
POST /api/user/login
Content-Type: application/json

{
  "username": "testuser",
  "password": "123456"
}
```

响应：
```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {
    "userInfo": {
      "id": 1,
      "username": "testuser",
      "realName": "测试用户",
      "role": 0,
      "status": 1
    },
    "token": "eyJhbGciOiJIUzI1NiJ9..."
  }
}
```

## 默认管理员账户
- 用户名：admin
- 密码：Lml123

## 项目结构
```
src/main/java/com/example/demo/
├── config/          # 配置类
├── controller/      # 控制器
├── service/         # 业务逻辑
├── repository/      # 数据访问
├── entity/          # 实体类
├── dto/             # 数据传输对象
└── utils/           # 工具类
```

## 注意事项
1. 确保MySQL服务已启动
2. 首次运行会自动创建表结构
3. JWT token有效期为24小时
4. 默认端口为8080，上下文路径为/api
