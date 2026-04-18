<!-- 登录页面 -->
<template>
  <div class="login-page">
    <div class="login-shell">
      <section class="brand-panel">
        <div class="brand-copy">
          <span class="brand-badge">RK3588 Edge Vision Console</span>
          <div class="brand-logo-wrap">
            <img src="@/assets/images/logo.svg" alt="系统Logo" class="brand-logo" />
            <div class="brand-logo-ring"></div>
          </div>
          <h1 class="brand-title">追踪与智能预警系统</h1>
          <p class="brand-subtitle">
            面向边缘侧视觉监测场景的一体化控制台，聚合摄像头接入、实时推理、轨迹跟踪与告警联动。
          </p>
        </div>

        <div class="capability-grid">
          <article class="capability-card">
            <span class="capability-tag">双路采集</span>
            <strong>Camera In</strong>
            <p>支持本地摄像头接入、状态回显与稳定的边缘侧采集链路。</p>
          </article>
          <article class="capability-card capability-card-accent">
            <span class="capability-tag">实时分析</span>
            <strong>Track + Detect</strong>
            <p>YOLOv8 推理与 ByteTrack 目标跟踪在同一工作流里协同运行。</p>
          </article>
          <article class="capability-card">
            <span class="capability-tag">远端分发</span>
            <strong>RTSP / WebRTC</strong>
            <p>输出链路覆盖本地调试与浏览器播放，方便系统联调与上线展示。</p>
          </article>
        </div>

        <div class="signal-strip">
          <div class="signal-item">
            <span class="signal-dot"></span>
            <span>实时推理</span>
          </div>
          <div class="signal-item">
            <span class="signal-dot"></span>
            <span>智能预警</span>
          </div>
          <div class="signal-item">
            <span class="signal-dot"></span>
            <span>边缘部署</span>
          </div>
        </div>
      </section>

      <section class="auth-panel">
        <div class="auth-card">
          <div class="auth-header">
            <span class="auth-eyebrow">Welcome Back</span>
            <h2 class="auth-title">登录控制台</h2>
            <p class="auth-subtitle">
              进入系统后即可管理模型切换、视频流、禁入区与检测告警。
            </p>
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
                placeholder="请输入用户名"
                :prefix-icon="User"
              />
            </el-form-item>
            <el-form-item prop="password">
              <el-input
                v-model="loginForm.password"
                type="password"
                placeholder="请输入密码"
                :prefix-icon="Lock"
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
                进入系统
              </el-button>
            </el-form-item>
          </el-form>

          <div class="auth-footer">
            <div class="auth-tip">
              <span class="tip-line"></span>
              <p>登录后可直接进入监测大屏与模型管理页面。</p>
            </div>
            <router-link to="/register" class="register-link">
              还没有账号？立即注册
            </router-link>
          </div>
        </div>
      </section>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import type { FormInstance } from 'element-plus'
import { Lock, User } from '@element-plus/icons-vue'
import { useUserStore } from '@/stores/user'
import { useRoute, useRouter } from 'vue-router'

const userStore = useUserStore()
const route = useRoute()
const router = useRouter()

const loginForm = ref({
  username: '',
  password: ''
})

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

const handleLogin = async () => {
  if (!loginFormRef.value) return

  try {
    await loginFormRef.value.validate()
    loading.value = true

    await userStore.login(loginForm.value.username, loginForm.value.password)

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
.login-page {
  min-height: 100vh;
  display: grid;
  place-items: center;
  padding: 36px;
  overflow: hidden;
  background:
    radial-gradient(circle at 15% 20%, rgba(245, 178, 67, 0.28), transparent 28%),
    radial-gradient(circle at 85% 18%, rgba(68, 190, 182, 0.18), transparent 26%),
    radial-gradient(circle at 85% 82%, rgba(45, 84, 180, 0.38), transparent 32%),
    linear-gradient(135deg, #08111f 0%, #0e2038 45%, #123058 100%);
  position: relative;
  isolation: isolate;
  font-family: 'Avenir Next', 'PingFang SC', 'Hiragino Sans GB', 'Microsoft YaHei', sans-serif;
}

.login-page::before,
.login-page::after {
  content: '';
  position: absolute;
  inset: auto;
  pointer-events: none;
  z-index: -1;
}

.login-page::before {
  width: 38rem;
  height: 38rem;
  top: -12rem;
  right: -8rem;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(255, 255, 255, 0.14), rgba(255, 255, 255, 0));
  filter: blur(10px);
}

.login-page::after {
  left: 6%;
  bottom: -12%;
  width: min(52vw, 36rem);
  height: min(52vw, 36rem);
  border-radius: 43% 57% 58% 42% / 42% 39% 61% 58%;
  background: linear-gradient(135deg, rgba(17, 179, 165, 0.24), rgba(244, 165, 74, 0.08));
  filter: blur(18px);
}

.login-shell {
  width: min(1180px, 100%);
  min-height: min(760px, calc(100vh - 72px));
  display: grid;
  grid-template-columns: 1.15fr 0.85fr;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 30px;
  overflow: hidden;
  background: rgba(6, 13, 24, 0.34);
  backdrop-filter: blur(18px);
  box-shadow: 0 32px 80px rgba(2, 8, 18, 0.42);
}

.brand-panel {
  position: relative;
  padding: 56px 56px 44px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  color: #f3f8ff;
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.04), rgba(255, 255, 255, 0)),
    linear-gradient(135deg, rgba(10, 19, 33, 0.78), rgba(15, 46, 81, 0.72));
}

.brand-panel::before {
  content: '';
  position: absolute;
  inset: 24px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 24px;
  pointer-events: none;
}

.brand-copy,
.capability-grid,
.signal-strip {
  position: relative;
  z-index: 1;
}

.brand-badge {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 10px 16px;
  border-radius: 999px;
  color: #d7e8ff;
  background: rgba(255, 255, 255, 0.08);
  border: 1px solid rgba(255, 255, 255, 0.12);
  font-size: 12px;
  letter-spacing: 0.18em;
  text-transform: uppercase;
}

.brand-logo-wrap {
  position: relative;
  width: 110px;
  height: 110px;
  display: grid;
  place-items: center;
  margin: 28px 0 20px;
}

.brand-logo {
  width: 84px;
  height: 84px;
  border-radius: 24px;
  padding: 14px;
  background: rgba(250, 252, 255, 0.92);
  box-shadow: 0 18px 40px rgba(1, 7, 16, 0.28);
}

.brand-logo-ring {
  position: absolute;
  inset: 0;
  border-radius: 28px;
  border: 1px solid rgba(255, 255, 255, 0.24);
  animation: pulse-ring 4s ease-in-out infinite;
}

.brand-title {
  max-width: 12ch;
  margin: 0;
  font-size: clamp(2.4rem, 4vw, 4rem);
  line-height: 1.02;
  font-weight: 700;
  letter-spacing: -0.04em;
}

.brand-subtitle {
  max-width: 620px;
  margin: 18px 0 0;
  color: rgba(231, 239, 248, 0.82);
  font-size: 16px;
  line-height: 1.8;
}

.capability-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 16px;
  margin-top: 32px;
}

.capability-card {
  padding: 20px 18px;
  border-radius: 20px;
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid rgba(255, 255, 255, 0.1);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.04);
}

.capability-card-accent {
  background: linear-gradient(180deg, rgba(243, 174, 75, 0.16), rgba(255, 255, 255, 0.05));
}

.capability-tag {
  display: inline-block;
  margin-bottom: 14px;
  padding: 6px 10px;
  border-radius: 999px;
  color: #d6e7ff;
  background: rgba(255, 255, 255, 0.1);
  font-size: 12px;
  letter-spacing: 0.08em;
}

.capability-card strong {
  display: block;
  font-size: 18px;
  font-weight: 700;
  color: #ffffff;
}

.capability-card p {
  margin: 10px 0 0;
  color: rgba(231, 239, 248, 0.76);
  line-height: 1.7;
  font-size: 13px;
}

.signal-strip {
  display: flex;
  flex-wrap: wrap;
  gap: 14px;
  margin-top: 28px;
}

.signal-item {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  padding: 12px 16px;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid rgba(255, 255, 255, 0.08);
  color: rgba(243, 248, 255, 0.9);
  font-size: 14px;
}

.signal-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #f5b243;
  box-shadow: 0 0 0 6px rgba(245, 178, 67, 0.16);
}

.auth-panel {
  display: grid;
  place-items: center;
  padding: 34px;
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.9), rgba(244, 248, 252, 0.88)),
    rgba(255, 255, 255, 0.76);
}

.auth-card {
  width: min(430px, 100%);
  padding: 42px 36px 32px;
  border-radius: 28px;
  background: rgba(255, 255, 255, 0.92);
  border: 1px solid rgba(11, 32, 56, 0.08);
  box-shadow: 0 24px 48px rgba(7, 22, 40, 0.12);
}

.auth-header {
  margin-bottom: 30px;
}

.auth-eyebrow {
  display: inline-block;
  padding: 6px 12px;
  border-radius: 999px;
  background: #eef4fb;
  color: #26466e;
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0.14em;
  text-transform: uppercase;
}

.auth-title {
  margin: 18px 0 10px;
  color: #102843;
  font-size: 34px;
  line-height: 1.1;
  font-weight: 700;
  letter-spacing: -0.04em;
}

.auth-subtitle {
  margin: 0;
  color: #5d7186;
  font-size: 15px;
  line-height: 1.75;
}

.login-form :deep(.el-form-item) {
  margin-bottom: 22px;
}

.login-form :deep(.el-input__wrapper) {
  padding: 8px 18px;
  min-height: 56px;
  border-radius: 18px;
  background: #f7fafc;
  box-shadow: inset 0 0 0 1px #d7e4ee;
  transition: box-shadow 0.25s ease, transform 0.25s ease, background-color 0.25s ease;
}

.login-form :deep(.el-input__wrapper:hover) {
  background: #fbfdff;
  box-shadow: inset 0 0 0 1px #8fa6bc;
}

.login-form :deep(.el-input__wrapper.is-focus) {
  background: #ffffff;
  box-shadow:
    inset 0 0 0 1px #163d63,
    0 0 0 5px rgba(31, 72, 116, 0.1);
  transform: translateY(-1px);
}

.login-form :deep(.el-input__inner) {
  color: #162b41;
  font-size: 16px;
}

.login-form :deep(.el-input__prefix-inner) {
  color: #51708e;
}

.login-form :deep(.el-form-item.is-error .el-input__wrapper) {
  box-shadow: inset 0 0 0 1px #d96062;
}

.login-button {
  width: 100%;
  min-height: 56px;
  border: none;
  border-radius: 18px;
  background: linear-gradient(135deg, #12335b 0%, #1d5378 48%, #2f8b86 100%);
  box-shadow: 0 18px 30px rgba(18, 51, 91, 0.24);
  font-size: 16px;
  font-weight: 700;
  letter-spacing: 0.04em;
  transition: transform 0.25s ease, box-shadow 0.25s ease, filter 0.25s ease;
}

.login-button:hover {
  transform: translateY(-2px);
  box-shadow: 0 22px 36px rgba(18, 51, 91, 0.28);
  filter: saturate(1.08);
}

.login-button:active {
  transform: translateY(0);
}

.auth-footer {
  margin-top: 10px;
}

.auth-tip {
  display: flex;
  align-items: flex-start;
  gap: 12px;
  padding: 16px 18px;
  border-radius: 18px;
  background: #f4f8fb;
  color: #5b6f82;
}

.tip-line {
  width: 4px;
  min-width: 4px;
  height: 42px;
  border-radius: 999px;
  background: linear-gradient(180deg, #2c8d8b, #f1ab4c);
}

.auth-tip p {
  margin: 0;
  line-height: 1.7;
  font-size: 14px;
}

.register-link {
  display: inline-flex;
  margin-top: 18px;
  color: #193d62;
  font-weight: 700;
  text-decoration: none;
  position: relative;
  transition: color 0.25s ease;
}

.register-link::after {
  content: '';
  position: absolute;
  left: 0;
  bottom: -4px;
  width: 100%;
  height: 2px;
  background: linear-gradient(90deg, #21476f, #2c8d8b);
  transform: scaleX(0.45);
  transform-origin: left center;
  transition: transform 0.25s ease;
}

.register-link:hover {
  color: #0f2d49;
}

.register-link:hover::after {
  transform: scaleX(1);
}

@keyframes pulse-ring {
  0%,
  100% {
    transform: scale(1);
    opacity: 0.55;
  }
  50% {
    transform: scale(1.08);
    opacity: 0.2;
  }
}

@media (max-width: 1100px) {
  .login-shell {
    grid-template-columns: 1fr;
  }

  .brand-panel {
    padding-bottom: 32px;
  }

  .capability-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

@media (max-width: 720px) {
  .login-page {
    padding: 20px;
  }

  .login-shell {
    min-height: auto;
    border-radius: 24px;
  }

  .brand-panel {
    padding: 32px 24px 26px;
  }

  .brand-title {
    max-width: none;
    font-size: 36px;
  }

  .brand-subtitle {
    font-size: 14px;
  }

  .capability-grid {
    grid-template-columns: 1fr;
  }

  .auth-panel {
    padding: 20px;
  }

  .auth-card {
    padding: 30px 22px 24px;
    border-radius: 22px;
  }

  .auth-title {
    font-size: 28px;
  }
}

@media (max-width: 480px) {
  .signal-strip {
    gap: 10px;
  }

  .signal-item {
    width: 100%;
  }

  .login-form :deep(.el-input__wrapper),
  .login-button {
    min-height: 52px;
    border-radius: 16px;
  }
}
</style>
