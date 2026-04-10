import { request } from './request'

export interface ModelProfile {
  id: number
  username: string
  baseName: string
  modelObjectKey?: string
  labelObjectKey?: string
  yamlObjectKey?: string
  modelUrl?: string
  labelUrl?: string
  yamlUrl?: string
  selected: boolean
  ready: boolean
  createTime?: string
  updateTime?: string
  lastSelectedTime?: string
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
