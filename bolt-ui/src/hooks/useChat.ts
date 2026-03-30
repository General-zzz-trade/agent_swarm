import { useState, useCallback, useRef } from 'react'
import type { Message } from '../types'

let messageId = 0
const nextId = () => `msg-${++messageId}-${Date.now()}`

export function useChat(apiBase: string) {
  const [messages, setMessages] = useState<Message[]>([])
  const [isStreaming, setIsStreaming] = useState(false)
  const abortRef = useRef<AbortController | null>(null)

  const sendMessage = useCallback(async (text: string) => {
    const userMsg: Message = { id: nextId(), role: 'user', content: text, timestamp: Date.now() }
    const assistantMsg: Message = { id: nextId(), role: 'assistant', content: '', isStreaming: true, timestamp: Date.now() }

    setMessages(prev => [...prev, userMsg, assistantMsg])
    setIsStreaming(true)

    const controller = new AbortController()
    abortRef.current = controller

    try {
      const response = await fetch(`${apiBase}/api/chat/stream`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ message: text }),
        signal: controller.signal,
      })

      if (!response.ok) {
        const err = await response.text()
        setMessages(prev => prev.map(m => m.id === assistantMsg.id
          ? { ...m, content: `Error: ${err}`, isStreaming: false } : m))
        setIsStreaming(false)
        return
      }

      const reader = response.body?.getReader()
      const decoder = new TextDecoder()
      let fullText = ''

      if (reader) {
        while (true) {
          const { done, value } = await reader.read()
          if (done) break

          const chunk = decoder.decode(value, { stream: true })
          const lines = chunk.split('\n')

          for (const line of lines) {
            if (line.startsWith('data: ')) {
              const data = line.slice(6)
              if (data === '[DONE]') continue
              try {
                const token = JSON.parse(data)
                if (typeof token === 'string') {
                  fullText += token
                } else {
                  fullText += data
                }
              } catch {
                fullText += data
              }

              setMessages(prev => prev.map(m => m.id === assistantMsg.id
                ? { ...m, content: fullText } : m))
            }
          }
        }
      }

      setMessages(prev => prev.map(m => m.id === assistantMsg.id
        ? { ...m, content: fullText || '(No response)', isStreaming: false } : m))
    } catch (err: unknown) {
      if (err instanceof Error && err.name !== 'AbortError') {
        setMessages(prev => prev.map(m => m.id === assistantMsg.id
          ? { ...m, content: `Error: ${err instanceof Error ? err.message : 'Unknown error'}`, isStreaming: false } : m))
      }
    } finally {
      setIsStreaming(false)
      abortRef.current = null
    }
  }, [apiBase])

  const cancelStream = useCallback(() => {
    abortRef.current?.abort()
    setIsStreaming(false)
  }, [])

  const clearMessages = useCallback(async () => {
    await fetch(`${apiBase}/api/clear`, { method: 'POST' })
    setMessages([])
  }, [apiBase])

  return { messages, isStreaming, sendMessage, cancelStream, clearMessages }
}
