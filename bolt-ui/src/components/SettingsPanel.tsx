import { X, Key, Server, Bot } from 'lucide-react'
import { useState } from 'react'

const PROVIDERS = [
  { id: 'ollama', name: 'Ollama', desc: 'Local, no API key', keyEnv: '', models: ['qwen3:8b', 'llama3:8b', 'codellama:13b'] },
  { id: 'openai', name: 'OpenAI', desc: 'GPT-4o', keyEnv: 'OPENAI_API_KEY', models: ['gpt-4o', 'gpt-4o-mini', 'o3-mini'] },
  { id: 'claude', name: 'Claude', desc: 'Sonnet, Opus', keyEnv: 'ANTHROPIC_API_KEY', models: ['claude-sonnet-4-20250514', 'claude-opus-4-20250514'] },
  { id: 'gemini', name: 'Gemini', desc: 'Flash, Pro', keyEnv: 'GEMINI_API_KEY', models: ['gemini-2.0-flash', 'gemini-2.0-pro'] },
  { id: 'groq', name: 'Groq', desc: 'Fast inference', keyEnv: 'GROQ_API_KEY', models: ['llama-3.3-70b-versatile', 'mixtral-8x7b-32768'] },
  { id: 'deepseek', name: 'DeepSeek', desc: 'V3, R1', keyEnv: 'DEEPSEEK_API_KEY', models: ['deepseek-chat', 'deepseek-reasoner'] },
  { id: 'qwen', name: '通义千问', desc: 'Alibaba Qwen', keyEnv: 'DASHSCOPE_API_KEY', models: ['qwen-plus', 'qwen-max', 'qwen-turbo'] },
  { id: 'zhipu', name: '智谱 GLM', desc: 'GLM-4', keyEnv: 'ZHIPU_API_KEY', models: ['glm-4-flash', 'glm-4-plus'] },
  { id: 'moonshot', name: '月之暗面', desc: 'Kimi', keyEnv: 'MOONSHOT_API_KEY', models: ['moonshot-v1-128k', 'moonshot-v1-32k'] },
  { id: 'baichuan', name: '百川', desc: 'Baichuan', keyEnv: 'BAICHUAN_API_KEY', models: ['Baichuan4', 'Baichuan3-Turbo'] },
  { id: 'doubao', name: '豆包', desc: 'ByteDance', keyEnv: 'VOLC_API_KEY', models: ['doubao-pro-32k', 'doubao-lite-32k'] },
]

interface Props {
  open: boolean
  onClose: () => void
}

export default function SettingsPanel({ open, onClose }: Props) {
  const [provider, setProvider] = useState('moonshot')
  const [model, setModel] = useState('moonshot-v1-128k')
  const [apiKey, setApiKey] = useState('')

  const selectedProvider = PROVIDERS.find(p => p.id === provider)

  if (!open) return null

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm" onClick={onClose}>
      <div className="w-[520px] max-h-[80vh] bg-[#12121a] border border-[#2a2a3a] rounded-2xl shadow-2xl overflow-hidden" onClick={e => e.stopPropagation()}>
        {/* Header */}
        <div className="flex items-center justify-between px-6 py-4 border-b border-[#2a2a3a]">
          <h2 className="text-lg font-semibold text-white">Settings</h2>
          <button onClick={onClose} className="p-1.5 rounded-lg hover:bg-[#2a2a3a] text-gray-400 hover:text-white transition-colors">
            <X size={18} />
          </button>
        </div>

        <div className="p-6 space-y-6 overflow-y-auto max-h-[60vh]">
          {/* Provider Selection */}
          <div>
            <label className="flex items-center gap-2 text-sm font-medium text-gray-300 mb-3">
              <Server size={14} />
              LLM Provider
            </label>
            <div className="grid grid-cols-2 gap-2">
              {PROVIDERS.map(p => (
                <button
                  key={p.id}
                  onClick={() => { setProvider(p.id); setModel(p.models[0]) }}
                  className={`text-left px-3 py-2.5 rounded-lg border text-sm transition-all ${
                    provider === p.id
                      ? 'border-indigo-500 bg-indigo-500/10 text-white'
                      : 'border-[#2a2a3a] text-gray-400 hover:border-gray-600 hover:text-gray-200'
                  }`}
                >
                  <div className="font-medium">{p.name}</div>
                  <div className="text-xs text-gray-500 mt-0.5">{p.desc}</div>
                </button>
              ))}
            </div>
          </div>

          {/* Model Selection */}
          <div>
            <label className="flex items-center gap-2 text-sm font-medium text-gray-300 mb-2">
              <Bot size={14} />
              Model
            </label>
            <select
              value={model}
              onChange={e => setModel(e.target.value)}
              className="w-full bg-[#1a1a26] border border-[#2a2a3a] rounded-lg px-3 py-2.5 text-sm text-white outline-none focus:border-indigo-500/50"
            >
              {selectedProvider?.models.map(m => (
                <option key={m} value={m}>{m}</option>
              ))}
            </select>
          </div>

          {/* API Key */}
          {selectedProvider?.keyEnv && (
            <div>
              <label className="flex items-center gap-2 text-sm font-medium text-gray-300 mb-2">
                <Key size={14} />
                API Key
                <span className="text-xs text-gray-500 font-normal">({selectedProvider.keyEnv})</span>
              </label>
              <input
                type="password"
                value={apiKey}
                onChange={e => setApiKey(e.target.value)}
                placeholder={`Enter your ${selectedProvider.keyEnv}`}
                className="w-full bg-[#1a1a26] border border-[#2a2a3a] rounded-lg px-3 py-2.5 text-sm text-white placeholder-gray-600 outline-none focus:border-indigo-500/50"
              />
              <p className="text-xs text-gray-600 mt-1.5">
                Set via environment variable: export {selectedProvider.keyEnv}=your-key
              </p>
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="px-6 py-4 border-t border-[#2a2a3a] flex justify-end gap-3">
          <button onClick={onClose} className="px-4 py-2 text-sm text-gray-400 hover:text-white transition-colors">
            Cancel
          </button>
          <button
            onClick={() => { /* TODO: save settings */ onClose() }}
            className="px-4 py-2 text-sm bg-indigo-600 text-white rounded-lg hover:bg-indigo-500 transition-colors"
          >
            Save Changes
          </button>
        </div>
      </div>
    </div>
  )
}
