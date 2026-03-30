import ReactMarkdown from 'react-markdown'
import remarkGfm from 'remark-gfm'
import rehypeHighlight from 'rehype-highlight'
import { User, Bot, Copy, Check } from 'lucide-react'
import { useState } from 'react'
import type { Message } from '../types'

function CopyButton({ text }: { text: string }) {
  const [copied, setCopied] = useState(false)
  return (
    <button
      onClick={() => { navigator.clipboard.writeText(text); setCopied(true); setTimeout(() => setCopied(false), 2000) }}
      className="absolute top-2 right-2 p-1.5 rounded-md bg-black/40 hover:bg-black/60 text-gray-400 hover:text-white transition-all opacity-0 group-hover:opacity-100"
    >
      {copied ? <Check size={14} /> : <Copy size={14} />}
    </button>
  )
}

export default function MessageBubble({ message }: { message: Message }) {
  const isUser = message.role === 'user'
  const isSystem = message.role === 'system'

  if (isSystem) {
    return (
      <div className="flex justify-center py-2 animate-[slide-in_0.2s_ease-out]">
        <span className="text-xs text-gray-500 bg-gray-800/50 px-3 py-1 rounded-full">{message.content}</span>
      </div>
    )
  }

  return (
    <div className={`flex gap-3 py-4 px-4 animate-[slide-in_0.2s_ease-out] ${isUser ? 'justify-end' : ''}`}>
      {!isUser && (
        <div className="w-8 h-8 rounded-lg bg-indigo-500/20 flex items-center justify-center flex-shrink-0 mt-0.5">
          <Bot size={16} className="text-indigo-400" />
        </div>
      )}

      <div className={`max-w-[75%] ${isUser
        ? 'bg-indigo-600 text-white rounded-2xl rounded-br-md px-4 py-2.5'
        : 'bg-[#12121a] border border-[#2a2a3a] rounded-2xl rounded-bl-md px-4 py-3'
      }`}>
        {isUser ? (
          <p className="text-[15px] leading-relaxed whitespace-pre-wrap">{message.content}</p>
        ) : (
          <div className="markdown text-[15px] leading-relaxed relative group">
            <ReactMarkdown
              remarkPlugins={[remarkGfm]}
              rehypePlugins={[rehypeHighlight]}
              components={{
                pre: ({ children, ...props }) => (
                  <div className="relative group">
                    <pre {...props}>{children}</pre>
                    <CopyButton text={String(children || '')} />
                  </div>
                ),
              }}
            >
              {message.content}
            </ReactMarkdown>
            {message.isStreaming && (
              <span className="inline-block w-2 h-4 bg-indigo-400 ml-0.5 animate-[pulse-slow_1s_ease-in-out_infinite]" />
            )}
          </div>
        )}
      </div>

      {isUser && (
        <div className="w-8 h-8 rounded-lg bg-indigo-600/30 flex items-center justify-center flex-shrink-0 mt-0.5">
          <User size={16} className="text-indigo-300" />
        </div>
      )}
    </div>
  )
}
