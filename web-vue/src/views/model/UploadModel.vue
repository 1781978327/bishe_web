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
        同时上传 <code>.rknn</code> 模型文件和对应的 <code>.yaml</code> 配置文件，两者须保持相同的文件名（扩展名不同）。
        文件保存到 <code>uploads/models/用户名/</code> 目录。
      </p>
      <el-upload
        ref="uploadRef"
        class="model-uploader"
        drag
        :auto-upload="false"
        :limit="2"
        :on-change="handleFileChange"
        :on-exceed="handleExceed"
        accept=".rknn,.yaml"
        multiple
      >
        <el-icon class="upload-icon"><UploadFilled /></el-icon>
        <div class="el-upload__text">
          将 <strong>.rknn</strong> 和 <strong>.yaml</strong> 文件拖到此处，或<em>点击选择</em>
        </div>
        <template #tip>
          <div class="el-upload__tip">
            最多 2 个文件，扩展名限 .rknn / .yaml，单文件不超过 200MB，两者必须同名（basename 相同）
          </div>
        </template>
      </el-upload>

      <!-- 待上传文件列表 -->
      <div v-if="pendingFiles.length" class="file-list">
        <div class="file-list-title">待上传文件：</div>
        <div v-for="(f, i) in pendingFiles" :key="i" class="file-item">
          <el-icon><Document /></el-icon>
          <span>{{ f.name }}</span>
          <el-tag :type="f.name.endsWith('.rknn') ? 'primary' : 'success'" size="small">
            {{ f.name.endsWith('.rknn') ? '模型' : '配置' }}
          </el-tag>
        </div>
        <div class="actions">
          <el-button type="primary" :loading="uploading" @click="submitUpload">开始上传</el-button>
          <el-button @click="clearFiles">清空</el-button>
        </div>
      </div>

      <!-- 上传结果 -->
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

      <!-- 错误提示 -->
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
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import type { UploadInstance, UploadProps, UploadUserFile } from 'element-plus'
import { UploadFilled, Document } from '@element-plus/icons-vue'
import { fileRequest } from '@/api/file_request'
import { useUserStore } from '@/stores/user'

const MODEL_BUCKET = 'models'

const uploadRef = ref<UploadInstance>()
const pendingFiles = ref<UploadUserFile[]>([])
const uploading = ref(false)
const results = ref<{ name: string; url: string }[]>([])
const errorMsg = ref('')
const userStore = useUserStore()

const ALLOWED_EXT = new Set(['.rknn', '.yaml'])
const MAX_MODEL_FILE_SIZE = 200 * 1024 * 1024

/** 从文件名中提取 basename（不含扩展名） */
const getBaseName = (filename: string): string => {
  const lastDot = filename.lastIndexOf('.')
  return lastDot > 0 ? filename.substring(0, lastDot) : filename
}

const handleFileChange: UploadProps['onChange'] = (uploadFile, uploadFiles) => {
  errorMsg.value = ''

  const oversized = uploadFiles.find(f => (f.size || 0) > MAX_MODEL_FILE_SIZE)
  if (oversized) {
    const sizeMb = ((oversized.size || 0) / 1024 / 1024).toFixed(2)
    ElMessage.error(`文件 ${oversized.name} 大小为 ${sizeMb}MB，超过 200MB 限制`)
    uploadRef.value?.clearFiles()
    pendingFiles.value = []
    return
  }

  // 过滤掉非 rknn / yaml 的文件
  const valid = uploadFiles.filter(f => {
    const name = f.name || ''
    const ext = '.' + name.split('.').pop()?.toLowerCase()
    return ALLOWED_EXT.has(ext)
  })

  // 检查是否有非法后缀
  const invalid = uploadFiles.find(f => {
    const name = f.name || ''
    const ext = '.' + name.split('.').pop()?.toLowerCase()
    return !ALLOWED_EXT.has(ext)
  })
  if (invalid) {
    ElMessage.error(`不支持 ${invalid.name}，仅允许 .rknn 或 .yaml 文件`)
    uploadRef.value?.clearFiles()
    return
  }

  pendingFiles.value = valid
  results.value = []
}

const handleExceed: UploadProps['onExceed'] = () => {
  ElMessage.warning('最多只能上传 2 个文件（.rknn + .yaml）')
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
    ElMessage.warning('请同时上传 .rknn 和 .yaml 两个文件')
    return
  }

  // 调试：打印文件信息
  console.log('pendingFiles:', pendingFiles.value)

  // 获取文件对象 - Element Plus 使用 raw 属性存储实际 File 对象
  // 需要检查 file.size (UI显示的) 和 raw.size (实际的) 两个值
  const files: File[] = []
  for (const f of pendingFiles.value) {
    // 优先使用 raw，如果 raw 不存在或无效，则尝试 f 本身
    const rawFile = f.raw
    const targetFile = (rawFile && rawFile.size > 0) ? rawFile : f
    const fileSize = (targetFile as File).size

    if (fileSize > 0) {
      files.push(targetFile as File)
    } else {
      console.warn('文件为空或无效:', f.name, { rawSize: rawFile?.size, fSize: fileSize })
    }
  }

  console.log('processed files:', files)
  console.log('file sizes:', files.map(f => f.size))

  if (files.length === 0) {
    ElMessage.error('文件数据无效，请重新选择')
    return
  }
  const names = files.map(f => f.name)

  // 检查 basename 是否一致
  const bases = names.map(getBaseName)
  if (bases[0] !== bases[1]) {
    errorMsg.value = `文件名不一致：${names[0]} 与 ${names[1]} 的 basename 必须相同`
    return
  }

  // 检查扩展名是否齐全且不同
  const exts = names.map(n => '.' + n.split('.').pop()?.toLowerCase())
  if (!exts.includes('.rknn') || !exts.includes('.yaml')) {
    errorMsg.value = '必须同时包含一个 .rknn 文件和一个 .yaml 文件'
    return
  }

  uploading.value = true
  results.value = []
  errorMsg.value = ''

  try {
    // 获取当前用户名
    const username = userStore.userInfo?.username || 'default'

    // 两个文件串行上传（也可改为 Promise.all 并行）
    const uploaded: { name: string; url: string }[] = []
    for (const file of files) {
      const data = await fileRequest.upload(MODEL_BUCKET, file, false, username)
      uploaded.push({ name: file.name, url: data.url })
    }
    results.value = uploaded
    ElMessage.success('上传成功')
    uploadRef.value?.clearFiles()
    pendingFiles.value = []
  } catch {
    // 错误已在拦截器中提示
  } finally {
    uploading.value = false
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
</script>

<style scoped>
.upload-model-page {
  max-width: 720px;
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
</style>
