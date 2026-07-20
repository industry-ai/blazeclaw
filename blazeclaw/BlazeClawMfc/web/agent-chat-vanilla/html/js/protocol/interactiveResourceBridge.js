/* ================================================================
   WebView postMessage Bridge - H5 页面与宿主双向通信
   对齐 agent/docs/protocols/AI运行工作区与Skill接入协议.md §4.2
   对齐 Vue 版 interactiveResourceBridge.ts + useInteractiveResourceBridge.ts
   ================================================================ */

// ── 桥接事件类型常量（对齐协议 §4.2）──

export const INTERACTIVE_READY = 'interactive.ready';
export const INTERACTIVE_SEND_MESSAGE = 'interactive.send_message';
export const HOST_BRIDGE_CONTEXT = 'agentchat.bridge_context';
export const HOST_CONVERSATION_MESSAGE = 'agentchat.conversation_message';
export const HOST_SKILL_RESULT = 'agentchat.skill_result';

// ── 工具函数 ──

function _trim(value) {
  return String(value ?? '').trim();
}

function _isRecord(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function _hashText(value) {
  let hash = 2166136261;
  for (let i = 0; i < value.length; i++) {
    hash ^= value.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0).toString(36);
}

// ── 解析资源上下文 ──

/**
 * 从 WebView 任务 payload 解析桥接上下文
 * 对齐 Vue 版 resolveInteractiveResourceContext
 * @param {object} payload - { type, url, title, summary, postId, taskNo, ... }
 * @param {string} conversationId
 * @param {object} viewer - { userId, phone, displayName }
 * @returns {InteractiveResourceContext | null}
 */
export function resolveInteractiveResourceContext(payload, conversationId, viewer) {
  const activeConversationId = _trim(conversationId);
  if (!activeConversationId || !payload || payload.type !== 'webview') return null;

  const url = _trim(payload.url);
  if (!url) return null;

  let parsed;
  try {
    parsed = new URL(url);
  } catch {
    return null;
  }
  if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') return null;

  const resourceId = _trim(payload.postId) || `webview:${_hashText(url)}`;
  const instanceId = `interactive:${_hashText(`${activeConversationId}|${resourceId}|${url}`)}`;

  // 解析 input 快照
  let input;
  if (payload.input) {
    input = {};
    if (payload.input.text) input.text = _trim(payload.input.text) || undefined;
    if (payload.input.requester) {
      input.requester = {
        userId: _trim(payload.input.requester.userId) || undefined,
        phone: _trim(payload.input.requester.phone) || undefined,
        displayName: _trim(payload.input.requester.displayName) || undefined,
      };
    }
  }

  return {
    conversationId: activeConversationId,
    resourceId,
    instanceId,
    origin: parsed.origin,
    url,
    title: _trim(payload.title) || undefined,
    viewer: viewer
      ? {
          userId: _trim(viewer.userId) || undefined,
          phone: _trim(viewer.phone) || undefined,
          displayName: _trim(viewer.displayName) || undefined,
        }
      : undefined,
    summary: _trim(payload.summary) || undefined,
    postId: _trim(payload.postId) || undefined,
    taskNo: _trim(payload.taskNo) || undefined,
    providerId: _trim(payload.providerId) || undefined,
    skillId: _trim(payload.skillId) || undefined,
    sourceSkillId: _trim(payload.sourceSkillId) || undefined,
    objectKind: _trim(payload.objectKind) || undefined,
    artifactType: _trim(payload.artifactType) || undefined,
    mimeType: _trim(payload.mimeType) || undefined,
    input,
  };
}

// ── 构建宿主->H5 的消息 ──

/**
 * 构建 agentchat.bridge_context 消息（对齐协议 §4.2.2）
 */
export function buildHostBridgeContextMessage(context) {
  return {
    type: HOST_BRIDGE_CONTEXT,
    version: 1,
    conversationId: context.conversationId,
    resourceId: context.resourceId,
    instanceId: context.instanceId,
    title: context.title,
    viewer: context.viewer,
    resource: {
      type: 'webview',
      url: context.url,
      title: context.title,
      summary: context.summary,
      postId: context.postId,
      taskNo: context.taskNo,
      providerId: context.providerId,
      skillId: context.skillId,
      sourceSkillId: context.sourceSkillId,
      objectKind: context.objectKind,
      artifactType: context.artifactType,
      mimeType: context.mimeType,
      input: context.input,
    },
  };
}

/**
 * 构建 agentchat.conversation_message 消息（对齐协议 §4.2.2）
 */
export function buildHostConversationMessage(context, message) {
  const text = _trim(message.content);
  if (!text || message.conversationId !== context.conversationId) return null;
  if (message.author !== 'user' && message.author !== 'other_user') return null;

  const senderUserId = _trim(message.authorId) || undefined;
  const senderDisplayName = _trim(message.authorName) || senderUserId || '';

  return {
    type: HOST_CONVERSATION_MESSAGE,
    version: 1,
    conversationId: context.conversationId,
    resourceId: context.resourceId,
    instanceId: context.instanceId,
    message: {
      messageId: message.serverMessageId || message.id,
      sender: {
        userId: senderUserId,
        phone: senderUserId,
        displayName: senderDisplayName,
      },
      kind: 'text',
      text,
      timestamp: message.createdAt,
    },
  };
}

/**
 * 构建 agentchat.skill_result 消息（对齐协议 §4.2.3）
 */
export function buildHostSkillResultMessage(context, message) {
  if (message.author !== 'agent') return null;
  if (message.conversationId !== context.conversationId) return null;
  if (message.status === 'streaming') return null;

  const outputs = [];
  let taskNo, providerId, skillId, sourceSkillId;

  const attachments = message.attachments;
  if (attachments && attachments.length > 0) {
    for (const att of attachments) {
      if (att.type === 'webview') {
        outputs.push({
          type: 'webview',
          url: att.url,
          title: att.title,
          summary: att.summary,
        });
      }
      // 收集 skill 元数据
      taskNo = taskNo || _trim(att.taskNo) || undefined;
      providerId = providerId || _trim(att.providerId) || undefined;
      skillId = skillId || _trim(att.skillId) || undefined;
      sourceSkillId = sourceSkillId || _trim(att.sourceSkillId) || undefined;
    }
  }

  // 如果没有 webview 输出但有文本，作为 text output 转发
  if (outputs.length === 0) {
    const content = _trim(message.content);
    if (!content) return null;
    outputs.push({ type: 'text', content });
  }

  return {
    type: HOST_SKILL_RESULT,
    version: 1,
    conversationId: context.conversationId,
    resourceId: context.resourceId,
    instanceId: context.instanceId,
    result: {
      taskNo,
      providerId,
      skillId,
      sourceSkillId,
      status: 'done',
      summary: _trim(message.content) || undefined,
      outputs,
    },
  };
}

// ── 解析 H5->宿主 的事件 ──

function _matchesContext(data, context) {
  const conversationId = _trim(data.conversationId);
  const resourceId = _trim(data.resourceId);
  const instanceId = _trim(data.instanceId);
  if (conversationId && conversationId !== context.conversationId) return false;
  if (resourceId && resourceId !== context.resourceId) return false;
  if (instanceId && instanceId !== context.instanceId) return false;
  return true;
}

/**
 * 解析 H5 页面发来的 postMessage 事件
 * 对齐 Vue 版 parseInteractiveResourceEvent
 * @param {MessageEvent} event
 * @param {InteractiveResourceContext} context
 * @param {Window} expectedSource - iframe.contentWindow
 * @param {boolean} bridgeReady - 桥接是否已就绪
 * @returns {{ type: 'ready' } | { type: 'send_message', conversationId, text, replyTo } | null}
 */
export function parseInteractiveResourceEvent(event, context, expectedSource, bridgeReady) {
  if (event.origin !== context.origin) return null;
  if (event.source !== expectedSource) return null;
  if (!_isRecord(event.data)) return null;

  const type = _trim(event.data.type);

  if (type === INTERACTIVE_READY) {
    return _matchesContext(event.data, context) ? { type: 'ready' } : null;
  }

  if (type !== INTERACTIVE_SEND_MESSAGE || !bridgeReady) return null;
  if (!_matchesContext(event.data, context)) return null;

  const payload = _isRecord(event.data.payload) ? event.data.payload : {};
  const text = _trim(payload.text);
  if (!text) return null;

  return {
    type: 'send_message',
    conversationId: _trim(event.data.conversationId) || context.conversationId,
    text: text.slice(0, 2000),
    replyTo: _trim(event.data.replyTo) || undefined,
  };
}

export default {
  INTERACTIVE_READY,
  INTERACTIVE_SEND_MESSAGE,
  HOST_BRIDGE_CONTEXT,
  HOST_CONVERSATION_MESSAGE,
  HOST_SKILL_RESULT,
  resolveInteractiveResourceContext,
  buildHostBridgeContextMessage,
  buildHostConversationMessage,
  buildHostSkillResultMessage,
  parseInteractiveResourceEvent,
};
