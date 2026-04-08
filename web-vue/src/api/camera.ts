import {request} from './request'
import type {
  Camera,
  CameraQueryParams,
  CameraListResult,
  CameraCreateParams,
  CameraUpdateParams,
} from '@/types/camera'

// 获取摄像头列表
export const getCameraList = (params: CameraQueryParams) => {
  // 映射前端参数到后端参数
  const backendParams = {
    page: params.current - 1, // 后端从0开始，前端从1开始
    size: params.size,
    name: params.name,
    location: params.location,
    status: params.status,
    enabled: undefined // 如果需要筛选启用状态
  }
  return request.get<any>('/camera', { params: backendParams }).then(data => {
    // 将Spring Boot分页格式转换为前端期望的格式
    return {
      records: data.content,
      total: data.totalElements,
      size: data.size,
      current: data.number + 1 // 转换回前端从1开始的页码
    }
  })
}

// 获取摄像头详情
export const getCameraById = (id: number) => {
  return request.get<Camera>(`/camera/${id}`)
}

// 创建摄像头
export const createCamera = (data: CameraCreateParams) => {
  return request.post<Camera>('/camera', data)
}

// 更新摄像头
export const updateCamera = (data: CameraUpdateParams) => {
  return request.put(`/camera/${data.id}`, data)
}

// 删除摄像头
export const deleteCamera = (id: number) => {
  return request.delete(`/camera/${id}`)
}

// 更新摄像头状态
export const updateCameraStatus = (id: number, status: number) => {
  return request.put<void>(`/camera/${id}/status`, null, { params: { status } })
}