<template>
  <div class="upload-model-page">
    <el-card shadow="hover" class="main-card">
      <template #header>
        <div class="card-header">
          <el-icon :size="22" color="#1e40af"><UploadFilled /></el-icon>
          <span>上传模型</span>
        </div>
      </template>

      <p class="hint">
        内置模型 <code>person_2700_i8</code>、<code>yolov8s</code>、<code>yolov8n</code> 可直接选择使用。
        <br />
        同时上传 <code>.rknn</code> 模型文件和对应的 <code>.txt</code> 标签文件，两者须保持同名（扩展名不同）。
        上传后会自动建立模型与标签关联，选择模型时后端会自动把模型和标签下发给视觉服务。
      </p>

      <el-upload
        ref="uploadRef"
        class="model-uploader"
        drag
        :auto-upload="false"
        :limit="2"
        :on-change="handleFileChange"
        :on-exceed="handleExceed"
        accept=".rknn,.txt"
        multiple
      >
        <el-icon class="upload-icon"><UploadFilled /></el-icon>
        <div class="el-upload__text">
          将 <strong>.rknn</strong> 和 <strong>.txt</strong> 文件拖到此处，或<em>点击选择</em>
        </div>
        <template #tip>
          <div class="el-upload__tip">
            最多 2 个文件，扩展名限 .rknn / .txt，单文件不超过 200MB，两者必须同名（basename 相同）
          </div>
        </template>
      </el-upload>

      <div v-if="pendingFiles.length" class="file-list">
        <div class="file-list-title">待上传文件：</div>
        <div v-for="(f, i) in pendingFiles" :key="i" class="file-item">
          <el-icon><Document /></el-icon>
          <span>{{ f.name }}</span>
          <el-tag :type="f.name.toLowerCase().endsWith('.rknn') ? 'primary' : 'success'" size="small">
            {{ f.name.toLowerCase().endsWith('.rknn') ? '模型' : '标签' }}
          </el-tag>
        </div>
        <div class="actions">
          <el-button type="primary" :loading="uploading" @click="submitUpload">开始上传</el-button>
          <el-button @click="clearFiles">清空</el-button>
        </div>
      </div>

      <div v-if="results.length" class="results">
        <el-alert
          v-for="(r, i) in results"
          :key="i"
          type="success"
          :closable="true"
          show-icon
          class="result-alert"
          @close="results.splice(i, 1)"
        >
          <template #title>{{ r.name }} 上传成功</template>
          <div class="result-body">
            <div class="result-url">
              <code>{{ r.url }}</code>
              <el-button size="small" @click="copyUrl(r.url)">复制</el-button>
            </div>
          </div>
        </el-alert>
      </div>

      <el-alert
        v-if="errorMsg"
        type="error"
        closable
        show-icon
        class="error-alert"
        @close="errorMsg = ''"
      >
        {{ errorMsg }}
      </el-alert>
    </el-card>

    <el-card shadow="hover" class="main-card model-table-card">
      <template #header>
        <div class="table-header">
          <span>已上传模型</span>
          <el-button size="small" @click="loadModelProfiles" :loading="loadingModels">刷新</el-button>
        </div>
      </template>

      <el-table :data="modelProfiles" v-loading="loadingModels" empty-text="暂无模型，请先上传">
        <el-table-column label="模型名" min-width="220">
          <template #default="{ row }">
            <div class="model-name-cell">
              <span>{{ row.baseName }}</span>
              <el-tag v-if="row.builtin" type="primary" size="small">内置</el-tag>
            </div>
          </template>
        </el-table-column>
        <el-table-column label=".rknn" width="90" align="center">
          <template #default="{ row }">
            <el-tag v-if="row.builtin" type="primary" size="small">内置</el-tag>
            <el-tag v-else-if="row.modelObjectKey" type="success" size="small">已上传</el-tag>
            <el-tag v-else type="info" size="small">缺失</el-tag>
          </template>
        </el-table-column>
        <el-table-column label=".txt" width="90" align="center">
          <template #default="{ row }">
            <el-tag v-if="row.builtin" type="primary" size="small">内置</el-tag>
            <el-tag v-else-if="row.labelObjectKey" type="success" size="small">已上传</el-tag>
            <el-tag v-else type="info" size="small">缺失</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="状态" width="120" align="center">
          <template #default="{ row }">
            <el-tag v-if="row.ready" :type="row.builtin ? 'primary' : 'success'" size="small">
              {{ row.builtin ? '内置可用' : '可切换' }}
            </el-tag>
            <el-tag v-else type="warning" size="small">不完整</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="180" align="center">
          <template #default="{ row }">
            <el-button
              v-if="!row.selected"
              type="primary"
              size="small"
              :disabled="!row.ready"
              :loading="selectingModelId === row.id"
              @click="handleSelectModel(row)"
            >
              {{ row.builtin ? '使用内置' : '设为当前' }}
            </el-button>
            <el-tag v-else type="success" size="small">当前模型</el-tag>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import type { UploadInstance, UploadProps, UploadUserFile } from 'element-plus'
import { UploadFilled, Document } from '@element-plus/icons-vue'
import { fileRequest } from '@/api/file_request'
import { useUserStore } from '@/stores/user'
import { listModelProfiles, selectModelProfile, type ModelProfile } from '@/api/model'

const MODEL_BUCKET = 'models'
const ALLOWED_EXT = new Set(['.rknn', '.txt'])
const MAX_MODEL_FILE_SIZE = 200 * 1024 * 1024

const uploadRef = ref<UploadInstance>()
const pendingFiles = ref<UploadUserFile[]>([])
const uploading = ref(false)
const results = ref<{ name: string; url: string }[]>([])
const errorMsg = ref('')
const userStore = useUserStore()

const modelProfiles = ref<ModelProfile[]>([])
const loadingModels = ref(false)
const selectingModelId = ref<number | null>(null)

const getBaseName = (filename: string): string => {
  const lastDot = filename.lastIndexOf('.')
  return lastDot > 0 ? filename.substring(0, lastDot) : filename
}

const handleFileChange: UploadProps['onChange'] = (_uploadFile, uploadFiles) => {
  errorMsg.value = ''

  const oversized = uploadFiles.find(f => (f.size || 0) > MAX_MODEL_FILE_SIZE)
  if (oversized) {
    const sizeMb = ((oversized.size || 0) / 1024 / 1024).toFixed(2)
    ElMessage.error(`文件 ${oversized.name} 大小为 ${sizeMb}MB，超过 200MB 限制`)
    uploadRef.value?.clearFiles()
    pendingFiles.value = []
    return
  }

  const invalid = uploadFiles.find(f => {
    const name = f.name || ''
    const ext = '.' + name.split('.').pop()?.toLowerCase()
    return !ALLOWED_EXT.has(ext)
  })
  if (invalid) {
    ElMessage.error(`不支持 ${invalid.name}，仅允许 .rknn 或 .txt 文件`)
    uploadRef.value?.clearFiles()
    pendingFiles.value = []
    return
  }

  pendingFiles.value = uploadFiles
  results.value = []
}

const handleExceed: UploadProps['onExceed'] = () => {
  ElMessage.warning('最多只能上传 2 个文件（.rknn + .txt）')
}

const clearFiles = () => {
  uploadRef.value?.clearFiles()
  pendingFiles.value = []
}

const submitUpload = async () => {
  if (pendingFiles.value.length === 0) {
    ElMessage.warning('请先选择文件')
    return
  }

  if (pendingFiles.value.length < 2) {
    ElMessage.warning('请同时上传 .rknn 和 .txt 两个文件')
    return
  }

  const files: File[] = []
  for (const f of pendingFiles.value) {
    const rawFile = f.raw
    const targetFile = (rawFile && rawFile.size > 0) ? rawFile : f
    const fileSize = (targetFile as File).size
    if (fileSize > 0) {
      files.push(targetFile as File)
    }
  }

  if (files.length !== 2) {
    ElMessage.error('文件数据无效，请重新选择')
    return
  }

  const names = files.map(f => f.name)
  const bases = names.map(getBaseName)
  if (bases[0] !== bases[1]) {
    errorMsg.value = `文件名不一致：${names[0]} 与 ${names[1]} 的 basename 必须相同`
    return
  }

  const exts = names.map(n => '.' + n.split('.').pop()?.toLowerCase())
  if (!exts.includes('.rknn') || !exts.includes('.txt')) {
    errorMsg.value = '必须同时包含一个 .rknn 文件和一个 .txt 文件'
    return
  }

  uploading.value = true
  results.value = []
  errorMsg.value = ''

  try {
    const username = userStore.userInfo?.username || 'default'
    const uploaded: { name: string; url: string }[] = []
    for (const file of files) {
      const data = await fileRequest.upload(MODEL_BUCKET, file, false, username)
      uploaded.push({ name: file.name, url: data.url })
    }
    results.value = uploaded
    ElMessage.success('上传成功')
    uploadRef.value?.clearFiles()
    pendingFiles.value = []
    await loadModelProfiles()
  } catch {
    // 错误已由拦截器处理
  } finally {
    uploading.value = false
  }
}

const loadModelProfiles = async () => {
  loadingModels.value = true
  try {
    modelProfiles.value = await listModelProfiles()
  } catch {
    // 错误已由拦截器处理
  } finally {
    loadingModels.value = false
  }
}

const handleSelectModel = async (profile: ModelProfile) => {
  if (!profile.ready) {
    ElMessage.warning(profile.builtin ? '内置模型文件缺失，无法切换' : '该模型缺少 .rknn 或 .txt 文件，无法切换')
    return
  }
  selectingModelId.value = profile.id
  try {
    await selectModelProfile(profile.id)
    ElMessage.success(profile.builtin ? `已切换到内置模型：${profile.baseName}` : `已切换到模型：${profile.baseName}`)
    await loadModelProfiles()
  } catch {
    // 错误已由拦截器处理
  } finally {
    selectingModelId.value = null
  }
}

const copyUrl = async (url: string) => {
  const full = url.startsWith('http') ? url : `${window.location.origin}${url}`
  try {
    await navigator.clipboard.writeText(full)
    ElMessage.success('已复制到剪贴板')
  } catch {
    ElMessage.info(full)
  }
}

onMounted(() => {
  loadModelProfiles()
})
</script>

<style scoped>
.upload-model-page {
  max-width: 920px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.main-card {
  border-radius: 12px;
  border: 1px solid #e2e8f0;
}

.card-header {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 18px;
  font-weight: 600;
  color: #0f172a;
}

.model-name-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.hint {
  color: #64748b;
  font-size: 14px;
  line-height: 1.6;
  margin: 0 0 20px;
}

.hint code {
  background: #f1f5f9;
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 13px;
}

.model-uploader {
  width: 100%;
}

.model-uploader :deep(.el-upload) {
  width: 100%;
}

.model-uploader :deep(.el-upload-dragger) {
  width: 100%;
  padding: 40px 20px;
  border-radius: 12px;
  border-color: #cbd5e1;
  background: #f8fafc;
}

.upload-icon {
  font-size: 48px;
  color: #1e40af;
  margin-bottom: 8px;
}

.file-list {
  margin-top: 20px;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  padding: 16px 20px;
}

.file-list-title {
  font-size: 14px;
  color: #475569;
  font-weight: 500;
  margin-bottom: 12px;
}

.file-item {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 10px;
  font-size: 14px;
  color: #334155;
}

.actions {
  margin-top: 16px;
  display: flex;
  gap: 12px;
}

.results {
  margin-top: 20px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.result-alert {
  border-radius: 10px;
}

.result-body {
  font-size: 14px;
  line-height: 1.6;
  margin-top: 4px;
}

.result-url {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.result-url code {
  background: #f1f5f9;
  padding: 4px 10px;
  border-radius: 6px;
  font-size: 13px;
  word-break: break-all;
  flex: 1;
  min-width: 0;
}

.error-alert {
  margin-top: 16px;
}

.model-table-card :deep(.el-card__header) {
  padding-bottom: 12px;
}

.table-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-weight: 600;
  color: #0f172a;
}
</style>
