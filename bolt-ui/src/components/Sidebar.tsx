import { Plus, Settings, Wrench, Cpu, Zap, ChevronDown } from 'lucide-react'
import { useState } from 'react'
import type { ToolInfo, ApiStatus } from '../types'

interface Props {
  status: ApiStatus | null
  tools: ToolInfo[]
  connected: boolean
  onClear: () => void
  onOpenSettings: () => void
}

export default function Sidebar({ status, tools, connected, onClear, onOpenSettings }: Props) {
  const [showTools, setShowTools] = useState(false)

  return (
    <div className="w-72 bg-[#0a0a0f] border-r border-[#2a2a3a] flex flex-col h-full">
      {/* Header */}
      <div className="p-4 border-b border-[#2a2a3a]">
        <div className="flex items-center gap-2.5">
          <div className="w-9 h-9 rounded-xl bg-indigo-600/20 flex items-center justify-center">
            <Zap size={18} className="text-indigo-400" />
          </div>
          <div>
            <h1 className="text-base font-semibold text-white">Bolt</h1>
            <p className="text-xs text-gray-500">AI Coding Agent</p>
          </div>
        </div>
      </div>

      {/* Status */}
      <div className="px-4 py-3 border-b border-[#2a2a3a]">
        <div className="flex items-center gap-2 mb-2">
          <div className={`w-2 h-2 rounded-full ${connected ? 'bg-green-500' : 'bg-red-500'}`} />
          <span className="text-xs text-gray-400">{connected ? 'Connected' : 'Disconnected'}</span>
        </div>
        {status && (
          <div className="space-y-1">
            <div className="flex items-center gap-2 text-xs">
              <Cpu size={12} className="text-gray-500" />
              <span className="text-gray-400 truncate">{status.model}</span>
            </div>
          </div>
        )}
      </div>

      {/* Actions */}
      <div className="p-3 space-y-1">
        <button
          onClick={onClear}
          className="w-full flex items-center gap-2.5 px-3 py-2 rounded-lg text-sm text-gray-400 hover:text-white hover:bg-[#1a1a26] transition-colors"
        >
          <Plus size={16} />
          <span>New Chat</span>
        </button>

        <button
          onClick={onOpenSettings}
          className="w-full flex items-center gap-2.5 px-3 py-2 rounded-lg text-sm text-gray-400 hover:text-white hover:bg-[#1a1a26] transition-colors"
        >
          <Settings size={16} />
          <span>Settings</span>
        </button>
      </div>

      {/* Tools */}
      <div className="flex-1 overflow-y-auto px-3">
        <button
          onClick={() => setShowTools(!showTools)}
          className="w-full flex items-center justify-between px-3 py-2 text-xs text-gray-500 hover:text-gray-300 transition-colors"
        >
          <div className="flex items-center gap-2">
            <Wrench size={12} />
            <span>Tools ({tools.length})</span>
          </div>
          <ChevronDown size={12} className={`transition-transform ${showTools ? 'rotate-180' : ''}`} />
        </button>

        {showTools && (
          <div className="space-y-0.5 pb-3">
            {tools.map(tool => (
              <div
                key={tool.name}
                className="px-3 py-1.5 rounded-md text-xs text-gray-500 hover:text-gray-300 hover:bg-[#1a1a26] transition-colors cursor-default"
                title={tool.description}
              >
                {tool.name}
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Footer */}
      <div className="p-3 border-t border-[#2a2a3a]">
        <div className="text-[10px] text-gray-600 text-center">
          Bolt v0.4.0 · {tools.length} tools · {status?.model || 'No model'}
        </div>
      </div>
    </div>
  )
}
