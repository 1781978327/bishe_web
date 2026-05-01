const PROJECT_DEFAULT_STREAM_HOST = '10.70.15.69'
const LOOPBACK_HOSTS = new Set(['127.0.0.1', 'localhost', '0.0.0.0', '::1'])

const readEnvStreamHost = (): string => {
  return ((import.meta.env.VITE_STREAM_HOST as string | undefined) || '').trim()
}

export const isLoopbackHost = (host: string): boolean => {
  return LOOPBACK_HOSTS.has(host.trim().toLowerCase())
}

export const getCurrentPageHost = (): string => {
  if (typeof window === 'undefined') return ''
  return window.location.hostname.trim()
}

export const getPreferredStreamHost = (): string => {
  const envHost = readEnvStreamHost()
  if (envHost) return envHost

  const pageHost = getCurrentPageHost()
  if (pageHost && !isLoopbackHost(pageHost)) {
    return pageHost
  }

  return PROJECT_DEFAULT_STREAM_HOST
}

export const buildDefaultRtspUrl = (path: string): string => {
  const normalizedPath = path.replace(/^\/+/, '')
  return `rtsp://${getPreferredStreamHost()}:8554/${normalizedPath}`
}

export const rewriteRtspUrlForPlayback = (rtspUrl: string, fallbackPath?: string): string => {
  const trimmed = rtspUrl.trim()
  if (!trimmed) {
    return fallbackPath ? buildDefaultRtspUrl(fallbackPath) : ''
  }

  try {
    const url = new URL(trimmed)
    if (url.protocol !== 'rtsp:') {
      return trimmed
    }

    if (isLoopbackHost(url.hostname)) {
      url.hostname = getPreferredStreamHost()
      return url.toString()
    }

    return trimmed
  } catch {
    return fallbackPath ? buildDefaultRtspUrl(fallbackPath) : trimmed
  }
}

export const getKnownLocalStreamHosts = (): string[] => {
  const hosts = new Set<string>(['127.0.0.1', 'localhost', PROJECT_DEFAULT_STREAM_HOST])
  const pageHost = getCurrentPageHost()
  if (pageHost) {
    hosts.add(pageHost)
  }

  const preferredHost = getPreferredStreamHost()
  if (preferredHost) {
    hosts.add(preferredHost)
  }

  return Array.from(hosts)
}
