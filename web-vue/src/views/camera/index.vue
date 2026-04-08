<template>
  <div class="camera">
    <!-- 工具栏 -->
    <div class="toolbar">
      <div class="left">
        <el-button type="primary" @click="handleCreate">
          <el-icon><Plus /></el-icon>
          新增监控设备
        </el-button>
        <el-button type="primary" @click="checkAllCamerasStatus" :loading="loading">
          <el-icon><Refresh /></el-icon>
          检测状态
        </el-button>
      </div>
    </div>

    <!-- 搜索表单 -->
    <el-form :model="queryParams" inline class="search-form">
      <el-form-item label="设备名称">
        <el-input v-model="queryParams.name" placeholder="请输入设备名称" clearable />
      </el-form-item>
      <el-form-item label="安装位置">
        <el-input v-model="queryParams.location" placeholder="请输入安装位置" clearable />
      </el-form-item>
      <el-form-item label="设备状态">
        <el-select v-model="queryParams.status" placeholder="请选择设备状态" clearable class="status-select">
          <el-option
            v-for="status in [0, 1]"
            :key="status"
            :label="formatDeviceStatus(status)"
            :value="status"
          />
        </el-select>
      </el-form-item>
      <el-form-item>
        <el-button type="primary" :loading="loading" @click="handleQuery">
          <el-icon><Search /></el-icon>
          查询
        </el-button>
        <el-button @click="handleReset">
          <el-icon><Refresh /></el-icon>
          重置
        </el-button>
      </el-form-item>
    </el-form>

    <!-- 数据表格 -->
    <el-table v-loading="loading" :data="list" border stripe>
      <el-table-column type="index" label="序号" width="60" align="center" />
      <el-table-column prop="name" label="设备名称" min-width="120" show-overflow-tooltip />
      <el-table-column prop="location" label="安装位置" min-width="120" show-overflow-tooltip />
      <el-table-column prop="rtspUrl" label="RTSP地址" min-width="200" show-overflow-tooltip />
      <!-- 管理员才显示创建者信息 -->
      <el-table-column v-if="userStore.isAdmin()" prop="userRealName" label="创建者" width="120" align="center">
        <template #default="{ row }">
          <span>{{ row.userRealName || row.userName || '未知' }}</span>
        </template>
      </el-table-column>
      <el-table-column prop="status" label="设备状态" width="100" align="center">
        <template #default="{ row }">
          <div v-if="checkingCameraIds.has(row.id)" class="checking-status">
            <el-icon class="is-loading"><Loading /></el-icon>
            <span>检测中...</span>
          </div>
          <el-tag v-else :type="getStatusType(row.status)">
            {{ formatDeviceStatus(row.status) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="createTime" label="创建时间" width="180" align="center">
        <template #default="{ row }">
          {{ formatDateTime(row.createTime) }}
        </template>
      </el-table-column>
      <el-table-column label="操作" width="200" align="center" fixed="right">
        <template #default="{ row }">
          <el-button link type="primary" @click="handleUpdate(row)">
            <el-icon><Edit /></el-icon>
            编辑
          </el-button>
          <el-button link type="danger" @click="handleDelete(row)">
            <el-icon><Delete /></el-icon>
            删除
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <!-- 分页 -->
    <pagination
      v-model:current="queryParams.current"
      v-model:size="queryParams.size"
      :total="total"
      @change="getList"
    />

    <!-- 新增/编辑对话框 -->
    <el-dialog
      v-model="dialogVisible"
      :title="dialogType === 'create' ? '新增监控设备' : '编辑监控设备'"
      width="600px"
      @close="resetForm"
    >
      <el-form ref="formRef" :model="form" :rules="rules" label-width="100px">
        <el-form-item label="设备名称" prop="name">
          <el-input v-model="form.name" placeholder="请输入设备名称" />
        </el-form-item>
        <el-form-item label="安装位置" prop="location">
          <el-input v-model="form.location" placeholder="请输入安装位置" />
        </el-form-item>
        <el-form-item label="RTSP地址" prop="rtspUrl">
          <el-input v-model="form.rtspUrl" placeholder="请输入RTSP地址" />
        </el-form-item>
        <el-form-item v-if="dialogType === 'update'" label="设备状态" prop="status">
          <el-select v-model="form.status" placeholder="请选择设备状态">
            <el-option
              v-for="status in [0, 1]"
              :key="status"
              :label="formatDeviceStatus(status)"
              :value="status"
            />
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="submitLoading" @click="submitForm">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, onBeforeUnmount, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import type { Camera } from '@/types/camera'
import { CameraStatus } from '@/types/camera'
import { getCameraList, createCamera, updateCamera, deleteCamera, updateCameraStatus } from '@/api/camera'
import { formatDateTime } from '@/utils/format'
import { validateRTSP, validateIP } from '@/utils/validate'
import Pagination from '@/components/Pagination/index.vue'
import wsClient from '@/utils/websocket'
import type { WebSocketMessage } from '@/utils/websocket'
import { useUserStore } from '@/stores/user'

// 用户store
const userStore = useUserStore()

// 设备状态格式化
const formatDeviceStatus = (status: number): string => {
  const statusMap: Record<number, string> = {
    [CameraStatus.OFFLINE]: '离线',
    [CameraStatus.ONLINE]: '在线',
  }
  return statusMap[status] || '未知'
}

// 查询参数
const queryParams = reactive({
  current: 1,
  size: 10,
  name: undefined,
  location: undefined,
  ipAddress: undefined,
  status: undefined,
})

// 数据列表
const list = ref<Camera[]>([])
const total = ref(0)
const loading = ref(false)
const checkingStatus = ref(false)
const checkingCameraIds = ref<Set<number>>(new Set())

// 自动更新状态的定时器
let statusUpdateTimer: number | null = null

// 获取数据列表
const getList = async () => {
  loading.value = true
  try {
    const res = await getCameraList(queryParams)
    list.value = res.records
    total.value = res.total
    // 获取列表成功后立即检查状态
    if (list.value.length > 0) {
      checkAllCamerasStatus()
    }
  } catch (error) {
    console.error(error)
  } finally {
    loading.value = false
  }
}

// 查询
const handleQuery = () => {
  queryParams.current = 1
  getList()
}

// 重置
const handleReset = () => {
  Object.assign(queryParams, {
    current: 1,
    size: 10,
    name: undefined,
    location: undefined,
    ipAddress: undefined,
    status: undefined,
  })
  getList()
}

// 获取状态类型
const getStatusType = (status: number): 'success' | 'warning' | 'info' | 'danger' | 'primary' => {
  const types = ['danger', 'success', 'warning'] as const
  return types[status] as 'success' | 'warning' | 'info' | 'danger' | 'primary'
}

// 表单
const formRef = ref<FormInstance>()
const dialogVisible = ref(false)
const dialogType = ref<'create' | 'update'>('create')
const submitLoading = ref(false)

const form = reactive({
  id: 0,
  name: '',
  location: '',
  ipAddress: '',
  rtspUrl: '',
  status: 1,
  faultType: '',
})

const rules: FormRules = {
  name: [{ required: true, message: '请输入设备名称', trigger: 'blur' }],
  location: [{ required: true, message: '请输入安装位置', trigger: 'blur' }],
  ipAddress: [
    { required: true, message: '请输入IP地址', trigger: 'blur' },
    { 
      validator: (rule: any, value: string, callback: any) => {
        if (!validateIP(value)) {
          callback(new Error('请输入正确的IP地址'))
        } else {
          callback()
        }
      }, 
      trigger: 'blur' 
    },
  ],
  rtspUrl: [
    { required: true, message: '请输入RTSP地址', trigger: 'blur' },
    { 
      validator: (rule: any, value: string, callback: any) => {
        if (!validateRTSP(value)) {
          callback(new Error('请输入正确的RTSP地址'))
        } else {
          callback()
        }
      }, 
      trigger: 'blur' 
    },
  ],
  faultType: [
    { 
      required: true, 
      message: '请选择故障类型', 
      trigger: 'change',
      validator: (rule: any, value: string, callback: any) => {
        if (form.status === 2 && !value) {
          callback(new Error('请选择故障类型'))
        } else {
          callback()
        }
      }
    }
  ],
}

// 监听状态变化，当状态不为故障时，清空故障类型
watch(() => form.status, (newStatus: number) => {
  if (newStatus !== 2) {
    form.faultType = ''
  } else if (!form.faultType) {
    // 当状态变为故障，但没有设置故障类型时，默认设置为其他故障
    form.faultType = 'other'
  }
}, { immediate: true })

// 新增
const handleCreate = () => {
  dialogType.value = 'create'
  dialogVisible.value = true
}

// 编辑
const handleUpdate = (row: Camera) => {
  dialogType.value = 'update'
  Object.assign(form, row)
  dialogVisible.value = true
}

// 删除
const handleDelete = (row: Camera) => {
  ElMessageBox.confirm('确认删除该摄像头？删除后无法恢复！', '警告', {
    type: 'warning',
  })
    .then(async () => {
      await deleteCamera(row.id)
      ElMessage.success('删除成功')
      getList()
    })
    .catch(() => {})
}

// 重置表单
const resetForm = () => {
  if (formRef.value) {
    formRef.value.resetFields()
  }
  Object.assign(form, {
    id: 0,
    name: '',
    location: '',
    ipAddress: '',
    rtspUrl: '',
    status: 1,
    faultType: '',
  })
}

// 提交表单
const submitForm = async () => {
  if (!formRef.value) return
  await formRef.value.validate()
  submitLoading.value = true
  try {
    // 确保在非故障状态下清空故障类型
    if (form.status !== 2) {
      form.faultType = ''
    }
    
    if (dialogType.value === 'create') {
      await createCamera(form)
      ElMessage.success('新增成功')
    } else {
      await updateCamera(form)
      ElMessage.success('更新成功')
    }
    dialogVisible.value = false
    getList()
  } catch (error) {
    console.error(error)
  } finally {
    submitLoading.value = false
  }
}

// 检查所有摄像头状态
const checkAllCamerasStatus = async () => {
  if (loading.value) return // 如果正在加载中，直接返回
  
  // 设置检测中状态
  checkingStatus.value = true
  
  try {
    // 记录正在检测的摄像头ID
    checkingCameraIds.value.clear()
    list.value.forEach((camera: Camera) => {
      checkingCameraIds.value.add(camera.id)
    })
    
    // 发送检测请求，检测所有设备
    list.value.forEach((camera: Camera) => {
      wsClient.send({
          type: 'check_camera',
          data: {
            cameraId: camera.id,
            rtspUrl: camera.rtspUrl
          }
        })
    })
    
    // 设置超时处理，如果30秒内没有收到所有响应，也结束检测中状态
    setTimeout(() => {
      if (checkingStatus.value) {
        checkingStatus.value = false
        checkingCameraIds.value.clear()
        console.log('检测状态超时结束')
      }
    }, 30000)
  } catch (error) {
    console.error('检查摄像头状态失败:', error)
    // 出错时也结束检测中状态
    checkingStatus.value = false
    checkingCameraIds.value.clear()
  }
}

// 处理WebSocket消息
const handleWebSocketMessage = async (message: WebSocketMessage) => {
  const { type, data } = message

  if (type === 'camera_status') {
    // 更新摄像头状态
    const targetCamera = list.value.find((c: Camera) => c.id === data.cameraId)
    if (targetCamera) {
      const oldStatus = targetCamera.status
      const newStatus = data.status || 0
      
      // 将此摄像头从检测中状态移除
      checkingCameraIds.value.delete(data.cameraId)
      
      // 检查是否所有摄像头都已完成检测
      if (checkingCameraIds.value.size === 0) {
        checkingStatus.value = false
      }
      
      // 只在状态真正发生变化时才更新数据库和显示消息
      if (oldStatus !== newStatus) {
        targetCamera.status = newStatus
        
        // 更新数据库中的状态
        try {
          await updateCameraStatus(targetCamera.id, newStatus)
          
          const statusText = newStatus === 1 ? '在线' : '离线'
          const messageType = newStatus === 1 ? 'success' : 'warning'
          
          ElMessage({
            type: messageType,
            message: '摄像头 ' + targetCamera.name + ' ' + statusText
          })
        } catch (error) {
          console.error('更新摄像头状态失败:', error)
          // 如果更新失败，回滚内存中的状态
          targetCamera.status = oldStatus
          ElMessage.error('更新摄像头状态失败')
        }
      }
    }
  }
}

// 启动自动更新
const startAutoUpdate = () => {
  // 设置定时器，每60秒检查一次
  statusUpdateTimer = window.setInterval(() => {
    if (list.value.length > 0) {
      checkAllCamerasStatus()
    }
  }, 60000)
}

// 停止自动更新
const stopAutoUpdate = () => {
  if (statusUpdateTimer) {
    clearInterval(statusUpdateTimer)
    statusUpdateTimer = null
  }
}

onMounted(() => {
  getList()
  // 添加WebSocket消息处理器
  wsClient.addMessageHandler(handleWebSocketMessage)
  // 连接WebSocket
  wsClient.connect()
  // 启动自动更新
  startAutoUpdate()
})

onBeforeUnmount(() => {
  // 停止自动更新
  stopAutoUpdate()
  // 移除WebSocket消息处理器
  wsClient.removeMessageHandler(handleWebSocketMessage)
  // 关闭WebSocket连接
  wsClient.close()
})
</script>

<style lang="scss" scoped>
.camera {
  padding: 20px;

  .toolbar {
    margin-bottom: 20px;
    display: flex;
    justify-content: space-between;
    align-items: center;

    .left {
      display: flex;
      gap: 10px;
    }
  }

  .search-form {
    margin-bottom: 20px;
    
    :deep(.el-form-item) {
      margin-bottom: 18px;
      margin-right: 18px;
      
      .el-input {
        width: 200px;
      }
      
      .status-select {
        width: 200px;
      }
    }
  }
  
  // 添加检测中动画样式
  .checking-status {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 5px;
    color: var(--el-color-primary);
    
    .el-icon {
      animation: rotating 2s linear infinite;
    }
  }
}

@keyframes rotating {
  0% {
    transform: rotate(0deg);
  }
  100% {
    transform: rotate(360deg);
  }
}
</style> 