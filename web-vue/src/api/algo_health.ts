import { algoRequest } from './algo_request'

export interface HealthResponse {
  status: string
  service: string
  version: string
}

// 检查算法服务健康状态
export const checkAlgoHealth = () => {
  return algoRequest.get<HealthResponse>('/health/health_check')
} 