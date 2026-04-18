<template>
  <div class="video-player" @click="handleClick">
    <div class="video-container" ref="videoContainer">
      <!-- 有算法 WebSocket 时：画布显示算法端推送的帧；无 WS 时：播放 RTSP 对应的 WebRTC / HLS -->
      <template v-if="algoWsEnabled">
        <canvas ref="canvas" :width="width" :height="height"></canvas>
      </template>
      <template v-else>
        <video
          ref="videoEl"
          class="video-el"
          :width="width"
          :height="height"
          muted
          autoplay
          playsinline
          controls
        ></video>
      </template>
      
      <!-- 通用目标检测框 -->
      <template v-for="(obj, index) in detectedObjects" :key="index">
        <div 
          class="detection-box" 
          :class="{ 'tracking-box': isTrackingEnabled && obj.trackId !== undefined }"
          :style="getBoxStyle(obj.bbox, obj.class)"
          @click.stop="handleObjectClick(obj)"
        >
          <span class="confidence" :style="getLabelStyle(obj.bbox, obj.class)">
            {{ obj.class }} {{ (obj.confidence * 100).toFixed(0) }}%
            <span v-if="getTrackId(obj)" class="track-id">#{{ getTrackId(obj) }}</span>
          </span>
        </div>
      </template>
    </div>
    
    <!-- 摄像头信息 -->
    <div class="camera-info">
      <span class="name">{{ camera.name }}</span>
      <span class="status" :class="camera.status === 1 ? 'online' : 'offline'">
        {{ camera.status === 1 ? '在线' : '离线' }}
      </span>
    </div>
    
    <!-- 通用预警 -->
    <div v-if="hasDetectionAlert" class="detection-alert-overlay" :class="{ 'alert-active': hasDetectionAlert }">
      <!-- 警告容器，应用缩放样式 -->
      <div class="alert-container" :style="alertScaleStyle">
        <!-- 警告图标 -->
        <div class="alert-icon">
          <el-icon><Warning /></el-icon>
        </div>
        
        <!-- 警告信息 -->
        <div class="alert-info">
          <div class="alert-title">
              <span class="warning-text">预警信息</span>
          </div>
          <div class="alert-details">
            <div class="detail-item">
              <span class="label">位置：</span>
              <span class="value">{{ camera.location || '未知位置' }}</span>
            </div>
            <div v-for="(result, index) in alertResults" :key="index" class="detail-item">
              <span class="label">目标 {{ result.groupIndex }}：</span>
              <span class="value">
                {{ result.description }}
              </span>
            </div>
          </div>
        </div>
      </div>
    </div>
    
    <!-- 目标截图显示区域 -->
    <div v-if="groupImages.length > 0" class="group-images-panel">
      <div class="panel-header">
        <span class="panel-title">
          <el-icon><PictureRounded /></el-icon>
          目标截图 ({{ groupImages.length }})
        </span>
        <el-button 
          type="primary" 
          size="small" 
          text 
          @click="toggleGroupImagesPanel"
          class="toggle-btn"
        >
          <el-icon><CaretBottom v-if="groupImagesPanelExpanded" /><CaretRight v-else /></el-icon>
        </el-button>
      </div>
      <div v-show="groupImagesPanelExpanded" class="panel-content">
        <div class="group-images-grid">
          <div 
            v-for="(groupImage, index) in groupImages" 
            :key="index"
            class="group-image-item"
            @click="previewGroupImage(groupImage, index)"
          >
            <div class="image-wrapper">
              <img
                v-if="groupImage.imageBase64"
                :src="`data:image/jpeg;base64,${groupImage.imageBase64}`"
                :alt="`目标${groupImage.groupIndex}`"
                class="group-image"
                @error="handleImageError"
              />
              <div v-else class="image-placeholder">
                <el-icon><Picture /></el-icon>
                <span>无图片</span>
              </div>
            </div>
            <div class="image-label">
              <span class="group-title">目标 {{ groupImage.groupIndex }}</span>
              <span v-if="groupImage.bbox" class="bbox-info">
                {{ formatGroupBbox(groupImage.bbox) }}
              </span>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch, computed } from 'vue'
import { useRouter } from 'vue-router'
import type { Camera } from '@/types/camera'
import wsClient from '@/utils/websocket'
import type { WebSocketMessage } from '@/utils/websocket'
import { ElMessage } from 'element-plus'
import { addDetectionRecord } from '@/api/detection'
import { 
  Warning, 
  PictureRounded, 
  CaretBottom, 
  CaretRight, 
  Picture 
} from '@element-plus/icons-vue'

const props = defineProps<{
  camera: Camera
  isActive: boolean
  serverDrawEnabled?: boolean
  width?: number
  height?: number
}>()

const emit = defineEmits<{
  (e: 'click', camera: Camera): void
}>()

const router = useRouter()

// 组件状态
const canvas = ref<HTMLCanvasElement | null>(null)
const ctx = ref<CanvasRenderingContext2D | null>(null)
const videoEl = ref<HTMLVideoElement | null>(null)
const hls = ref<any>(null)
const videoContainer = ref<HTMLDivElement | null>(null)
const detectedObjects = ref<any[]>([])
const frameCount = ref(0)  // 添加帧计数器
const classColors = ref<Record<string, string>>({}) // 类别颜色配置
const isTrackingEnabled = ref(false) // 是否启用了跟踪功能
const HLS_RETRY_DELAY_MS = 1200
const HLS_MAX_RETRIES = 8
let hlsRetryTimer: number | null = null
let hlsRetryCount = 0
const WEBRTC_RETRY_DELAY_MS = 1200
const WEBRTC_MAX_RETRIES = 8
let webrtcRetryTimer: number | null = null
let webrtcRetryCount = 0
let webrtcPc: RTCPeerConnection | null = null
let whepSessionUrl: string | null = null
// 预警检测相关状态
const hasAlert = ref(false) // 是否检测到预警事件
const alertResults = ref<any[]>([]) // 预警检测结果
const personGroups = ref<any[]>([]) // 人物群组信息
const groupImages = ref<any[]>([]) // 群组图片base64数组
const groupImagesPanelExpanded = ref(true) // 群组图片面板是否展开
const currentDrawArea = ref<{ x: number; y: number; width: number; height: number } | null>(null)
// 添加存储实际图像尺寸的变量
const actualImageWidth = ref(0)
const actualImageHeight = ref(0)
// 添加存储检测对象信息的变量，用于显示警告信息，即使serverDrawEnabled为true
const detectionInfo = ref<any[]>([])
// 存储ResizeObserver清理函数
let cleanupResizeObserver: (() => void) | undefined = undefined
// 页面可见性状态
const isPageVisible = ref(true)

// 页面可见性变化处理
const handleVisibilityChange = () => {
  isPageVisible.value = !document.hidden
  console.log('页面可见性变更:', isPageVisible.value ? '可见' : '不可见')
  
  if (document.hidden) {
    // 页面不可见时，暂停HLS播放以节省资源
    if (videoEl.value && !videoEl.value.paused) {
      videoEl.value.pause()
    }
  } else {
    // 页面重新可见时，恢复播放
    if (videoEl.value && props.isActive) {
      tryPlayVideo().then((played) => {
        if (!played) {
          if (isWebRtcMode.value) {
            scheduleWebRtcRetry('页面恢复可见后播放失败')
          } else {
            scheduleHlsRetry('页面恢复可见后播放失败')
          }
        }
      })
    }
  }
}

// 计算是否有预警（用于警告显示）
const hasDetectionAlert = computed(() => {
  return hasAlert.value && alertResults.value.length > 0
})

const algoWsEnabled = computed(() => !!import.meta.env.VITE_ALGO_WS_URL)
const streamProtocol = (((import.meta.env.VITE_STREAM_PROTOCOL as string | undefined) || 'webrtc')).toLowerCase()
const isWebRtcMode = computed(() => !algoWsEnabled.value && streamProtocol === 'webrtc')

const getCurrentPageHost = (): string => {
  if (typeof window === 'undefined') return ''
  return window.location.hostname.trim()
}

const resolveStreamHost = (rtspUrl: string): string => {
  const overrideHost = ((import.meta.env.VITE_STREAM_HOST as string | undefined) || '').trim()
  if (overrideHost) return overrideHost

  const pageHost = getCurrentPageHost()
  if (pageHost) return pageHost

  try {
    return new URL(rtspUrl).hostname
  } catch {
    return ''
  }
}

const rtspToHlsUrl = (rtspUrl: string): string => {
  try {
    const u = new URL(rtspUrl)
    const host = resolveStreamHost(rtspUrl)
    const path = u.pathname.replace(/^\/+/, '')
    if (!host || !path) return ''
    return `http://${host}:8888/${path}/index.m3u8`
  } catch {
    return ''
  }
}

const rtspToWhepUrl = (rtspUrl: string): string => {
  try {
    const u = new URL(rtspUrl)
    const host = resolveStreamHost(rtspUrl)
    const path = u.pathname.replace(/^\/+/, '')
    if (!host || !path) return ''

    const base = ((import.meta.env.VITE_WEBRTC_BASE_URL as string | undefined) || '').trim()
    const webrtcBaseUrl = base ? base.replace(/\/+$/, '') : `http://${host}:8889`
    return `${webrtcBaseUrl}/${path}/whep`
  } catch {
    return ''
  }
}

const clearHlsRetryTimer = () => {
  if (hlsRetryTimer !== null) {
    window.clearTimeout(hlsRetryTimer)
    hlsRetryTimer = null
  }
}

const clearWebRtcRetryTimer = () => {
  if (webrtcRetryTimer !== null) {
    window.clearTimeout(webrtcRetryTimer)
    webrtcRetryTimer = null
  }
}

const tryPlayVideo = async (): Promise<boolean> => {
  if (!videoEl.value) return false
  try {
    await videoEl.value.play()
    return true
  } catch (error) {
    console.warn('[Player] play() 失败:', error)
    return false
  }
}

const scheduleHlsRetry = (reason: string) => {
  if (!props.isActive) return
  if (hlsRetryCount >= HLS_MAX_RETRIES) {
    console.error(`[HLS] ${reason}，达到最大重试次数(${HLS_MAX_RETRIES})`)
    return
  }

  hlsRetryCount += 1
  const currentRetry = hlsRetryCount
  clearHlsRetryTimer()
  console.warn(`[HLS] ${reason}，${HLS_RETRY_DELAY_MS}ms 后重试 (${currentRetry}/${HLS_MAX_RETRIES})`)
  hlsRetryTimer = window.setTimeout(() => {
    startHlsPlayback(true)
  }, HLS_RETRY_DELAY_MS)
}

const scheduleWebRtcRetry = (reason: string) => {
  if (!props.isActive || !isWebRtcMode.value) return
  if (webrtcRetryCount >= WEBRTC_MAX_RETRIES) {
    console.error(`[WebRTC] ${reason}，达到最大重试次数(${WEBRTC_MAX_RETRIES})`)
    return
  }

  webrtcRetryCount += 1
  const currentRetry = webrtcRetryCount
  clearWebRtcRetryTimer()
  console.warn(`[WebRTC] ${reason}，${WEBRTC_RETRY_DELAY_MS}ms 后重试 (${currentRetry}/${WEBRTC_MAX_RETRIES})`)
  webrtcRetryTimer = window.setTimeout(() => {
    startWebRtcPlayback(true)
  }, WEBRTC_RETRY_DELAY_MS)
}

const destroyHls = () => {
  if (hls.value) {
    try { hls.value.destroy() } catch {}
    hls.value = null
  }
  if (videoEl.value) {
    videoEl.value.removeAttribute('src')
    try { videoEl.value.load() } catch {}
  }
}

const waitForIceGatheringComplete = (pc: RTCPeerConnection, timeoutMs = 3000): Promise<void> => {
  if (pc.iceGatheringState === 'complete') {
    return Promise.resolve()
  }

  return new Promise((resolve) => {
    const onStateChange = () => {
      if (pc.iceGatheringState === 'complete') {
        cleanup()
      }
    }
    const cleanup = () => {
      window.clearTimeout(timer)
      pc.removeEventListener('icegatheringstatechange', onStateChange)
      resolve()
    }
    const timer = window.setTimeout(cleanup, timeoutMs)
    pc.addEventListener('icegatheringstatechange', onStateChange)
  })
}

const cleanupWhepSession = () => {
  if (!whepSessionUrl) return
  const sessionUrl = whepSessionUrl
  whepSessionUrl = null
  fetch(sessionUrl, { method: 'DELETE' }).catch(() => {})
}

const destroyWebRtcPeer = () => {
  if (webrtcPc) {
    try { webrtcPc.close() } catch {}
    webrtcPc = null
  }

  cleanupWhepSession()

  if (videoEl.value && videoEl.value.srcObject) {
    videoEl.value.srcObject = null
  }
}

const startWebRtcPlayback = async (fromRetry = false) => {
  if (!videoEl.value || !props.isActive || !isWebRtcMode.value) return
  if (typeof RTCPeerConnection === 'undefined') {
    console.error('[WebRTC] 当前浏览器不支持 RTCPeerConnection')
    return
  }

  if (!fromRetry) {
    clearWebRtcRetryTimer()
    webrtcRetryCount = 0
  }

  const whepUrl = rtspToWhepUrl(props.camera.rtspUrl)
  console.log('[WebRTC] whep url =', whepUrl, 'from rtsp =', props.camera.rtspUrl)
  if (!whepUrl) {
    console.warn('[WebRTC] 无法从 RTSP 地址生成 WHEP 地址:', props.camera.rtspUrl)
    return
  }

  destroyWebRtcPeer()

  const pc = new RTCPeerConnection({ iceServers: [] })
  webrtcPc = pc
  pc.addTransceiver('video', { direction: 'recvonly' })

  pc.ontrack = async (event) => {
    if (!videoEl.value) return
    const mediaStream = event.streams[0] || new MediaStream([event.track])
    videoEl.value.srcObject = mediaStream
    const played = await tryPlayVideo()
    if (!played) {
      scheduleWebRtcRetry('WebRTC 建连成功但自动播放失败')
      return
    }
    clearWebRtcRetryTimer()
    webrtcRetryCount = 0
  }

  pc.onconnectionstatechange = () => {
    if (!props.isActive || !isWebRtcMode.value || webrtcPc !== pc) return
    if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected') {
      console.warn('[WebRTC] connectionState =', pc.connectionState)
      destroyWebRtcPeer()
      scheduleWebRtcRetry(`连接状态异常: ${pc.connectionState}`)
    }
  }

  try {
    const offer = await pc.createOffer()
    await pc.setLocalDescription(offer)
    await waitForIceGatheringComplete(pc)

    const localSdp = pc.localDescription?.sdp
    if (!localSdp) {
      throw new Error('未生成本地 SDP')
    }

    const response = await fetch(whepUrl, {
      method: 'POST',
      headers: { 'Content-Type': 'application/sdp' },
      body: localSdp
    })
    if (!response.ok) {
      throw new Error(`WHEP 握手失败: HTTP ${response.status}`)
    }

    const answerSdp = await response.text()
    const locationHeader = response.headers.get('location')
    if (locationHeader) {
      whepSessionUrl = new URL(locationHeader, whepUrl).toString()
    }

    await pc.setRemoteDescription({
      type: 'answer',
      sdp: answerSdp
    })
  } catch (error) {
    console.error('[WebRTC] 播放失败:', error)
    destroyWebRtcPeer()
    scheduleWebRtcRetry('WHEP 建连失败')
  }
}

const startHlsPlayback = async (fromRetry = false) => {
  if (!videoEl.value || !props.isActive) return
  if (!fromRetry) {
    clearHlsRetryTimer()
    hlsRetryCount = 0
  }

  const hlsUrl = rtspToHlsUrl(props.camera.rtspUrl)
  console.log('[HLS] url =', hlsUrl, 'from rtsp =', props.camera.rtspUrl)
  if (!hlsUrl) {
    console.warn('[HLS] 无法从 RTSP 地址生成 HLS 地址:', props.camera.rtspUrl)
    return
  }

  // 原生 HLS（Safari/部分平台）
  if (videoEl.value.canPlayType('application/vnd.apple.mpegurl')) {
    videoEl.value.src = hlsUrl
    const played = await tryPlayVideo()
    if (!played) {
      scheduleHlsRetry('原生 HLS 自动播放失败')
    } else {
      clearHlsRetryTimer()
      hlsRetryCount = 0
    }
    return
  }

  // 其余浏览器用 hls.js
  const mod = await import('hls.js')
  const Hls = mod.default
  if (!Hls.isSupported()) {
    console.warn('当前浏览器不支持 HLS 播放')
    return
  }

  destroyHls()
  hls.value = new Hls({
    lowLatencyMode: true,
    manifestLoadingMaxRetry: 6,
    levelLoadingMaxRetry: 6,
    fragLoadingMaxRetry: 6
  })

  hls.value.on(Hls.Events.MANIFEST_PARSED, async () => {
    const played = await tryPlayVideo()
    if (!played) {
      scheduleHlsRetry('HLS 清单就绪但播放失败')
      return
    }
    clearHlsRetryTimer()
    hlsRetryCount = 0
  })

  hls.value.on(Hls.Events.ERROR, (_evt: any, data: any) => {
    console.error('[HLS] error', data)
    if (!data?.fatal) return

    if (data.type === Hls.ErrorTypes.NETWORK_ERROR) {
      destroyHls()
      scheduleHlsRetry('HLS 网络错误')
      return
    }
    if (data.type === Hls.ErrorTypes.MEDIA_ERROR) {
      try {
        hls.value?.recoverMediaError()
      } catch {
        destroyHls()
        scheduleHlsRetry('HLS 媒体错误恢复失败')
      }
      return
    }

    destroyHls()
    scheduleHlsRetry('HLS 致命错误')
  })
  hls.value.loadSource(hlsUrl)
  hls.value.attachMedia(videoEl.value)
}

const stopHlsPlayback = () => {
  clearHlsRetryTimer()
  hlsRetryCount = 0
  destroyHls()
}

const stopWebRtcPlayback = () => {
  clearWebRtcRetryTimer()
  webrtcRetryCount = 0
  destroyWebRtcPeer()
}

// 存储是否已经保存了当前的检测记录，避免重复保存
const alreadySaved = ref(false)

// 保存检测记录
const saveDetectionRecord = async () => {
  try {
    // 如果没有检测到预警或已经保存过，则不保存
    if (!hasAlert.value || alreadySaved.value) {
      return
    }
    
    // 将canvas转换为base64图像
    const imageBase64 = canvas.value?.toDataURL('image/jpeg').split(',')[1]
    if (!imageBase64) {
      console.error('获取图像数据失败')
      return
    }
    
    // 验证base64字符串是否有效
    if (!isValidBase64(imageBase64)) {
      console.error('生成的base64图像数据无效，无法保存检测记录')
      return
    }
    
    console.log('检测到预警事件，正在保存检测记录...')
    
    try {
      // 保存检测记录，包含预警检测结果和相关信息
      await addDetectionRecord({
        cameraId: props.camera.id,
        imageBase64,
        detectionResult: JSON.stringify({
          detectedObjects: detectionInfo.value,
          alertResults: alertResults.value,
          hasAlert: hasAlert.value,
          personGroups: personGroups.value,
          groupImages: groupImages.value, // 包含群组图片base64数组
          timestamp: new Date().toISOString()
        })
      })
      
      console.log('预警检测记录已保存')
      alreadySaved.value = true
    } catch (apiError) {
      console.error('API调用保存检测记录失败:', apiError)
    }
  } catch (error) {
    console.error('保存检测记录失败:', error)
  }
}

// 默认尺寸
const width = props.width || 640
const height = props.height || 480

// 添加一个响应式变量存储容器尺寸
const containerSize = ref({ width: 0, height: 0 })

// 使用ResizeObserver监听容器尺寸变化
const setupResizeObserver = () => {
  if (!videoContainer.value) return
  
  try {
    const resizeObserver = new ResizeObserver((entries) => {
      for (const entry of entries) {
        // 更新容器尺寸
        containerSize.value = {
          width: entry.contentRect.width,
          height: entry.contentRect.height
        }
        console.log(`容器尺寸变化: ${containerSize.value.width}x${containerSize.value.height}`)
      }
    })
    
    // 开始观察
    resizeObserver.observe(videoContainer.value)
    
    // 返回清理函数
    return () => {
      resizeObserver.disconnect()
    }
  } catch (error) {
    console.error('ResizeObserver不可用:', error)
    return () => {}
  }
}

// 警告信息框样式缩放计算
const alertScaleStyle = computed(() => {
  if (!videoContainer.value) return {}
  
  // 使用当前容器尺寸或最新的响应式容器尺寸
  const containerWidth = containerSize.value.width || videoContainer.value.clientWidth
  const containerHeight = containerSize.value.height || videoContainer.value.clientHeight
  
  // 以标准尺寸为基准（假设1宫格的标准宽度为800px）
  const baseWidth = 800
  
  // 根据容器宽度计算缩放比例
  let scale = Math.max(0.5, Math.min(1, containerWidth / baseWidth))
  
  // 根据容器高度进一步调整缩放比例
  if (containerHeight < 300) {
    scale = Math.min(scale, 0.7) // 对于高度较小的容器，进一步降低缩放比例
  }
  
  console.log(`警告框缩放计算: 容器尺寸=${containerWidth}x${containerHeight}, 缩放比例=${scale.toFixed(2)}`)
  
  // 返回transform样式
  return {
    transform: `scale(${scale})`,
    transformOrigin: 'center center'
  }
})

// 清除画面和检测框
const clearDisplay = () => {
  console.log('清除画面:', props.camera.id)
  // 清除画布
  if (ctx.value) {
    ctx.value.fillStyle = '#000'  // 设置为黑色背景
    ctx.value.fillRect(0, 0, width, height)  // 填充整个画布
  }
  // 清除检测框和绘制区域
  detectedObjects.value = []
  detectionInfo.value = [] // 同时清除检测信息
  currentDrawArea.value = null
  // 重置预警检测状态
  hasAlert.value = false
  alertResults.value = []
  personGroups.value = []
  groupImages.value = []
  // 重置图像尺寸信息
  actualImageWidth.value = 0
  actualImageHeight.value = 0
  // 重置帧计数
  frameCount.value = 0
}

// 处理点击事件
const handleClick = () => {
  emit('click', props.camera)
}

// 风险等级（riskLevel/maxRiskLevel）已弃用

// 处理WebSocket消息
const handleMessage = (message: WebSocketMessage) => {
  if (message.data.cameraId !== props.camera.id) return

  // 处理通用检测结果
  if (message.type === 'detection_result') {
    console.log('收到检测结果:', message.data.cameraId)
    
    // 检查是否启用了跟踪功能
    isTrackingEnabled.value = !!message.data.trackingEnabled
    
    frameCount.value++
    if (frameCount.value % 30 === 0) {  // 每30帧打印一次日志
      console.log('已接收', frameCount.value, '帧')
    }
    
    // 暂存检测对象和颜色配置，用于前端绘制或显示对象信息
    const receivedObjects = message.data.detectedObjects || []
    const receivedColors = message.data.classColors
    
    // 从后端数据中获取图像尺寸
    if (message.data.imageWidth && message.data.imageHeight) {
      actualImageWidth.value = message.data.imageWidth
      actualImageHeight.value = message.data.imageHeight
      console.log(`从算法端获取图像实际尺寸: ${actualImageWidth.value}x${actualImageHeight.value}`)
    } else {
      console.warn('算法端未提供图像尺寸信息，无法正确定位检测框！请确保算法端传递imageWidth和imageHeight')
      // 如果没有图像尺寸信息，清空检测对象，避免定位错误
      detectedObjects.value = []
      detectionInfo.value = []
      return
    }
    
    // 如果有图像，更新画布
    if (message.data.frame && typeof message.data.frame === 'string') {
      // 验证base64图像数据是否有效
      if (!isValidBase64(message.data.frame)) {
        console.error('收到无效的base64图像数据')
        return
      }
      
      const image = new Image()
      image.onload = () => {
        if (ctx.value) {
          // 先清除画布
          ctx.value.clearRect(0, 0, width, height)
          
          // 计算保持宽高比的绘制区域
          const drawArea = calculateDrawArea(actualImageWidth.value, actualImageHeight.value, width, height)
          
          // 根据计算的区域绘制图像
          ctx.value.drawImage(
            image, 
            drawArea.x, drawArea.y, 
            drawArea.width, drawArea.height
          )
          
          // 保存绘制区域信息，用于检测框定位
          currentDrawArea.value = drawArea
          
          // 始终保存检测对象信息，用于警告显示
          detectionInfo.value = receivedObjects
          
          // 如果serverDrawEnabled为true，服务器已经绘制了检测框，前端不需要再绘制
          detectedObjects.value = props.serverDrawEnabled ? [] : receivedObjects
          
          // 更新类别颜色配置
          if (receivedColors) {
            classColors.value = receivedColors
          }
          
          // 更新预警检测信息
          if (message.data.alertResults) {
            alertResults.value = message.data.alertResults
          }
          if (message.data.hasAlert !== undefined) {
            hasAlert.value = message.data.hasAlert
          }
          // maxRiskLevel 已弃用
          if (message.data.personGroups) {
            personGroups.value = message.data.personGroups
          }
          if (message.data.groupImages) {
            groupImages.value = message.data.groupImages
          }
          
          // 如果检测到预警事件，保存检测记录
          if (hasAlert.value) {
            // 重置保存状态
            alreadySaved.value = false
            // 保存检测记录
            saveDetectionRecord()
          }
          
          if (props.serverDrawEnabled) {
            console.log('使用服务器端绘制的检测框图像，但仍显示警告信息')
          }
        }
      }
      
      image.onerror = (e) => {
        console.error('图像加载失败:', e)
        console.debug('错误的图像数据长度:', message.data.frame?.length)
        // 打印base64数据前20个字符，帮助调试
        if (message.data.frame) {
          console.debug('图像数据前20个字符:', message.data.frame.substring(0, 20))
        }
      }
      
      try {
        // 处理base64字符串，确保正确的格式
        let base64Data = message.data.frame
        
        // 检查base64数据是否已经包含前缀，避免重复添加
        if (base64Data.startsWith('data:image')) {
          // 已经包含前缀，直接使用
          image.src = base64Data
        } else {
          // 移除可能存在的非base64字符
          base64Data = base64Data.replace(/[^A-Za-z0-9+/=]/g, '')
          // 添加前缀
          image.src = 'data:image/jpeg;base64,' + base64Data
        }
      } catch (error) {
        console.error('设置图像源时发生错误:', error)
      }
    } else {
      console.log('收到的检测结果中没有图像数据')
      clearDisplay()
    }
    return
  }

  // 处理摄像头状态更新
  if (message.type === 'camera_status') {
    console.log('摄像头状态更新:', message.data)
    // 如果摄像头离线，清除画面
    if (message.data.status === 0) {
      clearDisplay()
    }
    return
  }

  // 处理停止流消息
  if (message.type === 'stream_stopped') {
    console.log('收到停止流消息')
    clearDisplay()
    return
  }
}

// 获取对象颜色
const getObjectColor = (className: string): string => {
  // 如果有配置的颜色，使用配置的颜色
  if (classColors.value && classColors.value[className]) {
    return classColors.value[className]
  }
  // 默认返回红色
  return '#FF0000'
}

// 处理检测对象点击
const handleObjectClick = (obj: any) => {
  // 点击对象时先阻止事件冒泡
  console.log('检测到对象点击:', obj)
  // 可以在这里添加对象点击后的特殊处理，例如突出显示此对象
  ElMessage({
    type: 'info',
    message: `点击了 ${obj.class}${obj.trackId !== undefined ? ' #' + obj.trackId : ''}，置信度: ${(obj.confidence * 100).toFixed(1)}%`,
    duration: 2000
  })
}

// 获取跟踪ID (根据算法端实际使用的trackId)
const getTrackId = (obj: any): number | null => {
  // 根据代码审查，算法端使用的属性名是trackId
  return obj && obj.trackId !== undefined ? obj.trackId : null
}

// 获取检测框样式
const getBoxStyle = (box: number[], className: string) => {
  if (!videoContainer.value || !currentDrawArea.value || !actualImageWidth.value || !actualImageHeight.value) return {}
  
  const [x1, y1, x2, y2] = box
  
  // 获取视频容器的实际可见尺寸
  const containerWidth = videoContainer.value.clientWidth
  const containerHeight = videoContainer.value.clientHeight
  
  const drawArea = currentDrawArea.value
  
  console.log(`容器尺寸: ${containerWidth}x${containerHeight}, 画布尺寸: ${width}x${height}, 绘制区域: ${drawArea.x},${drawArea.y},${drawArea.width}x${drawArea.height}`)
  
  // 重要：我们需要考虑以下因素
  // 1. 原始图像尺寸(actualImageWidth/Height)到Canvas绘制区域(drawArea)的映射
  // 2. Canvas绘制区域在Canvas中的定位(drawArea.x/y)
  // 3. Canvas到容器的缩放比例(containerWidth/Height vs width/height)
  
  // 第一步：将检测框坐标从原始图像空间转换到归一化坐标(0-1范围)
  const normalizedX1 = x1 / actualImageWidth.value
  const normalizedY1 = y1 / actualImageHeight.value
  const normalizedX2 = x2 / actualImageWidth.value
  const normalizedY2 = y2 / actualImageHeight.value
  
  // 第二步：将归一化坐标应用到绘制区域
  const drawX1 = drawArea.x + normalizedX1 * drawArea.width
  const drawY1 = drawArea.y + normalizedY1 * drawArea.height
  const drawX2 = drawArea.x + normalizedX2 * drawArea.width
  const drawY2 = drawArea.y + normalizedY2 * drawArea.height
  
  // 第三步：将Canvas坐标转换为容器像素坐标
  const scaleX = containerWidth / width
  const scaleY = containerHeight / height
  
  const boxLeft = drawX1 * scaleX
  const boxTop = drawY1 * scaleY
  const boxWidth = (drawX2 - drawX1) * scaleX
  const boxHeight = (drawY2 - drawY1) * scaleY
  
  // 使用类别对应的颜色
  const borderColor = getObjectColor(className)
  
  // 检测框太小时设置一个最小可见度，但保持宽高比
  const minDimension = 3 // 允许更小的最小尺寸，但保持宽高比
  let finalBoxWidth = boxWidth
  let finalBoxHeight = boxHeight
  
  // 如果宽度太小，按比例增加尺寸
  if (boxWidth < minDimension) {
    const scale = minDimension / boxWidth
    finalBoxWidth = minDimension
    finalBoxHeight = boxHeight * scale
  }
  
  // 如果高度太小，按比例增加尺寸
  if (boxHeight < minDimension) {
    const scale = minDimension / boxHeight
    finalBoxHeight = minDimension
    finalBoxWidth = finalBoxWidth < minDimension ? boxWidth * scale : finalBoxWidth // 只有当宽度尚未调整时才调整
  }
  
  console.log(`检测框坐标转换: 
    原始坐标=[${x1},${y1},${x2},${y2}] 
    归一化坐标=[${normalizedX1.toFixed(3)},${normalizedY1.toFixed(3)},${normalizedX2.toFixed(3)},${normalizedY2.toFixed(3)}]
    画布坐标=[${drawX1.toFixed(1)},${drawY1.toFixed(1)},${drawX2.toFixed(1)},${drawY2.toFixed(1)}]
    容器坐标=[${boxLeft.toFixed(1)},${boxTop.toFixed(1)},${(boxLeft+boxWidth).toFixed(1)},${(boxTop+boxHeight).toFixed(1)}]
    尺寸=${boxWidth.toFixed(1)}x${boxHeight.toFixed(1)}
  `)
  
  return {
    left: boxLeft + 'px',
    top: boxTop + 'px',
    width: finalBoxWidth + 'px',
    height: finalBoxHeight + 'px',
    borderColor
  }
}

// 获取标签样式
const getLabelStyle = (box: number[], className: string) => {
  if (!videoContainer.value || !currentDrawArea.value || !actualImageWidth.value || !actualImageHeight.value) return {}
  
  const [x1, y1, x2, y2] = box
  const containerWidth = videoContainer.value.clientWidth
  const containerHeight = videoContainer.value.clientHeight
  
  const drawArea = currentDrawArea.value
  
  // 使用与检测框相同的坐标转换逻辑 - 归一化坐标法
  const normalizedX1 = x1 / actualImageWidth.value
  const normalizedY1 = y1 / actualImageHeight.value
  const normalizedX2 = x2 / actualImageWidth.value
  const normalizedY2 = y2 / actualImageHeight.value
  
  // 应用到绘制区域
  const drawX1 = drawArea.x + normalizedX1 * drawArea.width
  const drawY1 = drawArea.y + normalizedY1 * drawArea.height
  const drawX2 = drawArea.x + normalizedX2 * drawArea.width
  const drawY2 = drawArea.y + normalizedY2 * drawArea.height
  
  // Canvas坐标转换为容器像素坐标
  const scaleX = containerWidth / width
  const scaleY = containerHeight / height
  
  const boxLeft = drawX1 * scaleX
  const boxTop = drawY1 * scaleY
  const boxRight = drawX2 * scaleX
  const boxBottom = drawY2 * scaleY
  const boxWidth = boxRight - boxLeft
  
  // 估计标签尺寸
  const className_length = className.length
  const estimatedLabelWidth = Math.max(80, className_length * 10 + 40) // 根据类名长度估计宽度
  
  // 检查边缘情况
  const isNearTop = boxTop < 30
  const isNearRight = containerWidth - boxRight < estimatedLabelWidth
  const isNearLeft = boxLeft < 10
  const isNearBottom = containerHeight - boxBottom < 30
  
  // 获取标签背景颜色
  const bgColor = getObjectColor(className)
  
  // 根据位置调整标签样式
  const style: any = {
    backgroundColor: bgColor,
  }
  
  // 垂直位置调整
  if (isNearTop) {
    if (isNearBottom) {
      // 既靠近顶部又靠近底部（框很大时），放在框内顶部
      style.top = '5px'
      style.bottom = 'auto'
    } else {
      // 靠近顶部，放在底部
      style.top = 'auto'
      style.bottom = '-24px'
    }
  } else {
    // 不靠近顶部，默认放在顶部
    style.top = '-24px'
    style.bottom = 'auto'
  }
  
  // 水平位置调整
  if (isNearRight) {
    if (isNearLeft) {
      // 框横跨大部分屏幕宽度
      style.left = 'auto'
      style.right = '0'
    } else {
      // 靠近右边缘，右对齐
      style.left = 'auto'
      style.right = '0'
    }
  } else if (boxWidth < estimatedLabelWidth && boxLeft + boxWidth/2 > containerWidth/2) {
    // 框较小且在屏幕右半部分，右对齐
    style.left = 'auto'
    style.right = '0'
  } else {
    // 默认左对齐
    style.left = '0'
    style.right = 'auto'
  }
  
  return style
}

// 切换群组图片面板展开状态
const toggleGroupImagesPanel = () => {
  groupImagesPanelExpanded.value = !groupImagesPanelExpanded.value
}

// 预览群组图片
const previewGroupImage = (groupImage: any, index: number) => {
  if (!groupImage.imageBase64) {
    ElMessage.warning('该目标截图不可用')
    return
  }
  
  // 创建图片预览数组
  const previewList = groupImages.value
    .filter(img => img.imageBase64)
    .map(img => `data:image/jpeg;base64,${img.imageBase64}`)
  
  if (previewList.length === 0) {
    ElMessage.warning('没有可预览的图片')
    return
  }
  
  // 找到当前图片在可预览列表中的索引
  const currentIndex = groupImages.value
    .slice(0, index + 1)
    .filter(img => img.imageBase64).length - 1
  
  // 创建图片查看器
  const imageViewer = document.createElement('div')
  imageViewer.style.cssText = `
    position: fixed;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    background: rgba(0, 0, 0, 0.8);
    z-index: 9999;
    display: flex;
    align-items: center;
    justify-content: center;
  `
  
  const img = document.createElement('img')
  img.src = `data:image/jpeg;base64,${groupImage.imageBase64}`
  img.style.cssText = `
    max-width: 90%;
    max-height: 90%;
    object-fit: contain;
    border-radius: 8px;
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.5);
  `
  
  const closeBtn = document.createElement('button')
  closeBtn.innerHTML = '×'
  closeBtn.style.cssText = `
    position: absolute;
    top: 20px;
    right: 20px;
    background: rgba(255, 255, 255, 0.2);
    border: none;
    color: white;
    font-size: 30px;
    cursor: pointer;
    border-radius: 50%;
    width: 50px;
    height: 50px;
    display: flex;
    align-items: center;
    justify-content: center;
  `
  
  const title = document.createElement('div')
  title.innerHTML = `目标 ${groupImage.groupIndex} 截图`
  title.style.cssText = `
    position: absolute;
    top: 20px;
    left: 20px;
    color: white;
    font-size: 18px;
    font-weight: bold;
    background: rgba(0, 0, 0, 0.5);
    padding: 8px 16px;
    border-radius: 4px;
  `
  
  imageViewer.appendChild(img)
  imageViewer.appendChild(closeBtn)
  imageViewer.appendChild(title)
  document.body.appendChild(imageViewer)
  
  // 关闭事件
  const closeViewer = () => {
    document.body.removeChild(imageViewer)
  }
  
  closeBtn.onclick = closeViewer
  imageViewer.onclick = (e) => {
    if (e.target === imageViewer) closeViewer()
  }
  
  // ESC键关闭
  const handleKeydown = (e: KeyboardEvent) => {
    if (e.key === 'Escape') {
      closeViewer()
      document.removeEventListener('keydown', handleKeydown)
    }
  }
  document.addEventListener('keydown', handleKeydown)
}

// 处理图片加载错误
const handleImageError = (event: Event) => {
  console.error('目标截图加载失败:', event)
}

// 格式化群组边界框
const formatGroupBbox = (bbox: number[]): string => {
  if (!Array.isArray(bbox) || bbox.length !== 4) {
    return '无效边界框'
  }
  const [x1, y1, x2, y2] = bbox.map(num => Math.round(num))
  return `${x2 - x1}×${y2 - y1}`
}

// 启动视频流
const startStream = () => {
  console.log('启动视频流:', props.camera.id, props.camera.rtspUrl)

  // 无算法 WS：根据配置播放 WebRTC / HLS
  if (!algoWsEnabled.value) {
    if (isWebRtcMode.value) {
      stopHlsPlayback()
      startWebRtcPlayback()
    } else {
      stopWebRtcPlayback()
      startHlsPlayback()
    }
    return
  }
  
  if (!wsClient.isConnected()) {
    // 如果WebSocket未连接，先连接WebSocket
    console.log('WebSocket未连接，先连接再启动视频流')
    wsClient.connect()
    
    // 创建一个轮询检查，确保在连接成功后发送启动流消息
    const checkConnectionAndStart = () => {
      if (wsClient.isConnected()) {
        // 连接成功，发送启动流消息
        console.log('WebSocket已连接成功，发送启动流消息:', props.camera.id)
        wsClient.send({
          type: 'start_stream',
          data: {
            cameraId: props.camera.id,
            rtspUrl: props.camera.rtspUrl
          }
        })
      } else {
        // 如果仍未连接，延迟再次检查
        console.log('WebSocket仍未连接，稍后再次检查', new Date().toISOString())
        setTimeout(checkConnectionAndStart, 500)
      }
    }
    
    // 启动第一次检查
    setTimeout(checkConnectionAndStart, 500)
  } else {
    // WebSocket已连接，直接发送消息
    console.log('WebSocket已连接，直接发送启动流消息:', props.camera.id)
    wsClient.send({
      type: 'start_stream',
      data: {
        cameraId: props.camera.id,
        rtspUrl: props.camera.rtspUrl
      }
    })
  }
}

// 验证base64字符串是否有效
const isValidBase64 = (str: string): boolean => {
  if (!str || typeof str !== 'string') return false
  
  // 如果已经包含前缀，先移除前缀
  if (str.startsWith('data:image')) {
    const parts = str.split(',')
    if (parts.length !== 2) return false
    str = parts[1]
  }
  
  // 检查是否只包含base64合法字符
  const regex = /^[A-Za-z0-9+/=]+$/
  const isValid = regex.test(str)
  
  if (!isValid) {
    console.error('无效的base64字符串：', str.substring(0, 50) + '...')
  }
  
  return isValid
}

// 监听激活状态
watch(() => props.isActive, (newValue) => {
  console.log('摄像头激活状态变更:', props.camera.id, newValue)
  if (newValue) {
    startStream()
  } else {
    // 当摄像头不再激活时，清除画面
    clearDisplay()
    stopHlsPlayback()
    stopWebRtcPlayback()
  }
})

onMounted(() => {
  console.log('VideoPlayer组件挂载:', props.camera.id)
  
  // 监听页面可见性变化
  document.addEventListener('visibilitychange', handleVisibilityChange)
  
  // 初始化Canvas（仅算法 WS 模式需要）
  if (algoWsEnabled.value) {
    if (canvas.value) {
      ctx.value = canvas.value.getContext('2d')
      if (!ctx.value) {
        console.error('无法获取Canvas上下文')
      }
    } else {
      console.error('Canvas元素不存在')
    }
  }
  
  // 设置ResizeObserver监听容器尺寸变化
  cleanupResizeObserver = setupResizeObserver()
  
  // 添加消息处理器（仅算法 WS 模式需要）
  if (algoWsEnabled.value) {
    wsClient.addMessageHandler(handleMessage)
  }
  
  // 如果是激活状态，启动视频流
  if (props.isActive) {
    startStream()
  }
  
  // 初始读取一次容器尺寸
  if (videoContainer.value) {
    containerSize.value = {
      width: videoContainer.value.clientWidth,
      height: videoContainer.value.clientHeight
    }
  }
})

onBeforeUnmount(() => {
  console.log('VideoPlayer组件卸载:', props.camera.id)
  
  // 移除页面可见性变化监听
  document.removeEventListener('visibilitychange', handleVisibilityChange)
  
  // 清除画面
  clearDisplay()
  // 移除消息处理器
  if (algoWsEnabled.value) {
    wsClient.removeMessageHandler(handleMessage)
  }
  stopHlsPlayback()
  stopWebRtcPlayback()
  // 调用清理函数，断开ResizeObserver连接
  if (cleanupResizeObserver) {
    cleanupResizeObserver()
  }
})

// 计算保持宽高比的绘制区域
const calculateDrawArea = (imageWidth: number, imageHeight: number, canvasWidth: number, canvasHeight: number) => {
  // 计算图像和画布的宽高比
  const imageRatio = imageWidth / imageHeight
  const canvasRatio = canvasWidth / canvasHeight
  
  let drawWidth, drawHeight, drawX, drawY
  
  // 如果图像的宽高比大于画布的宽高比，以宽度为准，确保整个图像宽度都在画布内
  if (imageRatio > canvasRatio) {
    drawWidth = canvasWidth
    drawHeight = canvasWidth / imageRatio
    drawX = 0
    drawY = (canvasHeight - drawHeight) / 2
  } 
  // 否则以高度为准，确保整个图像高度都在画布内
  else {
    drawHeight = canvasHeight
    drawWidth = canvasHeight * imageRatio
    drawX = (canvasWidth - drawWidth) / 2
    drawY = 0
  }
  
  console.log(`
  ====== 图像绘制区域计算 ======
  - 原始图像尺寸: ${imageWidth} x ${imageHeight}, 比例: ${imageRatio.toFixed(3)}
  - Canvas尺寸: ${canvasWidth} x ${canvasHeight}, 比例: ${canvasRatio.toFixed(3)}
  - 绘制区域: x=${drawX.toFixed(1)}, y=${drawY.toFixed(1)}, width=${drawWidth.toFixed(1)}, height=${drawHeight.toFixed(1)}
  - 计算方式: ${imageRatio > canvasRatio ? '以宽度为准' : '以高度为准'}
  ================================
  `)
  
  return { x: drawX, y: drawY, width: drawWidth, height: drawHeight }
}
</script>

<style lang="scss" scoped>
.video-player {
  position: relative;
  width: 100%;
  height: 100%;
  background-color: #000;
  border-radius: 8px;
  overflow: hidden;
  
  .video-container {
    position: relative;
    width: 100%;
    height: 100%;
    
    canvas {
      width: 100%;
      height: 100%;
      object-fit: contain;
    }

    .video-el {
      width: 100%;
      height: 100%;
      object-fit: contain;
      background: #000;
    }
    
    .detection-box {
      position: absolute;
      border: 2px solid #FF0000;
      border-radius: 4px;
      pointer-events: all;
      overflow: visible;
      transition: all 0.2s ease;
      
      // 跟踪框特殊样式
      &.tracking-box {
        border-style: dashed;
        animation: borderPulse 2s infinite;
      }
      
      &:hover {
        border-width: 3px;
        box-shadow: 0 0 8px rgba(255, 0, 0, 0.6);
        z-index: 11; // 确保悬停时在最上层
        
        .confidence {
          font-weight: bold;
          box-shadow: 0 0 5px rgba(0, 0, 0, 0.3);
          transform: scale(1.05);
        }
      }
      
      .confidence {
        position: absolute;
        padding: 2px 6px;
        color: #fff;
        font-size: 12px;
        border-radius: 4px;
        background-color: #FF0000;
        white-space: nowrap;
        z-index: 10;
        transition: all 0.2s ease;
        transform-origin: left center;
        cursor: pointer;
        text-shadow: 0 0 2px rgba(0, 0, 0, 0.5); // 添加文字阴影提高可读性
        
        .track-id {
          display: inline-block;
          margin-left: 4px;
          font-weight: bold;
          color: #ffff00; // 黄色使跟踪ID更加突出
          font-size: 12px;
          text-shadow: 0 0 3px rgba(0, 0, 0, 0.7); // 加强阴影使其在任何背景下都清晰可见
        }
      }
    }
  }
  
  .camera-info {
    position: absolute;
    top: 10px;
    left: 10px;
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 5px 10px;
    background-color: rgba(0, 0, 0, 0.5);
    border-radius: 4px;
    
    .name {
      color: #fff;
      font-size: 14px;
    }
    
    .status {
      padding: 2px 6px;
      border-radius: 4px;
      font-size: 12px;
      
      &.online {
        background-color: #67c23a;
        color: #fff;
      }
      
      &.offline {
        background-color: #909399;
        color: #fff;
      }
    }
  }
  
  .detection-alert-overlay {
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    background: linear-gradient(to bottom, 
      rgba(255, 0, 0, 0.2), 
      rgba(0, 0, 0, 0.4)
    );
    opacity: 0;
    transition: opacity 0.3s ease;
    pointer-events: none;
    
    &.alert-active {
      opacity: 1;
      pointer-events: auto;
      animation: pulseBackground 2s infinite;
    }

    // 新增警告容器，用于统一应用缩放
    .alert-container {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      transition: transform 0.3s ease;
      transform-origin: center center;
      width: 100%;
      max-width: 400px; // 设置最大宽度
    }

    .alert-icon {
      margin-bottom: 20px;
      .el-icon {
        font-size: 48px;
        color: #fff;
        animation: pulse 1s infinite;
      }
    }

    .alert-info {
      background: rgba(0, 0, 0, 0.75);
      padding: 15px;
      border-radius: 8px;
      backdrop-filter: blur(4px);
      border: 2px solid #FF0000;
      animation: borderBlink 2s infinite;
      width: 90%;
      max-width: 360px; // 设置最大宽度

      .alert-title {
        display: flex;
        align-items: center;
        gap: 10px;
        margin-bottom: 10px;

        .warning-text {
          color: #FF0000;
          font-size: 20px;
          font-weight: bold;
        }

        .level-tag {
          margin-left: auto;
        }
      }

      .alert-details {
        display: grid;
        gap: 8px;

        .detail-item {
          display: flex;
          align-items: center;
          gap: 8px;

          .label {
            color: #909399;
            min-width: 50px;
          }

          .value {
            color: #fff;
            font-weight: 500;
          }
        }
      }
    }

    .alert-actions {
      margin-top: 15px;

      .action-button {
        animation: shake 1s infinite;
      }
    }
  }
}

@keyframes pulse {
  0% { transform: scale(1); opacity: 1; }
  50% { transform: scale(1.2); opacity: 0.8; }
  100% { transform: scale(1); opacity: 1; }
}

@keyframes shake {
  0%, 100% { transform: translateX(0); }
  25% { transform: translateX(-2px); }
  75% { transform: translateX(2px); }
}

@keyframes borderBlink {
  0% { border-color: rgba(255, 0, 0, 0.4); box-shadow: 0 0 0 rgba(255, 0, 0, 0); }
  50% { border-color: rgba(255, 0, 0, 1); box-shadow: 0 0 8px rgba(255, 0, 0, 0.4); }
  100% { border-color: rgba(255, 0, 0, 0.4); box-shadow: 0 0 0 rgba(255, 0, 0, 0); }
}

@keyframes pulseBackground {
  0% { background: linear-gradient(to bottom, rgba(255, 0, 0, 0.1), rgba(0, 0, 0, 0.4)); }
  50% { background: linear-gradient(to bottom, rgba(255, 0, 0, 0.3), rgba(0, 0, 0, 0.4)); }
  100% { background: linear-gradient(to bottom, rgba(255, 0, 0, 0.1), rgba(0, 0, 0, 0.4)); }
}

// 添加边框闪烁动画
@keyframes borderPulse {
  0% { border-width: 2px; }
  50% { border-width: 3px; }
  100% { border-width: 2px; }
}

// 目标截图面板样式
.group-images-panel {
  position: absolute;
  bottom: 10px;
  right: 10px;
  background: rgba(0, 0, 0, 0.85);
  border-radius: 8px;
  backdrop-filter: blur(8px);
  border: 1px solid rgba(255, 255, 255, 0.2);
  min-width: 280px;
  max-width: 400px;
  max-height: 60vh;
  overflow: hidden;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
  transition: all 0.3s ease;
  
  .panel-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 12px 16px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
    background: rgba(255, 255, 255, 0.05);
    
    .panel-title {
      display: flex;
      align-items: center;
      gap: 8px;
      color: #fff;
      font-size: 14px;
      font-weight: 600;
      
      .el-icon {
        color: #409eff;
      }
    }
    
    .toggle-btn {
      padding: 4px;
      min-height: auto;
      color: #fff;
      
      &:hover {
        background: rgba(255, 255, 255, 0.1);
      }
    }
  }
  
  .panel-content {
    padding: 12px;
    max-height: 400px;
    overflow-y: auto;
    
    &::-webkit-scrollbar {
      width: 4px;
    }
    
    &::-webkit-scrollbar-track {
      background: rgba(255, 255, 255, 0.1);
      border-radius: 2px;
    }
    
    &::-webkit-scrollbar-thumb {
      background: rgba(255, 255, 255, 0.3);
      border-radius: 2px;
      
      &:hover {
        background: rgba(255, 255, 255, 0.5);
      }
    }
  }
  
  .group-images-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
    gap: 10px;
  }
  
  .group-image-item {
    background: rgba(255, 255, 255, 0.05);
    border-radius: 6px;
    overflow: hidden;
    cursor: pointer;
    transition: all 0.2s ease;
    border: 1px solid rgba(255, 255, 255, 0.1);
    
    &:hover {
      background: rgba(255, 255, 255, 0.1);
      transform: translateY(-2px);
      box-shadow: 0 2px 12px rgba(0, 0, 0, 0.4);
    }
    
    .image-wrapper {
      width: 100%;
      height: 80px;
      position: relative;
      overflow: hidden;
      
      .group-image {
        width: 100%;
        height: 100%;
        object-fit: cover;
        transition: transform 0.2s ease;
      }
      
      .image-placeholder {
        width: 100%;
        height: 100%;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        color: rgba(255, 255, 255, 0.5);
        font-size: 12px;
        
        .el-icon {
          font-size: 20px;
          margin-bottom: 4px;
        }
      }
    }
    
    .image-label {
      padding: 8px;
      display: flex;
      flex-direction: column;
      gap: 2px;
      
      .group-title {
        color: #fff;
        font-size: 12px;
        font-weight: 600;
      }
      
      .bbox-info {
        color: rgba(255, 255, 255, 0.7);
        font-size: 10px;
      }
    }
    
    &:hover .group-image {
      transform: scale(1.05);
    }
  }
}
</style> 
