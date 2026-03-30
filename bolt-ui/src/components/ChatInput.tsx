import { useState, useRef, useEffect } from 'react'
import { Send, Square } from 'lucide-react'

interface Props {
  onSend: (message: string) => void
  onCancel: () => void
  isStreaming: boolean
  disabled: boolean
}

export default function ChatInput({ onSend, onCancel, isStreaming, disabled }: Props) {
  const [input, setInput] = useState('')
  const textareaRef = useRef<HTMLTextAreaElement>(null)

  useEffect(() => {
    if (!isStreaming) textareaRef.current?.focus()
  }, [isStreaming])

  const handleSubmit = () => {
    const text = input.trim()
    if (!text || disabled) return
    onSend(text)
    setInput('')
    if (textareaRef.current) textareaRef.current.style.height = 'auto'
  }

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      if (isStreaming) return
      handleSubmit()
    }
  }

  const handleInput = () => {
    const el = textareaRef.current
    if (el) {
      el.style.height = 'auto'
      el.style.height = Math.min(el.scrollHeight, 200) + 'px'
    }
  }

  return (
    <div className="border-t border-[#2a2a3a] bg-[#0a0a0f] p-4">
      <div className="max-w-3xl mx-auto relative">
        <div className="flex items-end gap-2 bg-[#12121a] border border-[#2a2a3a] rounded-xl p-2 focus-within:border-indigo-500/50 transition-colors">
          <textarea
            ref={textareaRef}
            value={input}
            onChange={e => setInput(e.target.value)}
            onKeyDown={handleKeyDown}
            onInput={handleInput}
            placeholder={isStreaming ? 'Generating...' : 'Ask anything... (Shift+Enter for new line)'}
            disabled={isStreaming}
            rows={1}
            className="flex-1 bg-transparent text-[15px] text-[#e8e8f0] placeholder-gray-500 resize-none outline-none px-2 py-1.5 max-h-[200px]"
          />

          {isStreaming ? (
            <button
              onClick={onCancel}
              className="p-2 rounded-lg bg-red-500/20 text-red-400 hover:bg-red-500/30 transition-colors flex-shrink-0"
              title="Stop generating"
            >
              <Square size={18} />
            </button>
          ) : (
            <button
              onClick={handleSubmit}
              disabled={!input.trim() || disabled}
              className="p-2 rounded-lg bg-indigo-600 text-white hover:bg-indigo-500 disabled:opacity-30 disabled:cursor-not-allowed transition-colors flex-shrink-0"
              title="Send message"
            >
              <Send size={18} />
            </button>
          )}
        </div>

        <div className="flex items-center justify-between mt-2 px-1">
          <span className="text-xs text-gray-600">
            @file to reference files · Shift+Enter for new line
          </span>
          <span className="text-xs text-gray-600">
            Enter to send
          </span>
        </div>
      </div>
    </div>
  )
}
