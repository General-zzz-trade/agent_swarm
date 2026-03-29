const messagesEl=document.getElementById("messages"),inputEl=document.getElementById("message-input"),sendBtn=document.getElementById("send-button"),clearBtn=document.getElementById("clear-button"),statusModel=document.getElementById("status-model"),statusBusy=document.getElementById("status-busy"),streamInd=document.getElementById("streaming-indicator"),approvalSheet=document.getElementById("approval-sheet"),approvalTool=document.getElementById("approval-tool"),approvalReason=document.getElementById("approval-reason"),approvalDetails=document.getElementById("approval-details"),approveBtn=document.getElementById("approve-button"),denyBtn=document.getElementById("deny-button");
let sending=false;

// Configure marked for Markdown rendering
if(typeof marked!=="undefined"){
  marked.setOptions({breaks:true,gfm:true,highlight:function(code,lang){
    if(typeof hljs!=="undefined"&&lang&&hljs.getLanguage(lang))return hljs.highlight(code,{language:lang}).value;
    return code;
  }});
}

function renderMd(text){
  if(typeof marked!=="undefined")return marked.parse(text);
  return text.replace(/</g,"&lt;").replace(/\n/g,"<br>");
}

function appendMessage(role,text){
  const div=document.createElement("div");
  div.className="message "+role;
  const body=document.createElement("div");
  body.className="msg-body";
  if(role==="assistant"){
    body.innerHTML=renderMd(text);
    body.querySelectorAll("pre code").forEach(el=>{if(typeof hljs!=="undefined")hljs.highlightElement(el)});
  }else{
    body.textContent=text;
  }
  div.appendChild(body);
  messagesEl.appendChild(div);
  messagesEl.scrollTop=messagesEl.scrollHeight;
  return body;
}

function setSending(v){
  sending=v;
  sendBtn.disabled=v;
  inputEl.disabled=v;
  streamInd.classList.toggle("hidden",!v);
}

// SSE streaming chat
async function submitMessage(message){
  if(sending)return;
  setSending(true);
  appendMessage("user",message);
  inputEl.value="";

  // Try SSE streaming first
  try{
    const resp=await fetch("/api/chat/stream",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({message})});
    if(resp.ok&&resp.headers.get("content-type")?.includes("text/event-stream")){
      const body=appendMessage("assistant","");
      const reader=resp.body.getReader();
      const decoder=new TextDecoder();
      let fullText="";
      while(true){
        const{done,value}=await reader.read();
        if(done)break;
        const chunk=decoder.decode(value,{stream:true});
        // Parse SSE events
        for(const line of chunk.split("\n")){
          if(line.startsWith("data: ")){
            const token=line.slice(6).replace(/\n/g,"\n").replace(/\\"/g,'"').replace(/\\/g,"\\");
            fullText+=token;
            body.innerHTML=renderMd(fullText);
            body.querySelectorAll("pre code").forEach(el=>{if(typeof hljs!=="undefined")hljs.highlightElement(el)});
            messagesEl.scrollTop=messagesEl.scrollHeight;
          }
        }
      }
      setSending(false);
      return;
    }
  }catch(e){/* fall through to sync */}

  // Fallback: synchronous chat
  try{
    const resp=await fetch("/api/chat",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({message})});
    const data=await resp.json();
    if(data.ok)appendMessage("assistant",data.reply);
    else appendMessage("system","Error: "+(data.error||"Unknown"));
  }catch(e){appendMessage("system","Request failed: "+e.message)}
  setSending(false);
}

// Auto-resize textarea
inputEl.addEventListener("input",()=>{
  inputEl.style.height="auto";
  inputEl.style.height=Math.min(inputEl.scrollHeight,200)+"px";
});
inputEl.addEventListener("keydown",e=>{
  if(e.key==="Enter"&&!e.shiftKey){e.preventDefault();document.getElementById("composer").dispatchEvent(new Event("submit"))}
});
document.getElementById("composer").addEventListener("submit",e=>{e.preventDefault();const m=inputEl.value.trim();if(m)submitMessage(m)});
document.querySelectorAll(".qa").forEach(b=>b.addEventListener("click",()=>submitMessage(b.dataset.prompt)));
clearBtn.addEventListener("click",async()=>{
  try{await fetch("/api/clear",{method:"POST",body:"{}"});messagesEl.innerHTML="";appendMessage("system","History cleared.")}catch(e){appendMessage("system","Failed: "+e.message)}
});

// Approval handlers
approveBtn.addEventListener("click",async()=>{try{await fetch("/api/approval",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({approved:true})})}catch(e){}});
denyBtn.addEventListener("click",async()=>{try{await fetch("/api/approval",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({approved:false})})}catch(e){}});

// Poll state
async function poll(){
  try{
    const[info,state]=await Promise.all([fetch("/api/info").then(r=>r.json()),fetch("/api/state").then(r=>r.json())]);
    statusModel.textContent=info.model||"-";
    statusBusy.textContent=state.busy?"Working...":"Idle";
    statusBusy.style.color=state.busy?"var(--yellow)":"var(--green)";
    if(state.has_pending_approval){
      approvalTool.textContent="Tool: "+(state.approval?.tool_name||"-");
      approvalReason.textContent=state.approval?.reason||"-";
      approvalDetails.textContent=state.approval?.preview_details||"-";
      approvalSheet.classList.remove("hidden");
    }else{approvalSheet.classList.add("hidden")}
  }catch(e){}
}
poll();setInterval(poll,1500);
