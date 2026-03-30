import { Zap, Code, Search, GitBranch, Globe, Calculator, Terminal, FileText } from 'lucide-react'

const SUGGESTIONS = [
  { icon: <Code size={16} />, text: 'Read src/main.cpp and explain the architecture', color: 'text-blue-400' },
  { icon: <Search size={16} />, text: 'Search for TODO comments in the codebase', color: 'text-green-400' },
  { icon: <GitBranch size={16} />, text: 'Show recent git commits and summarize changes', color: 'text-purple-400' },
  { icon: <Globe size={16} />, text: 'Search the web for React best practices 2026', color: 'text-orange-400' },
  { icon: <Calculator size={16} />, text: 'Calculate (2^10 + 3^5) * 2', color: 'text-pink-400' },
  { icon: <FileText size={16} />, text: 'Create a Python script that sorts a CSV file', color: 'text-cyan-400' },
]

interface Props {
  onSend: (message: string) => void
}

export default function WelcomeScreen({ onSend }: Props) {
  return (
    <div className="flex-1 flex flex-col items-center justify-center px-4">
      <div className="w-16 h-16 rounded-2xl bg-indigo-600/20 flex items-center justify-center mb-6">
        <Zap size={32} className="text-indigo-400" />
      </div>

      <h2 className="text-2xl font-semibold text-white mb-2">Bolt AI Agent</h2>
      <p className="text-gray-500 text-center max-w-md mb-10">
        Ask me anything about code. I can read files, search code, run commands, browse the web, and write code.
      </p>

      <div className="grid grid-cols-2 gap-3 max-w-lg w-full">
        {SUGGESTIONS.map((s, i) => (
          <button
            key={i}
            onClick={() => onSend(s.text)}
            className="flex items-start gap-3 p-3.5 rounded-xl border border-[#2a2a3a] bg-[#12121a] hover:bg-[#1a1a26] hover:border-[#3a3a4a] text-left transition-all group"
          >
            <span className={`${s.color} mt-0.5 opacity-60 group-hover:opacity-100 transition-opacity`}>{s.icon}</span>
            <span className="text-sm text-gray-400 group-hover:text-gray-200 transition-colors leading-snug">{s.text}</span>
          </button>
        ))}
      </div>

      <div className="mt-10 flex items-center gap-6 text-xs text-gray-600">
        <span className="flex items-center gap-1.5"><Terminal size={12} /> 27 Tools</span>
        <span className="flex items-center gap-1.5"><Zap size={12} /> 11 Providers</span>
        <span className="flex items-center gap-1.5"><Code size={12} /> 0.2ms Overhead</span>
      </div>
    </div>
  )
}
