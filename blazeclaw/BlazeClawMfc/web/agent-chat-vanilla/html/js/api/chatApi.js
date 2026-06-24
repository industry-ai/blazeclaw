/* ================================================================
   AgentChat HTML 版 - 聊天 API 层
   对应原版 chatRoomApi.ts、chatBridgeApi.ts、personalWorkspaceApi.ts
   ================================================================ */

import AppHttp from './httpClient.js';
import AppConfig from '../config.js';

function _trim(value) {
  return String(value ?? '').trim();
}

function _normalizeMyChatRoom(item) {
  if (typeof item === 'string' || typeof item === 'number') {
    const id = _trim(item);
    return id ? { id } : null;
  }
  if (!item || typeof item !== 'object') return null;
  const id = _trim(
    item.id ||
    item.room_id ||
    item.roomId ||
    item.conversation_id ||
    item.conversationId,
  );
  if (!id) return null;
  const title = _trim(
    item.title ||
    item.name ||
    item.room_name ||
    item.roomName,
  ) || undefined;
  const kind = _trim(item.kind || item.type) || undefined;
  return { id, title, kind };
}

function _normalizeMyChatRoomsFromResponse(raw) {
  const response = raw?.result || raw?.data || raw || {};
  const list = response.rooms || response.chat_rooms || response.chatRooms
    || response.conversations || response.data || response.rows
    || (Array.isArray(response) ? response : []);
  if (!Array.isArray(list)) return [];
  const seen = new Set();
  const rooms = [];
  list.forEach((item) => {
    const room = _normalizeMyChatRoom(item);
    if (!room || seen.has(room.id)) return;
    seen.add(room.id);
    rooms.push(room);
  });
  return rooms;
}

/**
 * 获取会话列表
 * 对齐 Vue 版 listMyChatRooms → normalizeMyChatRoomsFromResponse
 */
async function listConversations() {
  const raw = await AppHttp.get('/conversations');
  const rooms = _normalizeMyChatRoomsFromResponse(raw);
  return {
    conversations: rooms.map((room) => ({
      id: room.id.startsWith('#') ? room.id : `#${room.id}`,
      title: room.title || '',
      kind: room.kind || 'group',
    })),
  };
}

/**
 * 创建会话
 */
async function createConversation(name, type = 'group') {
  const raw = await AppHttp.post('/conversations', {
    name,
    conversation_type: type,
  }, {
    timeoutMs: 35000,
  });
  const result = raw?.result || raw?.data || raw || {};
  const conversation = result?.conversation || result?.room || result;
  if (!conversation?.id && !conversation?.room_id) {
    throw new Error('CREATE_CONVERSATION response missing conversation');
  }
  return {
    conversation: {
      id: _trim(conversation.id || conversation.room_id || conversation.roomId),
      title: _trim(conversation.title || conversation.name || conversation.room_name || conversation.roomName),
      kind: 'group',
      ownerUserId: _trim(conversation.owner_user_id || conversation.ownerUserId),
      createdBy: _trim(conversation.created_by || conversation.createdBy),
      status: _trim(conversation.status || 'active'),
      createdAt: _trim(conversation.created_at || conversation.createdAt),
      updatedAt: _trim(conversation.updated_at || conversation.updatedAt),
    },
    ts: _trim(result?.ts),
    ok: result?.ok !== false,
  };
}

/**
 * 获取房间信息（成员列表、名称等）
 * 对齐 Vue 版 getRoomInfo → normalizeRoomName
 */
async function getRoomInfo(roomId) {
  const raw = await AppHttp.post('/room/info', { room_id: roomId }, { timeoutMs: 30000 });
  const info = raw?.room || raw?.room_info || raw?.info || raw || {};
  const members = Array.isArray(info.members) ? info.members
    : Array.isArray(raw?.members) ? raw.members
    : Array.isArray(raw?.room_members) ? raw.room_members
    : [];
  return {
    name: _trim(info.name || info.title || info.room_name || info.roomName) || roomId.replace(/^#/, ''),
    topic: _trim(info.topic),
    ownerUserId: _trim(info.owner_user_id || info.ownerUserId),
    members,
    memberCount: members.length,
    createdBy: _trim(info.created_by || info.createdBy),
    status: _trim(info.status),
    createdAt: _trim(info.created_at || info.createdAt),
    updatedAt: _trim(info.updated_at || info.updatedAt),
  };
}

/**
 * 获取房间成员列表
 * 对齐 Vue 版 listRoomMembers → normalizeRoomMembersFromResponse
 */
async function listRoomMembers(roomId) {
  const raw = await AppHttp.post('/room/members', { room_id: roomId }, { timeoutMs: 30000 });
  // 兼容多种响应 key
  const list = raw?.members || raw?.room_members || raw?.data?.members
    || raw?.result?.members || (Array.isArray(raw) ? raw : []);
  if (!Array.isArray(list)) return { members: [], roomId };
  return {
    members: list,
    roomId,
  };
}

/**
 * 邀请成员加入房间
 */
async function inviteMember(roomId, memberKind, targetId, role = 'member') {
  const payload = {
    room_id: roomId,
    member_kind: memberKind,
    role,
  };
  // 对齐 Vue 版 chatRoomMemberService：phone 邀请用 phone 字段，node 用 node_id
  if (memberKind === 'phone') {
    payload.phone = targetId;
  } else if (memberKind === 'node') {
    payload.node_id = targetId;
  } else {
    payload.user_id = targetId;
  }
  return await AppHttp.post('/room/invite', payload);
}

/**
 * 移除房间成员
 */
async function removeMember(roomId, memberKind, targetId) {
  const payload = {
    room_id: roomId,
    member_kind: memberKind,
  };
  if (memberKind === 'node') {
    payload.node_id = targetId;
  } else {
    payload.user_id = targetId;
  }
  return await AppHttp.post('/room/remove', payload);
}

/**
 * 获取个人工作空间 ID
 */
async function getPersonalWorkspaceId() {
  return await AppHttp.get('/workspace/personal');
}

/**
 * 获取个人任务列表
 */
async function listPersonalTasks() {
  return await AppHttp.get('/tasks/personal');
}

/**
 * 创建个人任务
 */
async function createPersonalTask(data) {
  return await AppHttp.post('/tasks/personal', data);
}

/**
 * 更新个人任务状态
 */
async function updatePersonalTaskStatus(taskId, ownerUserId, status) {
  return await AppHttp.put(`/tasks/personal/${taskId}/status`, {
    personal_task_id: taskId,
    owner_user_id: ownerUserId,
    status,
  });
}

/**
 * 重新调度个人任务
 */
async function reschedulePersonalTask(taskId, ownerUserId, dueTime) {
  return await AppHttp.put(`/tasks/personal/${taskId}/reschedule`, {
    personal_task_id: taskId,
    owner_user_id: ownerUserId,
    due_time: String(dueTime),
  });
}

/**
 * 创建设备绑定会话
 */
async function createDeviceBindSession(data) {
  return await AppHttp.post('/devices/bind-sessions', data || {});
}

/**
 * 查询设备绑定会话
 */
async function getDeviceBindSession(bindToken) {
  return await AppHttp.get(`/devices/bind-sessions/${encodeURIComponent(bindToken)}`);
}

/**
 * 确认绑定设备
 */
async function confirmDeviceBind(bindToken, data) {
  return await AppHttp.post(`/devices/bind-sessions/${encodeURIComponent(bindToken)}/confirm`, data || {});
}

/**
 * 获取已绑定设备
 */
async function listBoundDevices() {
  return await AppHttp.get('/devices');
}

/**
 * 解绑设备
 */
async function unbindDevice(deviceId) {
  return await AppHttp.delete(`/devices/${encodeURIComponent(deviceId)}`);
}

/**
 * 获取会话帖子列表
 */
async function listConversationPosts(conversationId) {
  return await AppHttp.get(`/posts?conversation_id=${conversationId}`);
}

/**
 * 创建会话帖子
 */
async function createConversationPost(data) {
  return await AppHttp.post('/posts', data);
}

/**
 * 更新会话帖子
 */
async function updateConversationPost(postId, conversationId, data) {
  return await AppHttp.put(`/posts/${postId}`, {
    postId,
    conversationId,
    ...data,
  });
}

/**
 * 关闭会话帖子
 */
async function closeConversationPost(postId, conversationId) {
  return await AppHttp.put(`/posts/${postId}/close`, {
    postId,
    conversationId,
  });
}

/**
 * 响应会话帖子
 */
async function respondToConversationPost(data) {
  return await AppHttp.post('/posts/respond', data);
}

/**
 * 提交作业帖子响应
 */
async function submitHomeworkResponse(conversationId, postId, note, file) {
  const formData = new FormData();
  formData.append('conversation_id', conversationId);
  formData.append('post_id', postId);
  if (note) formData.append('note', note);
  if (file) formData.append('file', file);
  return await AppHttp.post('/posts/homework/submit', formData, {
    headers: { 'Content-Type': 'multipart/form-data' },
  });
}

/**
 * 健康检查
 */
async function healthCheck() {
  return await AppHttp.get('/health');
}

/**
 * 加入频道
 */
async function joinChannel(channel, sessionId) {
  try {
    await AppHttp.post('/send', {
      cmd: 'JOIN',
      channel,
      ts: Date.now(),
      session_id: sessionId || '0',
    });
  } catch {
    // best-effort
  }
}

/**
 * 发送消息到频道
 */
async function sendPrivmsg(channel, message, sessionId) {
  return await AppHttp.post('/send', {
    cmd: 'PRIVMSG',
    channel: channel.startsWith('#') ? channel : `#${channel}`,
    message,
    ts: Math.floor(Date.now() / 1000),
    session_id: sessionId || '0',
  });
}

/**
 * 长轮询接收消息
 */
async function pollMessages(sessionId, channels, lastTs, timeoutMs = 25000) {
  return await AppHttp.post('/poll', {
    session_id: sessionId,
    channels: channels.map(c => c.startsWith('#') ? c : `#${c}`),
    last_ts: lastTs || 0,
    timeout_ms: timeoutMs,
  }, { timeoutMs: timeoutMs + 5000 });
}

/**
 * 获取会话历史消息
 */
async function getConversationHistory(conversationId, limit = 50) {
  return await AppHttp.get(`/history?conversation_id=${encodeURIComponent(conversationId)}&limit=${limit}`);
}

/**
 * 调用 OpenClaw Agent 获取 AI 回复（SSE 流式）
 * 对齐 Vue 版 openclawAgentProvider.buildOpenClawMessage + buildBridgeBody：
 *   1. message 字段构建为 AI_TASK_REQUEST 协议格式（含上下文指令）
 *   2. 检测卡片生成请求时追加 h5-cards 技能指令
 *   3. sessionKey 按会话作用域隔离
 *   4. 合理的超时配置
 * @param {string} message - 用户消息
 * @param {string} conversationId - 会话 ID
 * @param {string} sessionId - 会话标识
 * @param {object} callbacks - { onDelta(text, fullText), onDone(fullText), onError(err), onToolResult(content) }
 * @param {object} opts - { conversationContext: string[] }
 * @returns {Promise<{text: string}>}
 */
// 对齐 Vue 版 OPENCLAW_AGENTCHAT_PROTOCOL_CONTEXT
const _OPENCLAW_PROTOCOL_CONTEXT = [
  'AgentChat AI provider. Read AI_TASK_REQUEST JSON and answer in concise Simplified Chinese.',
  'Use tools only when needed. For generated files/images/audio/pages, return a public http/https URL.',
  'For generated HTML pages, upload them with Content-Type text/html; charset=utf-8 and inline disposition so browsers and iframes render the page instead of downloading it.',
  'If AI_TASK_REQUEST.resourceNaming exists, pass its objectKeyPrefix to upload tools when supported.',
  'Do not expose tool names, logs, code, PowerShell, base64, HTML errors, or debugging details.',
].join('\n');

// 对齐 Vue 版 isExplicitAiCardRequest
function _isExplicitAiCardRequest(query) {
  const q = String(query || '').toLowerCase();
  return /卡片|card|连环画|漫画|comic|预览/.test(q)
    && /生成|创建|制作|渲染|做一个|来一个|帮我|发一个|发给|send|create|generate|make|render/.test(q);
}

// 对齐 Vue 版 buildOpenClawMessage
function _buildOpenClawMessage(userQuery, conversationContext) {
  const parts = [
    _OPENCLAW_PROTOCOL_CONTEXT,
    '',
    'AI_TASK_REQUEST:',
    JSON.stringify({ input: { text: userQuery } }),
  ];
  if (_isExplicitAiCardRequest(userQuery)) {
    parts.push(
      '',
      'Card generation requirement:',
      'Use the h5-cards skill when available. Return the generated self-contained HTML as a public http/https URL.',
      'The HTML URL must open directly in a browser/webview; do not return a COS object configured as a file download.',
      'Do not return only card JSON. The frontend needs a webview attachment URL to open the card.',
    );
  }
  if (conversationContext && conversationContext.length) {
    parts.push('', 'Recent context:', ...conversationContext);
  }
  return parts.join('\n');
}

// 对齐 Vue 版 toOpenClawSessionSegment + scopeOpenClawConfig
function _scopeSessionKey(conversationId) {
  const scope = String(conversationId || '').trim().replace(/[^a-zA-Z0-9_.:-]+/g, '_').slice(0, 96) || 'default';
  return scope === 'default' ? 'main' : `agent-chat:${scope}:gen1`;
}

async function callAgent(message, conversationId, sessionId, callbacks, opts = {}) {
  const { onDelta, onDone, onError } = callbacks || {};
  const controller = new AbortController();
  const timeoutMs = 600000; // 对齐 Vue 版 pollTimeoutMs

  const chatCfg = AppConfig.getChatConfig();
  const baseUrl = chatCfg.openclawBridgeUrl || chatCfg.httpBaseUrl || 'http://localhost:8787';
  const url = `${baseUrl}/api/openclaw-agent`;

  const timer = setTimeout(() => {
    controller.abort();
    onError?.(new Error('Agent request timed out'));
  }, timeoutMs);

  try {
    // 对齐 Vue 版 buildBridgeBody：构建 AI_TASK_REQUEST 协议消息
    const openclawMessage = _buildOpenClawMessage(message, opts.conversationContext);
    const sessionKey = _scopeSessionKey(conversationId);

    const resp = await fetch(url, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        sessionKey,
        message: openclawMessage,
        rawMessage: message,
        conversationId: conversationId || 'default',
        sessionId: sessionId || '0',
        stream: true,
        timeoutMs: 10000,
        pollTimeoutMs: 600000,
      }),
      signal: controller.signal,
    });

    clearTimeout(timer);

    if (!resp.ok) {
      const errText = await resp.text().catch(() => '');
      throw new Error(errText || `Agent API error: ${resp.status}`);
    }

    const contentType = resp.headers.get('content-type') || '';

    if (contentType.includes('text/event-stream')) {
      // SSE 流式响应
      const reader = resp.body.getReader();
      const decoder = new TextDecoder();
      let fullText = '';
      let buffer = '';

      while (true) {
        const { done, value } = await reader.read();
        if (done) break;

        buffer += decoder.decode(value, { stream: true });
        const lines = buffer.split('\n');

        // 保留最后一个不完整的行
        buffer = lines.pop() || '';

        for (const line of lines) {
          const trimmed = line.trim();
          if (!trimmed.startsWith('data: ')) continue;

          const jsonStr = trimmed.slice(6);
          if (!jsonStr) continue;

          try {
            const event = JSON.parse(jsonStr);

            if (event.type === 'delta' && event.text) {
              fullText += event.text;
              onDelta?.(event.text, fullText);
            } else if (event.type === 'tool_result') {
              // 对齐 Vue 版 requestBridgeStream onToolResult：
              // OpenClaw 上传 H5 卡片后的 URL 在 tool_result.content 中
              const toolResultContent = String(event.content || '').trim();
              if (toolResultContent) {
                callbacks?.onToolResult?.(toolResultContent);
              }
            } else if (event.type === 'final') {
              const finalText = event.text || fullText;
              onDone?.(finalText);
              return { text: finalText };
            } else if (event.type === 'error') {
              throw new Error(event.message || 'Agent stream error');
            }
          } catch (parseErr) {
            if (parseErr instanceof SyntaxError) continue;
            throw parseErr;
          }
        }
      }

      // 流结束但没有 final 事件
      if (fullText) {
        onDone?.(fullText);
        return { text: fullText };
      }
      onDone?.('');
      return { text: '' };
    } else {
      // JSON 响应（非流式）
      const data = await resp.json();
      if (data.ok && data.text) {
        onDelta?.(data.text, data.text);
        onDone?.(data.text);
        return { text: data.text };
      } else if (data.error) {
        throw new Error(data.error);
      }
      onDone?.('');
      return { text: '' };
    }
  } catch (err) {
    clearTimeout(timer);
    if (err.name === 'AbortError') {
      console.warn('[ChatApi] Agent request aborted');
      return { text: '' };
    }
    onError?.(err);
    return { text: '', error: err.message };
  }
}

/**
 * 广播 Agent 回复到群聊其他成员（对齐 Vue 版 AGENT_BROADCAST）
 * 服务端会将此消息以 PRIVMSG(session_id=0, from=炎图AI助手) 广播给除发送者外的在线成员。
 * 发送者本地已展示 agent 气泡，不会收到服务端的 222。
 */
async function broadcastAgentReply(channel, message, sessionId, attachments = []) {
  return await AppHttp.post('/send', {
    cmd: 'AGENT_BROADCAST',
    channel: channel.startsWith('#') ? channel : `#${channel}`,
    message,
    ts: Math.floor(Date.now() / 1000),
    session_id: sessionId || '0',
    attachments,
  });
}

export default {
  listConversations,
  createConversation,
  getRoomInfo,
  listRoomMembers,
  inviteMember,
  removeMember,
  getPersonalWorkspaceId,
  listPersonalTasks,
  createPersonalTask,
  updatePersonalTaskStatus,
  reschedulePersonalTask,
  createDeviceBindSession,
  getDeviceBindSession,
  confirmDeviceBind,
  listBoundDevices,
  unbindDevice,
  listConversationPosts,
  createConversationPost,
  updateConversationPost,
  closeConversationPost,
  respondToConversationPost,
  submitHomeworkResponse,
  healthCheck,
  joinChannel,
  sendPrivmsg,
  pollMessages,
  getConversationHistory,
  callAgent,
  broadcastAgentReply,
};
