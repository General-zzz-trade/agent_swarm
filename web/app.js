const messagesEl = document.getElementById("messages");
const composerEl = document.getElementById("composer");
const inputEl = document.getElementById("message-input");
const sendButtonEl = document.getElementById("send-button");
const clearButtonEl = document.getElementById("clear-button");
const composerStatusEl = document.getElementById("composer-status");
const approvalSheetEl = document.getElementById("approval-sheet");
const approvalToolEl = document.getElementById("approval-tool");
const approvalRiskEl = document.getElementById("approval-risk");
const approvalReasonEl = document.getElementById("approval-reason");
const approvalSummaryEl = document.getElementById("approval-summary");
const approvalDetailsEl = document.getElementById("approval-details");
const approveButtonEl = document.getElementById("approve-button");
const denyButtonEl = document.getElementById("deny-button");
const statusModelEl = document.getElementById("status-model");
const statusPortEl = document.getElementById("status-port");
const statusOverallEl = document.getElementById("status-overall");
const statusBusyEl = document.getElementById("status-busy");
const statusSelfCheckEl = document.getElementById("status-self-check");
const selfCheckButtonEl = document.getElementById("self-check-button");
const capabilitySummaryEl = document.getElementById("capability-summary");
const capabilityListEl = document.getElementById("capability-list");
const traceStatusEl = document.getElementById("trace-status");
const traceStepsEl = document.getElementById("trace-steps");

let sending = false;
let pendingApprovalVisible = false;
let lastTraceSignature = "";

function appendMessage(role, text) {
  const messageEl = document.createElement("div");
  messageEl.className = `message ${role}`;
  messageEl.textContent = text;
  messagesEl.appendChild(messageEl);
  messagesEl.scrollTop = messagesEl.scrollHeight;
}

function setComposerStatus(text) {
  composerStatusEl.textContent = text;
}

function setSending(nextSending) {
  sending = nextSending;
  sendButtonEl.disabled = nextSending;
  inputEl.disabled = nextSending;
}

function renderTrace(trace) {
  const nextTrace = Array.isArray(trace) ? trace : [];
  const signature = JSON.stringify(nextTrace);
  if (signature === lastTraceSignature) {
    return;
  }
  lastTraceSignature = signature;

  traceStepsEl.innerHTML = "";
  if (nextTrace.length === 0) {
    traceStatusEl.textContent = "最近一次执行还没有步骤。";
    return;
  }

  traceStatusEl.textContent = `最近一次执行共 ${nextTrace.length} 步。`;
  nextTrace.forEach((step) => {
    const card = document.createElement("article");
    card.className = `trace-step ${step.status || "unknown"}`;

    const header = document.createElement("div");
    header.className = "trace-step-header";
    header.textContent = `#${step.index} ${step.tool_name} [${step.status}]`;
    card.appendChild(header);

    if (step.reason) {
      const reason = document.createElement("div");
      reason.className = "trace-step-reason";
      reason.textContent = `原因: ${step.reason}`;
      card.appendChild(reason);
    }

    if (step.detail) {
      const detail = document.createElement("pre");
      detail.className = "trace-step-detail";
      detail.textContent = step.detail;
      card.appendChild(detail);
    }

    traceStepsEl.appendChild(card);
  });
}

function activeTraceLabel(trace) {
  const nextTrace = Array.isArray(trace) ? trace : [];
  if (nextTrace.length === 0) {
    return "";
  }
  const step = nextTrace[nextTrace.length - 1];
  return `第 ${step.index} 步 ${step.tool_name}`;
}

function renderHealth(health) {
  if (!health) {
    statusOverallEl.textContent = "健康: 未知";
    statusSelfCheckEl.textContent = "最近自检: 未执行";
    return;
  }

  statusOverallEl.textContent =
    `健康: ${health.overall} | 已实现 ${health.implemented_count} | 已验证 ${health.verified_count} | 异常 ${health.degraded_count}`;
  statusSelfCheckEl.textContent =
    `最近自检: ${health.last_self_check_at || "未执行"}`;
}

function renderCapabilities(capabilities) {
  const nextCapabilities = Array.isArray(capabilities) ? capabilities : [];
  capabilityListEl.innerHTML = "";

  if (nextCapabilities.length === 0) {
    capabilitySummaryEl.textContent = "当前没有可展示的能力快照。";
    return;
  }

  const verifiedCount = nextCapabilities.filter((capability) => capability.verified).length;
  capabilitySummaryEl.textContent =
    `共 ${nextCapabilities.length} 项能力，已验证 ${verifiedCount} 项。`;

  nextCapabilities.forEach((capability) => {
    const card = document.createElement("article");
    card.className = `capability-card ${capability.level || "unknown"}`;

    const title = document.createElement("div");
    title.className = "capability-card-title";
    title.textContent = `${capability.label} [${capability.level || "unknown"}]`;
    card.appendChild(title);

    const meta = document.createElement("div");
    meta.className = "capability-card-meta";
    meta.textContent =
      `implemented=${capability.implemented} ready=${capability.ready} verified=${capability.verified}`;
    card.appendChild(meta);

    const detail = document.createElement("div");
    detail.className = "capability-card-detail";
    detail.textContent = capability.detail || "-";
    card.appendChild(detail);

    if (capability.last_checked_at) {
      const checkedAt = document.createElement("div");
      checkedAt.className = "capability-card-meta";
      checkedAt.textContent = `last_checked_at=${capability.last_checked_at}`;
      card.appendChild(checkedAt);
    }

    capabilityListEl.appendChild(card);
  });
}

async function fetchJson(url, options = {}) {
  const response = await fetch(url, {
    headers: {
      "Content-Type": "application/json",
    },
    ...options,
  });

  const text = await response.text();
  let payload = {};
  if (text) {
    payload = JSON.parse(text);
  }

  if (!response.ok) {
    const message = payload.error || `HTTP ${response.status}`;
    throw new Error(message);
  }

  return payload;
}

async function refreshCapabilities() {
  try {
    const payload = await fetchJson("/api/capabilities", { method: "GET", headers: {} });
    renderCapabilities(payload.capabilities);
  } catch (error) {
    capabilitySummaryEl.textContent = `能力快照加载失败: ${error.message}`;
  }
}

function renderApproval(state) {
  if (!state.has_pending_approval) {
    approvalSheetEl.classList.add("hidden");
    pendingApprovalVisible = false;
    return;
  }

  const approval = state.approval;
  approvalToolEl.textContent = `工具: ${approval.tool_name}`;
  approvalRiskEl.textContent = approval.risk || "unknown";
  approvalReasonEl.textContent = approval.reason || "-";
  approvalSummaryEl.textContent = approval.preview_summary || "-";
  approvalDetailsEl.textContent = approval.preview_details || "-";
  approvalSheetEl.classList.remove("hidden");
  pendingApprovalVisible = true;
}

async function refreshState() {
  try {
    const [info, state, health] = await Promise.all([
      fetchJson("/api/info", { method: "GET", headers: {} }),
      fetchJson("/api/state", { method: "GET", headers: {} }),
      fetchJson("/api/health", { method: "GET", headers: {} }),
    ]);

    statusModelEl.textContent = `模型: ${info.model}`;
    statusPortEl.textContent = `端口: ${info.port}`;
    const traceLabel = activeTraceLabel(state.last_trace);
    statusBusyEl.textContent = state.busy
      ? `状态: 处理中${traceLabel ? ` (${traceLabel})` : ""}`
      : "状态: 空闲";
    renderHealth(health);
    renderApproval(state);
    renderTrace(state.last_trace);
  } catch (error) {
    statusBusyEl.textContent = `状态: ${error.message}`;
  }
}

async function submitMessage(message) {
  if (sending) {
    return;
  }

  setSending(true);
  setComposerStatus("正在等待 Agent 响应...");
  appendMessage("user", message);

  try {
    const payload = await fetchJson("/api/chat", {
      method: "POST",
      body: JSON.stringify({ message }),
    });
    appendMessage("assistant", payload.reply);
    renderTrace(payload.trace);
    setComposerStatus("已完成");
  } catch (error) {
    appendMessage("system", `请求失败: ${error.message}`);
    setComposerStatus("请求失败");
  } finally {
    setSending(false);
    inputEl.value = "";
    inputEl.focus();
  }
}

composerEl.addEventListener("submit", async (event) => {
  event.preventDefault();
  const message = inputEl.value.trim();
  if (!message) {
    return;
  }
  await submitMessage(message);
});

document.querySelectorAll(".quick-action").forEach((button) => {
  button.addEventListener("click", async () => {
    await submitMessage(button.dataset.prompt);
  });
});

clearButtonEl.addEventListener("click", async () => {
  try {
    await fetchJson("/api/clear", { method: "POST", body: "{}" });
    messagesEl.innerHTML = "";
    renderTrace([]);
    appendMessage("system", "历史已清空。");
    setComposerStatus("历史已清空");
  } catch (error) {
    appendMessage("system", `清空失败: ${error.message}`);
  }
});

approveButtonEl.addEventListener("click", async () => {
  try {
    await fetchJson("/api/approval", {
      method: "POST",
      body: JSON.stringify({ approved: true }),
    });
    setComposerStatus("已批准，等待 Agent 继续执行...");
  } catch (error) {
    appendMessage("system", `批准失败: ${error.message}`);
  }
});

denyButtonEl.addEventListener("click", async () => {
  try {
    await fetchJson("/api/approval", {
      method: "POST",
      body: JSON.stringify({ approved: false }),
    });
    setComposerStatus("已拒绝，等待 Agent 返回结果...");
  } catch (error) {
    appendMessage("system", `拒绝失败: ${error.message}`);
  }
});

selfCheckButtonEl.addEventListener("click", async () => {
  selfCheckButtonEl.disabled = true;
  setComposerStatus("正在运行自检...");
  try {
    const payload = await fetchJson("/api/self-check", {
      method: "POST",
      body: "{}",
    });
    renderHealth(payload.health);
    renderCapabilities(payload.capabilities);
    setComposerStatus("自检完成");
  } catch (error) {
    appendMessage("system", `自检失败: ${error.message}`);
    setComposerStatus("自检失败");
  } finally {
    selfCheckButtonEl.disabled = false;
  }
});

appendMessage("system", "网页聊天界面已连接。可以直接验证当前 Agent 的本地能力。");
refreshState();
refreshCapabilities();
setInterval(refreshState, 1000);
setInterval(refreshCapabilities, 5000);
