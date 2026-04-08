<!-- 登录页面 -->
<template>
  <div class="login-container">
    <div class="login-box">
      <div class="login-header">
        <div class="logo-section">
          <img src="@/assets/images/logo.png" alt="系统Logo" class="logo-img" />
        </div>
        <h2 class="system-title">嵌入式多目标追踪与智能预警系统</h2>
        <p class="system-subtitle">安全监控 · 智能识别 · 实时预警</p>
      </div>
      <el-form
        ref="loginFormRef"
        :model="loginForm"
        :rules="loginRules"
        label-width="0"
        size="large"
        class="login-form"
      >
        <el-form-item prop="username">
          <el-input
            v-model="loginForm.username"
            placeholder="用户名"
            prefix-icon="User"
          />
        </el-form-item>
        <el-form-item prop="password">
          <el-input
            v-model="loginForm.password"
            type="password"
            placeholder="密码"
            prefix-icon="Lock"
            show-password
            @keyup.enter="handleLogin"
          />
        </el-form-item>
        <el-form-item>
          <el-button
            type="primary"
            :loading="loading"
            class="login-button"
            @click="handleLogin"
          >
            登录
          </el-button>
        </el-form-item>
        <div class="login-options">
          <router-link to="/register" class="register-link">
            注册账号
          </router-link>
        </div>
      </el-form>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import type { FormInstance } from 'element-plus'
import { useUserStore } from '@/stores/user'
import { useRoute, useRouter } from 'vue-router'

const userStore = useUserStore()
const route = useRoute()
const router = useRouter()

// 登录表单
const loginForm = ref({
  username: '',
  password: ''
})

// 表单校验规则
const loginRules = {
  username: [
    { required: true, message: '请输入用户名', trigger: 'blur' },
    { min: 3, max: 20, message: '用户名长度应在3-20个字符之间', trigger: 'blur' }
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 6, max: 20, message: '密码长度应在6-20个字符之间', trigger: 'blur' }
  ]
}

const loading = ref(false)
const loginFormRef = ref<FormInstance>()

// 处理登录
const handleLogin = async () => {
  if (!loginFormRef.value) return

  try {
    await loginFormRef.value.validate()
    loading.value = true

    await userStore.login(loginForm.value.username, loginForm.value.password)

    // 如果有重定向地址，则跳转到重定向地址
    const redirect = route.query.redirect as string
    router.replace(redirect || '/')
  } catch (error) {
    console.error('登录失败:', error)
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-container {
  height: 100vh;
  display: flex;
  justify-content: center;
  align-items: center;
  background: url('@/assets/images/login-bg.jpg') no-repeat center center;
  background-size: cover;
  background-attachment: fixed;
  position: relative;
}

/* 移除整体背景模糊遮罩，保持背景图清晰 */

.login-box {
  width: 450px;
  padding: 0;
  background: rgba(255, 255, 255, 0.97);
  border-radius: 20px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.15);
  backdrop-filter: blur(5px);
  border: 1px solid rgba(255, 255, 255, 0.3);
  position: relative;
  z-index: 1;
  overflow: hidden;
}

.login-header {
  background: linear-gradient(135deg, #1e40af 0%, #1e3a8a 100%);
  padding: 40px 40px 30px 40px;
  text-align: center;
  color: white;
  position: relative;
}

.login-header::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: url('data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 100"><path d="M0,0 C150,100 350,0 500,50 C650,100 850,0 1000,50 L1000,0 Z" fill="rgba(255,255,255,0.1)"/></svg>');
  background-size: cover;
}

.logo-section {
  position: relative;
  z-index: 1;
  margin-bottom: 20px;
}

.logo-img {
  width: 80px;
  height: 80px;
  border-radius: 50%;
  border: 3px solid rgba(255, 255, 255, 0.3);
  padding: 8px;
  background: rgba(255, 255, 255, 0.15);
  backdrop-filter: blur(3px);
}

.system-title {
  font-size: 28px;
  font-weight: 700;
  margin: 0 0 8px 0;
  text-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
  position: relative;
  z-index: 1;
}

.system-subtitle {
  font-size: 14px;
  margin: 0;
  opacity: 0.9;
  font-weight: 300;
  letter-spacing: 2px;
  position: relative;
  z-index: 1;
}

.login-form {
  padding: 40px;
}

.login-form :deep(.el-form-item) {
  margin-bottom: 25px;
}

.login-form :deep(.el-input__wrapper) {
  border-radius: 12px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
  border: 1px solid #e2e8f0;
  transition: all 0.3s ease;
  padding: 16px 18px;
}

.login-form :deep(.el-input__wrapper):hover {
  border-color: #1e40af;
  box-shadow: 0 4px 12px rgba(30, 64, 175, 0.15);
}

.login-form :deep(.el-input__wrapper.is-focus) {
  border-color: #1e40af;
  box-shadow: 0 0 0 3px rgba(30, 64, 175, 0.1);
}

.login-form :deep(.el-input__inner) {
  font-size: 16px;
  color: #1f2937;
}

.login-form :deep(.el-input__prefix) {
  color: #6b7280;
}

.login-button {
  width: 100%;
  height: 50px;
  border-radius: 12px;
  background: linear-gradient(135deg, #1e40af 0%, #1e3a8a 100%);
  border: none;
  font-size: 16px;
  font-weight: 600;
  transition: all 0.3s ease;
  box-shadow: 0 4px 16px rgba(30, 64, 175, 0.3);
}

.login-button:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(30, 64, 175, 0.4);
}

.login-button:active {
  transform: translateY(0);
}

.login-options {
  margin-top: 25px;
  text-align: center;
  padding-bottom: 10px;
}

.register-link {
  color: #1e40af;
  text-decoration: none;
  font-weight: 500;
  transition: all 0.3s ease;
  padding: 8px 16px;
  border-radius: 8px;
  display: inline-block;
}

.register-link:hover {
  color: #1e3a8a;
  background: rgba(30, 64, 175, 0.1);
  transform: translateY(-1px);
}

/* 响应式设计 */
@media (max-width: 480px) {
  .login-box {
    width: 90%;
    margin: 20px;
  }
  
  .login-header {
    padding: 30px 20px 20px 20px;
  }
  
  .login-form {
    padding: 30px 20px;
  }
  
  .logo-img {
    width: 60px;
    height: 60px;
  }
  
  .system-title {
    font-size: 24px;
  }
}
</style> 