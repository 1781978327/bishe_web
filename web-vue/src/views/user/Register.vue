<!-- 注册页面 -->
<template>
  <div class="register-container">
    <div class="register-box">
      <div class="register-header">
        <div class="logo-section">
          <img src="@/assets/images/logo.png" alt="系统Logo" class="logo-img" />
        </div>
        <h2 class="system-title">系统账号注册</h2>
        <p class="system-subtitle">创建您的安全监控账户</p>
      </div>
      <el-form
        ref="registerFormRef"
        :model="registerForm"
        :rules="registerRules"
        label-width="80px"
        size="large"
        class="register-form"
      >
        <el-form-item label="用户名" prop="username">
          <el-input
            v-model="registerForm.username"
            placeholder="请输入用户名"
          />
        </el-form-item>
        <el-form-item label="密码" prop="password">
          <el-input
            v-model="registerForm.password"
            type="password"
            placeholder="请输入密码"
            show-password
          />
        </el-form-item>
        <el-form-item label="确认密码" prop="confirmPassword">
          <el-input
            v-model="registerForm.confirmPassword"
            type="password"
            placeholder="请确认密码"
            show-password
          />
        </el-form-item>
        <el-form-item label="真实姓名" prop="realName">
          <el-input
            v-model="registerForm.realName"
            placeholder="请输入真实姓名"
          />
        </el-form-item>
        <el-form-item label="手机号" prop="phone">
          <el-input
            v-model="registerForm.phone"
            placeholder="请输入手机号"
          />
        </el-form-item>
        <el-form-item label="邮箱" prop="email">
          <el-input
            v-model="registerForm.email"
            placeholder="请输入邮箱"
          />
        </el-form-item>
        <el-form-item>
          <el-button
            type="primary"
            :loading="loading"
            class="register-button"
            @click="handleRegister"
          >
            注册
          </el-button>
        </el-form-item>
        <div class="register-options">
          已有账号？
          <router-link to="/login" class="login-link">
            立即登录
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
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'

const userStore = useUserStore()
const router = useRouter()

// 注册表单
const registerForm = ref({
  username: '',
  password: '',
  confirmPassword: '',
  realName: '',
  phone: '',
  email: ''
})

// 表单校验规则
const validatePass2 = (rule: any, value: string, callback: any) => {
  if (value === '') {
    callback(new Error('请再次输入密码'))
  } else if (value !== registerForm.value.password) {
    callback(new Error('两次输入密码不一致!'))
  } else {
    callback()
  }
}

const registerRules = {
  username: [
    { required: true, message: '请输入用户名', trigger: 'blur' },
    { min: 3, max: 20, message: '用户名长度应在3-20个字符之间', trigger: 'blur' }
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 6, max: 20, message: '密码长度应在6-20个字符之间', trigger: 'blur' }
  ],
  confirmPassword: [
    { required: true, message: '请再次输入密码', trigger: 'blur' },
    { validator: validatePass2, trigger: 'blur' }
  ],
  realName: [
    { required: true, message: '请输入真实姓名', trigger: 'blur' },
    { min: 2, max: 20, message: '真实姓名长度应在2-20个字符之间', trigger: 'blur' }
  ],
  phone: [
    { required: true, message: '请输入手机号', trigger: 'blur' },
    { pattern: /^1[3-9]\d{9}$/, message: '请输入正确的手机号', trigger: 'blur' }
  ],
  email: [
    { required: true, message: '请输入邮箱', trigger: 'blur' },
    { type: 'email' as const, message: '请输入正确的邮箱地址', trigger: 'blur' }
  ]
}

const loading = ref(false)
const registerFormRef = ref<FormInstance>()

// 处理注册
const handleRegister = async () => {
  if (!registerFormRef.value) return

  try {
    await registerFormRef.value.validate()
    loading.value = true

    const { confirmPassword, ...registerData } = registerForm.value
    await userStore.register(registerData)

    ElMessage.success('注册成功，请登录')
    router.push('/login')
  } catch (error) {
    console.error('注册失败:', error)
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.register-container {
  height: 100vh;
  display: flex;
  justify-content: center;
  align-items: center;
  background:
    radial-gradient(circle at 18% 20%, rgba(245, 178, 67, 0.24), transparent 28%),
    radial-gradient(circle at 82% 16%, rgba(68, 190, 182, 0.16), transparent 24%),
    linear-gradient(135deg, #08111f 0%, #0e2038 45%, #123058 100%);
  position: relative;
  padding: 20px 0;
}

/* 移除整体背景模糊遮罩，保持背景图清晰 */

.register-box {
  width: 550px;
  max-width: 90%;
  padding: 0;
  background: rgba(255, 255, 255, 0.97);
  border-radius: 20px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.15);
  backdrop-filter: blur(5px);
  border: 1px solid rgba(255, 255, 255, 0.3);
  position: relative;
  z-index: 1;
  overflow: hidden;
  margin: 20px;
}

.register-header {
  background: linear-gradient(135deg, #1e40af 0%, #1e3a8a 100%);
  padding: 30px 40px 25px 40px;
  text-align: center;
  color: white;
  position: relative;
}

.register-header::before {
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
  margin-bottom: 15px;
}

.logo-img {
  width: 70px;
  height: 70px;
  border-radius: 50%;
  border: 3px solid rgba(255, 255, 255, 0.3);
  padding: 6px;
  background: rgba(255, 255, 255, 0.15);
  backdrop-filter: blur(3px);
}

.system-title {
  font-size: 26px;
  font-weight: 700;
  margin: 0 0 6px 0;
  text-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
  position: relative;
  z-index: 1;
}

.system-subtitle {
  font-size: 13px;
  margin: 0;
  opacity: 0.9;
  font-weight: 300;
  letter-spacing: 1.5px;
  position: relative;
  z-index: 1;
}

.register-form {
  padding: 35px 40px 40px 40px;
}

.register-form :deep(.el-form-item) {
  margin-bottom: 20px;
}

.register-form :deep(.el-form-item__label) {
  color: #374151;
  font-weight: 500;
  font-size: 14px;
}

.register-form :deep(.el-input__wrapper) {
  border-radius: 10px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
  border: 1px solid #e2e8f0;
  transition: all 0.3s ease;
  padding: 14px 16px;
}

.register-form :deep(.el-input__wrapper):hover {
  border-color: #1e40af;
  box-shadow: 0 4px 12px rgba(30, 64, 175, 0.15);
}

.register-form :deep(.el-input__wrapper.is-focus) {
  border-color: #1e40af;
  box-shadow: 0 0 0 3px rgba(30, 64, 175, 0.1);
}

.register-form :deep(.el-input__inner) {
  font-size: 15px;
  color: #1f2937;
}

.register-button {
  width: 100%;
  height: 48px;
  border-radius: 12px;
  background: linear-gradient(135deg, #1e40af 0%, #1e3a8a 100%);
  border: none;
  font-size: 16px;
  font-weight: 600;
  transition: all 0.3s ease;
  box-shadow: 0 4px 16px rgba(30, 64, 175, 0.3);
  margin-top: 10px;
}

.register-button:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(30, 64, 175, 0.4);
}

.register-button:active {
  transform: translateY(0);
}

.register-options {
  margin-top: 20px;
  text-align: center;
  padding-bottom: 5px;
}

.login-link {
  color: #1e40af;
  text-decoration: none;
  font-weight: 500;
  transition: all 0.3s ease;
  padding: 6px 12px;
  border-radius: 8px;
  display: inline-block;
}

.login-link:hover {
  color: #1e3a8a;
  background: rgba(30, 64, 175, 0.1);
  transform: translateY(-1px);
}

/* 响应式设计 */
@media (max-width: 600px) {
  .register-box {
    width: 95%;
    margin: 10px;
  }
  
  .register-header {
    padding: 25px 20px 20px 20px;
  }
  
  .register-form {
    padding: 25px 20px 30px 20px;
  }
  
  .logo-img {
    width: 60px;
    height: 60px;
  }
  
  .system-title {
    font-size: 22px;
  }
  
  .register-form :deep(.el-form-item__label) {
    font-size: 13px;
  }
}

@media (max-width: 480px) {
  .register-form {
    padding: 20px 15px 25px 15px;
  }
  
  .register-header {
    padding: 20px 15px 15px 15px;
  }
}
</style> 
