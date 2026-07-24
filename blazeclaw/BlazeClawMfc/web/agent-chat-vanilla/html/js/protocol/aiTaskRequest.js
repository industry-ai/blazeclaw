/* ================================================================
   AI_TASK_REQUEST 协议模块
   ----------------------------------------------------------------
   对齐 agent 项目 openclawAgentProvider.ts / agentRuntimeService.ts
   对齐协议文档 §2.3 AI_TASK_REQUEST 任务请求

   核心职责：构建 AI_TASK_REQUEST 负载，并嵌入到 prompt 文本中。
   AI_TASK_REQUEST 不是独立网络事件，而是嵌入 prompt 文本发送给 AI 引擎。

   发送格式：
   [AgentChat Context Header - 内部协议头]

   AI_TASK_REQUEST:
   {JSON}
   ================================================================ */

// ── 工具函数 ──

function toTrimmedString(value) {
  return String(value ?? '').trim();
}

function hasRecordEntries(record) {
  return Boolean(record && Object.keys(record).length > 0);
}

function sanitizeResourcePathSegment(value) {
  const normalized = toTrimmedString(value)
    .replace(/^#+/, '')
    .replace(/[^\w.-]+/g, '-')
    .replace(/-+/g, '-')
    .replace(/^-|-$/g, '');
  return normalized || 'unknown';
}

// ── taskNo 生成（对齐 agentRuntimeService.ts createAgentTaskNo）──

/**
 * 生成任务号
 * 格式: AI{YYYYMMDD}-{6位随机大写字符}
 * 示例: AI20260721-ABCDEF
 */
export function createAgentTaskNo(now = new Date()) {
  const y = now.getFullYear();
  const m = String(now.getMonth() + 1).padStart(2, '0');
  const d = String(now.getDate()).padStart(2, '0');
  const suffix = Math.random().toString(36).slice(2, 8).toUpperCase().padEnd(6, '0');
  return `AI${y}${m}${d}-${suffix}`;
}

// ── 协议上下文头（对齐 openclawAgentProvider.ts OPENCLAW_AGENTCHAT_PROTOCOL_CONTEXT）──

const OPENCLAW_AGENTCHAT_PROTOCOL_CONTEXT = [
  'AgentChat AI provider. Read AI_TASK_REQUEST JSON and answer in concise Simplified Chinese.',
  'Use tools only when needed. For generated files/images/audio/pages, return a public http/https URL.',
  'For generated HTML pages, upload them with Content-Type text/html; charset=utf-8 and inline disposition so browsers and iframes render the page instead of downloading it.',
  'If AI_TASK_REQUEST.resourceNaming exists, pass its objectKeyPrefix to upload tools when supported.',
  'When AI_TASK_REQUEST.input.requester exists, use it to identify who initiated the request (userId, phone, displayName).',
  'When AI_TASK_REQUEST.input.state exists, use it as conversation-scoped context from previous turns.',
  'Do not expose tool names, logs, code, PowerShell, base64, HTML errors, or debugging details.',
].join('\n');

// ── 卡片生成请求检测（对齐 agentSkillRegistry.ts isExplicitAiCardRequest）──

/**
 * 检测用户查询是否是显式的 AI 卡片生成请求
 */
export function isExplicitAiCardRequest(userQuery) {
  const query = String(userQuery || '').toLowerCase();
  return (
    /卡片|card|连环画|漫画|comic|预览/.test(query) &&
    /生成|创建|制作|渲染|做一个|来一个|帮我|发一个|发给|send|create|generate|make|render/.test(query)
  );
}

// ── 后续上下文检测（对齐 openclawAgentProvider.ts shouldIncludeOpenClawRecentContext）──

const OPENCLAW_CONTEXT_FOLLOW_UP_RE = /^(再|继续|换|改|这个|那个|它|上一个|上一张|刚才|同样|也|重新|加上|去掉|不要|帮我把|把这|把它|再来|换成|改成|follow up|again|same|change|make it)/iu;

export function shouldIncludeRecentContext(userQuery) {
  return OPENCLAW_CONTEXT_FOLLOW_UP_RE.test(String(userQuery || '').trim());
}

// ── Requester 构建（对齐 openclawAgentProvider.ts buildRequester）──

/**
 * 构建发起人信息
 * @param {object} metadata - 元数据 { userId, userPhone }
 * @returns {object|undefined} { userId, phone, displayName }
 */
export function buildRequester(metadata) {
  const userId = toTrimmedString(metadata?.userId);
  const phone = toTrimmedString(metadata?.userPhone) || userId || '';
  if (!userId && !phone) return undefined;
  return {
    userId: userId || '',
    phone,
    displayName: phone || userId || '',
  };
}

// ── 资源命名（对齐 openclawAgentProvider.ts resolveOpenClawResourceNaming）──

/**
 * 解析资源命名信息
 * @param {object} metadata - 元数据 { taskNo, providerId }
 * @returns {object|undefined} { sourceAgent, taskNo, pathMarker, objectKeyPrefix, fileNamePrefix }
 */
export function resolveResourceNaming(metadata) {
  const rawTaskNo = toTrimmedString(metadata?.taskNo);
  if (!rawTaskNo) return undefined;

  const sourceAgent = sanitizeResourcePathSegment(metadata?.providerId || 'openclaw');
  const taskNo = sanitizeResourcePathSegment(rawTaskNo);
  const pathMarker = `ai/${sourceAgent}/${taskNo}`;

  return {
    sourceAgent,
    taskNo,
    pathMarker,
    objectKeyPrefix: pathMarker,
    fileNamePrefix: `${taskNo}_`,
  };
}

// ── AI_TASK_REQUEST 负载构建（对齐 openclawAgentProvider.ts buildAiTaskRequestPayload）──

/**
 * 构建 AI_TASK_REQUEST 负载
 *
 * 输出结构（对齐协议文档 §2.3）：
 * {
 *   "taskNo": "AI20260721-ABCDEF",
 *   "input": {
 *     "text": "用户消息",
 *     "requester": { "userId": "...", "phone": "...", "displayName": "..." },
 *     "state": {}
 *   },
 *   "resourceNaming": { "objectKeyPrefix": "ai/openclaw/AI20260721-ABCDEF" },
 *   "bizPayload": {}
 * }
 *
 * @param {string} userQuery - 用户消息文本
 * @param {object} metadata - 元数据 { taskNo, providerId, userId, userPhone, bizPayload }
 * @param {object} state - 会话上下文状态（可选）
 * @returns {object} AI_TASK_REQUEST 负载对象
 */
export function buildAiTaskRequestPayload(userQuery, metadata = {}, state) {
  const resourceNaming = resolveResourceNaming(metadata);
  const requester = buildRequester(metadata);
  const input = { text: userQuery };
  if (requester) input.requester = requester;
  if (state && Object.keys(state).length > 0) input.state = state;

  const payload = {
    ...(metadata?.taskNo ? { taskNo: metadata.taskNo } : {}),
    input,
    ...(resourceNaming ? { resourceNaming: { objectKeyPrefix: resourceNaming.objectKeyPrefix } } : {}),
  };
  if (hasRecordEntries(metadata?.bizPayload)) payload.bizPayload = metadata.bizPayload;
  return payload;
}

// ── 完整 prompt 构建（对齐 openclawAgentProvider.ts buildOpenClawMessage）──

/**
 * 构建发送给 AI 的完整 prompt 文本
 *
 * 格式（对齐协议文档 §2.3）：
 * [AgentChat Context Header - 内部协议头]
 *
 * AI_TASK_REQUEST:
 * {JSON}
 *
 * [可选] Card generation requirement: ...
 * [可选] Recent context: ...
 *
 * @param {string} userQuery - 用户消息文本
 * @param {object} metadata - 元数据 { taskNo, providerId, userId, userPhone, bizPayload }
 * @param {object} state - 会话上下文状态（可选）
 * @param {string[]} conversationContext - 近期对话上下文（可选，用于后续追问）
 * @returns {string} 完整 prompt 文本
 */
export function buildOpenClawMessage(userQuery, metadata = {}, state, conversationContext = []) {
  const parts = [
    OPENCLAW_AGENTCHAT_PROTOCOL_CONTEXT,
    '',
    'AI_TASK_REQUEST:',
    JSON.stringify(buildAiTaskRequestPayload(userQuery, metadata, state)),
  ];

  // 显式卡片生成请求时追加额外指令
  if (isExplicitAiCardRequest(userQuery)) {
    parts.push(
      '',
      'Card generation requirement:',
      'Use the h5-cards skill when available. Return the generated self-contained HTML as a public http/https URL.',
      'The HTML URL must open directly in a browser/webview; do not return a COS object configured as a file download.',
      'Do not return only card JSON. The frontend needs a webview attachment URL to open the card.',
    );
  }

  // 后续追问时追加近期上下文
  if (conversationContext.length && shouldIncludeRecentContext(userQuery)) {
    parts.push('', 'Recent context:', ...conversationContext);
  }

  return parts.join('\n');
}

export default {
  createAgentTaskNo,
  buildRequester,
  resolveResourceNaming,
  buildAiTaskRequestPayload,
  buildOpenClawMessage,
  isExplicitAiCardRequest,
  shouldIncludeRecentContext,
};
