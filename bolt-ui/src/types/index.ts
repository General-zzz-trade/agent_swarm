export interface Message {
  id: string
  role: 'user' | 'assistant' | 'system' | 'tool'
  content: string
  toolName?: string
  toolArgs?: string
  isStreaming?: boolean
  timestamp: number
}

export interface ToolInfo {
  name: string
  description: string
  inputSchema: Record<string, unknown>
}

export interface ApiStatus {
  model: string
  debug: boolean
  port: number
  busy: boolean
}

export interface TokenUsage {
  input_tokens: number
  output_tokens: number
}

export interface Session {
  id: string
  last_message: string
  message_count: number
  modified_at: string
}

export interface Settings {
  provider: string
  model: string
  apiKey: string
}
