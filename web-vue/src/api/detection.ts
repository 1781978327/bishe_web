import { request } from './request'
import type { PageVO } from '@/types/common'

// 检测记录接口

/**
 * 检测记录类型
 */
export interface DetectionRecord {
  id: number
  cameraId: number
  cameraName: string
  imageUrl: string
  detectionTime: string
  detectionResult: string
  processed: number
  processContent?: string
  processImageUrl?: string
  processTime?: string
  createTime: string
  aiDescription?: string
  processNotes?: string
}

/**
 * 添加检测记录参数
 */
export interface DetectionRecordAddParams {
  cameraId: number
  imageBase64: string
  detectionTime?: string
  detectionResult: string
}

/**
 * 查询检测记录参数
 */
export interface DetectionRecordQueryParams {
  current: number
  size: number
  cameraId?: number
  startTime?: string
  endTime?: string
  processed?: number
  /** 事件类型：video/video异常/video打架/video持刀, sound/声音异常/声音尖叫/声音玻璃破碎/声音打斗, env/环境异常 */
  eventType?: string
}

/**
 * 处理检测记录参数
 */
export interface DetectionRecordProcessParams {
  id: number
  processed: number
  processContent?: string
  processImageBase64?: string
}

/**
 * 添加检测记录
 */
export function addDetectionRecord(data: DetectionRecordAddParams) {
  return request.post<boolean>('/detection/record', data)
}

/**
 * 分页查询检测记录
 */
export function pageDetectionRecords(params: DetectionRecordQueryParams) {
  return request.get<PageVO<DetectionRecord>>('/detection/record/page', { params })
}

/**
 * 获取检测记录详情
 */
export function getDetectionRecordById(id: number) {
  return request.get<DetectionRecord>(`/detection/record/${id}`)
}

/**
 * 更新检测记录处理状态
 */
export function updateProcessStatus(id: number, processed: number) {
  return request.put<boolean>(`/detection/record/${id}/process`, null, { params: { processed } })
}

/**
 * 处理检测记录（带内容和照片）
 */
export function processDetectionRecord(data: DetectionRecordProcessParams) {
  return request.put<boolean>('/detection/record/process', data)
}

/**
 * 删除检测记录
 */
export function deleteDetectionRecord(id: number) {
  return request.delete<boolean>(`/detection/record/${id}`)
}

/**
 * 批量删除检测记录
 */
export function batchDeleteDetectionRecords(ids: number[]) {
  return request.delete<boolean>('/detection/record/batch', { data: ids })
}

/**
 * 清空所有检测记录（当前用户可见的）
 */
export function clearAllDetectionRecords() {
  return request.delete<boolean>('/detection/record/clear-all')
} 