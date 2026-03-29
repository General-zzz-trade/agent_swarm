import * as vscode from "vscode";
import { spawn, ChildProcess } from "child_process";
import * as path from "path";
import * as fs from "fs";

let chatPanel: BoltChatPanel | undefined;

export function activate(context: vscode.ExtensionContext) {
  // Register sidebar webview
  const provider = new BoltChatViewProvider(context);
  context.subscriptions.push(
    vscode.window.registerWebviewViewProvider("bolt.chatView", provider)
  );

  // Commands
  context.subscriptions.push(
    vscode.commands.registerCommand("bolt.chat", () => {
      vscode.commands.executeCommand("bolt.chatView.focus");
    }),
    vscode.commands.registerCommand("bolt.explain", () => {
      const text = getSelectedText();
      if (text) provider.sendPrompt(`Explain this code:\n\`\`\`\n${text}\n\`\`\``);
    }),
    vscode.commands.registerCommand("bolt.fix", () => {
      const text = getSelectedText();
      if (text) provider.sendPrompt(`Fix this code:\n\`\`\`\n${text}\n\`\`\``);
    }),
    vscode.commands.registerCommand("bolt.test", () => {
      const editor = vscode.window.activeTextEditor;
      if (editor) {
        const file = vscode.workspace.asRelativePath(editor.document.uri);
        provider.sendPrompt(`Generate unit tests for ${file}`);
      }
    }),
    vscode.commands.registerCommand("bolt.buildAndTest", () => {
      provider.sendPrompt("Use build_and_test to verify the project compiles and tests pass");
    })
  );
}

function getSelectedText(): string | undefined {
  const editor = vscode.window.activeTextEditor;
  if (!editor) return undefined;
  const selection = editor.selection;
  return selection.isEmpty ? undefined : editor.document.getText(selection);
}

function findBoltBinary(): string {
  const config = vscode.workspace.getConfiguration("bolt");
  const custom = config.get<string>("binaryPath");
  if (custom && fs.existsSync(custom)) return custom;

  // Check common locations
  const candidates = [
    "bolt",
    path.join(__dirname, "..", "..", "build", "bolt"),
    path.join(__dirname, "..", "..", "build_perf", "bolt"),
  ];
  const ext = process.platform === "win32" ? ".exe" : "";
  for (const c of candidates) {
    const full = c + ext;
    if (fs.existsSync(full)) return full;
  }
  return "bolt" + ext; // Hope it's in PATH
}

class BoltChatViewProvider implements vscode.WebviewViewProvider {
  private view?: vscode.WebviewView;
  private boltProcess?: ChildProcess;

  constructor(private readonly context: vscode.ExtensionContext) {}

  resolveWebviewView(webviewView: vscode.WebviewView) {
    this.view = webviewView;
    webviewView.webview.options = { enableScripts: true };
    webviewView.webview.html = this.getHtml();

    webviewView.webview.onDidReceiveMessage(async (msg) => {
      if (msg.type === "send") {
        await this.handleUserMessage(msg.text);
      }
    });
  }

  async sendPrompt(text: string) {
    if (this.view) {
      this.view.webview.postMessage({ type: "userMessage", text });
      await this.handleUserMessage(text);
    }
  }

  private async handleUserMessage(text: string) {
    const binary = findBoltBinary();
    const workspaceFolder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath || process.cwd();

    const config = vscode.workspace.getConfiguration("bolt");
    const provider = config.get<string>("provider") || "ollama-chat";
    const model = config.get<string>("model") || "";

    const args = ["agent"];
    if (model) args.push("--model", model);
    args.push(text);

    const env = { ...process.env, BOLT_PROVIDER: provider };

    try {
      const child = spawn(binary, args, { cwd: workspaceFolder, env });
      let output = "";

      child.stdout?.on("data", (data: Buffer) => {
        const chunk = data.toString();
        output += chunk;
        this.view?.webview.postMessage({ type: "stream", text: chunk });
      });

      child.stderr?.on("data", (data: Buffer) => {
        // Debug output, ignore in UI
      });

      child.on("close", () => {
        this.view?.webview.postMessage({ type: "done", text: output });
      });

      child.on("error", (err) => {
        this.view?.webview.postMessage({
          type: "error",
          text: `Failed to start bolt: ${err.message}. Set bolt.binaryPath in settings.`,
        });
      });
    } catch (err: any) {
      this.view?.webview.postMessage({ type: "error", text: err.message });
    }
  }

  private getHtml(): string {
    return `<!DOCTYPE html>
<html>
<head>
<style>
  body{font-family:var(--vscode-font-family);color:var(--vscode-foreground);background:var(--vscode-sideBar-background);padding:0;margin:0;display:flex;flex-direction:column;height:100vh}
  .messages{flex:1;overflow-y:auto;padding:12px;font-size:13px}
  .msg{margin-bottom:12px;line-height:1.5}
  .msg.user{color:var(--vscode-textLink-foreground);font-weight:600}
  .msg.assistant{white-space:pre-wrap}
  .msg.error{color:var(--vscode-errorForeground)}
  .msg code{font-family:var(--vscode-editor-font-family);background:var(--vscode-textCodeBlock-background);padding:2px 4px;border-radius:3px}
  .msg pre{background:var(--vscode-textCodeBlock-background);padding:8px;border-radius:4px;overflow-x:auto;margin:6px 0}
  .composer{padding:8px 12px;border-top:1px solid var(--vscode-panel-border);display:flex;gap:6px}
  .composer textarea{flex:1;background:var(--vscode-input-background);color:var(--vscode-input-foreground);border:1px solid var(--vscode-input-border);padding:6px 8px;border-radius:4px;font-size:13px;font-family:var(--vscode-font-family);resize:none;outline:none}
  .composer button{background:var(--vscode-button-background);color:var(--vscode-button-foreground);border:none;padding:6px 12px;border-radius:4px;cursor:pointer;font-size:13px}
  .thinking{color:var(--vscode-descriptionForeground);font-style:italic;font-size:12px;padding:4px 12px}
</style>
</head>
<body>
  <div class="messages" id="messages">
    <div class="msg assistant">&#9889; <strong>Bolt</strong> ready. Ask me anything about your code.</div>
  </div>
  <div id="thinking" class="thinking" style="display:none">Thinking...</div>
  <div class="composer">
    <textarea id="input" rows="2" placeholder="Ask Bolt..."></textarea>
    <button id="send">Send</button>
  </div>
  <script>
    const vscode=acquireVsCodeApi();
    const msgs=document.getElementById("messages");
    const input=document.getElementById("input");
    const thinking=document.getElementById("thinking");
    let currentAssistant=null;

    function addMsg(role,text){
      const d=document.createElement("div");
      d.className="msg "+role;
      d.textContent=text;
      msgs.appendChild(d);
      msgs.scrollTop=msgs.scrollHeight;
      return d;
    }

    document.getElementById("send").addEventListener("click",()=>{
      const t=input.value.trim();
      if(!t)return;
      addMsg("user",t);
      input.value="";
      thinking.style.display="block";
      currentAssistant=null;
      vscode.postMessage({type:"send",text:t});
    });

    input.addEventListener("keydown",e=>{
      if(e.key==="Enter"&&!e.shiftKey){e.preventDefault();document.getElementById("send").click()}
    });

    window.addEventListener("message",e=>{
      const msg=e.data;
      if(msg.type==="userMessage"){addMsg("user",msg.text);thinking.style.display="block";currentAssistant=null}
      if(msg.type==="stream"){
        thinking.style.display="none";
        if(!currentAssistant)currentAssistant=addMsg("assistant","");
        currentAssistant.textContent+=msg.text;
        msgs.scrollTop=msgs.scrollHeight;
      }
      if(msg.type==="done"){thinking.style.display="none";if(!currentAssistant&&msg.text)addMsg("assistant",msg.text);currentAssistant=null}
      if(msg.type==="error"){thinking.style.display="none";addMsg("error",msg.text);currentAssistant=null}
    });
  </script>
</body>
</html>`;
  }
}

export function deactivate() {}
