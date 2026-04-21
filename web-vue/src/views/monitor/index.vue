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
        <el-select
          v-model="selectedModelProfileId"
          class="model-select"
          size="small"
          placeholder="选择模型"
          :loading="loadingModelProfiles"
          :disabled="loadingModelProfiles || modelProfiles.length === 0"
          @change="handleModelProfileChange"
        >
          <el-option
            v-for="profile in modelProfiles"
            :key="profile.id"
            :label="buildModelOptionLabel(profile)"
            :value="profile.id"
            :disabled="!profile.ready"
          />
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
            <el-select
              v-model="selectedRecordingCameraId"
              class="recording-camera-select"
              size="small"
              placeholder="选择录像摄像头"
            >
              <el-option
                v-for="cameraOption in recordingCameraOptions"
                :key="cameraOption.id"
                :label="cameraOption.label"
                :value="cameraOption.id"
              />
            </el-select>
            <el-tag
              v-if="selectedRecordingStatus"
              :type="selectedRecordingStatus.recording ? 'danger' : 'info'"
              effect="dark"
            >
              {{ selectedRecordingCameraLabel }}{{ selectedRecordingStatus.recording ? '录像中' : '未录像' }}
            </el-tag>
            <el-tooltip
              v-if="selectedRecordingCameraId !== null"
              :content="selectedRecordingStatus?.recording ? `停止${selectedRecordingCameraLabel}录像` : `开始录制${selectedRecordingCameraLabel}`"
              placement="top"
              effect="dark"
            >
              <el-button
                :type="selectedRecordingStatus?.recording ? 'danger' : 'primary'"
                size="small"
                plain
                :loading="recordingActionLoading"
                @click="toggleCameraRecording"
              >
                <el-icon>
                  <VideoPause v-if="selectedRecordingStatus?.recording" />
                  <VideoPlay v-else />
                </el-icon>
                {{ selectedRecordingStatus?.recording ? '停止录像' : '开始录像' }}
              </el-button>
            </el-tooltip>
            <el-button size="small" plain @click="openRecordingFilesDialog">
              <el-icon><FolderOpened /></el-icon>
              录像文件
            </el-button>
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
            <span v-if="activeCamera">- {{ getCameraDisplayName(activeCamera) }}</span>
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
                      <span class="name">{{ getCameraDisplayName(camera) }}</span>
                      <span class="location">
                        <el-icon><LocationFilled /></el-icon>
                        <span class="location-label">{{ getCameraLocationLabel(camera) }}：</span>
                        <span class="location-text">{{ getCameraLocationDisplay(camera) }}</span>
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
            <span class="value">{{ getCameraDisplayName(activeCamera) }}</span>
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
          <div class="info-item">
            <span class="label">录像状态：</span>
            <el-tag :type="activeCameraSupportsRecording ? (activeRecordingStatus?.recording ? 'danger' : 'info') : 'warning'">
              {{ activeCameraSupportsRecording ? (activeRecordingStatus?.recording ? '录像中' : '未录像') : '不支持' }}
            </el-tag>
          </div>
          <div v-if="activeRecordingStatus?.file" class="info-item">
            <span class="label">录像文件：</span>
            <span class="value recording-file">{{ formatRecordingFileName(activeRecordingStatus.file) }}</span>
          </div>
          <div v-if="activeRecordingStatus?.startedAt" class="info-item">
            <span class="label">开始时间：</span>
            <span class="value">{{ activeRecordingStatus.startedAt }}</span>
          </div>
          <div v-if="activeRecordingStatus?.error" class="info-item recording-error">
            <span class="label">录像错误：</span>
            <span class="value">{{ activeRecordingStatus.error }}</span>
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

    <el-dialog
      v-model="recordingFilesDialogVisible"
      title="录像文件"
      width="820px"
      destroy-on-close
    >
      <div class="recording-files-toolbar">
        <el-select
          v-model="selectedRecordingCameraId"
          class="recording-camera-select"
          size="small"
          placeholder="筛选摄像头"
          @change="handleRecordingCameraChange"
        >
          <el-option
            v-for="cameraOption in recordingCameraOptions"
            :key="cameraOption.id"
            :label="cameraOption.label"
            :value="cameraOption.id"
          />
        </el-select>
        <el-button size="small" @click="() => loadRecordingFiles(false)" :loading="recordingFilesLoading">
          <el-icon><Refresh /></el-icon>
          刷新文件
        </el-button>
      </div>

      <el-table v-loading="recordingFilesLoading" :data="recordingFiles" stripe>
        <el-table-column prop="cameraLabel" label="摄像头" width="110" />
        <el-table-column prop="name" label="文件名" min-width="240" show-overflow-tooltip />
        <el-table-column prop="sizeText" label="大小" width="110" />
        <el-table-column prop="modifiedAtText" label="更新时间" width="180" />
        <el-table-column label="状态" width="110">
          <template #default="scope">
            <el-tag :type="scope.row.active ? 'danger' : 'info'" size="small">
              {{ scope.row.active ? '当前录像' : '已完成' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="120" fixed="right">
          <template #default="scope">
            <el-button type="primary" link @click="downloadRecordingFile(scope.row)">
              <el-icon><Download /></el-icon>
              下载
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div v-if="!recordingFilesLoading && recordingFiles.length === 0" class="recording-files-empty">
        当前摄像头还没有录像文件
      </div>
    </el-dialog>

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
import { listModelProfiles, selectModelProfile, type ModelProfile } from '@/api/model'
import type { UserInfo } from '@/types/user'
import { 
  Monitor, Grid, Refresh, VideoCameraFilled, Select,
  PictureFilled, Warning, Picture as PictureIcon, Close, Delete, Message, Plus,
  ChatDotRound, VideoCamera, Clock, User, ChatLineRound, InfoFilled, Timer,
  ArrowUp, ArrowDown, LocationFilled, Setting, VideoPlay, VideoPause, FolderOpened, Download
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
const recordingActionLoading = ref(false)
const recordingFilesDialogVisible = ref(false)
const recordingFilesLoading = ref(false)

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
const modelProfiles = ref<ModelProfile[]>([])
const loadingModelProfiles = ref(false)
const selectedModelProfileId = ref<number | null>(null)

const readyModelProfiles = computed(() => {
  return modelProfiles.value.filter(profile => profile.ready)
})

const selectedModelProfile = computed(() => {
  if (selectedModelProfileId.value === null) return null
  return modelProfiles.value.find(profile => profile.id === selectedModelProfileId.value) ?? null
})

type RecordingStatusItem = {
  recording: boolean
  file: string
  log: string
  startedAt: string
  error: string
}

type RecordingFileItem = {
  name: string
  cameraId: number
  cameraKey: string
  cameraLabel: string
  size: number
  sizeText: string
  modifiedAt: string
  modifiedAtText: string
  downloadUrl: string
  active: boolean
}

const createEmptyRecordingStatus = (): RecordingStatusItem => ({
  recording: false,
  file: '',
  log: '',
  startedAt: '',
  error: ''
})

const recordingStatus = ref<{
  outputDir: string
  ffmpegBin: string
  cam0: RecordingStatusItem
  cam1: RecordingStatusItem
}>({
  outputDir: '',
  ffmpegBin: '',
  cam0: createEmptyRecordingStatus(),
  cam1: createEmptyRecordingStatus()
})

const selectedRecordingCameraId = ref<number | null>(1)
const recordingFiles = ref<RecordingFileItem[]>([])

const resolveRecordingCameraKey = (camera?: Camera | null): 'cam0' | 'cam1' | null => {
  if (!camera) return null
  if (camera.id === 1) return 'cam0'
  if (camera.id === 2) return 'cam1'
  if (camera.rtspUrl.includes('/cam0')) return 'cam0'
  if (camera.rtspUrl.includes('/cam1')) return 'cam1'
  return null
}

const resolveRecordingCameraId = (camera?: Camera | null): number | null => {
  const key = resolveRecordingCameraKey(camera)
  if (key === 'cam0') return 1
  if (key === 'cam1') return 2
  return null
}

const isMosaicCamera = (camera?: Camera | null): boolean => {
  if (!camera) return false
  return camera.rtspUrl.includes('/cam2')
}

const isRtspLikeText = (value: string): boolean => {
  const normalized = value.trim().toLowerCase()
  return normalized.startsWith('rtsp://')
    || normalized.startsWith('rtmp://')
    || normalized.startsWith('http://')
    || normalized.startsWith('https://')
}

const getDefaultCameraDisplayName = (camera: Camera): string => {
  const mappedId = resolveRecordingCameraId(camera)
  if (mappedId === 1) return '摄像头1 (cam0)'
  if (mappedId === 2) return '摄像头2 (cam1)'
  if (isMosaicCamera(camera)) return '融合画面 (cam2)'
  return `摄像头${camera.id}`
}

const getCameraDisplayName = (camera?: Camera | null): string => {
  if (!camera) return '未命名摄像头'
  const name = (camera.name || '').trim()
  if (name && !isRtspLikeText(name) && name !== camera.rtspUrl) {
    return name
  }
  return getDefaultCameraDisplayName(camera)
}

const recordingCameraOptions = computed(() => {
  const defaultOptions = [
    { id: 1, label: '摄像头1 (cam0)' },
    { id: 2, label: '摄像头2 (cam1)' }
  ]
  const labels = new Map<number, string>(defaultOptions.map(item => [item.id, item.label]))

  cameras.value.forEach(camera => {
    if (camera.id === 1 || camera.id === 2) {
      const suffix = camera.id === 1 ? 'cam0' : 'cam1'
      labels.set(camera.id, `${getCameraDisplayName(camera)} (${suffix})`)
    }
  })

  return defaultOptions.map(item => ({
    id: item.id,
    label: labels.get(item.id) ?? item.label
  }))
})

const activeRecordingStatus = computed<RecordingStatusItem | null>(() => {
  const key = resolveRecordingCameraKey(activeCamera.value)
  if (!key) return null
  return recordingStatus.value[key]
})

const usesPlaceholderLocation = (camera: Camera): boolean => {
  const location = (camera.location || '').trim()
  const name = (camera.name || '').trim()
  const isDefaultName = name === '摄像头1' || name === '摄像头2' || name === '融合画面'
  const isPlaceholder = location === '' || location === '校园门口' || location === '教学楼' || location === '双路拼接流'
  return isDefaultName && isPlaceholder
}

const getCameraLocationLabel = (camera: Camera): string => {
  if (usesPlaceholderLocation(camera) && camera.rtspUrl) {
    return 'RTSP'
  }
  return '位置'
}

const getCameraLocationDisplay = (camera: Camera): string => {
  if (usesPlaceholderLocation(camera) && camera.rtspUrl) {
    return camera.rtspUrl
  }
  if (camera.location && camera.location.trim()) {
    return camera.location
  }
  return camera.rtspUrl || '未配置'
}

const selectedRecordingCameraLabel = computed(() => {
  return recordingCameraOptions.value.find(item => item.id === selectedRecordingCameraId.value)?.label ?? '录像摄像头'
})

const selectedRecordingStatus = computed<RecordingStatusItem | null>(() => {
  if (selectedRecordingCameraId.value === 1) return recordingStatus.value.cam0
  if (selectedRecordingCameraId.value === 2) return recordingStatus.value.cam1
  return null
})

const activeCameraSupportsRecording = computed(() => {
  return resolveRecordingCameraId(activeCamera.value) !== null
})

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

const getErrorMessage = (error: unknown, fallback: string): string => {
  if (
    error &&
    typeof error === 'object' &&
    'message' in error &&
    typeof (error as { message?: unknown }).message === 'string'
  ) {
    const message = (error as { message: string }).message.trim()
    if (message) return message
  }
  return fallback
}

const getCurrentStreamHost = (): string => {
  if (typeof window === 'undefined') return '127.0.0.1'
  return window.location.hostname.trim() || '127.0.0.1'
}

const buildDefaultRtspUrl = (path: string): string => {
  return `rtsp://${getCurrentStreamHost()}:8554/${path}`
}

const buildModelOptionLabel = (profile: ModelProfile): string => {
  if (profile.ready) {
    return profile.builtin ? `${profile.baseName}（内置）` : profile.baseName
  }
  if (profile.builtin) return `${profile.baseName}（内置文件缺失）`
  return `${profile.baseName}（缺少.rknn或.txt）`
}

const cameraMatchesStreamPath = (camera: Camera, path: string): boolean => {
  return camera.id === 1 && path === 'cam0'
    || camera.id === 2 && path === 'cam1'
    || isMosaicCamera(camera) && path === 'cam2'
    || camera.rtspUrl.includes(`/${path}`)
}

const inferCameraOnlineStatusFromRknn = (camera: Camera, status: Record<string, any>): number | null => {
  const serviceRunning = parseBooleanLike(status.running) ?? false
  if (!serviceRunning) return 0

  const cam0Ready = !!status.input_source_cam0
  const cam1Ready = !!status.input_source_cam1
  const mosaicReady = cam0Ready && cam1Ready && typeof status.rtsp_url_mosaic === 'string' && !!status.rtsp_url_mosaic

  if (cameraMatchesStreamPath(camera, 'cam0')) return cam0Ready ? 1 : 0
  if (cameraMatchesStreamPath(camera, 'cam1')) return cam1Ready ? 1 : 0
  if (cameraMatchesStreamPath(camera, 'cam2')) return mosaicReady ? 1 : 0

  return null
}

const syncSelectedModelProfile = () => {
  const selectedByServer = modelProfiles.value.find(profile => profile.selected && profile.ready)
  if (selectedByServer) {
    selectedModelProfileId.value = selectedByServer.id
    return
  }

  if (
    selectedModelProfileId.value !== null &&
    modelProfiles.value.some(profile => profile.id === selectedModelProfileId.value && profile.ready)
  ) {
    return
  }

  selectedModelProfileId.value = readyModelProfiles.value[0]?.id ?? null
}

const fetchModelProfiles = async (silent = true) => {
  loadingModelProfiles.value = true
  try {
    const profiles = await listModelProfiles()
    modelProfiles.value = Array.isArray(profiles) ? profiles : []
    syncSelectedModelProfile()
    if (!silent && readyModelProfiles.value.length === 0) {
      ElMessage.warning('暂无可用模型，请先上传 .rknn 和同名 .txt，或检查内置模型文件')
    }
  } catch (error) {
    console.error('获取模型列表失败:', error)
    if (!silent) {
      ElMessage.error(getErrorMessage(error, '获取模型列表失败'))
    }
  } finally {
    loadingModelProfiles.value = false
  }
}

const applySelectedModelProfile = async (silent = false): Promise<boolean> => {
  if (modelProfiles.value.length === 0) {
    await fetchModelProfiles(true)
  }

  if (selectedModelProfileId.value === null) {
    const fallbackProfile = readyModelProfiles.value[0]
    if (fallbackProfile) {
      selectedModelProfileId.value = fallbackProfile.id
    }
  }

  const profile = selectedModelProfile.value
  if (!profile) {
    if (!silent) {
      ElMessage.warning('暂无可用模型，请先上传 .rknn 和同名 .txt，或检查内置模型文件')
    }
    return false
  }

  if (!profile.ready) {
    if (!silent) {
      ElMessage.warning(
        profile.builtin
          ? `内置模型 ${profile.baseName} 不可用，请检查内置模型文件`
          : `模型 ${profile.baseName} 未就绪，请确保已上传 .rknn 和 .txt`
      )
    }
    return false
  }

  try {
    await selectModelProfile(profile.id)
    await fetchModelProfiles(true)
    return true
  } catch (error) {
    console.error('加载模型失败:', error)
    await fetchModelProfiles(true)
    if (!silent) {
      ElMessage.error(getErrorMessage(error, `加载模型 ${profile.baseName} 失败`))
    }
    return false
  }
}

const handleModelProfileChange = async (value: number | null) => {
  if (value === null) return

  const profile = modelProfiles.value.find(item => item.id === value)
  if (!profile) return

  if (!objectDetectionEnabled.value) {
    ElMessage.success(
      profile.builtin
        ? `已选择内置模型 ${profile.baseName}，开启目标检测时自动加载`
        : `已选择模型 ${profile.baseName}，开启目标检测时自动加载`
    )
    return
  }

  const applied = await applySelectedModelProfile(false)
  if (applied) {
    ElMessage.success(profile.builtin ? `模型已切换为内置 ${profile.baseName}` : `模型已切换为 ${profile.baseName}`)
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

const parseRecordingStatusItem = (raw: any): RecordingStatusItem => {
  return {
    recording: parseBooleanLike(raw?.recording) ?? false,
    file: typeof raw?.file === 'string' ? raw.file : '',
    log: typeof raw?.log === 'string' ? raw.log : '',
    startedAt: typeof raw?.started_at === 'string'
      ? raw.started_at
      : (typeof raw?.startedAt === 'string' ? raw.startedAt : ''),
    error: typeof raw?.error === 'string' ? raw.error : ''
  }
}

const syncRecordingStatusFromServer = async (silent = true): Promise<boolean> => {
  try {
    const res = await fetch('/api/rknn/record/status')
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`)
    }

    const payload = await res.json()
    if (payload?.code !== 200 || !payload?.data) {
      throw new Error(getRknnOperationErrorMessage(payload, '未获取到录像状态'))
    }

    const data = payload.data
    recordingStatus.value = {
      outputDir: typeof data.record_output_dir === 'string' ? data.record_output_dir : '',
      ffmpegBin: typeof data.record_ffmpeg_bin === 'string' ? data.record_ffmpeg_bin : '',
      cam0: parseRecordingStatusItem(data.cam0),
      cam1: parseRecordingStatusItem(data.cam1)
    }
    return true
  } catch (err) {
    console.error('同步录像状态失败:', err)
    if (!silent) {
      ElMessage.warning('未获取到录像状态，页面已保留当前显示')
    }
    return false
  }
}

const formatRecordingFileName = (path: string): string => {
  if (!path) return ''
  const normalized = path.replace(/\\/g, '/')
  const parts = normalized.split('/')
  return parts[parts.length - 1] || path
}

const formatRecordingFileSize = (size: number): string => {
  if (!Number.isFinite(size) || size <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB']
  let value = size
  let unitIndex = 0
  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024
    unitIndex += 1
  }
  return `${value.toFixed(value >= 10 || unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`
}

const formatRecordingDateTime = (value: string): string => {
  if (!value) return '--'
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return value
  return date.toLocaleString('zh-CN', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false
  })
}

const getRecordingStateByCameraId = (cameraId: number): RecordingStatusItem => {
  return cameraId === 2 ? recordingStatus.value.cam1 : recordingStatus.value.cam0
}

const parseRecordingFile = (raw: any): RecordingFileItem => {
  const cameraId = raw?.cameraId === 2 ? 2 : 1
  const name = typeof raw?.name === 'string' ? raw.name : ''
  const size = typeof raw?.size === 'number' ? raw.size : Number(raw?.size ?? 0)
  const modifiedAt = typeof raw?.modifiedAt === 'string' ? raw.modifiedAt : ''
  const cameraLabel = recordingCameraOptions.value.find(item => item.id === cameraId)?.label
    ?? (cameraId === 2 ? '摄像头2 (cam1)' : '摄像头1 (cam0)')
  const activeFile = formatRecordingFileName(getRecordingStateByCameraId(cameraId).file)

  return {
    name,
    cameraId,
    cameraKey: typeof raw?.cameraKey === 'string' ? raw.cameraKey : (cameraId === 2 ? 'cam1' : 'cam0'),
    cameraLabel,
    size,
    sizeText: formatRecordingFileSize(size),
    modifiedAt,
    modifiedAtText: formatRecordingDateTime(modifiedAt),
    downloadUrl: typeof raw?.downloadUrl === 'string' ? raw.downloadUrl : `/api/rknn/record/file?name=${encodeURIComponent(name)}`,
    active: !!name && activeFile === name
  }
}

const loadRecordingFiles = async (silent = false): Promise<boolean> => {
  recordingFilesLoading.value = true
  try {
    const query = selectedRecordingCameraId.value !== null ? `?cameraId=${selectedRecordingCameraId.value}` : ''
    const res = await fetch(`/api/rknn/record/files${query}`)
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`)
    }

    const payload = await res.json()
    if (payload?.code !== 200 || !payload?.data) {
      throw new Error(getRknnOperationErrorMessage(payload, '未获取到录像文件列表'))
    }

    const files = Array.isArray(payload.data.files) ? payload.data.files : []
    recordingFiles.value = files.map((item: any) => parseRecordingFile(item))
    return true
  } catch (err) {
    console.error('获取录像文件列表失败:', err)
    if (!silent) {
      ElMessage.error('获取录像文件列表失败')
    }
    return false
  } finally {
    recordingFilesLoading.value = false
  }
}

const openRecordingFilesDialog = async () => {
  recordingFilesDialogVisible.value = true
  await loadRecordingFiles(false)
}

const handleRecordingCameraChange = async () => {
  if (recordingFilesDialogVisible.value) {
    await loadRecordingFiles(true)
  }
}

const downloadRecordingFile = (file: RecordingFileItem) => {
  if (!file.downloadUrl) {
    ElMessage.warning('该录像文件暂时不可下载')
    return
  }
  window.open(file.downloadUrl, '_blank')
}

const toggleCameraRecording = async () => {
  if (recordingActionLoading.value) return
  if (selectedRecordingCameraId.value === null) {
    ElMessage.warning('请先选择要录像的摄像头')
    return
  }

  recordingActionLoading.value = true
  const cameraId = selectedRecordingCameraId.value
  const currentlyRecording = selectedRecordingStatus.value?.recording ?? false

  try {
    const endpoint = currentlyRecording
      ? `/api/rknn/record/stop?cameraId=${cameraId}`
      : `/api/rknn/record/start?cameraId=${cameraId}`
    const res = await fetch(endpoint, { method: 'POST' })
    const data = await res.json()

    if (isRknnOperationSuccess(data)) {
      await syncRecordingStatusFromServer(true)
      if (recordingFilesDialogVisible.value) {
        await loadRecordingFiles(true)
      }
      ElMessage.success(
        currentlyRecording
          ? `已停止${selectedRecordingCameraLabel.value}录像`
          : `已开始录制${selectedRecordingCameraLabel.value}`
      )
    } else {
      ElMessage.error(
        getRknnOperationErrorMessage(data, currentlyRecording ? '停止录像失败' : '开始录像失败')
      )
    }
  } catch (err) {
    console.error('切换录像状态失败:', err)
    ElMessage.error(currentlyRecording ? '停止录像失败' : '开始录像失败')
  } finally {
    recordingActionLoading.value = false
  }
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
    const endpoint = `/api/rknn/tracker/set?enabled=${targetEnabled ? 'true' : 'false'}`
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
    if (targetEnabled) {
      const modelApplied = await applySelectedModelProfile(false)
      if (!modelApplied) {
        setDetectionToggleState(previousEnabled)
        return
      }
    }

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
          ? `目标检测已开启（模型${selectedModelProfile.value?.baseName ?? '未命名'}，跟踪${trackingEnabled.value ? '开启' : '关闭'}，${trackerBackendLabel(trackerBackend.value)}）`
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
    const streamStarted = await startRtspStream()
    if (!streamStarted) {
      ElMessage.warning('推流未成功启动，已保留当前设备状态')
      return
    }

    const res = await fetch('/api/rknn/status')
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`)
    }

    const payload = await res.json()
    const statusData = payload?.data && typeof payload.data === 'object' ? payload.data : payload

    let updatedCount = 0
    cameras.value.forEach(camera => {
      if (camera.status === 2) return
      const inferredStatus = inferCameraOnlineStatusFromRknn(camera, statusData)
      if (inferredStatus === null) return
      camera.status = inferredStatus
      updatedCount += 1
    })

    if (updatedCount > 0) {
      updateDisplayCameras()
      ElMessage.success('已按视觉服务运行状态刷新设备状态')
    } else {
      ElMessage.info('当前没有可按视觉服务状态校验的设备')
    }
  } catch (error) {
    console.error('检查摄像头状态失败:', error)
    ElMessage.error(getErrorMessage(error, '检查摄像头状态失败'))
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
  const recordingCameraId = resolveRecordingCameraId(camera)
  if (recordingCameraId !== null) {
    selectedRecordingCameraId.value = recordingCameraId
  }
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
  const recordingCameraId = resolveRecordingCameraId(camera)
  if (recordingCameraId !== null) {
    selectedRecordingCameraId.value = recordingCameraId
  }
  
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

const streamProtocol = (((import.meta.env.VITE_STREAM_PROTOCOL as string | undefined) || 'webrtc')).toLowerCase()
const isWebRtcStream = streamProtocol === 'webrtc'

const sleep = (ms: number) => new Promise<void>((resolve) => {
  window.setTimeout(resolve, ms)
})

const buildDefaultCameras = (): Camera[] => [
  {
    id: 1,
    name: '摄像头1',
    location: '校园门口',
    rtspUrl: buildDefaultRtspUrl('cam0'),
    status: 1,
    isEnabled: true,
    detectionEnabled: true
  },
  {
    id: 2,
    name: '摄像头2',
    location: '教学楼',
    rtspUrl: buildDefaultRtspUrl('cam1'),
    status: 1,
    isEnabled: true,
    detectionEnabled: true
  },
  {
    id: 1003,
    name: '融合画面',
    location: '双路拼接流',
    rtspUrl: buildDefaultRtspUrl('cam2'),
    status: 1,
    isEnabled: true,
    detectionEnabled: false
  }
]

const ensureDefaultCamerasInList = () => {
  const defaults = buildDefaultCameras()
  defaults.forEach(defaultCamera => {
    if (!cameras.value.find(camera => camera.id === defaultCamera.id)) {
      cameras.value.push(defaultCamera)
    }
  })
}

const resolveCameraAfterRefresh = (cameraId?: number): Camera | undefined => {
  if (cameraId == null) return undefined
  return cameras.value.find(camera => camera.id === cameraId)
}

const restoreDisplayCamerasAfterRefresh = (previousDisplayIds: number[], previousActiveId?: number) => {
  const restored = previousDisplayIds
    .map(cameraId => resolveCameraAfterRefresh(cameraId))
    .filter((camera): camera is Camera => !!camera)
    .slice(0, layout.value)

  if (restored.length > 0) {
    displayCameras.value = restored
    activeCamera.value = restored.find(camera => camera.id === previousActiveId) ?? restored[0]
    const recordingCameraId = resolveRecordingCameraId(activeCamera.value)
    if (recordingCameraId !== null) {
      selectedRecordingCameraId.value = recordingCameraId
    }
    return
  }

  autoSelectDefaultCameras()
}

// 初始化
onMounted(async () => {
  // 初始化默认接收人
  initDefaultRecipients()

  // 加载跟踪算法选择
  loadTrackerBackend()
  loadTrackingEnabled()

  // 加载目标检测阈值设置
  loadThresholdSettings()

  // 获取模型列表（用于开启推理前的模型与标签自动下发）
  await fetchModelProfiles(true)

  // 检查并建立WebSocket连接
  await checkWebSocketConnection()

  // 从视觉模块同步目标检测实时状态，避免仅依赖本地缓存导致开关显示不准
  await syncDetectionToggleFromServer(true)
  await syncRecordingStatusFromServer(true)

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
  detectionCountInterval.value = window.setInterval(fetchDetectionCounts, 2000)
  recordingStatusInterval.value = window.setInterval(() => {
    void syncRecordingStatusFromServer(true)
  }, 5000)
})

let detectionCountInterval = ref<number | null>(null)
let recordingStatusInterval = ref<number | null>(null)

// 自动开启推流
const startRtspStream = async (): Promise<boolean> => {
  const maxAttempts = 3

  for (let attempt = 1; attempt <= maxAttempts; attempt++) {
    try {
      const res = await fetch('/api/rknn/rtsp/camera/start', { method: 'POST' })
      const data = await res.json()
      if (isRknnOperationSuccess(data)) {
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
  const defaultCameras = buildDefaultCameras()

  displayCameras.value = defaultCameras
  activeCamera.value = defaultCameras[0]
  selectedRecordingCameraId.value = 1

  ensureDefaultCamerasInList()

  // 查询第一个摄像头的检测记录
  fetchRecentDetections(defaultCameras[0].id)
}

// 页面卸载前的清理
onBeforeUnmount(() => {
  // 停止定时获取检测数量
  if (detectionCountInterval.value) {
    clearInterval(detectionCountInterval.value)
  }
  if (recordingStatusInterval.value) {
    clearInterval(recordingStatusInterval.value)
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
  const previousDisplayIds = displayCameras.value.map(camera => camera.id)
  const previousActiveId = activeCamera.value?.id

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
  
  // 重新获取摄像头列表与模型列表
  await Promise.all([
    fetchCameras(),
    fetchModelProfiles(true),
    syncRecordingStatusFromServer(true)
  ])

  ensureDefaultCamerasInList()
  restoreDisplayCamerasAfterRefresh(previousDisplayIds, previousActiveId)
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
          align-items: center;
          flex-wrap: wrap;

          .recording-camera-select {
            width: 180px;
          }
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
            min-width: 0;
            
            .el-icon {
              font-size: 12px;
              flex-shrink: 0;
            }

            .location-label {
              flex-shrink: 0;
              color: var(--el-text-color-secondary);
            }

            .location-text {
              min-width: 0;
              overflow: hidden;
              text-overflow: ellipsis;
              white-space: nowrap;
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

  .recording-file {
    display: inline-block;
    max-width: 220px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    vertical-align: bottom;
  }

  .recording-error {
    .value {
      color: var(--el-color-danger);
      line-height: 1.5;
    }
  }
}

.recording-files-toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  margin-bottom: 16px;

  .recording-camera-select {
    width: 200px;
  }
}

.recording-files-empty {
  padding: 18px 0 4px;
  text-align: center;
  color: var(--el-text-color-secondary);
  font-size: 13px;
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

    .model-select {
      width: 220px;
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
