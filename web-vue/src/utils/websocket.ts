export interface WebSocketMessage {
  type: 'camera_status' | 'start_stream' | 'stop_stream' | 'check_camera' | 'stream_stopped' | 'detection_result'
  data: {
    wsId?: string      // WebSocket连接唯一标识
    cameraId: number   // 摄像头ID
    rtspUrl?: string   // RTSP地址
    frame?: string     // base64编码的图片
    status?: number    // 摄像头状态
    // 通用检测结果字段
    detectedObjects?: Array<{
      class: string
      confidence: number
      bbox: number[]
      level: number
    }>
    // 对象跟踪结果
    trackedObjects?: Array<{
      class: string
      confidence: number
      bbox: number[]
      level: number
      trackId: number
    }>
    modelType?: string    // 模型类型
    supportedClasses?: string[] // 支持的类别
    processTime?: number  // 处理时间
    [key: string]: any    // 其他字段
  }
}

// WebSocket服务器URL（已不再默认指向 Flask:5000；未配置则禁用 WebSocket）
const WS_URL = import.meta.env.VITE_ALGO_WS_URL || ''

class WebSocketClient {
  private ws: WebSocket | null = null
  private url: string
  private reconnectAttempts = 0
  private maxReconnectAttempts = 10 // 增加重连尝试次数
  private reconnectTimeout = 2000 // 减少重连超时时间
  private messageHandlers: ((message: WebSocketMessage) => void)[] = []
  private isActiveClose = false
  private reconnectTimer: number | null = null
  private messageQueue: any[] = []  // 消息队列
  private isConnecting = false  // 连接状态标记
  private pingInterval: number | null = null // 心跳检测定时器

  constructor(url: string) {
    this.url = url
    console.log('WebSocket URL:', url)
  }

  // 连接WebSocket
  connect() {
    if (!this.url) {
      console.warn('未配置 VITE_ALGO_WS_URL，WebSocket 已禁用')
      return
    }
    // 如果已经连接，不要重复连接
    if (this.ws?.readyState === WebSocket.OPEN) {
      console.log('WebSocket已经连接')
      this.processMessageQueue()  // 处理可能存在的消息队列
      this.startPing() // 启动心跳
      return
    }

    // 如果正在连接，等待连接完成
    if (this.ws?.readyState === WebSocket.CONNECTING || this.isConnecting) {
      console.log('WebSocket正在连接中')
      return
    }

    // 如果存在旧的连接，先清理
    this.cleanup()

    // 重置状态
    this.isActiveClose = false
    this.isConnecting = true
    console.log('正在连接WebSocket:', this.url)

    try {
      this.ws = new WebSocket(this.url)

      this.ws.onopen = () => {
        console.log('WebSocket连接成功')
        this.reconnectAttempts = 0
        this.clearReconnectTimer()
        this.isConnecting = false
        this.processMessageQueue()  // 连接成功后处理消息队列
        this.startPing() // 启动心跳
      }

      this.ws.onmessage = (event) => {
        try {
          const message: WebSocketMessage = JSON.parse(event.data)
          this.messageHandlers.forEach(handler => handler(message))
        } catch (error) {
          console.error('WebSocket消息解析失败:', error)
        }
      }

      this.ws.onclose = (event) => {
        console.log('WebSocket连接关闭:', {
          code: event.code,
          reason: event.reason,
          wasClean: event.wasClean,
          isActiveClose: this.isActiveClose
        })

        this.isConnecting = false

        // 只有在非主动关闭且不是正常关闭的情况下才重连
        if (!this.isActiveClose && !event.wasClean) {
          this.scheduleReconnect()
        } else {
          console.log('WebSocket正常关闭，不进行重连')
          this.cleanup()
        }
      }

      this.ws.onerror = (event) => {
        this.isConnecting = false
        // 只在开发环境下打印详细错误
        if (import.meta.env.DEV) {
          console.error('WebSocket错误:', event)
        }
        // 生产环境下打印简化的错误信息
        console.error('WebSocket连接出错')
      }
    } catch (error) {
      this.isConnecting = false
      console.error('创建WebSocket连接失败:', error)
      this.scheduleReconnect()
    }
  }

  // 启动心跳检测
  private startPing() {
    this.clearPing()
    this.pingInterval = window.setInterval(() => {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        try {
          // 发送ping消息
          this.ws.send(JSON.stringify({
            type: 'ping',
            data: { timestamp: Date.now() }
          }))
        } catch (error) {
          console.error('发送心跳消息失败:', error)
          this.clearPing()
          this.scheduleReconnect()
        }
      } else {
        this.clearPing()
        this.scheduleReconnect()
      }
    }, 30000) // 每30秒发送一次心跳
  }

  // 清除心跳检测
  private clearPing() {
    if (this.pingInterval) {
      clearInterval(this.pingInterval)
      this.pingInterval = null
    }
  }

  // 处理消息队列
  private processMessageQueue() {
    while (this.messageQueue.length > 0) {
      const message = this.messageQueue.shift()
      this.sendImmediate(message)
    }
  }

  // 立即发送消息
  private sendImmediate(message: any) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.error('WebSocket未连接，消息发送失败')
      return
    }

    try {
      this.ws.send(JSON.stringify(message))
    } catch (error) {
      console.error('发送消息失败:', error)
    }
  }

  // 发送消息
  send(message: any) {
    if (!this.url) {
      console.warn('未配置 VITE_ALGO_WS_URL，忽略 WebSocket 消息', message)
      return
    }
    // 如果WebSocket未连接，先连接
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.log('WebSocket未连接，将消息加入队列并尝试连接')
      this.messageQueue.push(message)
      this.connect()
      return
    }

    this.sendImmediate(message)
  }

  // 清理资源
  private cleanup() {
    if (this.ws) {
      // 移除所有事件监听器
      this.ws.onopen = null
      this.ws.onclose = null
      this.ws.onmessage = null
      this.ws.onerror = null

      // 如果连接还在打开状态，尝试正常关闭
      if (this.ws.readyState === WebSocket.OPEN) {
        try {
          this.ws.close(1000, 'Normal closure')
        } catch (error) {
          console.error('关闭WebSocket连接失败:', error)
        }
      }

      this.ws = null
    }

    this.clearReconnectTimer()
    this.clearPing()
  }

  // 清除重连定时器
  private clearReconnectTimer() {
    if (this.reconnectTimer) {
      window.clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
  }

  // 安排重连
  private scheduleReconnect() {
    if (this.reconnectAttempts >= this.maxReconnectAttempts) {
      console.error('WebSocket重连次数超过最大限制')
      this.cleanup()
      return
    }

    this.reconnectAttempts++
    console.log('WebSocket计划第' + this.reconnectAttempts + '次重连')

    this.clearReconnectTimer()
    this.reconnectTimer = window.setTimeout(() => {
      console.log('执行重连...')
      this.connect()
    }, this.reconnectTimeout)
  }

  // 添加消息处理器
  addMessageHandler(handler: (message: WebSocketMessage) => void) {
    this.messageHandlers.push(handler)
  }

  // 移除消息处理器
  removeMessageHandler(handler: (message: WebSocketMessage) => void) {
    const index = this.messageHandlers.indexOf(handler)
    if (index !== -1) {
      this.messageHandlers.splice(index, 1)
    }
  }

  // 关闭连接
  close() {
    console.log('主动关闭WebSocket连接')
    this.isActiveClose = true
    this.cleanup()
  }

  // 检查WebSocket是否已连接
  isConnected(): boolean {
    return this.ws !== null && this.ws.readyState === WebSocket.OPEN
  }
}

// 创建WebSocket客户端实例
const wsClient = new WebSocketClient(WS_URL)

export default wsClient 