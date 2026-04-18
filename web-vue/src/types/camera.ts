// 摄像头设备
export interface Camera {
  id: number
  name: string
  location: string
  rtspUrl: string
  status: number
  isEnabled: boolean
  detectionEnabled: boolean
  createTime?: string
  updateTime?: string
  // 兼容前端现有字段
  userId?: number
  userName?: string
  userRealName?: string
  streaming?: boolean  // 新增streaming属性，表示摄像头是否正在流式传输
}

// 摄像头状态
export enum CameraStatus {
  OFFLINE = 0,
  ONLINE = 1
}

// 摄像头列表查询参数
export interface CameraQueryParams {
  current: number
  size: number
  name?: string
  location?: string
  status?: number
}

// 摄像头列表响应
export interface CameraListResult {
  records: Camera[]
  total: number
  size: number
  current: number
}

// 创建摄像头参数
export interface CameraCreateParams {
  name: string
  location: string
  rtspUrl: string
}

// 更新摄像头参数
export interface CameraUpdateParams {
  id: number
  name: string
  location: string
  rtspUrl: string
  status?: number
} 
