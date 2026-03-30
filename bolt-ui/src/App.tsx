import { useState, useRef, useEffect } from 'react'
import { useChat } from './hooks/useChat'
import { useApi } from './hooks/useApi'
import Sidebar from './components/Sidebar'
import MessageBubble from './components/MessageBubble'
import ChatInput from './components/ChatInput'
import WelcomeScreen from './components/WelcomeScreen'
import SettingsPanel from './components/SettingsPanel'

const API_BASE = window.location.port === '3000' ? 'http://localhost:9090' : ''

export default function App() {
  const { messages, isStreaming, sendMessage, cancelStream, clearMessages } = useChat(API_BASE)
  const { status, tools, connected } = useApi(API_BASE)
  const [settingsOpen, setSettingsOpen] = useState(false)
  const messagesEndRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages])

  return (
    <div className="flex h-screen bg-[#0a0a0f]">
      {/* Sidebar */}
      <Sidebar
        status={status}
        tools={tools}
        connected={connected}
        onClear={clearMessages}
        onOpenSettings={() => setSettingsOpen(true)}
      />

      {/* Main Chat Area */}
      <div className="flex-1 flex flex-col min-w-0">
        {/* Messages */}
        <div className="flex-1 overflow-y-auto">
          {messages.length === 0 ? (
            <WelcomeScreen onSend={sendMessage} />
          ) : (
            <div className="max-w-3xl mx-auto py-4">
              {messages.map(msg => (
                <MessageBubble key={msg.id} message={msg} />
              ))}
              <div ref={messagesEndRef} />
            </div>
          )}
        </div>

        {/* Input */}
        <ChatInput
          onSend={sendMessage}
          onCancel={cancelStream}
          isStreaming={isStreaming}
          disabled={!connected}
        />
      </div>

      {/* Settings Modal */}
      <SettingsPanel open={settingsOpen} onClose={() => setSettingsOpen(false)} />
    </div>
  )
}
