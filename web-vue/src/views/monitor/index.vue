<template>
  <div class="monitor">
    <!-- 工具栏 -->
    <div class="toolbar">
      <div class="left">
        <el-radio-group v-model="layout" size="large">
          <el-radio-button :value="1">
            <el-icon><Monitor /></el-icon>
          </el-radio-button>
          <el-radio-button :value="4">
            <el-icon><Grid /></el-icon>
          </el-radio-button>
          <el-radio-button :value="9">
            <el-icon><Grid /></el-icon>
          </el-radio-button>
        </el-radio-group>
        
        <!-- 目标检测开关 -->
        <el-divider direction="vertical" />
        <el-switch
          v-model="objectDetectionEnabled"
          active-text="目标检测"
          inactive-text="目标检测"
          @change="handleObjectDetectionToggle"
          style="margin-left: 12px;"
        />
        <el-switch
          v-model="trackingEnabled"
          active-text="目标跟踪"
          inactive-text="目标跟踪"
          @change="handleTrackingToggle"
        />
        <el-select
          v-model="trackerBackend"
          class="tracker-select"
          size="small"
          placeholder="跟踪算法"
          @change="handleTrackerBackendChange"
        >
          <el-option label="ByteTrack" value="bytetrack" />
          <el-option label="DeepSORT" value="deepsort" />
        </el-select>
      </div>
      <div class="right">
        <el-button-group>
          <el-tooltip content="目标检测阈值设置" placement="top">
            <el-button @click="thresholdSettingsVisible = true" type="primary" plain>
              <el-icon><Setting /></el-icon>
            </el-button>
          </el-tooltip>
          <el-tooltip content="短信通知设置" placement="top">
            <el-button @click="smsSettingsVisible = true" type="primary" plain>
              <el-icon><Message /></el-icon>
            </el-button>
          </el-tooltip>
          <el-button @click="refreshCameras" :loading="loading">
            <el-icon><Refresh /></el-icon>
            刷新
          </el-button>
        </el-button-group>
      </div>
    </div>

    <div class="monitor-container">
      <!-- 左侧：摄像头视频区域 -->
      <div class="video-panel">
        <div class="panel-header">
          <h3>校园安全监控</h3>
          <div class="panel-actions">
            <el-tooltip content="清空所有显示" placement="top" effect="dark">
              <el-button 
                type="danger" 
                size="small" 
                plain 
                @click="clearAllCameras" 
                :disabled="displayCameras.length === 0"
              >
                <el-icon><Delete /></el-icon>
                清空显示
              </el-button>
            </el-tooltip>
          </div>
        </div>
        <div class="video-grid" :class="'grid-' + layout" :style="{ overflow: showScrollbar ? 'auto' : 'hidden' }">
          <template v-if="displayCameras.length > 0">
            <div v-for="camera in displayCameras" :key="camera.id" class="video-item">
              <div class="video-wrapper">
                <div class="video-controls">
                  <el-tooltip content="移除此摄像头" placement="top" effect="dark">
                    <el-button
                      class="remove-camera-btn"
                      type="danger"
                      size="small"
                      circle
                      @click.stop="removeCamera(camera)"
                    >
                      <el-icon><Close /></el-icon>
                    </el-button>
                  </el-tooltip>
                </div>
                <VideoPlayer
                  :camera="camera"
                  :is-active="isActiveCamera(camera.id)"
                  :server-draw-enabled="serverDrawEnabled"
                  @click="handleCameraClick"
                />
              </div>
            </div>
          </template>
          <template v-else>
            <div class="empty-state">
              <el-empty 
                description="请从右侧列表选择要查看的摄像头"
                :image-size="200"
              >
                <template #image>
                  <el-icon :size="60" style="margin-bottom: 15px;"><Monitor /></el-icon>
                </template>
              </el-empty>
            </div>
          </template>
        </div>
      </div>
      
      <!-- 右侧面板 -->
      <div class="right-panel">
        <!-- 右侧上方：近期检测记录 -->
        <div class="detection-panel">
          <div class="panel-header">
            <h3>安全事件记录</h3>
            <span v-if="activeCamera">- {{ activeCamera.name }}</span>
          </div>
          
          <template v-if="activeCamera">
            <div class="detection-grid" v-loading="loadingRecentDetections">
              <template v-if="recentDetections.length > 0">
                <div 
                  v-for="detection in recentDetections" 
                  :key="detection.id" 
                  class="detection-item"
                >
                  <div class="detection-image">
                    <el-image
                      :src="detection.imageUrl"
                      :preview-src-list="[detection.imageUrl]"
                      fit="cover"
                      :preview-teleported="true"
                      :z-index="9999"
                    />
                    <div class="detection-info">
                      <span class="detection-time">{{ formatTime(detection.detectionTime) }}</span>
                    </div>
                  </div>
                </div>
              </template>
              <template v-else>
                <div class="empty-state">
                  <el-empty 
                    description="暂无检测记录"
                    :image-size="100"
                  >
                    <template #image>
                      <el-icon :size="40" style="margin-bottom: 10px;"><PictureFilled /></el-icon>
                    </template>
                  </el-empty>
                </div>
              </template>
            </div>
          </template>
          <template v-else>
            <div class="empty-state">
              <el-empty 
                description="请先选择一个摄像头"
                :image-size="100"
              >
                <template #image>
                  <el-icon :size="40" style="margin-bottom: 10px;"><Select /></el-icon>
                </template>
              </el-empty>
            </div>
          </template>
        </div>
        
        <!-- 右侧下方：摄像头列表 -->
        <div class="camera-list">
          <div class="panel-header">
            <div class="header-title">
              <el-icon><VideoCameraFilled /></el-icon>
              <h3>监控设备</h3>
            </div>
            <el-button type="primary" link @click="checkAllCamerasStatus">
              <el-icon><Refresh /></el-icon>
              检测状态
            </el-button>
          </div>
          <el-scrollbar>
            <div class="list-content">
              <template v-if="cameras.length > 0">
                <div
                  v-for="camera in cameras"
                  :key="camera.id"
                  class="camera-item"
                  :class="{
                    'is-active': isActiveCamera(camera.id),
                    'is-online': camera.status === 1
                  }"
                  @click="handleCameraSelect(camera)"
                >
                  <div class="camera-item-content">
                    <div class="camera-icon">
                      <el-icon :size="24"><VideoCamera /></el-icon>
                      <div class="status-indicator" :class="{
                        'online': camera.status === 1,
                        'offline': camera.status === 0,
                        'fault': camera.status === 2
                      }"></div>
                    </div>
                    <div class="camera-details">
                      <span class="name">{{ camera.name }}</span>
                      <span class="location">
                        <el-icon><LocationFilled /></el-icon>
                        {{ camera.location }}
                      </span>
                      <span class="status" :class="{
                        'online': camera.status === 1,
                        'offline': camera.status === 0,
                        'fault': camera.status === 2
                      }">
                        {{ formatDeviceStatus(camera.status) }}
                      </span>
                    </div>
                  </div>
                </div>
              </template>
              <template v-else>
                <div class="empty-state">
                  <el-empty 
                    description="暂无摄像头"
                    :image-size="100"
                  >
                    <template #image>
                      <el-icon :size="40" style="margin-bottom: 10px;"><VideoCameraFilled /></el-icon>
                    </template>
                  </el-empty>
                </div>
              </template>
            </div>
          </el-scrollbar>
        </div>
      </div>
    </div>

    <!-- 摄像头详情抽屉 -->
    <el-drawer
      v-model="drawerVisible"
      title="摄像头详情"
      size="400px"
      :show-close="true"
      :with-header="true"
    >
      <template v-if="activeCamera">
        <div class="camera-info">
          <div class="info-item">
            <span class="label">设备名称：</span>
            <span class="value">{{ activeCamera.name }}</span>
          </div>
          <div class="info-item">
            <span class="label">安装位置：</span>
            <span class="value">{{ activeCamera.location }}</span>
          </div>
          <div class="info-item">
            <span class="label">RTSP地址：</span>
            <span class="value">{{ activeCamera.rtspUrl }}</span>
          </div>
          <div class="info-item">
            <span class="label">设备状态：</span>
            <el-tag :type="getStatusTagType(activeCamera.status)">
              {{ formatDeviceStatus(activeCamera.status) }}
            </el-tag>
          </div>
        </div>

        <!-- 检测结果 -->
        <div class="detection-results" v-if="activeDetection">
          <h3>最新检测结果</h3>
          
          <template v-if="activeDetection.detectedObjects && activeDetection.detectedObjects.length > 0">
            <div class="detection-info">
              <div class="info-item">
                <span class="label">模型类型：</span>
                <span class="value">{{ activeDetection.modelType || '未知' }}</span>
              </div>
              <div class="info-item">
                <span class="label">支持类别：</span>
                <span class="value">
                  <el-tag 
                    v-for="(cls, index) in activeDetection.supportedClasses" 
                    :key="index" 
                    size="small" 
                    class="mx-1"
                    :style="getClassTagStyle(cls)"
                  >
                    {{ cls }}
                  </el-tag>
                </span>
              </div>
              <div class="info-item">
                <span class="label">处理时间：</span>
                <span class="value">{{ formatProcessTime(activeDetection.processTime) }}</span>
              </div>
            </div>
            
            <div class="detected-objects">
              <h4>检测到的对象</h4>
              <el-table :data="activeDetection.detectedObjects" stripe style="width: 100%">
                <el-table-column prop="class" label="类别" width="100">
                  <template #default="scope">
                    <span class="class-label" :style="{ backgroundColor: getClassColor(scope.row.class), color: '#fff', padding: '2px 6px', borderRadius: '4px' }">
                      {{ scope.row.class }}
                    </span>
                  </template>
                </el-table-column>
                <el-table-column prop="confidence" label="置信度" width="100">
                  <template #default="scope">
                    {{ (scope.row.confidence * 100).toFixed(2) }}%
                  </template>
                </el-table-column>
                <el-table-column label="等级" width="100">
                  <template #default="scope">
                    <el-tag :type="getConfidenceLevelType(scope.row.level)">
                      {{ formatConfidenceLevel(scope.row.level) }}
                    </el-tag>
                  </template>
                </el-table-column>
              </el-table>
            </div>
            
            <!-- 跟踪对象 -->
            <div class="tracked-objects" v-if="activeDetection.trackedObjects && activeDetection.trackedObjects.length > 0">
              <h4>跟踪对象</h4>
              <el-table :data="activeDetection.trackedObjects" stripe style="width: 100%">
                <el-table-column prop="trackId" label="跟踪ID" width="80" />
                <el-table-column prop="class" label="类别" width="100" />
                <el-table-column prop="confidence" label="置信度" width="100">
                  <template #default="scope">
                    {{ (scope.row.confidence * 100).toFixed(2) }}%
                  </template>
                </el-table-column>
              </el-table>
            </div>
          </template>
          
          <div v-else class="no-detection">
            <el-empty description="未检测到任何对象" :image-size="100" />
          </div>
        </div>
      </template>
    </el-drawer>

    <!-- 短信通知设置对话框 -->
    <el-dialog
      v-model="smsSettingsVisible"
      title="短信通知设置"
      width="450px"
      destroy-on-close
    >
      <el-form label-width="120px" label-position="left">
        <el-form-item label="启用短信通知">
          <el-switch v-model="smsNotification.enabled" />
        </el-form-item>
        
        <el-form-item label="冷却时间" v-if="smsNotification.enabled">
          <el-select v-model="smsNotification.cooldownPeriod">
            <el-option :value="30000" label="30秒" />
            <el-option :value="60000" label="1分钟" />
            <el-option :value="300000" label="5分钟" />
            <el-option :value="600000" label="10分钟" />
            <el-option :value="1800000" label="30分钟" />
          </el-select>
          <div class="form-help-text">检测到警情后，在此时间内不会重复发送短信</div>
        </el-form-item>
        
        <el-form-item label="通知接收人" v-if="smsNotification.enabled">
          <el-tag
            v-for="(recipient, index) in smsNotification.recipients"
            :key="index"
            class="recipient-tag"
            closable
            @close="removeRecipient(index)"
          >
            {{ recipient.realName }}
          </el-tag>
          <el-button size="small" @click="addRecipient" :disabled="!selectedUserId">
            <el-icon><Plus /></el-icon> 添加接收人
          </el-button>
          <el-select v-model="selectedUserId" @change="addRecipient" :disabled="!smsNotification.enabled">
            <el-option v-for="user in systemUsers" :key="user.id" :label="user.realName" :value="user.id" />
          </el-select>
        </el-form-item>
        
        <el-form-item label="短信发送记录" v-if="smsNotification.enabled">
          <div v-if="smsNotification.lastSentTime" class="last-sent-info">
            <el-icon><Timer /></el-icon>
            <span>最近一次发送时间: {{ formatLastSentTime(smsNotification.lastSentTime) }}</span>
          </div>
          <div v-else class="last-sent-info empty-record">
            <el-icon><InfoFilled /></el-icon>
            <span>暂无发送记录</span>
          </div>
          
          <div v-if="smsNotification.history.length > 0" class="sms-history">
            <el-divider content-position="center">
              <el-icon><ChatDotRound /></el-icon>
              <span class="divider-text">历史记录</span>
            </el-divider>
            
            <div class="scroll-container">
              <div class="scroll-indicator top">
                <el-icon><ArrowUp /></el-icon>
              </div>
              <el-scrollbar>
                <div class="history-list">
                  <div 
                    v-for="(record, index) in smsNotification.history" 
                    :key="index"
                    class="history-card"
                  >
                    <div class="history-card-header">
                      <div class="history-card-title">
                        <el-icon class="camera-icon"><VideoCamera /></el-icon>
                        <span class="camera-name">{{ record.camera }}</span>
                      </div>
                      <div class="history-time">
                        <el-icon><Clock /></el-icon>
                        <span>{{ formatLastSentTime(record.time) }}</span>
                      </div>
                    </div>
                    
                    <div class="history-card-content">
                      <div class="message-section">
                        <div class="section-title">
                          <el-icon><Message /></el-icon>
                          <span>发送内容</span>
                        </div>
                        <div class="message-content">{{ record.content }}</div>
                      </div>
                      
                      <div class="recipients-section">
                        <div class="section-title">
                          <el-icon><User /></el-icon>
                          <span>接收人</span>
                          <span class="recipient-count">({{ record.recipients.length }}人)</span>
                        </div>
                        <div class="recipients-list">
                          <el-tag
                            v-for="(recipient, rIndex) in record.recipients"
                            :key="rIndex"
                            size="small"
                            class="recipient-tag"
                            effect="light"
                            round
                          >
                            {{ recipient.realName }}
                          </el-tag>
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              </el-scrollbar>
              <div class="scroll-indicator bottom">
                <el-icon><ArrowDown /></el-icon>
              </div>
            </div>
          </div>
          <div v-else class="empty-history">
            <div class="empty-history-content">
              <el-icon :size="32"><ChatLineRound /></el-icon>
              <div class="empty-text">暂无历史记录</div>
              <div class="empty-subtext">发送短信后将在此处显示历史记录</div>
            </div>
          </div>
        </el-form-item>
      </el-form>
      
      <template #footer>
        <span class="dialog-footer">
          <el-button @click="smsSettingsVisible = false">取消</el-button>
          <el-button type="primary" @click="saveSmsSettings">确认</el-button>
        </span>
      </template>
    </el-dialog>

    <!-- 目标检测阈值设置对话框 -->
    <el-dialog
      v-model="thresholdSettingsVisible"
      title="目标检测阈值设置"
      width="500px"
      destroy-on-close
    >
      <el-form label-width="100px" label-position="left">
        <el-form-item label="检测开关">
          <el-switch v-model="objectDetectionSettings.enabled" />
        </el-form-item>

        <el-divider content-position="center">置信度阈值</el-divider>

        <el-form-item label="目标阈值">
          <el-slider
            v-model="objectDetectionSettings.confidenceThreshold"
            :min="0"
            :max="100"
            :step="1"
            :disabled="!objectDetectionSettings.enabled"
            show-stops
            :format-tooltip="val => val + '%'"
          />
          <div class="threshold-help">
            置信度高于此值的检测结果将被显示
          </div>
        </el-form-item>

        <el-divider content-position="center">其他设置</el-divider>

        <el-form-item label="框数量阈值">
          <el-input-number
            v-model="objectDetectionSettings.boxCountThreshold"
            :min="0"
            :max="100"
            :step="1"
            :disabled="!objectDetectionSettings.enabled"
          />
          <div class="threshold-help">
            当检测到的目标框数量超过此值时触发告警（0表示不限制）
          </div>
        </el-form-item>

        <el-divider content-position="center">当前检测数量</el-divider>

        <div class="detection-counts">
          <div class="count-item">
            <span class="count-label">摄像头1 (cam0):</span>
            <span class="count-value">{{ cam0DetectionCount }}</span>
          </div>
          <div class="count-item">
            <span class="count-label">摄像头2 (cam1):</span>
            <span class="count-value">{{ cam1DetectionCount }}</span>
          </div>
        </div>
      </el-form>

      <template #footer>
        <el-button @click="resetThresholdSettings">恢复默认</el-button>
        <el-button @click="thresholdSettingsVisible = false">取消</el-button>
        <el-button type="primary" @click="saveThresholdSettings">保存设置</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import type { Camera } from '@/types/camera'
import { getCameraList } from '@/api/camera'
import wsClient from '@/utils/websocket'
import type { WebSocketMessage } from '@/utils/websocket'
import VideoPlayer from '@/components/VideoPlayer/index.vue'
import { pageDetectionRecords, type DetectionRecord, type DetectionRecordQueryParams } from '@/api/detection'
import { getUserList } from '@/api/user'
import type { UserInfo } from '@/types/user'
import { 
  Monitor, Grid, Refresh, VideoCameraFilled, Select,
  PictureFilled, Warning, Picture as PictureIcon, Close, Delete, Message, Plus,
  ChatDotRound, VideoCamera, Clock, User, ChatLineRound, InfoFilled, Timer,
  ArrowUp, ArrowDown, LocationFilled, Setting
} from '@element-plus/icons-vue'

const router = useRouter()

// 布局方式（1/4/9宫格）
const layout = ref(4)
// 摄像头列表
const cameras = ref<Camera[]>([])
// 当前显示的摄像头列表
const displayCameras = ref<Camera[]>([])
// 当前选中的摄像头
const activeCamera = ref<Camera>()
// 当前活跃的检测结果
const activeDetection = ref<any>(null)
// 抽屉可见性
const drawerVisible = ref(false)
// 加载状态
const loading = ref(false)
// 服务器端绘制检测框状态
const serverDrawEnabled = ref(false)

// 近期检测记录
const recentDetections = ref<DetectionRecord[]>([])
const loadingRecentDetections = ref(false)

// 网络状态
const networkState = ref({
  wsConnected: false,
  checkingConnection: false
})

// 防止弹窗重复显示
const messageDebounce = ref({
  lastMessageTime: 0,
  messageDebounceInterval: 3000 // 同类消息在3秒内不重复显示
})

// 短信通知设置
const smsNotification = ref({
  enabled: true,
  lastSentTime: null as null | number,
  cooldownPeriod: 60000, // 冷却时间，默认1分钟内不重复发送
  recipients: [] as { id: number; realName: string; }[], // 改为对象数组，包含用户ID和姓名
  history: [] as {
    time: number;
    camera: string;
    content: string;
    recipients: { id: number; realName: string; }[]; // 同样改为对象数组
  }[]
})

// 短信通知设置对话框可见性
const smsSettingsVisible = ref(false)
// 系统用户列表
const systemUsers = ref<UserInfo[]>([])
// 用户列表加载状态
const loadingUsers = ref(false)
// 用户选择的值 - 使用字符串类型避免null类型错误
const selectedUserId = ref<string>('')

// 目标检测阈值设置
const thresholdSettingsVisible = ref(false)
const objectDetectionEnabled = ref(true)
const isSyncingDetectionStatus = ref(false)
const trackingEnabled = ref(true)
const isSyncingTrackingStatus = ref(false)
type TrackerBackend = 'bytetrack' | 'deepsort'
const TRACKER_BACKEND_STORAGE_KEY = 'monitorTrackerBackend'
const TRACKING_ENABLED_STORAGE_KEY = 'monitorTrackingEnabled'
const trackerBackend = ref<TrackerBackend>('bytetrack')

// 目标检测阈值配置
const objectDetectionSettings = ref({
  enabled: true,
  confidenceThreshold: 50,      // 置信度阈值
  boxCountThreshold: 0           // 框数量阈值（0表示不限制）
})

const parseBooleanLike = (value: unknown): boolean | null => {
  if (typeof value === 'boolean') return value
  if (typeof value === 'number') return value !== 0
  if (typeof value === 'string') {
    const normalized = value.trim().toLowerCase()
    if (normalized === 'true' || normalized === '1' || normalized === 'on') return true
    if (normalized === 'false' || normalized === '0' || normalized === 'off') return false
  }
  return null
}

const parseTrackerBackendLike = (value: unknown): TrackerBackend | null => {
  if (typeof value !== 'string') return null
  const normalized = value.trim().toLowerCase()
  if (normalized === 'bytetrack' || normalized === 'deepsort') {
    return normalized
  }
  return null
}

const trackerBackendLabel = (backend: TrackerBackend): string => {
  return backend === 'deepsort' ? 'DeepSORT' : 'ByteTrack'
}

const saveTrackerBackend = (backend: TrackerBackend) => {
  localStorage.setItem(TRACKER_BACKEND_STORAGE_KEY, backend)
}

const saveTrackingEnabled = (enabled: boolean) => {
  localStorage.setItem(TRACKING_ENABLED_STORAGE_KEY, enabled ? '1' : '0')
}

const loadTrackerBackend = () => {
  const saved = localStorage.getItem(TRACKER_BACKEND_STORAGE_KEY)
  const parsed = parseTrackerBackendLike(saved)
  if (parsed) {
    trackerBackend.value = parsed
  }
}

const loadTrackingEnabled = () => {
  const saved = localStorage.getItem(TRACKING_ENABLED_STORAGE_KEY)
  const parsed = parseBooleanLike(saved)
  if (parsed !== null) {
    trackingEnabled.value = parsed
  }
}

const handleTrackerBackendChange = (value: TrackerBackend) => {
  saveTrackerBackend(value)
  if (objectDetectionEnabled.value) {
    ElMessage.success(`跟踪算法已切换为 ${trackerBackendLabel(value)}（请关闭后重新开启目标检测以生效）`)
  } else {
    ElMessage.success(`跟踪算法已切换为 ${trackerBackendLabel(value)}`)
  }
}

const setTrackingToggleState = (enabled: boolean) => {
  isSyncingTrackingStatus.value = true
  trackingEnabled.value = enabled
  saveTrackingEnabled(enabled)
  window.setTimeout(() => {
    isSyncingTrackingStatus.value = false
  }, 0)
}

const setDetectionToggleState = (enabled: boolean) => {
  isSyncingDetectionStatus.value = true
  objectDetectionEnabled.value = enabled
  objectDetectionSettings.value.enabled = enabled
  window.setTimeout(() => {
    isSyncingDetectionStatus.value = false
  }, 0)
}

const extractInferenceEnabled = (payload: any): boolean | null => {
  const data = payload?.data ?? payload
  return (
    parseBooleanLike(data?.inference_enabled) ??
    parseBooleanLike(data?.inferenceEnabled) ??
    parseBooleanLike(data?.inference) ??
    null
  )
}

const extractTrackerBackend = (payload: any): TrackerBackend | null => {
  const data = payload?.data ?? payload
  return (
    parseTrackerBackendLike(data?.tracker_backend) ??
    parseTrackerBackendLike(data?.trackerBackend) ??
    null
  )
}

const extractTrackerEnabled = (payload: any): boolean | null => {
  const data = payload?.data ?? payload
  return (
    parseBooleanLike(data?.tracker_enabled) ??
    parseBooleanLike(data?.trackerEnabled) ??
    parseBooleanLike(data?.tracking_enabled) ??
    parseBooleanLike(data?.trackingEnabled) ??
    null
  )
}

const isRknnOperationSuccess = (payload: any): boolean => {
  if (!payload || payload.code !== 200) {
    return false
  }

  const data = payload.data
  if (data && typeof data === 'object') {
    const successLike = parseBooleanLike(data.success)
    if (successLike === false) {
      return false
    }

    if (typeof data.status === 'string') {
      const status = data.status.trim().toLowerCase()
      if (status === 'error' || status === 'failed' || status === 'fail') {
        return false
      }
    }
  }

  return true
}

const getRknnOperationErrorMessage = (payload: any, fallback: string): string => {
  if (!payload || typeof payload !== 'object') {
    return fallback
  }

  const data = payload.data
  if (data && typeof data === 'object') {
    if (typeof data.error === 'string' && data.error.trim()) return data.error
    if (typeof data.message === 'string' && data.message.trim()) return data.message
  }
  if (typeof payload.msg === 'string' && payload.msg.trim()) return payload.msg
  return fallback
}

const syncDetectionToggleFromServer = async (silent = true): Promise<boolean> => {
  try {
    const res = await fetch('/api/rknn/status')
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`)
    }

    const payload = await res.json()
    const enabled = extractInferenceEnabled(payload)
    const backend = extractTrackerBackend(payload)
    const tracker = extractTrackerEnabled(payload)
    if (enabled === null) {
      throw new Error('未在状态响应中找到 inference_enabled 字段')
    }

    setDetectionToggleState(enabled)
    if (tracker !== null) {
      setTrackingToggleState(tracker)
    }
    if (backend) {
      trackerBackend.value = backend
      saveTrackerBackend(backend)
    }
    return true
  } catch (err) {
    console.error('同步目标检测状态失败:', err)
    if (!silent) {
      ElMessage.warning('未获取到视觉模块实时状态，已保留本地开关状态')
    }
    return false
  }
}

const handleTrackingToggle = async (enabled: string | number | boolean) => {
  if (isSyncingTrackingStatus.value) return

  const targetEnabled = parseBooleanLike(enabled)
  if (targetEnabled === null) {
    ElMessage.error('目标跟踪开关值无效')
    return
  }

  const previousEnabled = !targetEnabled
  setTrackingToggleState(targetEnabled)

  if (!objectDetectionEnabled.value) {
    ElMessage.success(targetEnabled ? '目标跟踪已开启（下次开启目标检测时生效）' : '目标跟踪已关闭（下次开启目标检测时生效）')
    return
  }

  try {
    // 兼容老后端：直接复用 inference/on 的 track 参数做跟踪即时切换
    const endpoint = `/api/rknn/inference/on?track=${targetEnabled ? 'true' : 'false'}&tracker=${encodeURIComponent(trackerBackend.value)}`
    const res = await fetch(endpoint, { method: 'POST' })
    const data = await res.json()

    if (isRknnOperationSuccess(data)) {
      const synced = await syncDetectionToggleFromServer(true)
      if (!synced) {
        setTrackingToggleState(targetEnabled)
      }
      ElMessage.success(targetEnabled ? '目标跟踪已开启' : '目标跟踪已关闭')
    } else {
      setTrackingToggleState(previousEnabled)
      ElMessage.error(getRknnOperationErrorMessage(data, '目标跟踪切换失败，已回滚'))
    }
  } catch (err) {
    setTrackingToggleState(previousEnabled)
    console.error('目标跟踪切换失败:', err)
    ElMessage.error('目标跟踪切换失败，已回滚')
  }
}

// 目标检测开关切换
const handleObjectDetectionToggle = async (enabled: string | number | boolean) => {
  if (isSyncingDetectionStatus.value) return

  const targetEnabled = parseBooleanLike(enabled)
  if (targetEnabled === null) {
    ElMessage.error('目标检测开关值无效')
    return
  }

  const previousEnabled = objectDetectionSettings.value.enabled
  objectDetectionSettings.value.enabled = targetEnabled

  try {
    const endpoint = targetEnabled
      ? `/api/rknn/inference/on?track=${trackingEnabled.value ? 'true' : 'false'}&tracker=${encodeURIComponent(trackerBackend.value)}`
      : '/api/rknn/inference/off'
    const res = await fetch(endpoint, { method: 'POST' })
    const data = await res.json()

    if (isRknnOperationSuccess(data)) {
      const synced = await syncDetectionToggleFromServer(true)
      if (!synced) {
        setDetectionToggleState(targetEnabled)
      }
      ElMessage.success(
        targetEnabled
          ? `目标检测已开启（跟踪${trackingEnabled.value ? '开启' : '关闭'}，${trackerBackendLabel(trackerBackend.value)}）`
          : '目标检测已关闭'
      )
      localStorage.setItem('objectDetectionSettings', JSON.stringify(objectDetectionSettings.value))
      saveTrackerBackend(trackerBackend.value)
      saveTrackingEnabled(trackingEnabled.value)
    } else {
      setDetectionToggleState(previousEnabled)
      ElMessage.error(getRknnOperationErrorMessage(data, '操作失败，已回滚开关状态'))
    }
  } catch (err) {
    setDetectionToggleState(previousEnabled)
    console.error('目标检测开关失败:', err)
    ElMessage.error('目标检测开关失败，已回滚开关状态')
  }
}

// 框数量阈值相关
const cam0DetectionCount = ref(0)
const cam1DetectionCount = ref(0)

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

// 保存阈值设置
const saveThresholdSettings = async () => {
  // 保存到本地存储
  localStorage.setItem('objectDetectionSettings', JSON.stringify(objectDetectionSettings.value))

  // 调用后端 API 设置置信度阈值
  try {
    const res = await fetch(
      `/api/rknn/threshold/set?value=${objectDetectionSettings.value.confidenceThreshold / 100}&boxCount=${objectDetectionSettings.value.boxCountThreshold}`,
      { method: 'POST' }
    )
    const data = await res.json()
    if (data.code === 200) {
      ElMessage.success('阈值设置已保存')
    } else {
      ElMessage.error(data.msg || '设置失败')
    }
  } catch (err) {
    console.error('保存阈值失败:', err)
    ElMessage.error('保存阈值失败')
  }

  thresholdSettingsVisible.value = false
}

// 恢复默认设置
const resetThresholdSettings = () => {
  objectDetectionSettings.value = {
    enabled: true,
    confidenceThreshold: 50,
    boxCountThreshold: 0
  }
  ElMessage.info('已恢复默认设置')
}

// 加载保存的设置
const loadThresholdSettings = () => {
  const saved = localStorage.getItem('objectDetectionSettings')
  if (saved) {
    try {
      const parsed = JSON.parse(saved)
      objectDetectionSettings.value = { ...objectDetectionSettings.value, ...parsed }
      objectDetectionEnabled.value = objectDetectionSettings.value.enabled
    } catch (e) {
      console.error('加载阈值设置失败:', e)
    }
  }
}

// 格式化设备状态
const formatDeviceStatus = (status: number): string => {
  const statusTexts = ['离线', '在线', '故障']
  return statusTexts[status] || '未知'
}

// 格式化处理时间
const formatProcessTime = (time?: number): string => {
  if (!time) return '未知'
  return `${time.toFixed(3)}秒`
}

// 格式化置信度等级
const formatConfidenceLevel = (level: number): string => {
  const levels = ['忽略', '很低', '低', '中', '高', '特别高']
  return levels[level] || '未知'
}

// 获取置信度等级类型
const getConfidenceLevelType = (level: number): 'success' | 'warning' | 'info' | 'danger' | 'primary' => {
  const types = ['info', 'info', 'success', 'warning', 'danger', 'danger']
  return types[level] as 'success' | 'warning' | 'info' | 'danger' | 'primary'
}

// 获取摄像头列表
const fetchCameras = async () => {
  if (loading.value) return // 防止重复调用

  try {
    loading.value = true
    const res = await getCameraList({
      current: 1,
      size: 100, // 获取更多摄像头
    })
    cameras.value = res.records
    // 不在这里初始化显示的摄像头，由 autoSelectDefaultCameras 处理
  } catch (error) {
    console.error('获取摄像头列表失败:', error)
    return // 如果获取列表失败，直接返回
  } finally {
    loading.value = false
  }
}

// 更新显示的摄像头列表
const updateDisplayCameras = () => {
  // 过滤掉离线的摄像头
  const oldDisplayCameras = displayCameras.value.slice()
  displayCameras.value = displayCameras.value.filter(camera => {
    const currentCamera = cameras.value.find(c => c.id === camera.id)
    return currentCamera && currentCamera.status === 1
  }).slice(0, layout.value)

  // 检查是否有摄像头被移除，确保停止其视频流
  oldDisplayCameras.forEach(camera => {
    if (!displayCameras.value.some(c => c.id === camera.id)) {
      wsClient.send({
        type: 'stop_stream',
        data: {
          cameraId: camera.id
        }
      })
    }
  })
}

// 检查摄像头是否在显示列表中（即是否激活）
const isActiveCamera = (cameraId: number) => {
  return displayCameras.value.some(camera => camera.id === cameraId)
}

// 检查所有摄像头状态
const checkAllCamerasStatus = async () => {
  if (loading.value) return // 如果正在加载中，直接返回
  
  loading.value = true
  try {
    // 清除所有非故障摄像头的状态
    cameras.value.forEach(camera => {
      if (camera.status !== 2) {
        camera.status = 0
      }
    })
    
    // 串行检查每个摄像头的状态，跳过故障状态的设备
    for (const camera of cameras.value) {
      // 跳过故障状态的设备
      if (camera.status === 2) {
        continue
      }
      
      // 发送检测请求
      wsClient.send({
        type: 'check_camera',
        data: {
          cameraId: camera.id,
          rtspUrl: camera.rtspUrl
        }
      })
      
      // 等待检测完成
      await new Promise<void>((resolve) => {
        // 创建一个超时检测
        const timeoutId = setTimeout(() => {
          // 检测超时，这里不再打印日志以减少控制台噪音
          resolve()
        }, 3000) // 给每个摄像头3秒的检测时间
        
        // 创建一个一次性的消息处理器
        const handler = (message: WebSocketMessage) => {
          if (message.type === 'camera_status' && message.data.cameraId === camera.id) {
            clearTimeout(timeoutId) // 清除超时定时器
            wsClient.removeMessageHandler(handler) // 移除这个临时处理器
            resolve()
          }
        }
        
        wsClient.addMessageHandler(handler)
      })
      
      // 检测完成后等待一小段时间再检测下一个，避免并发
      await new Promise(resolve => setTimeout(resolve, 500))
    }
  } catch (error) {
    console.error('检查摄像头状态失败:', error)
  } finally {
    loading.value = false
  }
}

// 监听布局变化
watch(() => layout.value, () => {
  // 更新显示的摄像头列表
  updateDisplayCameras()
})

// 处理摄像头点击
const handleCameraClick = (camera: Camera) => {
  activeCamera.value = camera
  drawerVisible.value = true
}

// 处理WebSocket消息
const handleWebSocketMessage = (message: WebSocketMessage) => {
  const { type, data } = message

  switch (type) {
    case 'detection_result':
      // 更新通用检测结果
      const cameraId = data.cameraId
      const targetCamera = cameras.value.find(c => c.id === cameraId)
      
      if (targetCamera) {
        // 如果检测的是当前选中的摄像头，更新活跃检测结果
        if (activeCamera.value && activeCamera.value.id === cameraId) {
          activeDetection.value = data
        }
        
        // 更新服务器端绘制检测框状态
        if (data.serverDrawEnabled !== undefined) {
          serverDrawEnabled.value = data.serverDrawEnabled
        }
        
        // 检测到高置信度对象时显示通知
        if (data.detectedObjects && data.detectedObjects.length > 0) {
          const highConfidenceObjects = data.detectedObjects.filter(obj => obj.level >= 3)
          
          if (highConfidenceObjects.length > 0) {
            // 发送短信通知
            if (smsNotification.value.enabled) {
              sendSmsAlert(targetCamera, highConfidenceObjects)
            }
          }
        }
      }
      break;

    case 'camera_status':
      // 更新摄像头状态
      const targetStatusCamera = cameras.value.find(c => c.id === data.cameraId)
      if (targetStatusCamera) {
        const oldStatus = targetStatusCamera.status
        targetStatusCamera.status = data.status || 0
        
        // 只在状态真正发生变化时才显示消息
        if (oldStatus !== targetStatusCamera.status) {
          const statusText = formatDeviceStatus(targetStatusCamera.status)
          const messageType = targetStatusCamera.status === 1 ? 'success' : targetStatusCamera.status === 2 ? 'warning' : 'error'
          
          // 如果摄像头离线或故障，从显示列表中移除
          if (targetStatusCamera.status === 0 || targetStatusCamera.status === 2) {
            targetStatusCamera.streaming = false
            displayCameras.value = displayCameras.value.filter(c => c.id !== data.cameraId)
            // 停止该摄像头的流
            wsClient.send({
              type: 'stop_stream',
              data: {
                cameraId: data.cameraId
              }
            })
            
            // 如果摄像头从在线变为离线或故障，发送短信通知
            if (oldStatus === 1 && smsNotification.value.enabled) {
              sendOfflineAlert(targetStatusCamera)
            }
          }
          
          ElMessage({
            type: messageType,
            message: '摄像头 ' + targetStatusCamera.name + ' ' + statusText
          })
        }
      }
      break;

    case 'stream_stopped':
      // 找到对应的摄像头
      const stoppedCamera = cameras.value.find(c => c.id === data.cameraId)
      if (stoppedCamera) {
        stoppedCamera.streaming = false
      }
      break;
  }
}

// 获取系统用户列表
const fetchSystemUsers = async () => {
  loadingUsers.value = true
  try {
    const res = await getUserList({
      current: 1,
      size: 100, // 获取足够多的用户
      status: 1 // 只获取启用状态的用户
    })
    systemUsers.value = res.records
  } catch (error) {
    console.error('获取用户列表失败:', error)
    ElMessage.error('获取用户列表失败')
  } finally {
    loadingUsers.value = false
  }
}

// 添加接收人
const addRecipient = () => {
  if (!selectedUserId.value) return

  // 检查是否已经添加过此用户
  const userId = Number(selectedUserId.value)
  if (smsNotification.value.recipients.some(r => r.id === userId)) {
    ElMessage.warning('该用户已在接收列表中')
    return
  }

  // 找到对应的用户信息
  const selectedUser = systemUsers.value.find(u => u.id === userId)
  if (selectedUser) {
    smsNotification.value.recipients.push({
      id: selectedUser.id,
      realName: selectedUser.realName
    })
    selectedUserId.value = '' // 重置选择
  }
}

// 移除接收人
const removeRecipient = (index: number) => {
  smsNotification.value.recipients.splice(index, 1)
}

// 初始化默认接收人
const initDefaultRecipients = () => {
  // 清空旧的接收人
  smsNotification.value.recipients = []
  
  // 初始化默认接收人（这里可以设置特定角色的用户，如管理员）
  const defaultRecipients = [
    { id: 1, realName: '系统管理员' }
  ]
  
  smsNotification.value.recipients = defaultRecipients
}

// 显示消息弹窗（防重复）
const showMessageWithDebounce = (message: string, type: 'success' | 'warning' | 'info' | 'error') => {
  const now = Date.now()
  // 如果距离上次显示消息的时间小于防抖间隔，则不显示
  if (now - messageDebounce.value.lastMessageTime < messageDebounce.value.messageDebounceInterval) {
    // 消息防抖：短时间内重复的提示不再输出到控制台
    return
  }
  
  // 更新最后一次显示消息的时间
  messageDebounce.value.lastMessageTime = now
  
  // 显示消息
  ElMessage({
    type,
    message,
    duration: 3000
  })
}

// 模拟发送短信通知
const sendSmsAlert = (camera: Camera, detectedObjects: any[]) => {
  // 检查是否在冷却期内（避免短时间内频繁发送短信）
  const now = Date.now()
  if (
    smsNotification.value.lastSentTime && 
    now - smsNotification.value.lastSentTime < smsNotification.value.cooldownPeriod
  ) {
    // 短信通知处于冷却期，跳过发送（不再打印详细日志）
    return
  }
  
  // 更新最后发送时间
  smsNotification.value.lastSentTime = now
  
  // 构建短信内容
  const objectClasses = [...new Set(detectedObjects.map(obj => obj.class))].join(', ')
  const timeStr = new Date().toLocaleString('zh-CN')
  const smsContent = `【安防系统】警告：${timeStr}，摄像头"${camera.name}"（位置：${camera.location}）检测到${objectClasses}，请及时查看处理。`
  
  // 模拟发送延迟
  setTimeout(() => {
  // 发送短信内容和接收人信息，这里不再打印到控制台以减少噪音
    
    // 记录发送历史
    smsNotification.value.history.unshift({
      time: now,
      camera: camera.name,
      content: smsContent,
      recipients: [...smsNotification.value.recipients]
    })
    
    // 最多保留10条历史记录
    if (smsNotification.value.history.length > 10) {
      smsNotification.value.history = smsNotification.value.history.slice(0, 10)
    }
    
    // 使用防抖函数显示发送成功消息
    showMessageWithDebounce(
      `已成功发送短信通知给${smsNotification.value.recipients.length}位相关人员`,
      'success'
    )
    
    // 在右侧检测记录上方显示一个通知图标
    showSmsNotificationIndicator()
  }, 1500)
}

// 显示短信通知指示器
const showSmsNotificationIndicator = () => {
  // 这里可以实现一个短信已发送的视觉指示
  // 简单起见，这里只做一个控制台输出
  console.log('显示短信通知指示器')
}

// 获取状态标签类型
const getStatusTagType = (status: number): 'success' | 'warning' | 'danger' => {
  const types = ['danger', 'success', 'warning']
  return types[status] as 'success' | 'warning' | 'danger'
}

// 处理摄像头选择
const handleCameraSelect = (camera: Camera) => {
  // 如果摄像头不在线，不进行任何操作
  if (camera.status !== 1) {
    ElMessage.warning(`摄像头 ${camera.name} 当前不在线`)
    return
  }
  
  activeCamera.value = camera
  
  // 查询该摄像头的近期检测记录
  fetchRecentDetections(camera.id)
  
  // 如果当前布局是单摄像头模式，则只显示这一个摄像头
  if (layout.value === 1) {
    // 先停止所有当前的流
    displayCameras.value.forEach(cam => {
      wsClient.send({
        type: 'stop_stream',
        data: {
          cameraId: cam.id
        }
      })
    })
    displayCameras.value = [camera]
    
    // 启动新的流
    startStream(camera)
  } else {
    // 如果当前摄像头已经在显示列表中，则不做任何变化
    if (displayCameras.value.some(c => c.id === camera.id)) {
      return
    }
    
    // 如果显示列表已满，则移除第一个摄像头
    if (displayCameras.value.length >= layout.value) {
      const oldCamera = displayCameras.value[0]
      displayCameras.value = displayCameras.value.slice(1)
      
      // 停止被移除摄像头的视频流
      wsClient.send({
        type: 'stop_stream',
        data: {
          cameraId: oldCamera.id
        }
      })
    }
    
    // 添加新的摄像头到显示列表
    displayCameras.value = [...displayCameras.value, camera]
    
    // 启动新的流
    startStream(camera)
  }
}

// 启动摄像头流
const startStream = (camera: Camera) => {
  // 当前版本不再依赖算法 WebSocket 来启动流；VideoPlayer 会直接播放 HLS
  return
}

// 检查WebSocket连接并尝试建立连接
const checkWebSocketConnection = async () => {
  // WebSocket 已禁用
  return false
  if (networkState.value.checkingConnection) return
  
  if (!wsClient.isConnected()) {
    networkState.value.checkingConnection = true
    
    try {
      // 尝试连接WebSocket服务器
      await wsClient.connect()
      
      // 添加消息处理器
      wsClient.addMessageHandler(handleWebSocketMessage)
      
      return true
    } catch (error) {
      console.error('WebSocket连接失败:', error)
      ElMessage.error('服务器连接失败，请稍后重试')
      return false
    } finally {
      networkState.value.checkingConnection = false
    }
  }
  
  return true
}

// 监听布局变化
watch(() => layout.value, () => {
  // 如果布局变小，需要停止多余的摄像头视频流
  if (displayCameras.value.length > layout.value) {
    // 停止多余的视频流
    const camerasToRemove = displayCameras.value.slice(0, displayCameras.value.length - layout.value)
    camerasToRemove.forEach(camera => {
      wsClient.send({
        type: 'stop_stream',
        data: {
          cameraId: camera.id
        }
      })
    })
    
    // 截取保留的摄像头
    displayCameras.value = displayCameras.value.slice(displayCameras.value.length - layout.value)
  }
})

// 获取摄像头近期检测记录
const fetchRecentDetections = async (cameraId: number) => {
  if (!cameraId) return
  loadingRecentDetections.value = true
  try {
    const queryParams: DetectionRecordQueryParams = {
      current: 1,
      size: 9,
      cameraId
    }
    const res = await pageDetectionRecords(queryParams)
    recentDetections.value = res.records
  } catch (error) {
    console.error('获取近期检测记录失败:', error)
    ElMessage.error('获取近期检测记录失败')
  } finally {
    loadingRecentDetections.value = false
  }
}

// 添加获取类别颜色的函数
const getClassColor = (className: string): string => {
  if (activeDetection.value && activeDetection.value.classColors && activeDetection.value.classColors[className]) {
    return activeDetection.value.classColors[className]
  }
  return '#FF0000' // 默认红色
}

// 获取类别标签样式
const getClassTagStyle = (className: string) => {
  const color = getClassColor(className)
  return {
    backgroundColor: color,
    color: '#fff',
    border: 'none'
  }
}

// 监控画面滚动条控制 - 当显示摄像头时才允许滚动
const showScrollbar = computed(() => {
  return displayCameras.value.length > 0
})

const streamProtocol = (((import.meta.env.VITE_STREAM_PROTOCOL as string | undefined) || 'hls')).toLowerCase()
const isWebRtcStream = streamProtocol === 'webrtc'

const sleep = (ms: number) => new Promise<void>((resolve) => {
  window.setTimeout(resolve, ms)
})

// 初始化
onMounted(async () => {
  // 初始化默认接收人
  initDefaultRecipients()

  // 加载跟踪算法选择
  loadTrackerBackend()
  loadTrackingEnabled()

  // 加载目标检测阈值设置
  loadThresholdSettings()

  // 检查并建立WebSocket连接
  await checkWebSocketConnection()

  // 从视觉模块同步目标检测实时状态，避免仅依赖本地缓存导致开关显示不准
  await syncDetectionToggleFromServer(true)

  // 获取摄像头列表
  await fetchCameras()

  // 添加WebSocket消息处理器
  wsClient.addMessageHandler(handleWebSocketMessage)

  // 添加页面离开时的清理逻辑
  window.addEventListener('beforeunload', handleBeforeUnload)

  // 自动开启推流
  const streamStarted = await startRtspStream()
  if (streamStarted && !isWebRtcStream) {
    // 给 HLS 切片生成一个短暂预热时间，避免首次进入时播放器拿到空清单
    await sleep(1200)
  }

  // 默认选择前两个在线的摄像头
  autoSelectDefaultCameras()

  // 启动定时获取检测数量
  detectionCountInterval.value = setInterval(fetchDetectionCounts, 2000)
})

let detectionCountInterval = ref<number | null>(null)

// 自动开启推流
const startRtspStream = async (): Promise<boolean> => {
  const maxAttempts = 3

  for (let attempt = 1; attempt <= maxAttempts; attempt++) {
    try {
      const res = await fetch('/api/rknn/rtsp/camera/start', { method: 'POST' })
      const data = await res.json()
      if (data.code === 200) {
        console.log('RTSP推流已自动开启')
        return true
      }
      console.warn(`自动开启推流返回异常（第 ${attempt} 次）:`, data)
    } catch (err) {
      console.error(`自动开启推流失败（第 ${attempt} 次）:`, err)
    }

    if (attempt < maxAttempts) {
      await sleep(800)
    }
  }

  return false
}

// 默认选择前两个摄像头
const autoSelectDefaultCameras = () => {
  // 默认使用 cam0 和 cam1 两个摄像头
  const defaultCameras: Camera[] = [
    {
      id: 1,
      name: '摄像头1',
      location: '校园门口',
      rtspUrl: 'rtsp://192.168.137.224:8554/cam0',
      status: 1
    },
    {
      id: 2,
      name: '摄像头2',
      location: '教学楼',
      rtspUrl: 'rtsp://192.168.137.224:8554/cam1',
      status: 1
    }
  ]

  displayCameras.value = defaultCameras
  activeCamera.value = defaultCameras[0]

  // 将默认摄像头添加到摄像头列表（如果不存在）
  defaultCameras.forEach(cam => {
    if (!cameras.value.find(c => c.id === cam.id)) {
      cameras.value.push(cam)
    }
  })

  // 查询第一个摄像头的检测记录
  fetchRecentDetections(defaultCameras[0].id)
}

// 页面卸载前的清理
onBeforeUnmount(() => {
  // 停止定时获取检测数量
  if (detectionCountInterval.value) {
    clearInterval(detectionCountInterval.value)
  }

  // 停止所有视频流
  displayCameras.value.forEach(camera => {
    wsClient.send({
      type: 'stop_stream',
      data: {
        cameraId: camera.id
      }
    })
  })

  // 移除WebSocket消息处理器
  wsClient.removeMessageHandler(handleWebSocketMessage)

  // 移除页面离开时的清理逻辑
  window.removeEventListener('beforeunload', handleBeforeUnload)
})

// 页面离开前的处理
const handleBeforeUnload = () => {
  // 停止所有视频流
  displayCameras.value.forEach(camera => {
    wsClient.send({
      type: 'stop_stream',
      data: {
        cameraId: camera.id
      }
    })
  })
}

// 刷新摄像头列表
const refreshCameras = async () => {
  // 停止所有视频流
  displayCameras.value.forEach(camera => {
    wsClient.send({
      type: 'stop_stream',
      data: {
        cameraId: camera.id
      }
    })
  })
  
  // 清空显示列表
  displayCameras.value = []
  
  // 重新获取摄像头列表
  await fetchCameras()
}

// 格式化时间
const formatTime = (dateStr: string) => {
  if (!dateStr) return ''
  const date = new Date(dateStr)
  return date.toLocaleString('zh-CN', {
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false
  })
}

// 从显示列表中移除摄像头
const removeCamera = (camera: Camera) => {
  // 停止摄像头的视频流
  wsClient.send({
    type: 'stop_stream',
    data: {
      cameraId: camera.id
    }
  })
  
  // 从显示列表中移除该摄像头
  displayCameras.value = displayCameras.value.filter(c => c.id !== camera.id)
  
  // 如果移除的是当前选中的摄像头，则清空近期检测记录
  if (activeCamera.value && activeCamera.value.id === camera.id) {
    recentDetections.value = []
    activeCamera.value = undefined
  }
  
  ElMessage.success(`已停止显示 ${camera.name} 的视频流`)
}

// 清空所有显示的摄像头
const clearAllCameras = () => {
  if (displayCameras.value.length === 0) return
  
  // 停止所有摄像头的视频流
  displayCameras.value.forEach(camera => {
    wsClient.send({
      type: 'stop_stream',
      data: {
        cameraId: camera.id
      }
    })
  })
  
  // 清空显示列表
  displayCameras.value = []
  
  // 清空近期检测记录和当前选中的摄像头
  recentDetections.value = []
  activeCamera.value = undefined
  
  ElMessage.success('已清空所有摄像头显示')
}

// 短信通知设置对话框相关逻辑
const saveSmsSettings = () => {
  // 这里可以添加保存设置的逻辑
  smsSettingsVisible.value = false
}

// 在对话框打开时加载用户列表
watch(() => smsSettingsVisible.value, (newVal) => {
  if (newVal) {
    fetchSystemUsers()
  }
})

// 模拟发送摄像头离线短信通知
const sendOfflineAlert = (camera: Camera) => {
  // 检查是否在冷却期内
  const now = Date.now()
  if (
    smsNotification.value.lastSentTime && 
    now - smsNotification.value.lastSentTime < smsNotification.value.cooldownPeriod
  ) {
    // 冷却期内，跳过发送
    return
  }
  
  // 更新最后发送时间
  smsNotification.value.lastSentTime = now
  
  // 构建短信内容
  const timeStr = new Date().toLocaleString('zh-CN')
  const statusText = camera.status === 0 ? '离线' : '故障'
  const smsContent = `【安防系统】警告：${timeStr}，摄像头"${camera.name}"（位置：${camera.location}）状态变为${statusText}，请检查设备状态。`
  
  // 模拟发送延迟
  setTimeout(() => {
  // 模拟发送摄像头离线短信及接收人信息，这里不再打印到控制台
    
    // 记录发送历史
    smsNotification.value.history.unshift({
      time: now,
      camera: camera.name,
      content: smsContent,
      recipients: [...smsNotification.value.recipients]
    })
    
    // 最多保留10条历史记录
    if (smsNotification.value.history.length > 10) {
      smsNotification.value.history = smsNotification.value.history.slice(0, 10)
    }
    
    // 使用防抖函数显示发送成功消息
    showMessageWithDebounce(
      `已发送摄像头${statusText}通知给${smsNotification.value.recipients.length}位相关人员`,
      'success'
    )
  }, 1500)
}

// 格式化时间戳为可读时间
const formatLastSentTime = (timestamp: number | null): string => {
  if (timestamp) {
    const date = new Date(timestamp)
    return date.toLocaleString('zh-CN')
  }
  return '未知'
}
</script>

<style lang="scss" scoped>
.monitor {
  height: calc(100vh - 120px);
  display: flex;
  flex-direction: column;
  
  .toolbar {
    padding: 15px 20px;
    background-color: var(--el-bg-color-overlay);
    border-radius: 8px;
    margin-bottom: 20px;
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .monitor-container {
    flex: 1;
    display: flex;
    gap: 20px;
    overflow: hidden;

    .video-panel {
      flex: 3; /* 左侧占据更多空间 */
      background-color: var(--el-bg-color-overlay);
      border-radius: 8px;
      display: flex;
      flex-direction: column;
      overflow: hidden;
      
      .panel-header {
        padding: 15px;
        display: flex;
        justify-content: space-between;
        align-items: center;
        border-bottom: 1px solid var(--el-border-color-light);
        
        h3 {
          margin: 0;
          font-size: 16px;
          color: var(--el-text-color-primary);
        }
        
        .panel-actions {
          display: flex;
          gap: 10px;
        }
      }
    }

    .right-panel {
      flex: 2; /* 右侧占据较少空间 */
      display: flex;
      flex-direction: column;
      gap: 20px;
      max-width: 500px;
      
      .detection-panel, .camera-list {
        background-color: var(--el-bg-color-overlay);
        border-radius: 8px;
        display: flex;
        flex-direction: column;
      }
      
      .detection-panel {
        flex: 0.4;
        min-height: 25%;
        max-height: 30%;
      }
      
      .camera-list {
        flex: 1;
        min-height: 70%;
      }
    }
  }
}

.video-grid {
  flex: 1;
  display: grid;
  gap: 20px;
  overflow: auto;
  padding: 15px;
  height: 100%; /* 确保网格占据全部高度 */

  &.grid-1 {
    grid-template-columns: 1fr;
  }

  &.grid-4 {
    grid-template-columns: repeat(2, 1fr);
    grid-template-rows: repeat(2, 1fr);
  }

  &.grid-9 {
    grid-template-columns: repeat(3, 1fr);
    grid-template-rows: repeat(3, 1fr);
  }

  .video-item {
    width: 100%;
    height: 100%;
    min-height: 300px;
    
    .video-wrapper {
      position: relative;
      width: 100%;
      height: 100%;
      
      .video-controls {
        position: absolute;
        top: 10px;
        right: 10px;
        z-index: 10;
        display: flex;
        gap: 5px;
        opacity: 0;
        transition: opacity 0.3s ease;
        
        .remove-camera-btn {
          background-color: rgba(0, 0, 0, 0.6);
          border: none;
          
          &:hover {
            background-color: var(--el-color-danger);
          }
        }
      }
      
      &:hover .video-controls {
        opacity: 1;
      }
    }
  }

  .empty-state {
    grid-column: 1 / -1;
    grid-row: 1 / -1;
    display: flex;
    align-items: center;
    justify-content: center;
    min-height: 400px;
    width: 100%;
    height: 100%; /* 确保占据整个格子高度 */

    :deep(.el-empty) {
      margin: 0;
    }
  }
}

.detection-panel {
  padding: 15px;
  display: flex;
  flex-direction: column;
  height: 100%; /* 确保占据父容器的全部高度 */

  .panel-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 15px;

    h3 {
      margin: 0;
      font-size: 16px;
      color: var(--el-text-color-primary);
    }
  }

  .detection-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    grid-template-rows: 1fr;
    gap: 10px;
    overflow: auto;
    height: 100%;
    min-height: 150px;
    flex: 1; /* 让网格占据剩余空间 */

    .detection-item {
      border-radius: 4px;
      overflow: hidden;
      box-shadow: 0 2px 12px 0 rgba(0, 0, 0, 0.1);
      height: 0;
      padding-bottom: 100%; /* 正方形 */
      position: relative;

      .detection-image {
        position: absolute;
        top: 0;
        left: 0;
        width: 100%;
        height: 100%;
        
        .el-image {
          width: 100%;
          height: 100%;
        }
        
        .detection-info {
          position: absolute;
          bottom: 0;
          left: 0;
          right: 0;
          padding: 5px 8px;
          background-color: rgba(0, 0, 0, 0.6);
          
          .detection-time {
            font-size: 12px;
            color: #fff;
          }
        }
      }
    }
    
    .empty-state {
      grid-column: 1 / -1;
      display: flex;
      align-items: center;
      justify-content: center;
      height: 150px; /* 固定高度确保垂直居中 */
    }
  }
}

.camera-list {
  background-color: var(--el-bg-color-overlay);
  border-radius: 12px;
  display: flex;
  flex-direction: column;
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.05);
  overflow: hidden;
  border: 1px solid rgba(var(--el-color-primary-rgb), 0.1);
  
  .panel-header {
    padding: 16px 20px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid rgba(var(--el-color-primary-rgb), 0.1);
    background: linear-gradient(to right, rgba(var(--el-color-primary-rgb), 0.05), rgba(var(--el-color-primary-rgb), 0.01));
    
    .header-title {
      display: flex;
      align-items: center;
      gap: 8px;
      
      .el-icon {
        color: var(--el-color-primary);
        font-size: 18px;
      }
      
      h3 {
        margin: 0;
        font-size: 16px;
        color: var(--el-text-color-primary);
        font-weight: 600;
      }
    }
  }
  
  .list-content {
    padding: 12px;
    overflow: auto;
    
    .camera-item {
      margin-bottom: 12px;
      background-color: var(--el-bg-color);
      border-radius: 8px;
      cursor: pointer;
      transition: all 0.3s;
      box-shadow: 0 2px 8px rgba(0, 0, 0, 0.05);
      overflow: hidden;
      border: 1px solid rgba(0, 0, 0, 0.04);
      
      &:last-child {
        margin-bottom: 0;
      }
      
      &:hover {
        transform: translateY(-2px);
        box-shadow: 0 6px 12px rgba(var(--el-color-primary-rgb), 0.1);
      }
      
      &.is-active {
        border-color: var(--el-color-primary);
        box-shadow: 0 0 0 1px var(--el-color-primary-light-5), 0 6px 16px rgba(var(--el-color-primary-rgb), 0.1);
        background-color: rgba(var(--el-color-primary-rgb), 0.02);
        
        .camera-icon {
          background-color: var(--el-color-primary-light-8);
          
          .el-icon {
            color: var(--el-color-primary);
          }
        }
      }
      
      &.is-online .name {
        color: var(--el-color-primary);
      }
      
      .camera-item-content {
        display: flex;
        align-items: center;
        padding: 14px;
        gap: 16px;
        
        .camera-icon {
          width: 50px;
          height: 50px;
          border-radius: 12px;
          background-color: var(--el-color-info-light-9);
          display: flex;
          align-items: center;
          justify-content: center;
          position: relative;
          flex-shrink: 0;
          
          .el-icon {
            color: var(--el-color-info-dark-2);
          }
          
          .status-indicator {
            position: absolute;
            bottom: -3px;
            right: -3px;
            width: 14px;
            height: 14px;
            border-radius: 50%;
            border: 2px solid var(--el-bg-color);
            
            &.online {
              background-color: var(--el-color-success);
              box-shadow: 0 0 0 1px var(--el-color-success-light-5);
            }
            
            &.offline {
              background-color: var(--el-color-info);
              box-shadow: 0 0 0 1px var(--el-color-info-light-5);
            }
            
            &.fault {
              background-color: var(--el-color-warning);
              box-shadow: 0 0 0 1px var(--el-color-warning-light-5);
            }
          }
        }
        
        .camera-details {
          flex: 1;
          display: flex;
          flex-direction: column;
          gap: 4px;
          overflow: hidden;
          
          .name {
            font-weight: 600;
            font-size: 15px;
            color: var(--el-text-color-primary);
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
          }
          
          .location {
            font-size: 12px;
            color: var(--el-text-color-secondary);
            display: flex;
            align-items: center;
            gap: 4px;
            
            .el-icon {
              font-size: 12px;
            }
          }
          
          .status {
            font-size: 12px;
            padding: 2px 8px;
            border-radius: 20px;
            display: inline-block;
            margin-top: 4px;
            width: fit-content;
            display: flex;
            align-items: center;
            gap: 4px;
            
            &.online {
              color: var(--el-color-success);
              background-color: var(--el-color-success-light-9);
            }
            
            &.offline {
              color: var(--el-color-info);
              background-color: var(--el-color-info-light-9);
            }
            
            &.fault {
              color: var(--el-color-warning);
              background-color: var(--el-color-warning-light-9);
            }
          }
        }
      }
    }
  }
}

.camera-info {
  margin-bottom: 30px;

  .info-item {
    margin-bottom: 15px;

    .label {
      color: var(--el-text-color-secondary);
      margin-right: 10px;
    }

    .value {
      color: var(--el-text-color-primary);
    }
  }
}

.detection-results {
  margin-top: 20px;
  
  h3 {
    font-size: 16px;
    margin-bottom: 15px;
    color: var(--el-text-color-primary);
  }
  
  h4 {
    font-size: 14px;
    margin: 15px 0 10px;
    color: var(--el-text-color-primary);
  }
  
  .detection-info {
    margin-bottom: 15px;
    
    .info-item {
      margin-bottom: 10px;
    }
  }
  
  .detected-objects, .tracked-objects {
    margin-bottom: 20px;
  }
  
  .no-detection {
    display: flex;
    justify-content: center;
    padding: 20px;
  }
}

.empty-state {
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px;
  width: 100%;
  
  :deep(.el-empty) {
    padding: 0; /* 移除额外的内边距 */
    margin: 0;
    
    .el-icon {
      color: var(--el-text-color-secondary);
    }
    
    .el-empty__description {
      margin-top: 10px;
      color: var(--el-text-color-secondary);
    }
  }
}

.sms-settings {
  margin-top: 20px;
  padding: 20px;
  background-color: var(--el-bg-color-overlay);
  border-radius: 8px;

  .form-help-text {
    font-size: 12px;
    color: var(--el-text-color-secondary);
  }

  .last-sent-info {
    padding: 10px 15px;
    margin-top: 5px;
    margin-bottom: 10px;
    border-left: 3px solid var(--el-color-primary-light-5);
    background-color: var(--el-color-primary-light-9);
    border-radius: 0 4px 4px 0;
    font-size: 13px;
    color: var(--el-text-color-regular);
    display: flex;
    align-items: center;
    gap: 8px;
    
    .el-icon {
      color: var(--el-color-primary);
      font-size: 16px;
    }
    
    &.empty-record {
      border-left-color: var(--el-color-info-light-5);
      background-color: var(--el-color-info-light-9);
      
      .el-icon {
        color: var(--el-color-info);
      }
    }
  }
}

.sms-history {
  margin-top: 15px;
  
  .divider-text {
    margin-left: 8px;
    font-size: 14px;
    font-weight: 500;
  }
  
  .scroll-container {
    position: relative;
    margin-top: 15px;
    border: 1px solid rgba(var(--el-color-primary-rgb), 0.1);
    border-radius: 12px;
    background-color: rgba(var(--el-color-primary-rgb), 0.01);
    max-height: 400px;
    overflow: hidden;
    box-shadow: 0 2px 10px rgba(0, 0, 0, 0.02);
    
    .scroll-indicator {
      position: absolute;
      left: 0;
      right: 0;
      height: 24px;
      display: flex;
      justify-content: center;
      align-items: center;
      color: var(--el-color-primary);
      font-size: 14px;
      z-index: 1;
      pointer-events: none;
      opacity: 0.7;
      transition: opacity 0.3s;
      
      &.top {
        top: 0;
        background: linear-gradient(to bottom, rgba(var(--el-color-primary-rgb), 0.05) 30%, transparent);
      }
      
      &.bottom {
        bottom: 0;
        background: linear-gradient(to top, rgba(var(--el-color-primary-rgb), 0.05) 30%, transparent);
      }
    }
    
    :deep(.el-scrollbar__wrap) {
      max-height: 400px;
    }
    
    :deep(.el-scrollbar__bar) {
      opacity: 0.2;
      
      &:hover {
        opacity: 0.8;
      }
    }
  }
  
  .history-list {
    display: flex;
    flex-direction: column;
    gap: 15px;
    padding: 15px;
  }
  
  .history-card {
    border-radius: 12px;
    border: none;
    overflow: hidden;
    background-color: #fff;
    box-shadow: 0 3px 10px rgba(var(--el-color-primary-rgb), 0.08);
    transition: all 0.3s;
    
    &:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 16px rgba(var(--el-color-primary-rgb), 0.12);
    }
    
    .history-card-header {
      padding: 12px 16px;
      background: linear-gradient(120deg, var(--el-color-primary-light-8), var(--el-color-primary-light-9));
      display: flex;
      justify-content: space-between;
      align-items: center;
      border-bottom: 1px solid rgba(var(--el-color-primary-rgb), 0.08);
      
      .history-card-title {
        display: flex;
        align-items: center;
        gap: 8px;
        
        .camera-icon {
          color: var(--el-color-primary);
          font-size: 18px;
        }
        
        .camera-name {
          font-weight: 600;
          color: var(--el-color-primary-dark-2);
        }
      }
      
      .history-time {
        display: flex;
        align-items: center;
        gap: 5px;
        font-size: 12px;
        color: var(--el-color-primary-dark-2);
        opacity: 0.8;
      }
    }
    
    .history-card-content {
      padding: 16px;
      background-color: #fff;
      
      .message-section, .recipients-section {
        margin-bottom: 16px;
        
        &:last-child {
          margin-bottom: 0;
        }
      }
      
      .section-title {
        display: flex;
        align-items: center;
        gap: 5px;
        margin-bottom: 8px;
        font-size: 13px;
        font-weight: 500;
        color: var(--el-color-primary-dark-2);
        
        .el-icon {
          color: var(--el-color-primary);
          font-size: 16px;
        }
        
        .recipient-count {
          margin-left: 5px;
          font-size: 12px;
          color: var(--el-color-info);
          background-color: var(--el-color-info-light-9);
          padding: 0 6px;
          border-radius: 10px;
        }
      }
      
      .message-content {
        padding: 12px;
        background-color: var(--el-color-primary-light-9);
        border-radius: 8px;
        font-size: 13px;
        line-height: 1.6;
        color: var(--el-color-primary-dark-2);
        box-shadow: inset 0 0 0 1px rgba(var(--el-color-primary-rgb), 0.08);
      }
      
      .recipients-list {
        display: flex;
        flex-wrap: wrap;
        gap: 8px;
        
        .recipient-tag {
          margin: 0;
          border-radius: 16px;
          background-color: var(--el-color-primary-light-9);
          border-color: transparent;
          color: var(--el-color-primary-dark-2);
          
          &:hover {
            background-color: var(--el-color-primary-light-8);
          }
        }
      }
    }
  }
}

// 阈值设置相关样式
.threshold-help {
  font-size: 12px;
  color: var(--el-color-info);
  margin-top: 4px;
  line-height: 1.4;

  &.warning {
    color: var(--el-color-warning);
  }
}

.detection-counts {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 16px;
  background: var(--el-fill-color-light);
  border-radius: 8px;
  margin-top: 8px;

  .count-item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 8px 12px;
    background: var(--el-bg-color);
    border-radius: 4px;

    .count-label {
      font-size: 14px;
      color: var(--el-text-color-regular);
    }

    .count-value {
      font-size: 18px;
      font-weight: bold;
      color: var(--el-color-primary);
      min-width: 40px;
      text-align: right;
    }
  }
}

// 工具栏样式优化
.toolbar {
  .left {
    display: flex;
    align-items: center;
    gap: 12px;

    .tracker-select {
      width: 140px;
    }
  }
}

.empty-history {
  padding: 30px 20px;
  margin-top: 15px;
  background: linear-gradient(120deg, var(--el-color-primary-light-9), var(--el-color-primary-light-8));
  border-radius: 12px;
  text-align: center;
  color: var(--el-color-primary-dark-2);
  display: flex;
  justify-content: center;
  align-items: center;
  box-shadow: 0 3px 10px rgba(var(--el-color-primary-rgb), 0.08);
  border: 1px solid rgba(var(--el-color-primary-rgb), 0.05);
  
  .empty-history-content {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 10px;
    
    .el-icon {
      color: var(--el-color-primary);
      margin-bottom: 5px;
    }
    
    .empty-text {
      font-size: 16px;
      font-weight: 500;
      color: var(--el-color-primary-dark-2);
    }
    
    .empty-subtext {
      font-size: 13px;
      color: var(--el-color-primary-dark-2);
      opacity: 0.7;
    }
  }
}
</style> 
