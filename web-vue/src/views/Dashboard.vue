<!-- 仪表盘页面 -->
<template>
  <div class="dashboard">
    <div class="welcome-section">
      <div class="welcome-header">
        <div class="welcome-info">
          <h1 class="welcome-title">追踪与智能预警系统</h1>
          <p class="welcome-subtitle">
            欢迎回来，<strong>{{ userInfo?.realName || userInfo?.username }}</strong>
          </p>
          <p class="current-date">{{ currentDate }}</p>
        </div>
        <div class="welcome-icon">
          <el-icon :size="80" color="#1e40af"><Monitor /></el-icon>
        </div>
      </div>
    </div>

    <div class="quick-actions">
      <h3>快速操作</h3>
      <el-row :gutter="30">
        <el-col :span="8">
          <el-card shadow="hover" class="action-card" @click="navigateTo('/monitor')">
            <div class="action-content">
              <el-icon :size="32" color="#059669"><Monitor /></el-icon>
              <h4>实时监控</h4>
              <p>查看校园监控画面</p>
            </div>
          </el-card>
        </el-col>
        <el-col :span="8">
          <el-card shadow="hover" class="action-card" @click="navigateTo('/detection/record')">
            <div class="action-content">
              <el-icon :size="32" color="#dc2626"><Warning /></el-icon>
              <h4>安全记录</h4>
              <p>查看检测记录</p>
            </div>
          </el-card>
        </el-col>
        <el-col :span="8">
          <el-card shadow="hover" class="action-card" @click="navigateTo('/profile')">
            <div class="action-content">
              <el-icon :size="32" color="#ea580c"><User /></el-icon>
              <h4>个人中心</h4>
              <p>管理个人信息</p>
            </div>
          </el-card>
        </el-col>
      </el-row>
    </div>

    <!-- 环境监测数据 -->
    <div class="sensor-data-section">
      <div class="section-header">
        <h3>环境监测数据</h3>
        <div class="header-buttons">
          <el-button
            :type="detectionEnabled ? 'danger' : 'primary'"
            :icon="detectionEnabled ? 'VideoPause' : 'VideoPlay'"
            @click="toggleDetection"
          >
            {{ detectionEnabled ? '关闭检测' : '开启检测' }}
          </el-button>
        </div>
      </div>
      <el-row :gutter="30">
        <el-col :span="6">
          <el-card shadow="hover" class="sensor-card temperature">
            <div class="sensor-content">
              <div class="sensor-icon">
                <el-icon :size="36"><HotWater /></el-icon>
              </div>
              <div class="sensor-value">
                <span class="value">{{ sensorData.temperature }}</span>
                <span class="unit">°C</span>
              </div>
              <div class="sensor-label">温度</div>
              <div class="threshold-input">
                <el-input-number
                  v-model="thresholds.temperature"
                  :min="0"
                  :max="100"
                  :step="1"
                  size="small"
                  :disabled="!detectionEnabled"
                  @change="updateThreshold"
                />
                <span class="threshold-label">阈值</span>
              </div>
              <div v-if="isAlert('temperature')" class="alert-indicator">
                <el-icon color="#dc2626"><WarningFilled /></el-icon>
                <span>超标</span>
              </div>
            </div>
          </el-card>
        </el-col>
        <el-col :span="6">
          <el-card shadow="hover" class="sensor-card humidity">
            <div class="sensor-content">
              <div class="sensor-icon">
                <el-icon :size="36"><MostlyCloudy /></el-icon>
              </div>
              <div class="sensor-value">
                <span class="value">{{ sensorData.humidity }}</span>
                <span class="unit">%</span>
              </div>
              <div class="sensor-label">湿度</div>
              <div class="threshold-input">
                <el-input-number
                  v-model="thresholds.humidity"
                  :min="0"
                  :max="100"
                  :step="1"
                  size="small"
                  :disabled="!detectionEnabled"
                  @change="updateThreshold"
                />
                <span class="threshold-label">阈值</span>
              </div>
              <div v-if="isAlert('humidity')" class="alert-indicator">
                <el-icon color="#dc2626"><WarningFilled /></el-icon>
                <span>超标</span>
              </div>
            </div>
          </el-card>
        </el-col>
        <el-col :span="6">
          <el-card shadow="hover" class="sensor-card smoke">
            <div class="sensor-content">
              <div class="sensor-icon">
                <el-icon :size="36"><WarningFilled /></el-icon>
              </div>
              <div class="sensor-value">
                <span class="value">{{ sensorData.smoke }}</span>
                <span class="unit">ppm</span>
              </div>
              <div class="sensor-label">烟雾浓度</div>
              <div class="threshold-input">
                <el-input-number
                  v-model="thresholds.smoke"
                  :min="0"
                  :max="1000"
                  :step="10"
                  size="small"
                  :disabled="!detectionEnabled"
                  @change="updateThreshold"
                />
                <span class="threshold-label">阈值</span>
              </div>
              <div v-if="isAlert('smoke')" class="alert-indicator">
                <el-icon color="#dc2626"><WarningFilled /></el-icon>
                <span>超标</span>
              </div>
            </div>
          </el-card>
        </el-col>
        <el-col :span="6">
          <el-card shadow="hover" class="sensor-card light">
            <div class="sensor-content">
              <div class="sensor-icon">
                <el-icon :size="36"><Sunny /></el-icon>
              </div>
              <div class="sensor-value">
                <span class="value">{{ sensorData.light }}</span>
                <span class="unit">lux</span>
              </div>
              <div class="sensor-label">光照强度</div>
              <div class="threshold-input">
                <el-input-number
                  v-model="thresholds.light"
                  :min="0"
                  :max="10000"
                  :step="100"
                  size="small"
                  :disabled="!detectionEnabled"
                  @change="updateThreshold"
                />
                <span class="threshold-label">阈值</span>
              </div>
              <div v-if="isAlert('light')" class="alert-indicator">
                <el-icon color="#dc2626"><WarningFilled /></el-icon>
                <span>超标</span>
              </div>
            </div>
          </el-card>
        </el-col>
      </el-row>
    </div>

    <!-- 摄像头目标检测 -->
    <div class="camera-detection-section">
      <div class="section-header">
        <h3>摄像头目标检测</h3>
        <div class="header-buttons">
          <el-button
            :type="objectDetectionEnabled ? 'success' : 'default'"
            :icon="objectDetectionEnabled ? 'VideoPause' : 'VideoPlay'"
            @click="toggleObjectDetection"
          >
            {{ objectDetectionEnabled ? '关闭目标检测' : '开启目标检测' }}
          </el-button>
          <el-button
            type="primary"
            plain
            :icon="Setting"
            @click="showThresholdDialog = true"
          >
            阈值设置
          </el-button>
        </div>
      </div>
      
      <!-- 阈值卡片展示 -->
      <el-row :gutter="20">
        <el-col :span="6">
          <el-card shadow="hover" class="threshold-card">
            <div class="threshold-content">
              <div class="threshold-icon">
                <el-icon :size="24" color="#6366f1"><Cpu /></el-icon>
              </div>
              <div class="threshold-info">
                <span class="threshold-label">目标阈值</span>
                <span class="threshold-value">{{ objectThresholds.confidenceThreshold }}%</span>
              </div>
            </div>
          </el-card>
        </el-col>
        <el-col :span="9">
          <el-card shadow="hover" class="detection-count-card">
            <div class="count-content">
              <div class="count-item">
                <span class="count-icon">📷</span>
                <span class="count-label">摄像头1</span>
                <span class="count-value">{{ cam0DetectionCount }}</span>
              </div>
              <div class="count-divider"></div>
              <div class="count-item">
                <span class="count-icon">📷</span>
                <span class="count-label">摄像头2</span>
                <span class="count-value">{{ cam1DetectionCount }}</span>
              </div>
            </div>
          </el-card>
        </el-col>
      </el-row>

      <div class="detection-status">
        <el-tag :type="objectDetectionEnabled ? 'success' : 'info'" size="large">
          <el-icon class="el-icon--left">
            <component :is="objectDetectionEnabled ? 'CircleCheck' : 'CircleClose'" />
          </el-icon>
          {{ objectDetectionEnabled ? '目标检测运行中' : '目标检测已停止' }}
        </el-tag>
      </div>

      <div class="forbidden-area-panel">
        <div class="forbidden-header">
          <div class="forbidden-title">禁入区域设置（四边形）</div>
          <div class="forbidden-actions">
            <el-radio-group v-model="forbiddenCameraId" size="small">
              <el-radio-button :value="1">摄像头1</el-radio-button>
              <el-radio-button :value="2">摄像头2</el-radio-button>
            </el-radio-group>
            <el-button size="small" :loading="forbiddenFrameLoading" @click="loadForbiddenFrame">
              获取当前帧
            </el-button>
            <el-button size="small" @click="clearForbiddenPoints">清空点位</el-button>
            <el-button
              size="small"
              type="primary"
              :disabled="forbiddenPoints.length !== 0 && forbiddenPoints.length !== 4"
              @click="saveForbiddenArea"
            >
              提交到后端
            </el-button>
          </div>
        </div>
        <div class="forbidden-tip">请按顺时针在图片上点击 4 个点形成四边形；清空点位后提交可关闭禁入判断。</div>

        <div class="forbidden-points">
          <span v-for="(p, idx) in forbiddenPoints" :key="idx" class="forbidden-point-chip">
            P{{ idx + 1 }}: ({{ p.x }}, {{ p.y }})
          </span>
          <span v-if="forbiddenPoints.length === 0" class="forbidden-point-empty">未设置点位</span>
        </div>

        <div class="forbidden-frame-wrap">
          <template v-if="forbiddenFrameUrl">
            <div class="forbidden-image-box">
              <img
                ref="forbiddenImageRef"
                :src="forbiddenFrameUrl"
                class="forbidden-image"
                @load="handleForbiddenImageLoad"
                @click="handleForbiddenImageClick"
              />
              <svg
                v-if="forbiddenImageSize.width > 0 && forbiddenImageSize.height > 0"
                class="forbidden-overlay"
                :viewBox="`0 0 ${forbiddenImageSize.width} ${forbiddenImageSize.height}`"
                preserveAspectRatio="none"
              >
                <polygon
                  v-if="forbiddenPoints.length >= 2"
                  :points="forbiddenPoints.map((p) => `${p.x},${p.y}`).join(' ')"
                  fill="rgba(220, 38, 38, 0.18)"
                  stroke="#dc2626"
                  stroke-width="3"
                />
                <circle
                  v-for="(p, idx) in forbiddenPoints"
                  :key="`point-${idx}`"
                  :cx="p.x"
                  :cy="p.y"
                  r="6"
                  fill="#2563eb"
                  stroke="#ffffff"
                  stroke-width="2"
                />
              </svg>
            </div>
          </template>
          <template v-else>
            <div class="forbidden-empty">点击“获取当前帧”后即可在图片上绘制四边形</div>
          </template>
        </div>
      </div>

      <div class="sound-status">
        <el-button
          :type="soundDetectionEnabled ? 'danger' : 'primary'"
          :icon="soundDetectionEnabled ? 'VideoPause' : 'Microphone'"
          @click="toggleSoundDetection"
        >
          {{ soundDetectionEnabled ? '关闭声音监测' : '开启声音监测' }}
        </el-button>
        
        <!-- 上传文件按钮 -->
        <el-button
          type="success"
          plain
          :icon="Upload"
          @click="triggerFileUpload"
        >
          上传音频文件
        </el-button>
        <input
          ref="fileInputRef"
          type="file"
          accept=".mp3,.wav,.ogg,.m4a"
          style="display: none"
          @change="handleFileUpload"
        />
        
        <!-- 状态显示 -->
        <el-tag :type="soundDetectionEnabled ? 'success' : 'info'" size="large">
          {{ soundDetectionEnabled ? '运行中' : '已停止' }}
        </el-tag>
      </div>
    </div>

    <!-- 阈值设置对话框 -->
    <el-dialog
      v-model="showThresholdDialog"
      title="目标检测阈值设置"
      width="500px"
      destroy-on-close
    >
      <el-form label-width="100px" label-position="left">
        <el-form-item label="检测开关">
          <el-switch v-model="objectThresholds.enabled" />
        </el-form-item>
        
        <el-divider content-position="center">置信度阈值</el-divider>
        
        <el-form-item label="目标阈值">
          <el-slider
            v-model="objectThresholds.confidenceThreshold"
            :min="0"
            :max="100"
            :step="1"
            :disabled="!objectThresholds.enabled"
            show-stops
          />
          <span class="slider-tip">置信度高于此值的检测结果将被显示</span>
        </el-form-item>
        
        <el-divider content-position="center">其他设置</el-divider>

        <el-form-item label="框数量阈值">
          <el-input-number
            v-model="objectThresholds.boxCountThreshold"
            :min="0"
            :max="100"
            :step="1"
            :disabled="!objectThresholds.enabled"
          />
          <span class="slider-tip">当检测到的目标框数量超过此值时触发告警（0表示不限制）</span>
        </el-form-item>
      </el-form>
      
      <template #footer>
        <el-button @click="resetObjectThresholds">恢复默认</el-button>
        <el-button @click="showThresholdDialog = false">取消</el-button>
        <el-button type="primary" @click="saveObjectThresholds">保存设置</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useRouter } from 'vue-router'
import { useUserStore } from '@/stores/user'
import axios from 'axios'
import {
  Monitor, Warning, User,
  HotWater, MostlyCloudy, WarningFilled, Sunny,
  VideoPlay, VideoPause, Microphone,
  Setting, Cpu, CircleCheck, CircleClose,
  Upload
} from '@element-plus/icons-vue'

const router = useRouter()
const userStore = useUserStore()
const apiBaseUrl = import.meta.env.VITE_API_BASE_URL || import.meta.env.VITE_API_URL || '/api'

// 用户信息
const userInfo = computed(() => userStore.userInfo)

// 当前日期
const currentDate = new Date().toLocaleDateString('zh-CN', {
  year: 'numeric',
  month: 'long',
  day: 'numeric',
  weekday: 'long'
})

// 导航函数
const navigateTo = (path: string) => {
  router.push(path)
}

// 传感器数据
const sensorData = ref({
  temperature: '--',
  humidity: '--',
  smoke: '--',
  light: '--'
})

// 报警信息
const alertMessage = ref<string | null>(null)

// 阈值配置
const thresholds = ref({
  temperature: 35,
  humidity: 80,
  smoke: 100,
  light: 500
})

// 检测开关
const detectionEnabled = ref(false)

// 声音检测开关
const soundDetectionEnabled = ref(false)

// 目标检测开关
const objectDetectionEnabled = ref(false)

// 目标检测阈值设置
const showThresholdDialog = ref(false)
const objectThresholds = ref({
  enabled: true,
  confidenceThreshold: 50,
  boxCountThreshold: 0  // 框数量阈值（0表示不限制）
})

// 摄像头检测数量
const cam0DetectionCount = ref(0)
const cam1DetectionCount = ref(0)

type ForbiddenPoint = { x: number; y: number }
const forbiddenCameraId = ref(1) // 1=cam0, 2=cam1
const forbiddenPoints = ref<ForbiddenPoint[]>([])
const forbiddenFrameUrl = ref('')
const forbiddenFrameLoading = ref(false)
const forbiddenImageRef = ref<HTMLImageElement | null>(null)
const forbiddenImageSize = ref({ width: 0, height: 0 })

// 文件上传引用
const fileInputRef = ref<HTMLInputElement | null>(null)

// 定时器
let refreshTimer: number | null = null

// 判断是否超标
const isAlert = (type: string): boolean => {
  if (!alertMessage.value) return false
  const data = sensorData.value
  switch (type) {
    case 'temperature':
      return data.temperature !== '--' && parseFloat(data.temperature as any) > thresholds.value.temperature
    case 'humidity':
      return data.humidity !== '--' && parseFloat(data.humidity as any) > thresholds.value.humidity
    case 'smoke':
      return data.smoke !== '--' && parseFloat(data.smoke as any) > thresholds.value.smoke
    case 'light':
      return data.light !== '--' && parseFloat(data.light as any) < thresholds.value.light
    default:
      return false
  }
}

// 获取最新传感器数据
const fetchLatestSensorData = async () => {
  try {
    const response = await axios.get(`${apiBaseUrl}/sensor/latest`)
    if (response.data.code === 200 && response.data.data) {
      const data = response.data.data
      sensorData.value = {
        temperature: data.temperature !== null ? data.temperature.toFixed(1) : '--',
        humidity: data.humidity !== null ? data.humidity.toFixed(1) : '--',
        smoke: data.smoke !== null ? Math.round(data.smoke) : '--',
        light: data.light !== null ? Math.round(data.light) : '--'
      }
      alertMessage.value = data.alertMessage
      if (data.alertMessage) {
        ElMessage.warning(`环境报警: ${data.alertMessage}`)
      }
    }
  } catch (error: any) {
    console.error('获取传感器数据失败:', error)
    // 如果API不可用，使用模拟数据
    sensorData.value = {
      temperature: (20 + Math.random() * 10).toFixed(1),
      humidity: (50 + Math.random() * 30).toFixed(1),
      smoke: Math.round(30 + Math.random() * 50),
      light: Math.round(200 + Math.random() * 500)
    }
  }
}

// 获取阈值
const fetchThresholds = async () => {
  try {
    const response = await axios.get(`${apiBaseUrl}/sensor/threshold`)
    if (response.data.code === 200 && response.data.data) {
      thresholds.value = response.data.data
    }
  } catch (error) {
    console.error('获取阈值失败:', error)
  }
}

// 更新阈值
const updateThreshold = async () => {
  try {
    await axios.put(`${apiBaseUrl}/sensor/threshold`, thresholds.value)
    ElMessage.success('阈值已更新')
  } catch (error) {
    console.error('更新阈值失败:', error)
    ElMessage.error('更新阈值失败')
  }
}

// 切换检测
const toggleDetection = async () => {
  detectionEnabled.value = !detectionEnabled.value
  try {
    await axios.put(`${apiBaseUrl}/sensor/monitoring?enabled=${detectionEnabled.value}`)
    if (detectionEnabled.value) {
      ElMessage.success('环境检测已开启')
      startAutoRefresh()
    } else {
      ElMessage.info('环境检测已关闭')
      stopAutoRefresh()
    }
  } catch (error) {
    console.error('切换检测状态失败:', error)
    ElMessage.error('操作失败')
  }
}

// 切换声音检测
const toggleSoundDetection = async () => {
  console.log('[Dashboard] 点击了声音监测按钮, 当前状态:', soundDetectionEnabled.value)
  const newState = !soundDetectionEnabled.value
  try {
    const url = newState ? '/api/sound/start' : '/api/sound/stop'
    console.log('[Dashboard] 正在发送请求:', url)
    const res = await fetch(url, { method: 'POST' })
    console.log('[Dashboard] 收到响应，状态:', res.status)
    const data = await res.json()
    console.log('[Dashboard] 响应数据:', data)
    if (data.code === 200) {
      soundDetectionEnabled.value = newState
      ElMessage.success(newState ? '声音监测已开启' : '声音监测已关闭')
    } else {
      ElMessage.error(data.msg || '操作失败')
      console.error('[Dashboard] 声音监测操作失败:', data.msg)
    }
  } catch (err) {
    console.error('[Dashboard] 声音监测请求异常:', err)
    ElMessage.error('操作失败: ' + (err as Error).message)
  }
}

const parseBooleanLike = (value: unknown): boolean | null => {
  if (typeof value === 'boolean') return value
  if (typeof value === 'number') return value !== 0
  if (typeof value === 'string') {
    const normalized = value.trim().toLowerCase()
    if (['true', '1', 'yes', 'on'].includes(normalized)) return true
    if (['false', '0', 'no', 'off'].includes(normalized)) return false
  }
  return null
}

const syncObjectDetectionStatusFromServer = async (silent = true): Promise<boolean> => {
  try {
    const res = await fetch(`${apiBaseUrl}/rknn/status`)
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`)
    }

    const payload = await res.json()
    const data = payload?.data && typeof payload.data === 'object' ? payload.data : payload
    const enabled = parseBooleanLike(data?.inference_enabled)
    if (enabled === null) {
      throw new Error('未在状态响应中找到 inference_enabled 字段')
    }

    objectDetectionEnabled.value = enabled
    objectThresholds.value.enabled = enabled
    localStorage.setItem('objectDetectionSettings', JSON.stringify(objectThresholds.value))
    return true
  } catch (err) {
    console.error('[Dashboard] 同步目标检测状态失败:', err)
    if (!silent) {
      ElMessage.warning('未获取到视觉模块实时状态，已保留当前开关状态')
    }
    return false
  }
}

// 触发文件上传
const triggerFileUpload = () => {
  console.log('[Dashboard] 触发文件上传')
  fileInputRef.value?.click()
}

// 处理文件上传
const handleFileUpload = async (event: Event) => {
  const target = event.target as HTMLInputElement
  const file = target.files?.[0]
  
  if (!file) {
    console.log('[Dashboard] 未选择文件')
    return
  }
  
  console.log('[Dashboard] 选择文件:', file.name, '大小:', file.size)
  
  // 检查文件类型
  const allowedTypes = ['.mp3', '.wav', '.ogg', '.m4a']
  const fileExt = '.' + file.name.split('.').pop()?.toLowerCase()
  if (!allowedTypes.includes(fileExt)) {
    ElMessage.error('不支持的文件格式，请上传 MP3、WAV、OGG 或 M4A 格式')
    return
  }
  
  // 检查文件大小（限制 50MB）
  const maxSize = 50 * 1024 * 1024
  if (file.size > maxSize) {
    ElMessage.error('文件过大，请上传小于 50MB 的音频文件')
    return
  }
  
  ElMessage.info('正在上传并分析音频文件...')
  
  try {
    const formData = new FormData()
    formData.append('file', file)
    
    console.log('[Dashboard] 正在上传文件到 /api/sound/upload')
    const res = await fetch('/api/sound/upload', {
      method: 'POST',
      body: formData
    })
    
    const data = await res.json()
    console.log('[Dashboard] 上传响应:', data)
    
    if (data.code === 200) {
      const result = data.data?.result
      if (result) {
        const typeText = result.soundType === 'anomaly' ? '异常' : '正常'
        const keywords = result.keywords || '无'
        const confidence = ((result.confidence || 0) * 100).toFixed(2)
        ElMessage.success({
          message: `分析完成！类型: ${typeText}，关键词: ${keywords}，置信度: ${confidence}%`,
          duration: 5000
        })
        console.log('[Dashboard] 检测结果:', {
          类型: result.soundType,
          关键词: result.keywords,
          置信度: result.confidence,
          时长: result.duration
        })
      } else {
        ElMessage.success('文件上传成功')
      }
    } else {
      ElMessage.error(data.msg || '上传失败')
    }
  } catch (err) {
    console.error('[Dashboard] 上传异常:', err)
    ElMessage.error('上传失败: ' + (err as Error).message)
  } finally {
    // 清空 input 以便重复选择同一文件
    if (fileInputRef.value) {
      fileInputRef.value.value = ''
    }
  }
}

// 手动刷新传感器数据
const refreshSensorData = async () => {
  try {
    const response = await axios.post(`${apiBaseUrl}/sensor/refresh`)
    if (response.data.code === 200 && response.data.data) {
      const data = response.data.data
      sensorData.value = {
        temperature: data.temperature !== null ? data.temperature.toFixed(1) : '--',
        humidity: data.humidity !== null ? data.humidity.toFixed(1) : '--',
        smoke: data.smoke !== null ? Math.round(data.smoke) : '--',
        light: data.light !== null ? Math.round(data.light) : '--'
      }
      alertMessage.value = data.alertMessage
    }
  } catch (error) {
    console.error('刷新传感器数据失败:', error)
  }
}

// 获取监测状态
const fetchMonitoringStatus = async () => {
  try {
    const response = await axios.get(`${apiBaseUrl}/sensor/monitoring`)
    if (response.data.code === 200 && response.data.data) {
      const status = response.data.data
      detectionEnabled.value = status.enabled
      if (status.thresholds) {
        thresholds.value = status.thresholds
      }
      if (detectionEnabled.value) {
        startAutoRefresh()
      }
    }
  } catch (error) {
    console.error('获取监测状态失败:', error)
  }
}

// 开始自动刷新
const startAutoRefresh = () => {
  if (refreshTimer) return
  refreshTimer = window.setInterval(() => {
    fetchLatestSensorData()
  }, 1000) // 每1秒刷新一次
}

// 停止自动刷新
const stopAutoRefresh = () => {
  if (refreshTimer) {
    clearInterval(refreshTimer)
    refreshTimer = null
  }
}

onMounted(() => {
  fetchMonitoringStatus()
  fetchLatestSensorData()
  fetchThresholds()
  fetchSoundStatus()
  loadObjectThresholds()
  void syncObjectDetectionStatusFromServer(true)
  startDetectionCountTimer()
  loadForbiddenArea()
  loadForbiddenFrame()
})

watch(forbiddenCameraId, () => {
  clearForbiddenPoints()
  loadForbiddenArea()
  loadForbiddenFrame()
})

// 获取声音监测状态
const fetchSoundStatus = async () => {
  try {
    const res = await fetch('/api/sound/status')
    const data = await res.json()
    console.log('[Dashboard] fetchSoundStatus 响应:', data)
    if (data.code === 200) {
      soundDetectionEnabled.value = data.data.enabled
      console.log('[Dashboard] 声音监测初始状态:', data.data.enabled ? '运行中' : '已停止')
    }
  } catch (err) {
    console.error('[Dashboard] 获取声音监测状态失败:', err)
  }
}

// 切换目标检测
const toggleObjectDetection = async () => {
  const newState = !objectDetectionEnabled.value
  try {
    // 调用后端 RKNN 推理接口
    const url = newState 
      ? `${apiBaseUrl}/rknn/inference/on` 
      : `${apiBaseUrl}/rknn/inference/off`
    const res = await fetch(url, { method: 'POST' })
    const data = await res.json()
    if (data.code === 200) {
      const synced = await syncObjectDetectionStatusFromServer(true)
      if (!synced) {
        objectDetectionEnabled.value = newState
        objectThresholds.value.enabled = newState
        localStorage.setItem('objectDetectionSettings', JSON.stringify(objectThresholds.value))
      }
      ElMessage.success(newState ? '目标检测已开启' : '目标检测已关闭')
    } else {
      ElMessage.error(data.msg || '操作失败')
    }
  } catch (err) {
    console.error('切换目标检测失败:', err)
    ElMessage.error('操作失败')
  }
}

// 保存目标检测阈值设置
const saveObjectThresholds = async () => {
  // 保存到本地存储
  localStorage.setItem('objectDetectionSettings', JSON.stringify(objectThresholds.value))

  // 同步到后端（置信度阈值 + 框数量阈值）
  try {
    const res = await fetch(
      `${apiBaseUrl}/rknn/threshold/set?value=${objectThresholds.value.confidenceThreshold / 100}&boxCount=${objectThresholds.value.boxCountThreshold}`,
      { method: 'POST' }
    )
    const data = await res.json()
    if (data.code === 200) {
      ElMessage.success('阈值设置已保存')
    } else {
      ElMessage.error(data.msg || '阈值设置失败')
      return
    }
  } catch (err) {
    console.error('保存目标检测阈值失败:', err)
    ElMessage.error('阈值设置失败')
    return
  }

  showThresholdDialog.value = false
}

// 恢复默认设置
const resetObjectThresholds = () => {
  objectThresholds.value = {
    enabled: true,
    confidenceThreshold: 50,
    boxCountThreshold: 0
  }
  ElMessage.info('已恢复默认设置')
}

const revokeForbiddenFrameUrl = () => {
  if (forbiddenFrameUrl.value) {
    URL.revokeObjectURL(forbiddenFrameUrl.value)
    forbiddenFrameUrl.value = ''
  }
}

const clearForbiddenPoints = () => {
  forbiddenPoints.value = []
}

const loadForbiddenArea = async () => {
  try {
    const res = await fetch(`${apiBaseUrl}/rknn/forbidden-area?cameraId=${forbiddenCameraId.value}`)
    const data = await res.json()
    if (data.code === 200 && data.data?.exists && Array.isArray(data.data.points)) {
      forbiddenPoints.value = data.data.points
        .slice(0, 4)
        .map((p: any) => ({ x: Number(p.x), y: Number(p.y) }))
        .filter((p: ForbiddenPoint) => Number.isFinite(p.x) && Number.isFinite(p.y))
    } else {
      forbiddenPoints.value = []
    }
  } catch (err) {
    console.error('加载禁入区域失败:', err)
  }
}

const loadForbiddenFrame = async () => {
  forbiddenFrameLoading.value = true
  try {
    const res = await fetch(
      `${apiBaseUrl}/rknn/frame/current?cameraId=${forbiddenCameraId.value}&track=0`,
      { cache: 'no-store' }
    )
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`)
    }
    const blob = await res.blob()
    revokeForbiddenFrameUrl()
    forbiddenFrameUrl.value = URL.createObjectURL(blob)
    forbiddenImageSize.value = { width: 0, height: 0 }
  } catch (err) {
    console.error('获取当前帧失败:', err)
    ElMessage.error('获取当前帧失败')
  } finally {
    forbiddenFrameLoading.value = false
  }
}

const handleForbiddenImageLoad = (event: Event) => {
  const img = event.target as HTMLImageElement
  forbiddenImageSize.value = {
    width: img.naturalWidth,
    height: img.naturalHeight
  }
}

const handleForbiddenImageClick = (event: MouseEvent) => {
  const img = forbiddenImageRef.value
  if (!img || forbiddenImageSize.value.width <= 0 || forbiddenImageSize.value.height <= 0) {
    return
  }
  if (forbiddenPoints.value.length >= 4) {
    ElMessage.warning('已选择 4 个点，如需重画请先清空点位')
    return
  }

  const rect = img.getBoundingClientRect()
  if (rect.width <= 0 || rect.height <= 0) return

  const localX = event.clientX - rect.left
  const localY = event.clientY - rect.top
  const xRatio = forbiddenImageSize.value.width / rect.width
  const yRatio = forbiddenImageSize.value.height / rect.height
  const x = Math.max(0, Math.min(forbiddenImageSize.value.width - 1, Math.round(localX * xRatio)))
  const y = Math.max(0, Math.min(forbiddenImageSize.value.height - 1, Math.round(localY * yRatio)))

  forbiddenPoints.value.push({ x, y })
}

const saveForbiddenArea = async () => {
  if (forbiddenPoints.value.length !== 0 && forbiddenPoints.value.length !== 4) {
    ElMessage.warning('请先在图片上选择 4 个点，或清空后提交')
    return
  }

  try {
    const payload = {
      cameraId: forbiddenCameraId.value,
      imageWidth: forbiddenImageSize.value.width,
      imageHeight: forbiddenImageSize.value.height,
      points: forbiddenPoints.value
    }
    const res = await fetch(`${apiBaseUrl}/rknn/forbidden-area`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })
    const data = await res.json()
    if (data.code === 200) {
      if (forbiddenPoints.value.length === 0) {
        ElMessage.success('禁入区域已清空，已关闭禁入判断')
      } else {
        ElMessage.success('禁入区域已提交到后端')
      }
    } else {
      ElMessage.error(data.msg || '保存禁入区域失败')
    }
  } catch (err) {
    console.error('保存禁入区域失败:', err)
    ElMessage.error('保存禁入区域失败')
  }
}

// 获取摄像头检测数量
const fetchDetectionCounts = async () => {
  try {
    const [res0, res1] = await Promise.all([
      fetch('/api/rknn/detection/count?cam=0'),
      fetch('/api/rknn/detection/count?cam=1')
    ])
    const data0 = await res0.json()
    const data1 = await res1.json()
    if (data0.code === 200) {
      cam0DetectionCount.value = data0.data?.count ?? 0
    }
    if (data1.code === 200) {
      cam1DetectionCount.value = data1.data?.count ?? 0
    }
  } catch (err) {
    console.error('获取检测数量失败:', err)
  }
}

// 启动定时获取检测数量
let detectionCountTimer: number | null = null
const startDetectionCountTimer = () => {
  if (detectionCountTimer) return
  detectionCountTimer = window.setInterval(fetchDetectionCounts, 2000)
  fetchDetectionCounts()
}

// 加载保存的阈值设置
const loadObjectThresholds = () => {
  const saved = localStorage.getItem('objectDetectionSettings')
  if (saved) {
    try {
      const parsed = JSON.parse(saved)
      objectThresholds.value = { ...objectThresholds.value, ...parsed }
      objectDetectionEnabled.value = objectThresholds.value.enabled
    } catch (e) {
      console.error('加载阈值设置失败:', e)
    }
  }
}

onUnmounted(() => {
  stopAutoRefresh()
  if (detectionCountTimer) {
    clearInterval(detectionCountTimer)
    detectionCountTimer = null
  }
  revokeForbiddenFrameUrl()
})
</script>

<style scoped>
.dashboard {
  padding: 24px;
  background: linear-gradient(135deg, #f8fafc 0%, #f1f5f9 100%);
  min-height: calc(100vh - 64px);
}

.welcome-section {
  margin-bottom: 32px;
}

.welcome-header {
  background: linear-gradient(135deg, #1e40af 0%, #1e3a8a 100%);
  border-radius: 16px;
  padding: 32px;
  color: white;
  display: flex;
  justify-content: space-between;
  align-items: center;
  box-shadow: 0 8px 32px rgba(30, 64, 175, 0.2);
}

.welcome-info {
  flex: 1;
}

.welcome-title {
  font-size: 32px;
  font-weight: 700;
  margin: 0 0 12px 0;
  text-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}

.welcome-subtitle {
  font-size: 18px;
  margin: 0 0 8px 0;
  opacity: 0.9;
}

.current-date {
  font-size: 14px;
  margin: 0;
  opacity: 0.8;
}

.welcome-icon {
  opacity: 0.2;
}

.quick-actions {
  margin-bottom: 50px;

  h3 {
    font-size: 20px;
    color: #1e40af;
    margin-bottom: 20px;
    font-weight: 600;
  }
}

.action-card {
  cursor: pointer;
  transition: all 0.3s ease;
  border: 1px solid transparent;

  &:hover {
    transform: translateY(-4px);
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.12);
    border-color: #1e40af;
  }
}

.action-content {
  text-align: center;
  padding: 24px;

  h4 {
    margin: 16px 0 8px 0;
    font-size: 16px;
    color: #1f2937;
    font-weight: 600;
  }

  p {
    margin: 0;
    color: #6b7280;
    font-size: 14px;
  }
}

/* 环境监测数据区域 */
.sensor-data-section {
  margin-bottom: 50px;

  .section-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 20px;

    .header-buttons {
      display: flex;
      gap: 12px;
    }

    h3 {
      font-size: 20px;
      color: #1e40af;
      font-weight: 600;
      margin: 0;
    }
  }

  h3 {
    font-size: 20px;
    color: #1e40af;
    margin-bottom: 20px;
    font-weight: 600;
  }
}

.sensor-card {
  transition: all 0.3s ease;
  border-radius: 16px;
  overflow: hidden;

  &:hover {
    transform: translateY(-4px);
    box-shadow: 0 12px 40px rgba(0, 0, 0, 0.12);
  }

  .sensor-content {
    text-align: center;
    padding: 20px;
    position: relative;

    .sensor-icon {
      width: 64px;
      height: 64px;
      border-radius: 50%;
      display: flex;
      align-items: center;
      justify-content: center;
      margin: 0 auto 16px;
    }

    .sensor-value {
      margin-bottom: 8px;

      .value {
        font-size: 36px;
        font-weight: 700;
        color: #1f2937;
      }

      .unit {
        font-size: 16px;
        color: #6b7280;
        margin-left: 4px;
      }
    }

    .sensor-label {
      font-size: 14px;
      color: #6b7280;
      font-weight: 500;
    }

    .threshold-input {
      margin-top: 12px;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;

      .threshold-label {
        font-size: 12px;
        color: #9ca3af;
      }
    }

    .alert-indicator {
      margin-top: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 4px;
      font-size: 12px;
      color: #dc2626;
      font-weight: 600;
      animation: blink 1s infinite;
    }
  }

  /* 温度卡片 */
  &.temperature {
    border: 1px solid #fed7aa;
    background: linear-gradient(135deg, #fff7ed 0%, #ffedd5 100%);

    .sensor-icon {
      background: linear-gradient(135deg, #ea580c 0%, #f97316 100%);
      color: white;
    }

    .value {
      color: #ea580c;
    }
  }

  /* 湿度卡片 */
  &.humidity {
    border: 1px solid #bae6fd;
    background: linear-gradient(135deg, #f0f9ff 0%, #e0f2fe 100%);

    .sensor-icon {
      background: linear-gradient(135deg, #0284c7 0%, #0ea5e9 100%);
      color: white;
    }

    .value {
      color: #0284c7;
    }
  }

  /* 烟雾卡片 */
  &.smoke {
    border: 1px solid #fecaca;
    background: linear-gradient(135deg, #fef2f2 0%, #fee2e2 100%);

    .sensor-icon {
      background: linear-gradient(135deg, #dc2626 0%, #ef4444 100%);
      color: white;
    }

    .value {
      color: #dc2626;
    }
  }

  /* 光照卡片 */
  &.light {
    border: 1px solid #fef08a;
    background: linear-gradient(135deg, #fefce8 0%, #fef9c3 100%);

    .sensor-icon {
      background: linear-gradient(135deg, #ca8a04 0%, #eab308 100%);
      color: white;
    }

    .value {
      color: #ca8a04;
    }
  }
}

@keyframes blink {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.5;
  }
}

/* 响应式设计 */
@media (max-width: 768px) {
  .sensor-data-section {
    .el-col {
      margin-bottom: 16px;
    }
  }

  .sensor-card {
    .sensor-content {
      .sensor-icon {
        width: 48px !important;
        height: 48px !important;
      }

      .value {
        font-size: 28px !important;
      }
    }
  }
}

/* 摄像头目标检测区域 */
.camera-detection-section {
  margin-bottom: 50px;

  .section-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 20px;

    .header-buttons {
      display: flex;
      gap: 12px;
    }

    h3 {
      font-size: 20px;
      color: #1e40af;
      font-weight: 600;
      margin: 0;
    }
  }
}

.threshold-card {
  border-radius: 12px;
  transition: all 0.3s ease;
  border: 1px solid #e5e7eb;

  &:hover {
    transform: translateY(-2px);
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.08);
  }

  .threshold-content {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 8px;
  }

  .threshold-icon {
    width: 48px;
    height: 48px;
    border-radius: 10px;
    display: flex;
    align-items: center;
    justify-content: center;
    background: #f1f5f9;
  }

  .threshold-info {
    display: flex;
    flex-direction: column;

    .threshold-label {
      font-size: 12px;
      color: #6b7280;
    }

    .threshold-value {
      font-size: 18px;
      font-weight: 700;
      color: #1f2937;
    }
  }

  &.person .threshold-icon {
    background: linear-gradient(135deg, #dbeafe 0%, #bfdbfe 100%);
  }

  &.vehicle .threshold-icon {
    background: linear-gradient(135deg, #d1fae5 0%, #a7f3d0 100%);
  }

  &.weapon .threshold-icon {
    background: linear-gradient(135deg, #fee2e2 0%, #fecaca 100%);
  }
}

.detection-count-card {
  .count-content {
    display: flex;
    align-items: center;
  }

  .count-item {
    flex: 1;
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .count-icon {
    font-size: 16px;
  }

  .count-label {
    font-size: 12px;
    color: #6b7280;
  }

  .count-value {
    font-size: 18px;
    font-weight: 700;
    color: #6366f1;
    min-width: 24px;
  }

  .count-divider {
    width: 1px;
    height: 24px;
    background: #e5e7eb;
    margin: 0 12px;
  }
}

.detection-status {
  margin-top: 20px;
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 16px 20px;
  background: linear-gradient(135deg, #f8fafc 0%, #f1f5f9 100%);
  border-radius: 12px;
  border: 1px solid #e5e7eb;

  .detection-info {
    font-size: 13px;
    color: #6b7280;
  }
}

.forbidden-area-panel {
  margin-top: 14px;
  padding: 14px;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  background: #ffffff;
}

.forbidden-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  flex-wrap: wrap;
}

.forbidden-title {
  font-size: 14px;
  font-weight: 700;
  color: #1f2937;
}

.forbidden-actions {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.forbidden-tip {
  margin-top: 8px;
  font-size: 12px;
  color: #6b7280;
}

.forbidden-points {
  margin-top: 10px;
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.forbidden-point-chip {
  font-size: 12px;
  color: #1d4ed8;
  background: #eff6ff;
  border: 1px solid #bfdbfe;
  border-radius: 999px;
  padding: 3px 10px;
}

.forbidden-point-empty {
  font-size: 12px;
  color: #9ca3af;
}

.forbidden-frame-wrap {
  margin-top: 10px;
}

.forbidden-image-box {
  width: 100%;
  max-width: 760px;
  position: relative;
  border-radius: 10px;
  border: 1px dashed #cbd5e1;
  overflow: hidden;
  background: #0f172a;
}

.forbidden-image {
  display: block;
  width: 100%;
  height: auto;
  cursor: crosshair;
  user-select: none;
}

.forbidden-overlay {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
}

.forbidden-empty {
  font-size: 13px;
  color: #6b7280;
  border: 1px dashed #d1d5db;
  border-radius: 10px;
  background: #f9fafb;
  padding: 24px;
}

.sound-status {
  margin-top: 12px;
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 16px 20px;
  background: linear-gradient(135deg, #f8fafc 0%, #f1f5f9 100%);
  border-radius: 12px;
  border: 1px solid #e5e7eb;
}

.slider-tip {
  font-size: 12px;
  color: #9ca3af;
  margin-top: 4px;
}
</style>
