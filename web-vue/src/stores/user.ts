import { defineStore } from 'pinia'
import { ref } from 'vue'
import type { UserInfo, LoginResponse } from '@/types/user'
import { login as userLogin, logout as userLogout, updatePassword as updateUserPassword, register as userRegister } from '@/api/user'
import { ElMessage } from 'element-plus'
import router from '@/router'

export const useUserStore = defineStore('user', () => {
  // 用户信息
  const userInfo = ref<UserInfo | null>(null)
  // token
  const token = ref<string | null>(null)

  const normalizeUserInfo = (info: UserInfo | null) => {
    if (!info) return info
    if (info.avatarUrl && !info.avatarUrl.startsWith('http') && !info.avatarUrl.startsWith('/')) {
      info.avatarUrl = `/${info.avatarUrl}`
    }
    return info
  }

  // 从localStorage中恢复用户信息
  const initUserInfo = () => {
    const storedUserInfo = localStorage.getItem('userInfo')
    if (storedUserInfo) {
      try {
        const data = JSON.parse(storedUserInfo)
        userInfo.value = normalizeUserInfo(data.userInfo)
        token.value = data.token
      } catch (error) {
        console.error('解析用户信息失败:', error)
        localStorage.removeItem('userInfo')
      }
    }
  }

  // 登录
  const login = async (username: string, password: string) => {
    try {
      const loginResponse = await userLogin(username, password)
      
      userInfo.value = normalizeUserInfo(loginResponse.userInfo)
      token.value = loginResponse.token
      
      // 存储用户信息和token到localStorage
      localStorage.setItem('userInfo', JSON.stringify({
        userInfo: userInfo.value,
        token: loginResponse.token
      }))
      
      ElMessage.success('登录成功')
      
      // 根据角色跳转到不同页面
      if (loginResponse.userInfo.role === 1) {
        router.push('/admin')
      } else {
        router.push('/dashboard')
      }
    } catch (error: any) {
      ElMessage.error(error.message || '登录失败')
      throw error
    }
  }

  // 退出登录
  const logout = async (sendRequest: boolean = true) => {
    try {
      // 仅在需要时发送请求
      if (sendRequest) {
        try {
          await userLogout()
        } catch (error) {
          console.error('退出请求失败，但会继续清除本地登录状态')
        }
      }
      
      // 无论请求是否成功，都清除本地状态
      userInfo.value = null
      token.value = null
      // 清除localStorage中的用户信息
      localStorage.removeItem('userInfo')
      
      if (sendRequest) {
        ElMessage.success('退出成功')
      }
      
      router.push('/login')
    } catch (error) {
      // 即使出错也确保本地状态被清除
      userInfo.value = null
      token.value = null
      localStorage.removeItem('userInfo')
      router.push('/login')
      
      if (sendRequest) {
        ElMessage.error('退出失败')
        throw error
      }
    }
  }

  // 判断是否登录
  const isLoggedIn = () => {
    try {
      const storedUserInfo = localStorage.getItem('userInfo')
      if (!storedUserInfo) return false
      
      const data = JSON.parse(storedUserInfo)
      return !!(data.token && data.userInfo)
    } catch (error) {
      console.error('解析用户信息失败:', error)
      localStorage.removeItem('userInfo')
      return false
    }
  }

  // 判断是否是管理员
  const isAdmin = () => {
    try {
      const storedUserInfo = localStorage.getItem('userInfo')
      if (!storedUserInfo) return false
      
      const data = JSON.parse(storedUserInfo)
      return data.userInfo?.role === 1
    } catch (error) {
      console.error('解析用户信息失败:', error)
      localStorage.removeItem('userInfo')
      return false
    }
  }

  // 修改密码
  const changePassword = async (data: { id: number; oldPassword: string; newPassword: string }) => {
    await updateUserPassword(data)
  }

  // 注册
  const register = async (data: { username: string; password: string; realName: string; phone: string; email: string }) => {
    try {
      await userRegister(data)
      ElMessage.success('注册成功，请登录')
      return true
    } catch (error: any) {
      ElMessage.error(error.message || '注册失败')
      throw error
    }
  }

  return {
    userInfo,
    token,
    initUserInfo,
    login,
    logout,
    isLoggedIn,
    isAdmin,
    changePassword,
    register
  }
}) 