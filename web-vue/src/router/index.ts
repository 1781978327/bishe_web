import { createRouter, createWebHistory } from 'vue-router'
import type { RouteRecordRaw } from 'vue-router'
import { useUserStore } from '@/stores/user'

const routes: RouteRecordRaw[] = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/user/Login.vue'),
    meta: {
      title: '登录',
      requiresAuth: false
    }
  },
  {
    path: '/register',
    name: 'Register',
    component: () => import('@/views/user/Register.vue'),
    meta: {
      title: '注册',
      requiresAuth: false
    }
  },
  {
    path: '/',
    component: () => import('@/components/Layout.vue'),
    redirect: '/dashboard',
    children: [
      {
        path: 'dashboard',
        name: 'Dashboard',
        component: () => import('@/views/Dashboard.vue'),
        meta: {
          title: '首页',
          requiresAuth: true
        }
      },
      {
        path: 'profile',
        name: 'Profile',
        component: () => import('@/views/user/Profile.vue'),
        meta: {
          title: '个人中心',
          requiresAuth: true
        }
      },
      {
        path: 'camera',
        name: 'Camera',
        component: () => import('@/views/camera/index.vue'),
        meta: {
          title: '摄像头管理',
          requiresAuth: true
        }
      },
      {
        path: 'model/upload',
        name: 'UploadModel',
        component: () => import('@/views/model/UploadModel.vue'),
        meta: {
          title: '上传模型',
          requiresAuth: true
        }
      },
      {
        path: 'detection/record',
        name: 'DetectionRecords',
        component: () => import('@/views/detection/RecordManagement.vue'),
        meta: {
          title: '检测记录管理',
          requiresAuth: true
        }
      },
      {
        path: 'monitor',
        name: 'Monitor',
        component: () => import('@/views/monitor/index.vue'),
        meta: {
          title: '监控管理',
          requiresAuth: true
        }
      }
    ]
  },
  {
    path: '/admin',
    component: () => import('@/components/Layout.vue'),
    meta: {
      requiresAuth: true,
      requiresAdmin: true
    },
    children: [
      {
        path: '',
        name: 'Admin',
        component: () => import('@/views/admin/Dashboard.vue'),
        meta: {
          title: '管理控制台',
          requiresAuth: true,
          requiresAdmin: true
        }
      },
      {
        path: 'users',
        name: 'UserManagement',
        component: () => import('@/views/admin/UserManagement.vue'),
        meta: {
          title: '用户管理',
          requiresAuth: true,
          requiresAdmin: true
        }
      }
    ]
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

// 路由守卫
router.beforeEach((to, from, next) => {
  const userStore = useUserStore()
  const requiresAuth = to.matched.some(record => record.meta.requiresAuth)
  const requiresAdmin = to.matched.some(record => record.meta.requiresAdmin)

  // 设置页面标题
  document.title = `${to.meta.title} - 嵌入式多目标追踪与智能预警系统`

  if (requiresAuth) {
    if (!userStore.isLoggedIn()) {
      next({ name: 'Login', query: { redirect: to.fullPath } })
      return
    }

    if (requiresAdmin && !userStore.isAdmin()) {
      next({ name: 'Dashboard' })
      return
    }
  }

  next()
})

export default router 