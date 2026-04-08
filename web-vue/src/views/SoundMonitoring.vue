<template>
  <div class="sound-monitoring">
    <div class="card">
      <div class="card-header">
        <h3>声音监测</h3>
        <el-tag :type="isMonitoring ? 'success' : 'info'">
          {{ isMonitoring ? '监测中' : '已停止' }}
        </el-tag>
      </div>
      
      <div class="card-body">
        <!-- 控制按钮 -->
        <div class="control-section">
          <el-button 
            type="success" 
            :icon="VideoPlay" 
            @click="startMonitoring"
            :disabled="isMonitoring"
          >
            开启环境监测
          </el-button>
          
          <el-button 
            type="danger" 
            :icon="VideoPause" 
            @click="stopMonitoring"
            :disabled="!isMonitoring"
          >
            停止监测
          </el-button>
          
          <el-button 
            type="primary" 
            :icon="Microphone" 
            @click="recordAndDetect"
            :loading="isRecording"
          >
            录制检测
          </el-button>
        </div>
        
        <!-- 上传音频区域 -->
        <div class="upload-section">
          <h4>上传音频文件检测</h4>
          <el-upload
            ref="uploadRef"
            class="audio-uploader"
            :action="uploadUrl"
            :headers="uploadHeaders"
            :show-file-list="true"
            :before-upload="beforeAudioUpload"
            :on-success="handleUploadSuccess"
            :on-error="handleUploadError"
            :on-progress="handleUploadProgress"
            accept=".wav,.mp3,.m4a,.ogg,.flac"
            :disabled="uploading"
          >
            <el-button type="primary" :loading="uploading" :icon="Upload">
              {{ uploading ? '上传中...' : '选择音频文件' }}
            </el-button>
            <template #tip>
              <div class="upload-tip">
                支持格式: WAV, MP3, M4A, OGG, FLAC | 单个文件不超过 50MB
              </div>
            </template>
          </el-upload>
          
          <!-- 上传进度 -->
          <el-progress 
            v-if="uploadProgress > 0 && uploadProgress < 100" 
            :percentage="uploadProgress" 
            :status="uploadProgress === 100 ? 'success' : undefined"
            style="margin-top: 10px;"
          />
          
          <!-- 上传结果 -->
          <div v-if="uploadResult" class="upload-result">
            <el-alert
              :title="uploadResult.success ? '检测完成' : '检测失败'"
              :description="uploadResult.message"
              :type="uploadResult.success ? 'success' : 'error'"
              :closable="true"
              @close="uploadResult = null"
              show-icon
            />
          </div>
        </div>
        
        <!-- 实时状态 -->
        <div class="status-section">
          <el-descriptions :column="2" border>
            <el-descriptions-item label="监测状态">
              <el-tag :type="isMonitoring ? 'success' : 'warning'">
                {{ isMonitoring ? '运行中' : '已停止' }}
              </el-tag>
            </el-descriptions-item>
            <el-descriptions-item label="麦克风">
              hw:4,0
            </el-descriptions-item>
            <el-descriptions-item label="采样率">
              16000 Hz
            </el-descriptions-item>
            <el-descriptions-item label="声道">
              单声道
            </el-descriptions-item>
          </el-descriptions>
        </div>
        
        <!-- 最新事件 -->
        <div v-if="latestEvent" class="event-section">
          <h4>最新检测事件</h4>
          <el-alert
            :title="latestEvent.soundType"
            :description="latestEvent.keywords"
            type="warning"
            :closable="false"
            show-icon
          >
            <template #default>
              <div>置信度: {{ (latestEvent.confidence * 100).toFixed(1) }}%</div>
              <div>时间: {{ formatTime(latestEvent.startTime) }}</div>
            </template>
          </el-alert>
        </div>
        
        <!-- 历史记录 -->
        <div class="history-section">
          <h4>检测历史</h4>
          <el-table :data="eventHistory" stripe style="width: 100%">
            <el-table-column prop="id" label="ID" width="80" />
            <el-table-column prop="soundType" label="类型" width="120" />
            <el-table-column prop="confidence" label="置信度" width="100">
              <template #default="{ row }">
                {{ (row.confidence * 100).toFixed(1) }}%
              </template>
            </el-table-column>
            <el-table-column prop="startTime" label="开始时间" width="180">
              <template #default="{ row }">
                {{ formatTime(row.startTime) }}
              </template>
            </el-table-column>
            <el-table-column prop="keywords" label="关键词" show-overflow-tooltip />
          </el-table>
          
          <el-pagination
            v-model:current-page="currentPage"
            :page-size="pageSize"
            :total="totalCount"
            layout="prev, pager, next"
            @current-change="fetchHistory"
          />
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { ElMessage } from 'element-plus'
import { VideoPlay, VideoPause, Microphone, Upload } from '@element-plus/icons-vue'

const isMonitoring = ref(false)
const isRecording = ref(false)
const latestEvent = ref(null)
const eventHistory = ref([])
const currentPage = ref(1)
const pageSize = ref(10)
const totalCount = ref(0)
let pollingTimer = null

// 上传相关
const uploadRef = ref(null)
const uploading = ref(false)
const uploadProgress = ref(0)
const uploadResult = ref(null)
const apiBaseUrl = import.meta.env.VITE_API_BASE_URL || import.meta.env.VITE_API_URL || '/api'
const uploadUrl = `${apiBaseUrl}/sound/upload`
const uploadHeaders = {
  // 如果需要认证token，在这里添加
}

// 上传前验证
const beforeAudioUpload = (file) => {
  const isValidType = ['audio/wav', 'audio/mp3', 'audio/mpeg', 'audio/ogg', 'audio/x-flac', 'audio/flac'].includes(file.type) ||
    file.name.endsWith('.wav') || file.name.endsWith('.mp3') || 
    file.name.endsWith('.m4a') || file.name.endsWith('.ogg') || file.name.endsWith('.flac')
  const isLt50M = file.size / 1024 / 1024 < 50

  if (!isValidType) {
    ElMessage.error('只能上传音频文件 (WAV, MP3, M4A, OGG, FLAC)')
    return false
  }
  if (!isLt50M) {
    ElMessage.error('文件大小不能超过 50MB')
    return false
  }
  
  uploading.value = true
  uploadProgress.value = 0
  uploadResult.value = null
  return true
}

// 上传进度
const handleUploadProgress = (event, file) => {
  uploadProgress.value = Math.round(event.percent || 0)
}

// 上传成功
const handleUploadSuccess = (response, file) => {
  uploading.value = false
  uploadProgress.value = 100
  
  if (response.code === 200) {
    uploadResult.value = {
      success: true,
      message: `检测到 ${response.data?.anomalyCount || 0} 个异常事件`
    }
    ElMessage.success('音频检测完成')
    // 刷新历史记录
    fetchHistory()
    fetchLatest()
  } else {
    uploadResult.value = {
      success: false,
      message: response.msg || '检测失败'
    }
    ElMessage.error(response.msg || '检测失败')
  }
}

// 上传失败
const handleUploadError = (error, file) => {
  uploading.value = false
  uploadProgress.value = 0
  uploadResult.value = {
    success: false,
    message: '上传失败，请重试'
  }
  ElMessage.error('上传失败: ' + (error.message || '未知错误'))
}

// 启动监测
const startMonitoring = async () => {
  console.log('[前端] 点击了开启监测按钮')
  try {
    console.log('[前端] 正在发送 POST /api/sound/start 请求...')
    const res = await fetch('/api/sound/start', { method: 'POST' })
    console.log('[前端] 收到响应，状态:', res.status)
    const data = await res.json()
    console.log('[前端] 响应数据:', data)
    if (data.code === 200) {
      isMonitoring.value = true
      ElMessage.success('声音监测已启动')
      console.log('[前端] 开启监测成功，开始轮询')
      startPolling()
    } else {
      ElMessage.error(data.msg || '启动失败')
      console.error('[前端] 开启监测失败:', data.msg)
    }
  } catch (err) {
    console.error('[前端] 开启监测请求异常:', err)
    ElMessage.error('启动失败: ' + err.message)
  }
}

// 停止监测
const stopMonitoring = async () => {
  console.log('[前端] 点击了停止监测按钮')
  try {
    console.log('[前端] 正在发送 POST /api/sound/stop 请求...')
    const res = await fetch('/api/sound/stop', { method: 'POST' })
    console.log('[前端] 收到响应，状态:', res.status)
    const data = await res.json()
    console.log('[前端] 响应数据:', data)
    if (data.code === 200) {
      isMonitoring.value = false
      ElMessage.success('声音监测已停止')
      console.log('[前端] 停止监测成功，停止轮询')
      stopPolling()
    } else {
      ElMessage.error(data.msg || '停止失败')
      console.error('[前端] 停止监测失败:', data.msg)
    }
  } catch (err) {
    console.error('[前端] 停止监测请求异常:', err)
    ElMessage.error('停止失败: ' + err.message)
  }
}

// 录制并检测
const recordAndDetect = async () => {
  isRecording.value = true
  try {
    const res = await fetch('/api/sound/record?duration=5', { method: 'POST' })
    const data = await res.json()
    if (data.code === 200) {
      ElMessage.success('录制检测完成')
      fetchLatest()
      fetchHistory()
    } else {
      ElMessage.error(data.msg || '检测失败')
    }
  } catch (err) {
    ElMessage.error('检测失败: ' + err.message)
  } finally {
    isRecording.value = false
  }
}

// 获取监测状态
const fetchStatus = async () => {
  try {
    const res = await fetch('/api/sound/status')
    const data = await res.json()
    console.log('[前端] fetchStatus 响应:', data)
    if (data.code === 200) {
      isMonitoring.value = data.data.enabled
      console.log('[前端] 监测状态:', data.data.enabled ? '运行中' : '已停止')
    }
  } catch (err) {
    console.error('[前端] fetchStatus 失败:', err)
  }
}

// 获取最新事件
const fetchLatest = async () => {
  try {
    const res = await fetch('/api/sound/latest')
    const data = await res.json()
    console.log('[前端] fetchLatest 响应:', data)
    if (data.code === 200) {
      latestEvent.value = data.data
    }
  } catch (err) {
    console.error('[前端] fetchLatest 失败:', err)
  }
}

// 获取历史记录
const fetchHistory = async () => {
  try {
    const res = await fetch(`/api/sound/history?page=${currentPage.value}&size=${pageSize.value}`)
    const data = await res.json()
    console.log('[前端] fetchHistory 响应:', data)
    if (data.code === 200) {
      eventHistory.value = data.data.content
      totalCount.value = data.data.totalElements
    }
  } catch (err) {
    console.error('[前端] fetchHistory 失败:', err)
  }
}

// 轮询最新事件
const startPolling = () => {
  console.log('[前端] startPolling 被调用')
  pollingTimer = setInterval(() => {
    console.log('[前端] 轮询...')
    fetchLatest()
    fetchStatus()
  }, 5000)
  console.log('[前端] 轮询已启动, interval ID:', pollingTimer)
}

const stopPolling = () => {
  if (pollingTimer) {
    clearInterval(pollingTimer)
    pollingTimer = null
  }
}

// 格式化时间
const formatTime = (time) => {
  if (!time) return '-'
  const date = new Date(time)
  return date.toLocaleString('zh-CN')
}

onMounted(() => {
  fetchStatus()
  fetchLatest()
  fetchHistory()
  if (isMonitoring.value) {
    startPolling()
  }
})

onUnmounted(() => {
  stopPolling()
})
</script>

<style scoped>
.sound-monitoring {
  padding: 20px;
}

.card {
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.1);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  border-bottom: 1px solid #eee;
}

.card-header h3 {
  margin: 0;
}

.card-body {
  padding: 20px;
}

.control-section {
  display: flex;
  gap: 12px;
  margin-bottom: 20px;
}

.upload-section {
  margin-bottom: 20px;
  padding: 16px;
  background: linear-gradient(135deg, #f0f9ff 0%, #e0f2fe 100%);
  border-radius: 12px;
  border: 1px solid #bae6fd;

  h4 {
    margin: 0 0 12px 0;
    font-size: 14px;
    color: #0284c7;
    font-weight: 600;
  }

  .upload-tip {
    margin-top: 8px;
    font-size: 12px;
    color: #6b7280;
  }

  .upload-result {
    margin-top: 16px;
  }
}

.status-section {
  margin-bottom: 20px;
}

.event-section {
  margin-bottom: 20px;
}

.event-section h4 {
  margin-bottom: 12px;
}

.history-section h4 {
  margin-bottom: 12px;
}

.el-pagination {
  margin-top: 16px;
  justify-content: flex-end;
}
</style>
