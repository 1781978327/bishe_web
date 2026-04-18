import { request } from './request'

export interface ModelProfile {
  id: number
  username: string
  baseName: string
  builtin?: boolean
  source?: 'builtin' | 'upload'
  builtinKey?: string
  modelObjectKey?: string
  labelObjectKey?: string
  modelUrl?: string
  labelUrl?: string
  modelPath?: string
  labelPath?: string
  selected: boolean
  ready: boolean
  createTime?: string
  updateTime?: string
}

export const listModelProfiles = () => {
  return request.get<ModelProfile[]>('/model/list')
}

export const getCurrentModelProfile = () => {
  return request.get<ModelProfile | null>('/model/current')
}

export const selectModelProfile = (id: number) => {
  return request.post('/model/select', null, {
    params: { id }
  })
}
