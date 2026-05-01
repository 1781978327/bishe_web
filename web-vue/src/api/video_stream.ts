import { request } from './request'

export type TrackerBackend = 'bytetrack' | 'deepsort'

export interface UploadedVideoResult {
  fileName: string
  originalFileName: string
  relativePath: string
  absolutePath: string
  url: string
  size: number
}

export interface VideoStreamStartPayload {
  sourcePath: string
  loop?: boolean
  startRtsp?: boolean
  enableInference?: boolean
  track?: boolean
  tracker?: TrackerBackend
}

export interface VideoStreamStopPayload {
  stopRtsp?: boolean
  restartCameraRtsp?: boolean
}

export interface VideoStreamStatus {
  videoMode: boolean
  videoPath: string
  videoLoop: boolean
  inferenceEnabled: boolean
  trackerEnabled: boolean
  trackerBackend: TrackerBackend | ''
  rtspStreaming: boolean
  rtspUrl: string
  running: boolean
}

export const uploadVideo = (file: File) => {
  const formData = new FormData()
  formData.append('file', file)
  return request.post<UploadedVideoResult>('/rknn/video/upload', formData, {
    headers: {
      'Content-Type': 'multipart/form-data',
    },
  })
}

export const startVideoStream = (payload: VideoStreamStartPayload) => {
  return request.post<any>('/rknn/video/start', payload)
}

export const stopVideoStream = (payload?: VideoStreamStopPayload) => {
  return request.post<any>('/rknn/video/stop', payload ?? {})
}

export const getVideoStreamStatus = () => {
  return request.get<VideoStreamStatus>('/rknn/video/status')
}

export const getVideoRtspUrl = async () => {
  const status = await getVideoStreamStatus()
  return status.rtspUrl
}
