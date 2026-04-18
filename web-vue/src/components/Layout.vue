<!-- 基础布局组件 -->
<template>
  <el-container class="layout-container">
    <!-- 侧边栏 -->
      <el-aside :width="isCollapse ? '64px' : '200px'" class="aside">
      <div class="logo" :class="{ 'logo-collapse': isCollapse }">
        <el-icon class="logo-mark"><Monitor /></el-icon>
        <span v-show="!isCollapse" class="logo-text">追踪与智能预警系统</span>
      </div>
      <el-menu
        :default-active="activeMenu"
        class="menu"
        :router="true"
        :collapse="isCollapse"
        background-color="#1e3a8a"
        text-color="#cbd5e1"
        active-text-color="#fbbf24"
      >
        <!-- 普通用户菜单 -->
        <template v-if="!isAdmin">
          <el-menu-item index="/dashboard">
            <el-icon><Monitor /></el-icon>
            <template #title>安全首页</template>
          </el-menu-item>
          <el-menu-item index="/model/upload">
            <el-icon><Upload /></el-icon>
            <template #title>上传模型</template>
          </el-menu-item>
          <el-menu-item index="/detection/record">
            <el-icon><Document /></el-icon>
            <template #title>检测记录</template>
          </el-menu-item>
          <el-menu-item index="/monitor">
            <el-icon><View /></el-icon>
            <template #title>实时监控</template>
          </el-menu-item>
          <el-menu-item index="/profile">
            <el-icon><User /></el-icon>
            <template #title>个人中心</template>
          </el-menu-item>
        </template>

        <!-- 管理员菜单 -->
        <template v-else>
          <el-menu-item index="/admin">
            <el-icon><Setting /></el-icon>
            <template #title>安全控制台</template>
          </el-menu-item>
          <el-menu-item index="/admin/users">
            <el-icon><User /></el-icon>
            <template #title>用户管理</template>
          </el-menu-item>
          <el-menu-item index="/model/upload">
            <el-icon><Upload /></el-icon>
            <template #title>上传模型</template>
          </el-menu-item>
          <el-menu-item index="/detection/record">
            <el-icon><Warning /></el-icon>
            <template #title>安全记录</template>
          </el-menu-item>
        </template>
      </el-menu>
    </el-aside>

    <!-- 主要内容区 -->
    <el-container>
      <!-- 头部 -->
      <el-header class="header">
        <div class="header-left">
          <el-icon
            class="collapse-btn"
            @click="toggleCollapse"
          >
            <Fold v-if="!isCollapse" />
            <Expand v-else />
          </el-icon>
          <el-breadcrumb separator="/">
            <el-breadcrumb-item :to="{ path: '/' }">首页</el-breadcrumb-item>
            <el-breadcrumb-item>{{ route.meta.title }}</el-breadcrumb-item>
          </el-breadcrumb>
        </div>
        <div class="header-right">
          <el-dropdown @command="handleCommand">
            <span class="user-info">
              <el-avatar :size="32" :src="userInfo?.avatarUrl" />
              <span>{{ userInfo?.username }}</span>
            </span>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="profile">个人中心</el-dropdown-item>
                <el-dropdown-item command="logout">退出登录</el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </el-header>

      <!-- 内容区 -->
      <el-main class="main">
        <router-view v-slot="{ Component }">
          <transition name="fade" mode="out-in">
            <component :is="Component" />
          </transition>
        </router-view>
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useUserStore } from '@/stores/user'
import { Monitor, User, Fold, Expand, View, Warning, Setting, Document, Upload } from '@element-plus/icons-vue'

const route = useRoute()
const router = useRouter()
const userStore = useUserStore()

// 计算当前激活的菜单项
const activeMenu = computed(() => route.path)

// 用户信息
const userInfo = computed(() => userStore.userInfo)

// 是否是管理员
const isAdmin = computed(() => userStore.isAdmin())

// 侧边栏折叠状态
const isCollapse = ref(false)

// 切换侧边栏折叠状态
const toggleCollapse = () => {
  isCollapse.value = !isCollapse.value
}

// 处理下拉菜单命令
const handleCommand = (command: string) => {
  switch (command) {
    case 'profile':
      router.push('/profile')
      break
    case 'logout':
      userStore.logout()
      break
  }
}
</script>

<style scoped>
.layout-container {
  height: 100vh;
}

.aside {
  background: linear-gradient(180deg, #1e3a8a 0%, #1e40af 100%);
  transition: width 0.3s;
  overflow: hidden;
  box-shadow: 2px 0 8px rgba(0, 0, 0, 0.1);
}

.logo {
  min-height: 64px;
  display: flex;
  align-items: center;
  padding: 0 14px;
  color: #fff;
  transition: all 0.3s;
  overflow: hidden;
  background: rgba(255, 255, 255, 0.1);
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
  font-weight: 600;
  font-size: 16px;
}

.logo-collapse {
  padding: 0 16px;
  min-height: 64px;
}

.logo-mark {
  width: 32px;
  height: 32px;
  margin-right: 8px;
  flex-shrink: 0;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  background: rgba(251, 191, 36, 0.18);
  color: #fbbf24;
  font-size: 18px;
  box-shadow: inset 0 0 0 1px rgba(251, 191, 36, 0.22);
}

.logo-text {
  min-width: 0;
  flex: 1;
  display: block;
  font-size: 12px;
  line-height: 1;
  white-space: nowrap;
  word-break: keep-all;
  overflow: hidden;
  text-overflow: ellipsis;
}

.menu {
  border-right: none;
  background-color: transparent;
}

.menu:not(.el-menu--collapse) {
  width: 200px;
}

:deep(.el-menu--collapse) {
  width: 64px;
}

:deep(.el-menu-item) {
  &.is-active {
    background: rgba(251, 191, 36, 0.2);
    border-right: 3px solid #fbbf24;
    color: #fbbf24;
  }

  &:hover {
    background: rgba(255, 255, 255, 0.1);
    color: #fbbf24;
  }
}

.header {
  background: linear-gradient(90deg, #f8fafc 0%, #f1f5f9 100%);
  border-bottom: 2px solid #e2e8f0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08);
}

.header-left {
  display: flex;
  align-items: center;
}

.collapse-btn {
  font-size: 20px;
  cursor: pointer;
  margin-right: 20px;
  transition: all 0.3s;
  color: #1e40af;
  padding: 8px;
  border-radius: 6px;
  
  &:hover {
    background: rgba(30, 64, 175, 0.1);
    transform: scale(1.1);
  }
}

.header-right {
  display: flex;
  align-items: center;
  gap: 16px;
}

.health-check {
  margin-right: 8px;
}

.user-info {
  display: flex;
  align-items: center;
  cursor: pointer;
}

.user-info span {
  margin-left: 8px;
}

.main {
  background: linear-gradient(135deg, #f8fafc 0%, #f1f5f9 100%);
  padding: 24px;
  min-height: calc(100vh - 64px);
}

/* 路由过渡动画 */
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.3s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style> 
