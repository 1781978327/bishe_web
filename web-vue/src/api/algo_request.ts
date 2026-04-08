import axios from 'axios'
import type { AxiosInstance, AxiosRequestConfig } from 'axios'
import { ElMessage } from 'element-plus'

// 创建axios实例
const service: AxiosInstance = axios.create({
  baseURL: import.meta.env.VITE_ALGO_URL,
  timeout: 60000 // 60秒超时
})

// 请求拦截器
service.interceptors.request.use(
  (config) => {
    // 从localStorage获取token
    const token = localStorage.getItem('token')
    if (token) {
      config.headers.Authorization = `Bearer ${token}`
    }
    return config
  },
  (error) => {
    return Promise.reject(error)
  }
)

// 响应拦截器
service.interceptors.response.use(
  (response) => {
    const { code, msg, data } = response.data

    // 成功
    if (code === 200) {
      return data
    }

    // 失败
    ElMessage.error(msg || '请求失败')
    return Promise.reject(new Error(msg || '请求失败'))
  },
  (error) => {
    ElMessage.error(error.message || '请求失败')
    return Promise.reject(error)
  }
)

// 封装请求方法
const algoRequest = {
  get<T = any>(url: string, config?: AxiosRequestConfig): Promise<T> {
    return service.get(url, config)
  },

  post<T = any>(url: string, data?: any, config?: AxiosRequestConfig): Promise<T> {
    return service.post(url, data, config)
  },
}

export { algoRequest }
