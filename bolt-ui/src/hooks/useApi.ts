import { useState, useEffect, useCallback } from 'react'
import type { ApiStatus, ToolInfo } from '../types'

export function useApi(apiBase: string) {
  const [status, setStatus] = useState<ApiStatus | null>(null)
  const [tools, setTools] = useState<ToolInfo[]>([])
  const [connected, setConnected] = useState(false)

  const fetchStatus = useCallback(async () => {
    try {
      const res = await fetch(`${apiBase}/api/status`)
      if (res.ok) {
        const data = await res.json()
        setStatus(data)
        setConnected(true)
      } else {
        setConnected(false)
      }
    } catch {
      setConnected(false)
    }
  }, [apiBase])

  const fetchTools = useCallback(async () => {
    try {
      const res = await fetch(`${apiBase}/api/tools`)
      if (res.ok) {
        const data = await res.json()
        setTools(data.tools || [])
      }
    } catch {
      // ignore
    }
  }, [apiBase])

  useEffect(() => {
    const initialFetch = window.setTimeout(() => {
      void fetchStatus()
      void fetchTools()
    }, 0)
    const interval = window.setInterval(() => {
      void fetchStatus()
    }, 3000)
    return () => {
      window.clearTimeout(initialFetch)
      window.clearInterval(interval)
    }
  }, [fetchStatus, fetchTools])

  return { status, tools, connected, refresh: fetchStatus }
}
