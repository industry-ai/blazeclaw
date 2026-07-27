/* ================================================================
   业务逻辑门面（Bridge）
   ----------------------------------------------------------------
   职责：页面与业务逻辑的唯一边界。
   - 页面只调用 Bridge 的方法，不直接访问任何网络/桥接服务
   - 所有方法通过 postMessage 与 C++ 原生宿主通信（见 core.js）
   - 响应回填到 state.js 视图缓存，页面通过 subscribe 重渲染
   ================================================================ */

import core from './core.js';
import state from './state.js';
import AppConfig from '../config.js';
import { chatroomBridgeRequest, onChatroomPush } from '../transport/chatroomTransport.js';
import chatHistoryStore from '../stores/chatHistoryStore.js';
import {
  resolveInteractiveResourceContext,
  buildHostBridgeContextMessage,
  buildHostConversationMessage,
  buildHostSkillResultMessage,
  parseInteractiveResourceEvent,
} from '../protocol/interactiveResourceBridge.js';
import {
  COLLABORATION_ACTIONS,
  isExpired as isCollabExpired,
} from '../protocol/collaborationProtocol.js';
import {
  COLLABORATION_CHAT_PREFIX,
  isCollaborationChatMessage,
  parseCollaborationChatMessage,
} from '../protocol/collaborationChatEnvelope.js';
import {
  createAgentTaskNo,
  buildOpenClawMessage,
} from '../protocol/aiTaskRequest.js';
import {
  appendInlineAttachments,
  stripInlineLinks,
  sanitizeAgentReply,
  dedupeAttachments,
  isImageUrl,
  isGeneratedH5CardUrl,
  titleFromUrl,
} from '../protocol/agentReplyParser.js';
import { tryParseReminderDraft, hasReminderKeyword } from '../protocol/reminderParser.js';

let _initialized = false;
let _connected = false;
const _agentStreamHandlers = new Set();

// ── 接收方遥流跟踪表 ──
// 当其他群成员触发 AI 回复时，C++ 会把 agent.turn.delta/final 推送给所有群成员。
// 发送方通过 _requestAgentReply + onAgentStream 处理（消息带 _isLocalStream 标记）。
// 接收方没有 handler 订阅，需要靠此表跟踪 receiver 侧的 streaming 占位消息。
const _remoteStreamingMsgs = new Map(); // convId -> { msgId, acc, safetyTimer }

// ── 协同指令去重表（对齐 origin-vanilla chatStore.js _collabDedupeKeys）──
// c:agentchat.collaboration 消息可能因网络重传/多端转发而重复到达，
// 用 dedupeKey/dispatchId 去重，避免同一指令被处理多次（重复冒系统消息）
const _collabDedupeKeys = new Map(); // key -> expiresAt
const _COLLAB_DEDUPE_TTL_MS = 5 * 60 * 1000; // 5 分钟

// ── 交互资源 Bridge 状态（H5 iframe <-> JS postMessage 桥接）──
// 对齐 origin-vanilla chatStore.js 的 _interactiveBridgeState
// 注意：保存 getFrameWindow 回调而非 frameWindow 值，保证每次发送
// bridge_context / conversation_message 时都能拿到最新的 iframe.contentWindow，
// 避免 interactive.ready 在 iframe.onload 之前到达时 frameWindow 为 null
let _interactiveBridgeState = {
  active: false,
  context: null,          // InteractiveResourceContext
  getFrameWindow: null,   // () => Window | null
  bridgeReady: false,
  forwardedMessageIds: new Set(),
  suppressedTexts: new Map(),
  _messageHandler: null,
};

// ── 聊天记录本地持久化（对齐 chatHistoryStore 防抖写入）──
let _persistTimer = null;
function _syncHistoryStoreUserId() {
  const uid = state.getUserId() || state.getPhone() || '';
  chatHistoryStore.setUserId(uid);
}
function _persistMessages() {
  if (_persistTimer) clearTimeout(_persistTimer);
  _persistTimer = setTimeout(() => {
    _persistTimer = null;
    const convs = state.getConversations();
    for (const conv of convs) {
      const msgs = state.getMessages(conv.id);
      if (msgs && msgs.length) {
        chatHistoryStore.saveMessages(conv.id, msgs);
      }
    }
  }, 1000);
}

// ── Agent deliveryId 去重（对齐原项目 _GROUP_AGENT_REPLY_RELAY_PREFIX 机制）──
// C++ 广播 agent 回复时，消息可能含两种标记：
// 1. relay 信封：[::GROUP_AGENT_REPLY::]{encodeURIComponent(JSON)} 整条消息是信封
// 2. delivery marker：⁣agent-delivery:<deliveryId>⁣ 嵌在消息文本末尾
// 接收方通过 deliveryId 去重，避免发送者重复展示。
const _GROUP_AGENT_REPLY_RELAY_PREFIX = '[::GROUP_AGENT_REPLY::]';
const _GROUP_AGENT_DELIVERY_MARKER_RE = /\u2063agent-delivery:([A-Za-z0-9%_.~:-]+)\u2063/g;
// Agent 打字指示器标记（对齐原项目 httpIrcTransport.js:12）
// C++ 通过 PRIVMSG 广播 [::AGENT_TYPING::]{"s":"started"/"stopped","r":"...","t":<ttl>} 信封，
// 接收方必须拦截该 marker，绝不能当成普通文本消息显示（否则显示为乱码）。
const _AGENT_TYPING_MARKER = '[::AGENT_TYPING::]';

function _decodeGroupAgentReplyRelay(content) {
  const text = String(content || '');
  if (!text.startsWith(_GROUP_AGENT_REPLY_RELAY_PREFIX)) return null;
  try {
    const payload = JSON.parse(decodeURIComponent(text.slice(_GROUP_AGENT_REPLY_RELAY_PREFIX.length)));
    const deliveryId = String(payload.deliveryId || '').trim();
    const message = String(payload.message || '').trim();
    if (!deliveryId || !message) return null;
    return {
      deliveryId,
      message,
      attachments: Array.isArray(payload.attachments) ? payload.attachments : undefined,
    };
  } catch (e) {
    return null;
  }
}

function _extractGroupAgentDeliveryMarker(content) {
  const text = String(content || '');
  let deliveryId;
  const cleaned = text.replace(_GROUP_AGENT_DELIVERY_MARKER_RE, (_match, rawId) => {
    if (!deliveryId) {
      try { deliveryId = decodeURIComponent(rawId); } catch (e) { deliveryId = rawId; }
    }
    return '';
  }).trim();
  return { content: cleaned, deliveryId };
}

/**
 * 统一解码 agent 消息：relay 信封 -> delivery marker -> _parseAgentReply
 * @param {string} text - 原始消息文本
 * @param {boolean} isAgent - 是否已判定为 agent 消息
 * @returns {{ text: string, attachments: array, agentDeliveryId: string|null, isAgent: boolean }}
 */
function _decodeAgentMessage(text, isAgent, extraAttachments = []) {
  let message = String(text || '').trim();
  let agentDeliveryId = null;
  let relayAttachments = [];

  // 1. 尝试解码 relay 信封（整条消息是信封的情况）
  if (message.startsWith(_GROUP_AGENT_REPLY_RELAY_PREFIX)) {
    const relay = _decodeGroupAgentReplyRelay(message);
    if (relay) {
      message = relay.message;
      agentDeliveryId = relay.deliveryId;
      relayAttachments = relay.attachments || [];
      isAgent = true; // 信封存在即视为 agent 广播
    }
  }

  // 2. 提取 delivery marker（嵌在消息文本末尾的情况）
  if (isAgent) {
    const marker = _extractGroupAgentDeliveryMarker(message);
    message = marker.content;
    if (marker.deliveryId) agentDeliveryId = marker.deliveryId;
  }

  // 3. Agent 消息解析卡片附件、清理 JSON 碎片
  let finalText = message;
  let attachments = [...relayAttachments, ...extraAttachments];
  if (isAgent) {
    const parsed = _parseAgentReply(message, attachments);
    finalText = parsed.text;
    attachments = parsed.attachments;
  }

  return { text: finalText, attachments, agentDeliveryId, isAgent };
}

// ── 鉴权持久化（对齐原项目 AuthStore 的 localStorage 机制）──
const AUTH_STORAGE_KEYS = {
  jwt: 'auth.jwt',
  sessionId: 'auth.session_id',
  userId: 'auth.user_id',
  phone: 'auth.phone',
};

function _storageGet(key) {
  try { return localStorage.getItem(key) || ''; } catch (e) { return ''; }
}
function _storageSet(key, value) {
  try { localStorage.setItem(key, String(value)); } catch (e) {}
}
function _storageRemove(key) {
  try { localStorage.removeItem(key); } catch (e) {}
}

/**
 * 初始化鉴权状态（对齐原项目 AuthStore.init 的逻辑）：
 * 1. 优先检查 C++ 宿主直接注入的 window.__INJECTED_AUTH__
 * 2. 回退到 localStorage 持久化数据（页面刷新后恢复）
 */
function _initAuth() {
  const injected = window.__INJECTED_AUTH__ || {};
  if (injected.token || injected.jwt) {
    _applyInjectedAuthInternal(injected);
    return;
  }
  // 回退到 localStorage
  const jwt = _storageGet(AUTH_STORAGE_KEYS.jwt);
  if (jwt.trim()) {
    state._setters.auth({
      isLoggedIn: true,
      phone: _storageGet(AUTH_STORAGE_KEYS.phone),
      userId: _storageGet(AUTH_STORAGE_KEYS.userId),
      sessionId: _storageGet(AUTH_STORAGE_KEYS.sessionId),
      jwt,
    });
    _syncHistoryStoreUserId();
    state._setters.notify();
  }
}

/**
 * 内部：应用注入的鉴权信息并持久化到 localStorage
 * 兼容 C++ 注入的 token 字段（映射为 jwt）
 */
function _applyInjectedAuthInternal(injected) {
  const phone = String(injected.phone || '').trim();
  const userId = String(injected.userId || '').trim();
  const sessionId = String(injected.sessionId || '').trim();
  const jwt = injected.token || injected.jwt || '';
  state._setters.auth({ isLoggedIn: true, phone, userId, sessionId, jwt });
  // 持久化到 localStorage（刷新后可恢复）
  _storageSet(AUTH_STORAGE_KEYS.jwt, jwt);
  _storageSet(AUTH_STORAGE_KEYS.phone, phone);
  _storageSet(AUTH_STORAGE_KEYS.userId, userId);
  if (sessionId) _storageSet(AUTH_STORAGE_KEYS.sessionId, sessionId);
  _syncHistoryStoreUserId();
  state._setters.notify();
}

/**
 * 初始化桥接：连接推送通道，恢复鉴权状态
 */
function init() {
  if (_initialized) return;
  _initialized = true;
  core.init();

  // 恢复鉴权状态（检查 window.__INJECTED_AUTH__ 或 localStorage）
  _initAuth();

  // 监听 C++ 宿主注入鉴权事件（运行时重新注入时触发）
  // 对齐原项目：事件仅作信号，从 window.__INJECTED_AUTH__ 读取鉴权数据
  window.addEventListener('__auth_injected__', (event) => {
    const injected = window.__INJECTED_AUTH__ || (event && event.detail) || {};
    if (injected && typeof injected === 'object' && (injected.token || injected.jwt)) {
      _applyInjectedAuthInternal(injected);
    }
  });

  // 订阅 agentchat 推送（chat.push、agent 流式等）
  core.onPush(_handlePush);
  // 订阅 chatroom 推送（IRC 风格事件：privmsg/join/part 等，群聊实时消息）
  onChatroomPush(_handleChatroomPush);
  // 订阅 state 变化，把新消息转发给已打开的 H5 iframe Bridge
  // （_forwardPendingMessages 是幂等的，仅转发 _forwardedMessageIds 中没有的新消息）
  state.subscribe(_notifyInteractiveBridge);
  // 订阅 state 变化，防抖持久化聊天记录到 localStorage
  state.subscribe(_persistMessages);

  // 启动通知轮询定时器（每5秒检查到期提醒）
  _startNotificationPolling();
}

// ── 推送事件处理（实时消息、Agent 流式、群邀请等）──

/**
 * 追加消息或去重（处理 echo 回显：乐观消息 delivery:sending -> sent）
 * @param {string} convId - 会话 ID（带 # 前缀，与 conversation.id 一致）
 * @param {object} msg - 消息对象，必须包含 author 字段
 */
function _appendOrDedupMessage(convId, msg) {
  const list = state.getMessages(convId) || [];

  // 按 messageId 去重
  if (list.some((m) => m.id === msg.id)) return;

  // agentDeliveryId 去重（对齐原项目：发送者本地已展示带相同 deliveryId 的 agent 消息时，丢弃服务端广播的重复消息）
  if (msg.agentDeliveryId && list.some((m) => m.agentDeliveryId === msg.agentDeliveryId)) {
    console.log('[bridge] duplicate agent delivery ignored', { convId, agentDeliveryId: msg.agentDeliveryId });
    return;
  }

  // AI 回复文本去重：agent.turn.final 已更新占位气泡后，chat.push 可能再推送一遍相同回复。
  // 此时占位已是 complete 状态，无法通过 agentDeliveryId 去重（可能无 deliveryId），
  // 通过文本匹配丢弃重复的 agent 消息。
  if (msg.author === 'agent' && msg.text && list.some((m) =>
    m.author === 'agent' && m.status === 'complete' && m.text === msg.text
  )) {
    console.log('[bridge] duplicate agent reply ignored', { convId, text: msg.text.slice(0, 80) });
    return;
  }

  // echo 去重：自己发送的消息回显时，匹配列表中相同文本的 sending/sent 消息
  // 需要匹配 sent 状态：send_message 响应可能先于 push echo 到达，
  // 此时乐观消息已从 sending 变为 sent，若只匹配 sending 会导致重复追加
  const isSelfEcho = msg.author === 'user';
  const pendingIdx = isSelfEcho ? list.findIndex((m) =>
    (m.delivery === 'sending' || m.delivery === 'sent') &&
    m.text === msg.text && m.convId === convId
  ) : -1;
  const isEcho = pendingIdx >= 0;
  if (isEcho) {
    list[pendingIdx] = { ...list[pendingIdx], delivery: 'sent', ts: msg.ts, createdAt: msg.createdAt || msg.ts, id: msg.id };
    state._setters.messages(convId, list);
  } else {
    state._setters.appendMessage(convId, msg);
  }

  // 更新会话列表中的最后消息和未读数（echo 回显不增加未读数）
  const conv = state.getConversations().find((c) => c.id === convId);
  if (conv) {
    conv.lastMessage = msg.text;
    conv.lastTs = msg.ts;
    if (!isEcho && convId !== state.getActiveConversationId()) {
      conv.unreadCount = (conv.unreadCount || 0) + 1;
    }
  }
  state._setters.notify();
}

/**
 * 规范化频道名为带 # 前缀（与会话 ID 格式一致）
 */
function _normalizeChannel(channel) {
  let ch = String(channel || '').trim();
  if (!ch) return '';
  if (!ch.startsWith('#')) ch = '#' + ch;
  return ch;
}

/**
 * 归一化时间戳为毫秒级。
 * C++ 推送的时间戳可能是秒级（< 10_000_000_000）或毫秒级，
 * 若为秒级则 ×1000，无效值回退到 Date.now()。
 * 这样确保 createdAt 可与 lastSeen（Date.now() 毫秒级）正确比较。
 */
function _normalizeTs(ts) {
  const n = Number(ts);
  if (!Number.isFinite(n) || n <= 0) return Date.now();
  if (n < 10_000_000_000) return n * 1000; // 秒级 -> 毫秒级
  return n;
}

function _handlePush(msg) {
  const { kind, payload } = msg;
  switch (kind) {
    case 'chat.push': {
      // { event, channel, data }
      const convId = _normalizeChannel(payload.channel);
      if (!convId) break;
      // 兼容 data 在 payload.data 或 payload 本身的情况
      const d = payload.data || payload;
      const evt = String(payload.event || d.event || '').toUpperCase();
      // isAgent 检测：匹配事件类型或 sender 中的 agent 关键词。
      // 注意：sender 可能是 UTF-8 被当作 GBK 解读的乱码（如 "炎图AI助手" -> "鐐"鐐庡浘AI鍔╂墜"），
      // 此时 "炎图"/"小炎" 不匹配，但 ASCII 的 "AI" 仍然保留，用 includes('AI') 兜底。
      const _senderStr = String(d.sender || d.from || d.authorId || '');
      const isAgent = evt === 'AGENT_BROADCAST' ||
        _senderStr.toLowerCase().includes('agent') ||
        _senderStr.includes('炎图') ||
        _senderStr.includes('小炎') ||
        _senderStr.includes('AI');
      if (evt === 'PRIVMSG' || evt === 'MESSAGE' || evt === 'AGENT_BROADCAST' ||
          evt === 'IRC_MESSAGE' || !evt) {
        // 兼容多种字段名
        const senderId = d.authorId || d.sender || d.from || d.author_id || d.nick || '';
        const selfId = state.getSessionId();
        const isSelf = senderId && (senderId === selfId || senderId === state.getPhone() || senderId === state.getUserId());
        const text = String(d.text || d.message || d.content || '');
        if (!text.trim()) break;
        // 拦截 AGENT_TYPING 打字指示器标记（对齐原项目 httpIrcTransport.js:362-375）
        if (text.startsWith(_AGENT_TYPING_MARKER)) {
          try {
            const parsed = JSON.parse(text.slice(_AGENT_TYPING_MARKER.length));
            console.log('[bridge] agent_typing:', parsed.s, parsed.r);
          } catch (e) { /* 静默丢弃 */ }
          break;
        }
        // 拦截协同协议消息（对齐 origin-vanilla chatStore.js:502-510）
        // c:agentchat.collaboration 前缀的消息不作为普通聊天展示，
        // 而是解析为 CollaborationInstruction 并执行对应动作（添加系统消息/TTS/卡片等）
        if (isCollaborationChatMessage(text)) {
          const parsed = parseCollaborationChatMessage(text);
          if (parsed) {
            _handleCollaborationInstruction(convId, parsed.instruction,
              d.id || d.message_id, d.ts || d.timestamp_ms || Date.now());
          }
          break;
        }
        // 统一解码 agent 消息：relay 信封 -> delivery marker -> 解析卡片附件/清理 JSON
        // 如果存在 streaming agent 占位符，且消息来自本地用户（C++ 可能通过 sender session
        // 转发 AI 回复）或 sender 已匹配 agent 关键词，才强制 isAgent=true 走完整解析管线。
        // 其他群成员的消息不应覆盖 AI 占位气泡，而应追加到列表末尾。
        const _hasStreamingPlaceholder = (state.getMessages(convId) || []).some(
          (m) => m.author === 'agent' && m.status === 'streaming'
        );
        const _forceAgent = _hasStreamingPlaceholder && (isSelf || isAgent);
        const decoded = _decodeAgentMessage(text, isAgent || _forceAgent);

        // 占位消息更新：如果有 streaming 占位且当前消息是 AI 回复（非用户回显、非其他用户），
        // 更新占位而非新增消息。C++ 可能通过 chat.push 推送 AI 回复（sender 可能不是 agent）。
        {
          const msgList = state.getMessages(convId) || [];
          const placeholder = msgList.find((m) => m.author === 'agent' && m.status === 'streaming');
          if (placeholder) {
            // 排除用户自身回显（相同文本的 sending/sent 消息）
            const isEcho = isSelf && msgList.some((m) =>
              (m.delivery === 'sending' || m.delivery === 'sent') &&
              m.text === decoded.text && m.convId === convId
            );
            // 用户回显 -> 直接丢弃，不更新占位也不追加新消息
            if (isEcho) break;
            // 排除其他用户的普通消息（既非自身也非 agent）
            const isOtherUser = !isSelf && !decoded.isAgent;
            if (!isOtherUser) {
              // 是 prompt/系统指令而非 AI 回复 -> 丢弃，保持 streaming 占位等待真正回复
              if (_isPromptText(decoded.text)) {
                break;
              }
              // 清理接收方遥流跟踪表（如有）
              const remoteStream = _remoteStreamingMsgs.get(convId);
              if (remoteStream && remoteStream.msgId === placeholder.id) {
                if (remoteStream.safetyTimer) { clearTimeout(remoteStream.safetyTimer); remoteStream.safetyTimer = null; }
                _remoteStreamingMsgs.delete(convId);
              }
              state._setters.updateMessage(convId, placeholder.id, {
                text: decoded.text,
                attachments: decoded.attachments,
                agentDeliveryId: decoded.agentDeliveryId || undefined,
                status: 'complete',
                _isLocalStream: false,
              });
              state._setters.notify();
              break; // 不走 _appendOrDedupMessage，避免重复
            }
          }
        }

        _appendOrDedupMessage(convId, {
          id: d.id || d.message_id || `push-${Date.now()}-${convId}`,
          convId,
          author: decoded.isAgent ? 'agent' : (isSelf ? 'user' : 'other_user'),
          authorId: senderId,
          authorName: isSelf ? '我' : (decoded.isAgent ? '炎图AI助手' : (d.authorName || d.author || senderId || '群成员')),
          text: decoded.text,
          attachments: decoded.attachments,
          agentDeliveryId: decoded.agentDeliveryId || undefined,
          ts: d.ts || d.timestamp_ms || d.timestamp || Date.now(),
          createdAt: d.ts || d.createdAt || d.timestamp_ms || Date.now(),
          isAgent: decoded.isAgent,
        });
      }
      break;
    }
    case 'chat.agent_typing':
      // 通知页面 Agent 正在输入
      state._setters.notify();
      break;
    case 'chat.group_invited':
      state._setters.groupInvitationNotifications([
        ...(state.getGroupInvitationNotifications()),
        { ...(payload || {}), createdAt: Date.now() },
      ]);
      state._setters.notify();
      break;
    case 'agent.turn.delta': {
      const convId = payload.conversationId || payload.convId;
      // 1. 转发给发送方的 onAgentStream handler
      for (const h of _agentStreamHandlers) { try { h({ type: 'delta', text: payload.text, conversationId: convId }); } catch (e) {} }
      // 2. 接收方遥流：无本地 streaming 占位时，为接收方创建/更新气泡
      _handleRemoteAgentDelta(convId, payload.text);
      break;
    }
    case 'agent.turn.final': {
      const convId = payload.conversationId || payload.convId;
      const attachments = payload.attachments || [];
      for (const h of _agentStreamHandlers) { try { h({ type: 'final', text: payload.text, attachments, conversationId: convId }); } catch (e) {} }
      _handleRemoteAgentFinal(convId, payload.text, attachments);
      break;
    }
    case 'agent.turn.error': {
      const convId = payload.conversationId || payload.convId;
      for (const h of _agentStreamHandlers) { try { h({ type: 'error', message: payload.message, conversationId: convId }); } catch (e) {} }
      _handleRemoteAgentError(convId, payload.message);
      break;
    }
    default:
      break;
  }
}

// ── chatroom 推送事件处理（IRC 风格：privmsg/join/part 等）──
// C++ 通过 chatroom.bridge.push 通道推送 IRC 事件，此处接收并转换为视图状态更新
function _handleChatroomPush(push) {
  const { eventType, payload } = push;
  const et = String(eventType || '').toLowerCase();
  const channel = _normalizeChannel(payload.channel);
  if (!channel) return;

  // 群聊消息事件：兼容多种事件类型名称
  // privmsg / irc_message_resp / message / chat_message / privmsg_resp
  const isMessageEvent = et === 'privmsg' || et === 'irc_message_resp' ||
    et === 'message' || et === 'chat_message' || et === 'privmsg_resp' ||
    et === 'msg' || et === 'text';

  if (isMessageEvent) {
    // 兼容多种字段名：sender/from/nick/authorId/author_id
    const sender = String(
      payload.sender || payload.from || payload.nick ||
      payload.authorId || payload.author_id || payload.author || ''
    );
    // 兼容多种字段名：message/text/content/data
    const message = String(
      payload.message || payload.text || payload.content || payload.data || ''
    );
    const ts = payload.timestamp_ms || payload.ts || payload.timestamp ||
      push.timestampMs || Date.now();

    // 空消息不处理
    if (!message.trim()) return;

    // 拦截 AGENT_TYPING 打字指示器标记（对齐原项目 httpIrcTransport.js:362-375）
    // 这些是打字指示器信号，不是实际消息，绝不能当作文本显示
    if (message.startsWith(_AGENT_TYPING_MARKER)) {
      try {
        const parsed = JSON.parse(message.slice(_AGENT_TYPING_MARKER.length));
        console.log('[bridge] agent_typing:', parsed.s, parsed.r);
      } catch (e) { /* 解析失败也静默丢弃 */ }
      return;
    }

    // 拦截协同协议消息（对齐 origin-vanilla chatStore.js:502-510）
    if (isCollaborationChatMessage(message)) {
      const parsed = parseCollaborationChatMessage(message);
      if (parsed) {
        _handleCollaborationInstruction(channel, parsed.instruction,
          payload.id || payload.message_id, ts);
      }
      return;
    }

    // 判断是否为自己发送的消息（用于显示"我"）
    const selfId = state.getSessionId();
    const isSelf = sender && (sender === selfId || sender === state.getPhone() || sender === state.getUserId());

    // 判断是否为 Agent 消息（同 chat.push handler：含 includes('AI') 乱码兜底）
    const isAgent = String(sender).toLowerCase().includes('agent') ||
      String(sender).includes('炎图') ||
      String(sender).includes('小炎') ||
      String(sender).includes('AI') ||
      payload.isAgent === true;

    // 统一解码 agent 消息：relay 信封 -> delivery marker -> 解析卡片附件/清理 JSON
    // 同 chat.push handler 逻辑：仅当消息来自本地用户或 sender 已匹配 agent 关键词时，
    // 才强制 isAgent=true。其他群成员的消息不覆盖 AI 占位气泡。
    const _hasStreamingPlaceholder = (state.getMessages(channel) || []).some(
      (m) => m.author === 'agent' && m.status === 'streaming'
    );
    const _forceAgent = _hasStreamingPlaceholder && (isSelf || isAgent);
    const decoded = _decodeAgentMessage(message, isAgent || _forceAgent);

    // 占位消息更新：同 chat.push handler 的逻辑
    {
      const msgList = state.getMessages(channel) || [];
      const placeholder = msgList.find((m) => m.author === 'agent' && m.status === 'streaming');
      if (placeholder) {
        const isEcho = isSelf && msgList.some((m) =>
          (m.delivery === 'sending' || m.delivery === 'sent') &&
          m.text === decoded.text && m.convId === channel
        );
        // 用户回显 -> 直接丢弃，不更新占位也不追加新消息
        if (isEcho) return;
        const isOtherUser = !isSelf && !decoded.isAgent;
        if (!isOtherUser) {
          // 是 prompt/系统指令而非 AI 回复 -> 丢弃，保持 streaming 占位等待真正回复
          if (_isPromptText(decoded.text)) {
            return;
          }
          const remoteStream = _remoteStreamingMsgs.get(channel);
          if (remoteStream && remoteStream.msgId === placeholder.id) {
            if (remoteStream.safetyTimer) { clearTimeout(remoteStream.safetyTimer); remoteStream.safetyTimer = null; }
            _remoteStreamingMsgs.delete(channel);
          }
          state._setters.updateMessage(channel, placeholder.id, {
            text: decoded.text,
            attachments: decoded.attachments,
            agentDeliveryId: decoded.agentDeliveryId || undefined,
            status: 'complete',
            _isLocalStream: false,
          });
          state._setters.notify();
          return; // 不走 _appendOrDedupMessage
        }
      }
    }

    _appendOrDedupMessage(channel, {
      id: `irc-${ts}-${sender}-${channel}`,
      convId: channel,
      author: decoded.isAgent ? 'agent' : (isSelf ? 'user' : 'other_user'),
      authorId: sender,
      authorName: isSelf ? '我' : (decoded.isAgent ? '炎图AI助手' : (sender || '群成员')),
      text: decoded.text,
      attachments: decoded.attachments,
      agentDeliveryId: decoded.agentDeliveryId || undefined,
      ts,
      createdAt: ts,
      isAgent: decoded.isAgent,
    });
    return;
  }

  // ── 通知类推送事件 ──

  // 群聊邀请：C++ 推送 group_invited 事件，创建通知
  if (et === 'group_invited') {
    console.log('[通知] 收到 C++ 推送 group_invited:', { eventType: et, channel, payload });
    // message 字段可能包含 JSON: {room_id, room_name, inviter, inviter_phone, ts}
    let roomId = channel;
    let roomName = '';
    let inviter = String(payload.sender || payload.inviter || '');
    let inviterPhone = String(payload.inviter_phone || payload.inviterPhone || '');
    const rawMsg = String(payload.message || payload.text || payload.content || '');
    try {
      if (rawMsg && rawMsg.trim().startsWith('{')) {
        const invInfo = JSON.parse(rawMsg);
        roomId = _normalizeChannel(invInfo.room_id || invInfo.roomId || roomId);
        roomName = invInfo.room_name || invInfo.roomName || '';
        inviter = invInfo.inviter || inviter;
        inviterPhone = invInfo.inviter_phone || invInfo.inviterPhone || inviterPhone;
      }
    } catch (e) {
      console.warn('[bridge] group_invited: failed to parse message JSON', e);
    }
    const convId = roomId || channel;
    // 使用前端接收时间作为 createdAt，保证 createdAt > lastSeen（角标计数依赖此比较）
    // C++ 推送的时间戳可能早于 lastSeen（app 启动时间），会导致角标永远为 0
    const ts = Date.now();
    const notification = {
      id: `group-invited:${convId}:${ts}`,
      conversationId: convId,
      roomName: roomName || convId.replace(/^#/, ''),
      inviter,
      inviterPhone,
      createdAt: ts,
    };
    console.log('[通知] group_invited 通知已创建, createdAt:', ts, 'lastSeen:', state.getLastSeenNotificationsAt());
    // 去重：同一会话只保留最新一条
    const existing = state.getGroupInvitationNotifications().filter(i => i.conversationId !== convId);
    state._setters.groupInvitationNotifications([notification, ...existing].slice(0, 50));
    state._setters.notify();
    // 被邀请进入新群聊后，重新获取会话列表（C++ 侧已创建会话，前端需刷新才能看到新群）
    console.log('[通知] group_invited: 刷新会话列表');
    loadConversations().catch((e) => console.warn('[通知] group_invited: 刷新会话列表失败', e));
    return;
  }

  // IRC NOTICE / 系统消息：创建系统通知
  if (et === 'notice') {
    console.log('[通知] 收到 C++ 推送 notice:', { eventType: et, channel, payload });
    const message = String(payload.message || payload.text || payload.content || '');
    if (!message.trim()) return;
    const sender = String(payload.sender || payload.from || payload.nick || '');
    const ts = Date.now();
    state._setters.addPushNotification({
      id: `push-notice:${channel}:${ts}`,
      kind: 'system',
      title: message.slice(0, 100),
      summary: sender ? `来自 ${sender}` : '',
      sourceName: channel.replace(/^#/, ''),
      conversationId: channel,
      createdAt: ts,
    });
    state._setters.notify();
    return;
  }

  // 被禁言：创建系统通知
  if (et === 'ban') {
    console.log('[通知] 收到 C++ 推送 ban:', { eventType: et, channel, payload });
    const sender = String(payload.sender || payload.from || payload.nick || '');
    const target = String(payload.target || payload.target_nick || payload.affected || '');
    const message = String(payload.message || payload.reason || payload.text || '');
    const ts = Date.now();
    state._setters.addPushNotification({
      id: `push-ban:${channel}:${ts}`,
      kind: 'system',
      title: `${target || '成员'} 被禁言`,
      summary: message || (sender ? `操作者：${sender}` : ''),
      sourceName: channel.replace(/^#/, ''),
      conversationId: channel,
      createdAt: ts,
    });
    state._setters.notify();
    return;
  }

  // 被踢出群聊：创建系统通知 + 本地保存被踢出群聊信息（用于会话列表展示与历史记录查看）
  if (et === 'kicked') {
    console.log('[通知] 收到 C++ 推送 kicked:', { eventType: et, channel, payload });
    const sender = payload.kicker_phone;
    // 使用前端接收时间作为 createdAt，保证 createdAt > lastSeen（角标计数依赖此比较）
    // C++ 推送的时间戳可能早于 lastSeen（app 启动时间），会导致角标永远为 0
    const ts = Date.now();
    // 本地保存被踢出的群聊信息，使会话列表仍能展示该群聊并查看本地缓存的历史记录
    const conv = (state.getConversations() || []).find((c) => c.id === channel);
    chatHistoryStore.saveKickedRoom({
      convId: channel,
      name: (conv && conv.name) || channel.replace(/^#/, ''),
      title: (conv && conv.title) || '',
      scope: (conv && conv.scope) || 'group',
      kickedAt: ts,
      reason: "",
      operator: sender,
    });
    // 标记当前会话为已踢出，UI 据此禁用发送并展示提示
    if (conv) conv.isKicked = true;
    // 创建系统通知（对齐 group_invited：去重 + createdAt 用前端接收时间）
    const pushNotif = {
      id: `push-kicked:${channel}:${ts}`,
      kind: 'system',
      title: `已被移出群聊 ${payload.room_name}`,
      summary: sender ? `您被 ${sender} 移除群聊` : '',
      sourceName: payload.room_name || channel.replace(/^#/, ''),
      conversationId: channel,
      createdAt: ts,
    };
    // 去重：同一会话只保留最新一条被踢出通知（对齐 group_invited 去重逻辑）
    const existing = state.getPushNotifications().filter((i) => !(i.id && i.id.startsWith('push-kicked:') && i.conversationId === channel));
    state._setters.pushNotifications([pushNotif, ...existing].slice(0, 100));
    // notify 触发 _updateBadges 重新计算"通知"tab 角标数（pushNotifications 中 createdAt > lastSeen 的条目会计入角标）
    state._setters.notify();
    // 刷新会话列表：把本地保存的被踢出群聊并入列表（对齐 group_invited 刷新会话列表）
    loadConversations().catch((e) => console.warn('[通知] kicked: 刷新会话列表失败', e));
    return;
  }

  // 其它信号事件（join/part/topic/mode 等）仅记录日志
  console.log('[bridge] chatroom push:', et, channel, payload);
}

// ================================================================
// 鉴权通道（原 AuthStore/AuthApi）
// ================================================================
async function sendSmsCode(phone) {
  return core.request('auth.send_code', { phone });
}
async function login(phone, code) {
  const r = await core.request('auth.login', { phone, code });
  if (r && r.ok !== false) {
    const jwt = r.jwt || '';
    const userId = r.user?.userId || r.userId || '';
    const sessionId = r.sessionId || '';
    state._setters.auth({
      isLoggedIn: true,
      phone,
      userId,
      sessionId,
      jwt,
    });
    // 持久化到 localStorage（刷新后可恢复，对齐原项目 AuthStore.login）
    _storageSet(AUTH_STORAGE_KEYS.jwt, jwt);
    _storageSet(AUTH_STORAGE_KEYS.phone, phone);
    _storageSet(AUTH_STORAGE_KEYS.userId, userId);
    if (sessionId) _storageSet(AUTH_STORAGE_KEYS.sessionId, sessionId);
    _syncHistoryStoreUserId();
    state._setters.notify();
  }
  return r;
}
async function logout() {
  try { await core.request('auth.logout', {}); } catch (e) {}
  // 清除 localStorage 鉴权数据（对齐原项目 AuthStore.logout）
  Object.values(AUTH_STORAGE_KEYS).forEach(k => _storageRemove(k));
  // 清除聊天记录、草稿与被踢出群聊缓存
  chatHistoryStore.clearAllMessages();
  chatHistoryStore.clearDrafts();
  chatHistoryStore.clearKickedRooms();
  state._setters.resetAuth();
  state._setters.resetAll();
  state._setters.notify();
}
function isLoggedIn() { return state.isLoggedIn(); }
function getPhone() { return state.getPhone(); }
function getUserId() { return state.getUserId(); }
function getSessionId() { return state.getSessionId(); }

// 支持 C++ 宿主直接注入鉴权信息（原 __auth_injected__ 机制保留）
function applyInjectedAuth(authPatch) {
  _applyInjectedAuthInternal(authPatch);
}

// ================================================================
// 聊天通道（原 ChatStore 会话/消息 + ChatApi + httpIrcTransport）
// ================================================================
async function connect(channels = []) {
  // 避免重复连接：app.js 中直接连接与 __auth_injected__ 事件可能导致重复调用，
  // 而会话列表（loadConversations）不依赖鉴权，无需重复拉取
  if (_connected) return;
  _connected = true;
  state._setters.status('connecting');
  state._setters.notify();
  try {
    // 先拉取会话列表（通过 chatroom.bridge.request 通道，不依赖 chat.connect）
    await loadConversations();
    state._setters.status('connected');
    state._setters.notify();
    // chat.connect 通过 agentchat.bridge.request 通道，失败不影响会话列表
    core.request('chat.connect', { channels, sessionId: state.getSessionId() }).catch(() => {});
  } catch (e) {
    _connected = false;
    state._setters.status('error');
    state._setters.notify();
    throw e;
  }
}
async function disconnect() {
  _connected = false;
  return core.request('chat.disconnect', {});
}
async function loadConversations() {
  const r = await chatroomBridgeRequest('list_conversations', {});
  console.log('chatroomBridgeRequest-list_conversations', r);
  // C++ 返回格式：{ channels: [...], conversations: [{ id, name, title }] }
  const conversations = (r && r.conversations) || [];
  console.log('chatroomBridgeRequest-list_conversations', conversations);
  const list = conversations.map((conv) => {
    // #workspace 开头的是"我的 AI 工作空间"，与普通群聊区分
    const isWorkspace = /(^#personal-workspace$|^#workspace[_-])/i.test(conv.room_id);
    return {
      id: conv.room_id,
      type: 'group',
      name: isWorkspace ? '我的 AI 工作空间' : (conv.name ||  conv.room_id),
      title:  conv.name,
      scope: isWorkspace ? 'personal_workspace' : 'group',
      lastMessage: '',
      lastTs: 0,
      unreadCount: 0,
      isKicked: false,
    };
  });

  // 合并本地存储的被踢出群聊：
  // - 若被踢出的群聊已重新出现在 C++ 列表中（被重新邀请），清除本地踢出标记；
  // - 否则把被踢出的群聊加入会话列表，以便查看本地缓存的历史记录。
  const kickedRooms = chatHistoryStore.getKickedRooms();
  const kickedNotInList = [];
  for (const kr of kickedRooms) {
    const existing = list.find((c) => c.id === kr.convId);
    if (existing) {
      chatHistoryStore.removeKickedRoom(kr.convId);
      existing.isKicked = false;
      // 用户被重新邀请回群聊：若当前正在查看该群聊（之前因被踢出未 join），
      // 需重新 join 频道，否则发消息会报 "send JOIN before PRIVMSG"
      if (state.getActiveConversationId() === existing.id) {
        chatroomBridgeRequest('join_channel', { channel: existing.id }).catch(() => {});
      }
    } else {
      kickedNotInList.push({
        id: kr.convId,
        type: 'group',
        name: kr.name || kr.convId.replace(/^#/, ''),
        title: kr.title || '',
        scope: kr.scope || 'group',
        lastMessage: '',
        lastTs: 0,
        unreadCount: 0,
        isKicked: true,
      });
    }
  }
  const mergedList = list.concat(kickedNotInList);

  if (mergedList.length) {
    state._setters.conversations(mergedList);
    // 从 localStorage 恢复聊天记录（页面刷新后立即显示缓存消息，不等 C++ 推送）
    mergedList.forEach((conv) => {
      const localMsgs = chatHistoryStore.loadMessages(conv.id);
      if (localMsgs.length) {
        state._setters.messages(conv.id, localMsgs);
        // 恢复会话最后消息预览
        const last = localMsgs[localMsgs.length - 1];
        if (last) { conv.lastMessage = last.text; conv.lastTs = last.ts; }
      }
    });
    state._setters.notify();
    // 暂时取消自动加入所有频道（新功能验证阶段避免干扰）
    // list.forEach((conv) => {
    //   chatroomBridgeRequest('join_channel', { channel: conv.id }).catch(() => {});
    // });
  }
  return mergedList;
}
function getConversations() { return state.getConversations(); }
function getActiveConversationId() { return state.getActiveConversationId(); }
function getActiveConversation() { return state.getActiveConversation(); }
function setActiveConversation(id) {
  state.setActiveConversationId(id);
  if (id) {
    state.clearMarkRead(id);
    const conv = state.getActiveConversation();
    // 被踢出的群聊不再 join 频道，仅展示本地缓存的历史记录
    if (!(conv && conv.isKicked)) {
      // 加入频道并拉取历史消息（与原项目一致：切换会话时先 join 再加载历史）
      chatroomBridgeRequest('join_channel', { channel: id }).catch(() => {});
    }
    // if ((state.getMessages(id) || []).length === 0) {
    //   loadConversationHistory(id).catch(() => {});
    // }
  }
  state._setters.notify();
}
function clearMarkRead(id) { state.clearMarkRead(id); state._setters.notify(); }
function getMessages(convId) { return state.getMessages(convId); }
function getActiveMessages() { return state.getActiveMessages(); }
function getStatus() { return state.getStatus(); }
function getConversationUnreadCount(id) { return state.getConversationUnreadCount(id); }
function getTotalUnreadCount() { return state.getTotalUnreadCount(); }

async function sendUserMessage(text, convId, options = {}) {
  // 乐观更新：立即在 UI 显示消息（与原项目 chatStore.sendUserMessage 一致）
  const now = Date.now();
  const optimisticMsg = {
    id: `local-${now}`,
    convId,
    author: 'user',
    authorId: state.getUserId() || 'self',
    authorName: '我',
    text,
    attachments: options.attachments || [],
    ts: now,
    createdAt: now,
    delivery: 'sending',
    isAgent: false,
  };
  state._setters.appendMessage(convId, optimisticMsg);
  const conv = state.getConversations().find((c) => c.id === convId);
  if (conv) { conv.lastMessage = text; conv.lastTs = optimisticMsg.ts; }
  state._setters.notify();

  // 先判断是否需要触发 AI 回复，在 send_message 之前就启动气泡预回复。
  // 避免 chatroomBridgeRequest 耗时或失败时气泡迟迟不出现 / 永远不出现。
  // 对齐 chat.js _isAgentConv：type==='agent' 或 scope==='personal_workspace'
  const shouldCallAgent = options.callAgent || (conv && (conv.type === 'agent' || conv.scope === 'personal_workspace')) || /@.+AI助手/.test(text);
  if (shouldCallAgent) {
    _requestAgentReply(text, convId);
  }

  // 个人提醒：检测 @炎图AI助手 + 提醒关键词，解析时间并创建任务
  // 对齐 agent 项目 sendMessageService.ts _createPersonalReminderTaskFromMessage
  if (shouldCallAgent && hasReminderKeyword(text)) {
    _tryCreateReminderTask(text, convId, optimisticMsg.id);
  }

  try {
    const r = await chatroomBridgeRequest('send_message', { channel: convId, message: text });
    // 发送成功：更新乐观消息状态为 sent
    const list = state.getMessages(convId) || [];
    const idx = list.findIndex((m) => m.id === optimisticMsg.id);
    if (idx >= 0 && list[idx].delivery === 'sending') {
      list[idx] = { ...list[idx], delivery: 'sent' };
      state._setters.messages(convId, list);
      state._setters.notify();
    }
    return r;
  } catch (e) {
    // 发送失败：更新乐观消息状态为 failed
    const list = state.getMessages(convId) || [];
    const idx = list.findIndex((m) => m.id === optimisticMsg.id);
    if (idx >= 0) {
      list[idx] = { ...list[idx], delivery: 'failed' };
      state._setters.messages(convId, list);
      state._setters.notify();
    }
    throw e;
  }
}
async function retrySend(msgId, convId) {
  // 查找原消息文本并重发
  const list = state.getMessages(convId) || [];
  const msg = list.find((m) => m.id === msgId);
  if (!msg) return;
  // 重置状态为 sending
  const idx = list.findIndex((m) => m.id === msgId);
  list[idx] = { ...list[idx], delivery: 'sending' };
  state._setters.messages(convId, list);
  state._setters.notify();
  try {
    const r = await chatroomBridgeRequest('send_message', { channel: convId, message: msg.text });
    list[idx] = { ...list[idx], delivery: 'sent' };
    state._setters.messages(convId, list);
    state._setters.notify();
    return r;
  } catch (e) {
    list[idx] = { ...list[idx], delivery: 'failed' };
    state._setters.messages(convId, list);
    state._setters.notify();
    throw e;
  }
}
async function loadConversationHistory(convId) {
  const r = await chatroomBridgeRequest('get_history', { channel: convId, limit: 50 });
  const rawMessages = (r && (r.messages || r.data)) || (Array.isArray(r) ? r : []);
  // 规范化历史消息：确保包含 author / createdAt 等 _renderBubble 所需字段
  const messages = rawMessages.map((m) => _normalizeHistoryMessage(m, convId)).filter(Boolean);
  if (messages.length) {
    // 合并而非覆盖：以服务端历史为基准，追加本地缓存中服务端未返回的消息
    const localMsgs = state.getMessages(convId) || [];
    const serverIds = new Set(messages.map((m) => m.id));
    const localOnly = localMsgs.filter((m) => !serverIds.has(m.id));
    const merged = localOnly.length ? [...messages, ...localOnly] : messages;
    state._setters.messages(convId, merged);
    state._setters.notify();
  }
  return r;
}

/**
 * 规范化历史消息为前端统一格式
 * C++ 返回的消息字段可能为 sender/message/timestamp_ms 或 authorId/text/ts 等
 */
function _normalizeHistoryMessage(m, convId) {
  // 如果已经是规范化格式（有 author 字段），直接返回
  if (m.author) return m;

  // 跳过 AGENT_TYPING 打字指示器标记（历史消息中也可能包含）
  const rawText = m.text || m.message || m.content || '';
  if (rawText.startsWith(_AGENT_TYPING_MARKER)) return null;
  // 跳过协同协议消息（历史中的协作指令已过期，不重放系统消息）
  if (isCollaborationChatMessage(rawText)) return null;

  const senderId = String(m.sender || m.authorId || m.from || m.nick || '');
  const selfId = state.getSessionId();
  const isSelf = senderId && (senderId === selfId || senderId === state.getPhone() || senderId === state.getUserId());
  const isAgent = m.isAgent || String(m.sender || '').toLowerCase().includes('agent')
    || String(m.from || '').includes('炎图')
    || String(m.sender || m.from || '').toLowerCase().includes('小炎')
    || String(m.sender || m.from || '').includes('AI');

  const ts = m.ts || m.timestamp || m.timestamp_ms || m.createdAt || Date.now();

  // 历史消息也可能含 group agent relay 信封或 delivery marker，未解码会显示为乱码
  const decoded = _decodeAgentMessage(rawText, isAgent);

  return {
    id: m.id || m.messageId || `hist-${ts}-${senderId}`,
    convId,
    author: decoded.isAgent ? 'agent' : (isSelf ? 'user' : 'other_user'),
    authorId: senderId,
    authorName: isSelf ? '我' : (decoded.isAgent ? '炎图AI助手' : (m.authorName || m.sender || m.nick || '群成员')),
    text: decoded.text,
    attachments: decoded.attachments,
    agentDeliveryId: decoded.agentDeliveryId || undefined,
    ts,
    createdAt: ts,
    isAgent: decoded.isAgent,
  };
}
async function createGroupConversation(name) {
  const r = await chatroomBridgeRequest('create_conversation', { name });
  // 创建成功后重新拉取会话列表，使新群聊出现在侧边栏
  await loadConversations();
  // 返回新会话 ID，兼容 C++ 返回格式：{ conversation_id } / { id } / { channel } / 直接返回字符串
  return (r && (r.conversation_id || r.id || r.channel)) || (typeof r === 'string' ? r : r);
}
async function deleteConversationFromList(id) {
  state.markLocallyDeleted(id);
  // 同步清除被踢出群聊的本地记录，避免下次 loadConversations 再次把它并入列表
  chatHistoryStore.removeKickedRoom(id);
  chatHistoryStore.clearMessages(id);
  state._setters.notify();
  return core.request('chat.command', { cmd: 'DELETE_CONVERSATION', channel: id, conversationId: id });
}

// ================================================================
// 帖子/群任务通道（通过 chatroom.bridge.request 通道与 C++ 通信）
// 对齐 chatroomTransport.js 中的 chatroomListPosts/chatroomCreatePost 等
// ================================================================

/**
 * 归一化 C++ 返回的帖子对象（兼容 snake_case / camelCase）
 */
function _normalizePost(p, convId) {
  if (!p || typeof p !== 'object') return null;
  const status = String(p.status || '').toLowerCase();
  return {
    id: String(p.id || p.post_id || ''),
    conversationId: p.conversation_id || p.conversationId || convId || '',
    type: p.type || 'task',
    title: p.title || '',
    summary: p.summary || '',
    status: status === 'closed' ? 'closed' : (status || 'published'),
    template: p.template || p.task_kind || p.taskKind || 'homework',
    actionType: p.action_type || p.actionType || 'read',
    resourceType: p.resource_type || p.resourceType || 'none',
    resourceUrl: p.resource_url || p.resourceUrl || '',
    deadlineAt: p.deadline_at || p.deadlineAt || null,
    createdAt: p.created_at || p.createdAt || Date.now(),
    createdById: p.created_by_id || p.createdById || '',
    createdByName: p.created_by_name || p.createdByName || '',
    responses: Array.isArray(p.responses) ? p.responses : [],
  };
}

async function loadGroupPosts(convId) {
  const r = await chatroomBridgeRequest('list_posts', { conversationId: convId });
  console.log('[bridge] list_posts 响应:', r);
  // C++ 可能返回数组、{ posts: [...] } 或 { conversations: [...] }
  let rawList = [];
  if (Array.isArray(r)) {
    rawList = r;
  } else if (r && Array.isArray(r.posts)) {
    rawList = r.posts;
  } else if (r && Array.isArray(r.list)) {
    rawList = r.list;
  } else if (r && r.data && Array.isArray(r.data)) {
    rawList = r.data;
  }
  const list = rawList.map(p => _normalizePost(p, convId)).filter(Boolean);
  state._setters.posts(convId, list);
  state._setters.notify();
  return list;
}
async function loadAllGroupPosts() {
  for (const c of state.getConversations()) {
    if (c.type === 'group') { try { await loadGroupPosts(c.id); } catch (e) {} }
  }
}
function getPosts(convId) { return state.getPosts(convId); }

// 帖子模板中文标签（对齐原项目 _groupPostTemplateLabel）
function _postTemplateLabel(template) {
  if (template === 'homework') return '家庭作业';
  if (template === 'event') return '群活动';
  if (template === 'news') return '群资讯';
  return '任务';
}

async function createGroupPost(convId, data) {
  const r = await chatroomBridgeRequest('create_post', {
    conversationId: convId,
    title: data.title || '',
    summary: data.summary || '',
    taskKind: data.template || data.taskKind || 'notice',
    actionType: data.actionType || 'read',
    resourceType: data.resourceType || 'none',
    resourceUrl: data.resourceUrl || '',
    deadlineAt: data.deadlineAt || '',
    createdById: data.createdById || '',
    createdByName: data.createdByName || '',
  });
  // 发布成功后在聊天框发送公告消息（对齐原项目 announceInChat 逻辑）
  if (data.announceInChat !== false) {
    const label = _postTemplateLabel(data.template);
    const announceText = `我发布了新的${label}：${data.title || ''}`;
    try {
      await sendUserMessage(announceText, convId);
    } catch (e) {
      console.warn('[bridge] announceInChat: 发送公告消息失败', e);
    }
  }
  return r;
}
async function closeGroupPost(postId, convId) {
  return chatroomBridgeRequest('close_post', { postId, conversationId: convId });
}
async function respondToGroupPost(postId, convId, response) {
  return chatroomBridgeRequest('respond_to_post', {
    postId,
    conversationId: convId,
    responseType: response.responseType || 'read',
    confirmation: response.confirmation || '',
    content: response.content || '',
  });
}
async function submitHomeworkPostResponse(postId, convId, { note, file }) {
  // 作业提交也走 respond_to_post，actionType 为 upload
  return chatroomBridgeRequest('respond_to_post', {
    postId,
    conversationId: convId,
    actionType: 'upload',
    content: note || '',
  });
}

// ================================================================
// 群成员通道
// ================================================================

/**
 * 从原始成员对象中提取手机号（兼容多种字段名）
 */
function _extractMemberPhone(raw) {
  if (!raw || typeof raw !== 'object') return '';
  const nestedUser = raw.user && typeof raw.user === 'object' ? raw.user : null;
  const phone = String(
    raw.phone || raw.mobile || raw.mobile_phone || raw.mobilePhone ||
    raw.user_phone || raw.userPhone || raw.member_phone || raw.memberPhone ||
    raw.phone_number || raw.phoneNumber || ''
  ).trim();
  if (phone) return phone;
  if (nestedUser) return _extractMemberPhone(nestedUser);
  return '';
}

/**
 * 规范化单个成员（与原项目 _normalizeMember 对齐）
 * 支持 user / phone / node 三种 member_kind
 */
function _normalizeMember(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const memberKind = String(raw.member_kind || raw.memberKind || raw.kind || '').trim().toLowerCase();
  const role = String(raw.role || '').trim() || 'member';
  const status = String(raw.status || '').trim() || undefined;
  const joinedAt = raw.joined_at || raw.joinedAt || raw.created_at || raw.createdAt || undefined;

  if (memberKind === 'user') {
    const userId = String(raw.user_id || raw.userId || raw.uid || '').trim();
    if (!userId) return null;
    return {
      memberKind: 'user',
      userId,
      phone: _extractMemberPhone(raw),
      role,
      status,
      joinedAt,
    };
  }
  if (memberKind === 'phone') {
    const phone = _extractMemberPhone(raw);
    if (!phone) return null;
    return { memberKind: 'phone', phone, role, status, joinedAt };
  }
  if (memberKind === 'node') {
    const nodeId = String(raw.node_id || raw.nodeId || '').trim();
    if (!nodeId) return null;
    const nodeRaw = raw.node || {};
    return {
      memberKind: 'node',
      nodeId,
      role,
      status,
      joinedAt,
      node: {
        nodeId: String(nodeRaw.node_id || nodeRaw.nodeId || nodeRaw.id || nodeId || '').trim(),
        nodeType: String(nodeRaw.node_type || nodeRaw.nodeType || '').trim(),
        name: String(nodeRaw.name || nodeRaw.display_name || nodeRaw.displayName || '').trim(),
        deviceType: String(nodeRaw.device_type || nodeRaw.deviceType || '').trim(),
      },
    };
  }
  // 未知 memberKind：尝试从 user_id/phone 推断
  const userId = String(raw.user_id || raw.userId || raw.uid || '').trim();
  const phone = _extractMemberPhone(raw);
  if (userId) {
    return { memberKind: 'user', userId, phone, role, status, joinedAt };
  }
  if (phone) {
    return { memberKind: 'phone', phone, role, status, joinedAt };
  }
  return null;
}

/**
 * 规范化 chatroom API 返回的成员列表
 * 兼容 GET_ROOM_INFO / LIST_ROOM_MEMBERS / NAMES 等多种响应格式
 */
function _normalizeMembersResponse(r) {
  console.log('[normalizeMembersResponse]', r);
  if (!r) return [];
  // 从响应中提取 members 数组（兼容 GET_ROOM_INFO / LIST_ROOM_MEMBERS / NAMES 等多种响应格式）
  let rawMembers = [];
  if (Array.isArray(r)) rawMembers = r;
  else if (Array.isArray(r.members)) rawMembers = r.members;
  else if (Array.isArray(r.room_members)) rawMembers = r.room_members;
  else if (Array.isArray(r.names)) rawMembers = r.names;
  else if (r.data && Array.isArray(r.data)) rawMembers = r.data;
  else if (r.data && Array.isArray(r.data.members)) rawMembers = r.data.members;
  else if (r.room_info && Array.isArray(r.room_info.members)) rawMembers = r.room_info.members;
  else if (r.info && Array.isArray(r.info.members)) rawMembers = r.info.members;
  else if (r.room && Array.isArray(r.room.members)) rawMembers = r.room.members;
  else if (r.result && Array.isArray(r.result)) rawMembers = r.result;
  else if (r.result && Array.isArray(r.result.members)) rawMembers = r.result.members;

  return rawMembers.map((item) => {
    if (!item) return null;
    // 对象格式：按原项目 _normalizeMember 逻辑处理
    if (typeof item === 'object') return _normalizeMember(item);
    // 字符串格式（IRC NAMES：@op / +voice / ~owner）
    const raw = String(item).trim();
    if (!raw) return null;
    let role = 'member';
    let nick = raw;
    if (raw.startsWith('@')) { role = 'admin'; nick = raw.slice(1); }
    else if (raw.startsWith('+')) { nick = raw.slice(1); }
    else if (raw.startsWith('&')) { role = 'admin'; nick = raw.slice(1); }
    else if (raw.startsWith('~')) { role = 'owner'; nick = raw.slice(1); }
    return {
      memberKind: 'user',
      userId: nick,
      phone: '',
      role,
      status: undefined,
      joinedAt: undefined,
    };
  }).filter(Boolean);
}

/**
 * 通过 get_room_info 获取成员列表（单次请求）
 */
async function _fetchRoomMembersOnce(normalizedChannel) {
  const r = await chatroomBridgeRequest('get_room_info', { channel: normalizedChannel });
  console.log('[get_room_info]', r);
  
  return _normalizeMembersResponse(r);
}

/**
 * 检查成员列表中是否有 user 类型成员缺少 phone（服务端传播延迟）
 */
function _hasPhonelessUser(members) {
  return members.some(m => m.memberKind === 'user' && !m.phone);
}

async function getRoomMembers(convId) {
  const normalizedChannel = String(convId || '').startsWith('#') ? convId : `#${convId}`;
  // 优先使用 get_room_info（与原项目一致，返回完整房间信息含成员手机号）
  try {
    let members = await _fetchRoomMembersOnce(normalizedChannel);
    if (members.length) {
      // 服务端传播延迟：user 类型成员首次可能缺 phone，等待后重试一次
      if (_hasPhonelessUser(members)) {
        await new Promise(resolve => setTimeout(resolve, 600));
        const retried = await _fetchRoomMembersOnce(normalizedChannel);
        if (retried.length) members = retried;
      }
      state._setters.roomMembers(convId, members);
      state._setters.notify();
      return members;
    }
  } catch (e) {
    console.warn('[bridge] get_room_info failed:', e.message);
  }
  // 其次使用 list_room_members
  try {
    const r = await chatroomBridgeRequest('list_room_members', { channel: normalizedChannel });
    const members = _normalizeMembersResponse(r);
    if (members.length) {
      // 同样检查 phone 缺失并重试
      if (_hasPhonelessUser(members)) {
        await new Promise(resolve => setTimeout(resolve, 600));
        const retried = _normalizeMembersResponse(
          await chatroomBridgeRequest('list_room_members', { channel: normalizedChannel })
        );
        if (retried.length) {
          state._setters.roomMembers(convId, retried);
          state._setters.notify();
          return retried;
        }
      }
      state._setters.roomMembers(convId, members);
      state._setters.notify();
      return members;
    }
  } catch (e) {
    console.warn('[bridge] list_room_members failed:', e.message);
  }
  // 再次使用 names（IRC NAMES，只有昵称无手机号）
  try {
    const r = await chatroomBridgeRequest('names', { channel: normalizedChannel });
    const members = _normalizeMembersResponse(r);
    if (members.length) {
      state._setters.roomMembers(convId, members);
      state._setters.notify();
      return members;
    }
  } catch (e) {
    console.warn('[bridge] names failed:', e.message);
  }
  // 最后回退到 agentchat bridge
  try {
    const list = await core.request('room.members.list', { conversationId: convId });
    if (Array.isArray(list)) {
      state._setters.roomMembers(convId, list);
      state._setters.notify();
    }
    return list || [];
  } catch (e) {
    return state.getRoomMembers(convId);
  }
}
async function inviteRoomMember({ roomId, memberKind, phone, role }) {
  const normalizedChannel = String(roomId || '').startsWith('#') ? roomId : `#${roomId}`;
  const kind = String(memberKind || 'phone').trim() || 'phone';
  const target = String(phone || '').trim();
  await chatroomBridgeRequest('invite_member', {
    channel: normalizedChannel,
    member_kind: kind,
    target,
    role: String(role || 'member').trim() || 'member',
  });
  // 邀请成功后刷新并返回成员列表
  return getRoomMembers(roomId);
}
async function removeRoomMember(convId, memberKind, memberId) {
  const normalizedChannel = String(convId || '').startsWith('#') ? convId : `#${convId}`;
  await chatroomBridgeRequest('remove_member', {
    channel: normalizedChannel,
    member_kind: String(memberKind || 'user').trim() || 'user',
    target: String(memberId || '').trim(),
  });
  // 移除成功后刷新并返回成员列表
  return getRoomMembers(convId);
}

// ================================================================
// 群聊话题通道（postMessage 占位，C++ 接入后由原生宿主处理）
// ================================================================
async function loadTopics(convId) {
  const list = await core.request('topics.list', { conversationId: convId });
  if (Array.isArray(list)) { state._setters.topics(convId, list); state._setters.notify(); }
  return list || [];
}
function getTopics(convId) { return state.getTopics(convId); }
async function createTopic(convId, title, summary = '') {
  return core.request('topics.create', { conversationId: convId, title, summary });
}
async function joinTopic(convId, topicId) {
  return core.request('topics.join', { conversationId: convId, topicId });
}
async function leaveTopic(convId, topicId) {
  return core.request('topics.leave', { conversationId: convId, topicId });
}

// ================================================================
// 个人任务通道
// ================================================================
function getPersonalTasksForCurrentUser() { return state.getPersonalTasksForCurrentUser(); }
async function loadPersonalTasksFromServer() {
  const list = await core.request('tasks.personal.list', {});
  if (Array.isArray(list)) { state._setters.personalTasks(list); state._setters.notify(); }
  return list || [];
}
async function createPersonalTask(data) {
  return core.request('tasks.personal.create', data);
}
async function completePersonalTask(taskId) {
  return core.request('tasks.personal.update_status', { taskId, status: 'done' });
}
async function cancelPersonalTask(taskId) {
  return core.request('tasks.personal.update_status', { taskId, status: 'canceled' });
}
async function reschedulePersonalTask(taskId, newDueAt) {
  return core.request('tasks.personal.reschedule', { taskId, newDueAt });
}

// ── 个人提醒触发：解析消息文本，创建本地任务 + 发送给 C++ ──
// 对齐 agent 项目 sessionStore.ts _createPersonalReminderTaskFromMessage
function _tryCreateReminderTask(text, convId, messageId) {
  const draft = tryParseReminderDraft(text);
  if (!draft) return null;

  const taskId = 'task-' + Date.now() + '-' + Math.random().toString(36).slice(2, 8);
  const reminderId = 'rem-' + taskId;
  const task = {
    id: taskId,
    ownerUserId: state.getUserId() || 'self',
    creatorUserId: state.getUserId() || 'self',
    title: draft.title,
    summary: '提醒时间：' + draft.delayLabel,
    sourceConversationId: convId,
    createdFromMessageId: messageId,
    dueAt: draft.dueAt,
    status: 'pending',
    deliveryTarget: { type: 'conversation', conversationId: convId },
    reminderId,
    createdAt: Date.now(),
    syncStatus: 'syncing',
  };

  // 本地立即创建（UI 即时显示任务卡片）
  state._setters.appendPersonalTask(task);
  state._setters.notify();

  // 异步发送给 C++ 持久化 + 注册 cron 定时
  createPersonalTask({
    id: taskId,
    ownerUserId: task.ownerUserId,
    creatorUserId: task.creatorUserId,
    title: task.title,
    summary: task.summary,
    dueAt: String(task.dueAt),
    status: 'pending',
    sourceConversationId: convId,
    createdFromMessageId: messageId,
    reminderId,
  }).then((resp) => {
    const serverTaskId = (resp && (resp.taskId || resp.id)) || taskId;
    state._setters.updatePersonalTask(taskId, { syncStatus: 'synced', serverTaskId });
    state._setters.notify();
  }).catch((e) => {
    console.warn('[bridge] createPersonalTask failed:', e && e.message);
    state._setters.updatePersonalTask(taskId, { syncStatus: 'failed', syncError: String(e && e.message || e) });
    state._setters.notify();
  });

  return task;
}

// ── 通知轮询定时器 ──
// 每 5 秒检查是否有任务到期，触发 UI 刷新让通知面板和红点更新
// 对齐 agent 项目 useNowTick(5_000) 的轮询机制
let _notificationTimer = null;
function _startNotificationPolling() {
  if (_notificationTimer) return;
  _notificationTimer = setInterval(() => {
    const now = Date.now();
    const hasDue = (state.getPersonalTasksForCurrentUser() || []).some((t) => {
      return t.status === 'pending' && t.dueAt && t.dueAt <= now;
    });
    if (hasDue) state._setters.notify();
  }, 5000);
}


// ================================================================
// 设备通道
// ================================================================
async function refreshBoundDevices() {
  try {
    const list = await core.request('devices.list', {});
    if (Array.isArray(list)) { state._setters.boundDevices(list); state._setters.notify(); }
    return list || [];
  } catch (e) {
    console.warn('[bridge] refreshBoundDevices failed:', e.message);
    return [];
  }
}
function getBoundDevices() { return state.getBoundDevices(); }
function getPendingDeviceSessions() { return state.getPendingDeviceSessions(); }
function getBindableConversations() { return state.getBindableConversations(); }
function parseDeviceBindPayload(raw) { return state.parseDeviceBindPayload(raw); }
async function createDeviceBindSession(name) {
  const s = await core.request('devices.bind_sessions.create', { name });
  if (s) { state._setters.pendingDeviceSessions([s, ...state.getPendingDeviceSessions()]); state._setters.notify(); }
  return s;
}
async function getDeviceBindSession(bindToken) {
  return core.request('devices.bind_sessions.get', { bindToken });
}
async function confirmDeviceBind({ bindToken, conversationId, conversationName, deviceName }) {
  const r = await core.request('devices.bind.confirm', { bindToken, conversationId, conversationName, deviceName });
  await refreshBoundDevices();
  return r;
}
async function unbindDevice(deviceId) {
  return core.request('devices.unbind', { deviceId });
}

// ================================================================
// Agent / 工作台通道（原 chatApi.callAgent + agentBridgeTransport + openclawAgentApi）
// ================================================================
function getAgentSkills() { return state.getAgentSkills(); }
function isSkillEnabled(id) { return state.isSkillEnabled(id); }
async function toggleSkill(id) {
  return core.request('agent.skills.toggle', { skillId: id });
}
async function enableAllSkills() { /* 占位 */ }
async function disableAllSkills() { /* 占位 */ }
function getExecutionHistory() { return state.getExecutionHistory(); }
function getWorkspaceDraftActions() { return state.getWorkspaceDraftActions(); }
async function publishWorkspaceDraft(skillId, targetId, sourceConvId) {
  return core.request('workspace.draft.publish', { skillId, targetId, conversationId: sourceConvId });
}
function getAgentSession(convId) {
  // 占位：返回一个简单的 agent 会话视图
  return { convId, history: state.getMessages(convId) };
}

// 订阅 Agent 流式增量（页面用于实时拼接 AI 回复）
function onAgentStream(handler) {
  _agentStreamHandlers.add(handler);
  return () => _agentStreamHandlers.delete(handler);
}

// 内部：发起 Agent 回复请求
// ── Agent 回复解析（对齐原项目 chatStore._parseAgentReplyText）──

function _titleFromUrl(url) {
  try {
    const u = new URL(url);
    const last = u.pathname.split('/').filter(Boolean).pop() || u.hostname;
    // return decodeURIComponent(last).replace(/\.\w+$/, '') || u.hostname;
    return decodeURIComponent(last).replace(/\.html?$/i, '');

  } catch (e) {
    return String(url || '').slice(0, 40);
  }
}

function _isH5CardUrl(url) {
  const lower = String(url || '').toLowerCase();
  return /\/[^/?#]*card[^/?#]*\.html?(?:[?#].*)?$/i.test(lower)
    || /\/index\.html?(?:[?#].*)?$/i.test(lower);
}

/**
 * 检测 AI 协议 JSON 特征字段（对齐 origin-vanilla openclaw-shared.ts hasAiProtocolJsonSignature）
 * 要求同时含 (outputs|providerId|skillId) 与 (summary|status)，避免误判普通 JSON。
 */
function _hasAiProtocolJsonSignature(text) {
  return /"(?:outputs|providerId|skillId)"\s*:/.test(text) && /"(?:summary|status)"\s*:/.test(text);
}

/**
 * 定位 AI_SKILL_RESULT JSON 的起止边界（对齐 origin-vanilla tryExtractAiSkillResultText 的边界查找）
 * 通过显式标记或特征字段定位起点，再用括号匹配（含字符串/转义感知）找到完整 JSON。
 * @param {string} raw
 * @returns {{start:number, end:number}|null} end 为闭合 '}' 的索引（含）
 */
function _locateAiSkillJsonBounds(raw) {
  const explicitIdx = raw.indexOf('AI_SKILL_RESULT');
  const hasSignature = _hasAiProtocolJsonSignature(raw);
  if (explicitIdx === -1 && !hasSignature) return null;

  // 无显式标记时，用特征字段定位起点
  const searchFrom = explicitIdx !== -1 ? explicitIdx : Math.min(
    raw.indexOf('"outputs"') !== -1 ? raw.indexOf('"outputs"') : Infinity,
    raw.indexOf('"providerId"') !== -1 ? raw.indexOf('"providerId"') : Infinity,
    raw.indexOf('"skillId"') !== -1 ? raw.indexOf('"skillId"') : Infinity,
  );
  if (searchFrom === Infinity) return null;

  // 从标记位向后找起始 '{'（JSON 可能跟在 "AI_SKILL_RESULT:\n```json\n" 之后）
  let start = searchFrom;
  while (start < raw.length) {
    if (raw[start] === '{') break;
    start += 1;
  }
  if (start >= raw.length || raw[start] !== '{') return null;

  // 括号匹配找闭合 '}'（感知字符串与转义，避免字符串内 '}' 误判）
  let depth = 0;
  let inString = false;
  let escaped = false;
  for (let i = start; i < raw.length; i += 1) {
    const ch = raw[i];
    if (escaped) { escaped = false; continue; }
    if (ch === '\\') { escaped = true; continue; }
    if (ch === '"') { inString = !inString; continue; }
    if (inString) continue;
    if (ch === '{') depth += 1;
    if (ch === '}') {
      depth -= 1;
      if (depth === 0) return { start, end: i };
    }
  }
  return null; // 未闭合，无法定位
}

/**
 * 解析 AI 回复文本，提取卡片附件并清理 JSON 碎片。
 * 对齐原项目 _parseAgentReplyText + _sanitizeAgentReplyText：
 * 1. 尝试解析文本中的 JSON（AI_SKILL_RESULT 格式），提取 webview outputs 作为附件
 * 2. 从纯文本中提取 URL 作为 webview 附件
 * 3. 清理文本：移除 JSON 碎片，H5 卡片存在时简化文本
 * @param {string} text - AI 回复原始文本
 * @param {array} existingAttachments - C++ 推送已携带的附件（优先使用）
 * @returns {{ text: string, attachments: array }}
 */
function _parseAgentReply(text, existingAttachments = []) {
  const raw = String(text || '').trim();
  if (!raw) return { text: '', attachments: existingAttachments };

  let cleanText = raw;
  const attachments = [];

  // 1. 定位并解析 AI_SKILL_RESULT 结构化 JSON（对齐 origin-vanilla tryExtractAiSkillResultText）
  const bounds = _locateAiSkillJsonBounds(raw);
  if (bounds) {
    const jsonStr = raw.slice(bounds.start, bounds.end + 1);
    const beforeJson = raw.slice(0, bounds.start).trim();
    const afterJson = raw.slice(bounds.end + 1).trim();
    let extractedText = '';
    let parsedOk = false;
    try {
      const obj = JSON.parse(jsonStr);
      if (obj && typeof obj === 'object') {
        const summary = String(obj.summary || '').trim();
        // outputs 兼容数组与单对象（对齐原项目）
        let outputs = [];
        if (Array.isArray(obj.outputs)) {
          outputs = obj.outputs;
        } else if (obj.outputs && typeof obj.outputs === 'object') {
          outputs = [obj.outputs];
        }
        const textParts = [];
        for (const output of outputs) {
          if (!output || typeof output !== 'object') continue;
          const type = String(output.type || '').trim();
          if (type === 'text') {
            // 标准格式：{ type: "text", content: "..." }，兜底 text 字段
            const c = String(output.content || output.text || '').trim();
            if (c) textParts.push(c);
          } else if (type === 'webview' || type === 'webview_content' || type === 'artifact') {
            const url = String(output.url || '').trim();
            if (url && /^https?:\/\//.test(url)) {
              attachments.push({
                type: 'webview',
                url,
                title: String(output.title || _titleFromUrl(url)),
                objectKind: output.objectKind || (output.url && _isH5CardUrl(output.url) ? 'h5_card' : ''),
                artifactType: output.artifactType || '',
                sourceSkillId: output.sourceSkillId || obj.skillId || '',
                taskNo: obj.taskNo || '',
                providerId: obj.providerId || '',
                skillId: obj.skillId || '',
              });
            }
          } else {
            // 任意带 url 字段的输出 - 保留为附件（对齐原项目兜底）
            const url = String(output.url || '').trim();
            if (url && /^https?:\/\//.test(url) && !attachments.some(a => a.url === url)) {
              attachments.push({
                type: 'webview',
                url,
                title: String(output.title || _titleFromUrl(url)),
                objectKind: output.url && _isH5CardUrl(output.url) ? 'h5_card' : '',
                artifactType: '',
              });
            }
            // 直接格式 { text: "..." }
            if (typeof output.text === 'string' && output.text.trim()) {
              textParts.push(output.text);
            }
          }
        }
        extractedText = textParts.join('\n') || summary;
        if (extractedText) parsedOk = true;
      }
    } catch (e) {
      // JSON 畸形 - 走正则兜底提取
    }
    if (!parsedOk) {
      // 正则兜底：从畸形 JSON 中提取 summary/content/text（对齐 origin-vanilla regex fallback）
      const summaryMatch = jsonStr.match(/"summary"\s*:\s*"((?:[^"\\]|\\.)*)"/);
      const summary = summaryMatch ? summaryMatch[1].replace(/\\"/g, '"').replace(/\\n/g, '\n') : '';
      const contentMatch = jsonStr.match(/"content"\s*:\s*"((?:[^"\\]|\\.)*)"/);
      const content = contentMatch ? contentMatch[1].replace(/\\"/g, '"').replace(/\\n/g, '\n') : '';
      const textMatch = jsonStr.match(/"text"\s*:\s*"((?:[^"\\]|\\.)*)"/);
      const textVal = textMatch ? textMatch[1].replace(/\\"/g, '"').replace(/\\n/g, '\n') : '';
      extractedText = content || textVal || summary;
    }
    if (extractedText) {
      cleanText = [beforeJson, extractedText, afterJson].filter(Boolean).join('\n').trim();
    }
  }

  // 2. 从文本中提取 URL 作为附件（对齐 agent 项目 appendInlineWebviewAttachments）
  // 同时匹配 markdown 链接 [label](url) 和裸 URL，比旧版仅匹配裸 URL 更完整
  appendInlineAttachments(cleanText, attachments);

  // 3. 合并 C++ 推送的附件 + 去重（对齐 agent 项目 dedupeAttachments）
  for (const att of existingAttachments) {
    attachments.push(att);
  }
  const dedupedAttachments = dedupeAttachments(attachments);

  // 4. 从文本中移除已提取为附件的 URL（对齐 agent 项目 stripAttachedInlineLinks）
  // 这样文本中不再重复显示 URL，用户看到的是干净文本 + 下方卡片
  cleanText = stripInlineLinks(cleanText, dedupedAttachments);

  // 5. H5 卡片存在时简化低信息文本
  const hasH5Card = dedupedAttachments.some(a =>
    a.objectKind === 'h5_card' || a.sourceSkillId === 'h5-cards' ||
    a.artifactType === 'html' || (a.url && _isH5CardUrl(a.url))
  );
  if (hasH5Card) {
    const t = cleanText.trim();
    if (!t || t.length < 20 || /^[\s{}"[\]:,\d]+$/.test(t)) {
      cleanText = '卡片已生成，请查看下方卡片。';
    }
  }

  // 6. 移除残留的 JSON 碎片行（如单独的 "outputs": [...] 片段）
  cleanText = cleanText.replace(/^[\s]*[{}".[\]:,\d]+[\s]*$/gm, '').trim();

  // 7. 清洗 AI 回复文本（对齐 agent 项目 sanitizeAgentVisibleReply）
  // 去除思考过程、工具细节、乱码、协议泄露等不应展示给用户的内容
  cleanText = sanitizeAgentReply(cleanText, dedupedAttachments);

  return { text: cleanText || raw, attachments: dedupedAttachments };
}

// 检测文本是否是 prompt/系统指令而非 AI 回复
// C++ 可能把发送给 AI 的 prompt（含 "Model(default):" / "Please reply in Chinese" 等）
// 作为 delta/final 推送回来，这些不是真正的 AI 回复，不应写入占位消息
function _isPromptText(text) {
  const t = String(text || '');
  if (!t.trim()) return false;
  // 检测 prompt 标志性内容
  if (/^Model\(/.test(t.trim())) return true;           // "Model(default): ..."
  if (/Please reply in Chinese/i.test(t)) return true;   // 系统指令
  if (/AI_TASK_REQUEST/.test(t)) return true;            // 协议信封
  return false;
}

async function _requestAgentReply(text, convId) {
  // 对齐 origin-vanilla chatStore.js _requestAgentReply：
  // 先创建占位 streaming agent 消息，UI 立即显示三点跳动 typing 指示器。
  // 气泡永不消失：有回复 -> 流式文字 -> complete；无回复 -> 显示错误文本 -> complete。
  // 只有 90s 超时才显示超时提示（气泡仍不消失，只是状态变为 complete）。
  const agentMsgId = `agent-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
  const agentMsg = {
    id: agentMsgId,
    convId,
    author: 'agent',         // 关键：_renderBubble 检查 msg.author === 'agent'
    authorId: 'agent',
    authorName: '炎图AI助手',
    text: '',                // 空内容 + streaming -> 显示 typing 三点气泡
    attachments: [],
    status: 'streaming',
    ts: Date.now(),
    createdAt: Date.now(),
    isAgent: true,
    _isLocalStream: true,   // 标记为本地发起的流式，push handler 据此跳过接收方遥流
  };
  state._setters.appendMessage(convId, agentMsg);
  state._setters.notify();

  // 安全超时：C++ 若迟迟不回 final/error，显示超时提示（气泡不消失，状态变 complete）
  // 但如果占位已被 chat.push 或 agent.turn.final 更新为 complete，则跳过
  let safetyTimer = setTimeout(() => {
    if (done) return;
    // 检查占位是否已被其他路径更新为 complete（chat.push 推送等）
    const list = state.getMessages(convId) || [];
    const placeholder = list.find((m) => m.id === agentMsgId);
    if (placeholder && placeholder.status === 'complete') {
      done = true;
      return; // 已有回复，不覆盖为超时文本
    }
    done = true;
    console.warn('[bridge] agent.turn safety timeout (90s) for', convId);
    state._setters.updateMessage(convId, agentMsgId, {
      text: '抱歉，AI 回复超时，请稍后再试',
      status: 'complete',
    });
    state._setters.notify();
    off();
  }, 90000);

  let acc = '';
  let done = false;
  const off = onAgentStream((evt) => {
    if (done) return;

    if (evt.type === 'delta') {
      acc += evt.text || '';
      // 不在 delta 阶段写入占位文本 -- 保持三点跳动 typing 指示器。
      // C++ 可能把 prompt/系统指令作为 delta 推送，写入会覆盖 typing 效果。
      // 真正的 AI 回复通过 chat.push 到达时由 chat.push handler 更新占位。
    } else if (evt.type === 'final') {
      const rawText = evt.text || acc;
      // 检测是否是 prompt/系统指令而非 AI 回复
      if (_isPromptText(rawText)) {
        console.log('[bridge] agent.turn.final looks like prompt, skipping:', rawText.slice(0, 100));
        // 不设 done，保持 streaming，等待 chat.push 推送真正回复
        return;
      }
      done = true;
      if (safetyTimer) { clearTimeout(safetyTimer); safetyTimer = null; }
      const pushedAttachments = evt.attachments || [];
      const decoded = _decodeAgentMessage(rawText, true, pushedAttachments);
      console.log('[bridge] agent.turn.final raw:', rawText.slice(0, 200),
        '-> decoded text:', decoded.text.slice(0, 100),
        'attachments:', decoded.attachments.length);
      state._setters.updateMessage(convId, agentMsgId, {
        text: decoded.text || '',
        attachments: decoded.attachments,
        agentDeliveryId: decoded.agentDeliveryId || undefined,
        status: 'complete',
      });
      state._setters.notify();
      off();
    } else if (evt.type === 'error') {
      done = true;
      if (safetyTimer) { clearTimeout(safetyTimer); safetyTimer = null; }
      state._setters.updateMessage(convId, agentMsgId, {
        text: `抱歉，AI 回复失败：${evt.message || '未知错误'}`,
        status: 'complete',
      });
      state._setters.notify();
      off();
    }
  });

  try {
    // 按照协议文档 §2.3 构建 AI_TASK_REQUEST，嵌入 prompt 文本发送给 AI 引擎。
    // 对齐 agent 项目 openclawAgentProvider.ts buildOpenClawMessage。

    // 对齐 agent 项目 sendMessageService.ts stripAgentMention：
    // 群聊 @提及场景下，发送给 AI 的 input.text 应为去除 @XX AI助手 前缀的纯用户意图文本。
    // 如 "@炎图AI助手 打开樱花大冒险" -> "打开樱花大冒险"
    // 否则 AI 会把 @提及 当作查询内容，导致回复格式异常。
    const userQuery = String(text || '')
      .replace(/\u200B/g, '')                          // 1. 去除零宽空格
      .replace(/@\S+AI助手\s*/g, '')                   // 2. 去除 @XX AI助手 前缀
      .trim() || text.trim();

    const taskNo = createAgentTaskNo();
    const metadata = {
      taskNo,
      providerId: AppConfig.getCurrentAi(),
      conversationId: convId,
      userId: state.getUserId() || undefined,
      userPhone: state.getPhone() || undefined,
    };
    // 构建会话上下文（对齐 agent 项目 agentRuntimeService.ts normalizeConversationContext）
    // 取最近 4 条消息（排除当前用户消息），用于后续追问场景
    const recentMsgs = (state.getMessages(convId) || [])
      .filter((m) => m && m.text && !m._isLocalStream && m.id !== agentMsgId)
      .slice(-4)
      .map((m) => {
        const role = m.author === 'agent' || m.isAgent ? '助手' : '用户';
        return `${role}: ${String(m.text).slice(0, 160)}`;
      });
    const prompt = buildOpenClawMessage(userQuery, metadata, undefined, recentMsgs);
    console.log('[bridge] agent.turn prompt:\n', prompt);
    console.log('[bridge] agent.turn userQuery:', userQuery, '| taskNo:', taskNo);
    const result = await core.request('agent.turn', { text: prompt, conversationId: convId, ai: AppConfig.getCurrentAi(), taskNo });
    if (!done) {
      const rawText = (result && (result.text || result.message || result.content))
        || (typeof result === 'string' ? result : '');
      if (rawText && !_isPromptText(rawText)) {
        done = true;
        if (safetyTimer) { clearTimeout(safetyTimer); safetyTimer = null; }
        const decoded = _decodeAgentMessage(rawText, true, (result && result.attachments) || []);
        state._setters.updateMessage(convId, agentMsgId, {
          text: decoded.text || '',
          attachments: decoded.attachments,
          agentDeliveryId: decoded.agentDeliveryId || undefined,
          status: 'complete',
        });
        state._setters.notify();
        off();
      }
      // 响应中无内容或是 prompt -> 不设 done，继续等待 chat.push 推送
    }
  } catch (e) {
    // 对齐 origin-vanilla catch：显示"AI 服务不可用"，状态变 complete（气泡不消失）
    if (!done) {
      done = true;
      if (safetyTimer) { clearTimeout(safetyTimer); safetyTimer = null; }
      console.warn('[bridge] agent.turn error', e);
      state._setters.updateMessage(convId, agentMsgId, {
        text: '抱歉，AI 服务暂时不可用',
        status: 'complete',
      });
      state._setters.notify();
      off();
    }
  }
}

// ================================================================
// 接收方遥流：当其他群成员触发 AI 回复时，本地没有 onAgentStream 订阅，
// 需要靠此处的逻辑创建/更新 streaming 气泡，让接收方也能看到 typing + 流式文字。
// 发送方有 _isLocalStream 标记的消息，此处跳过避免重复。
// ================================================================

function _hasLocalStreamingMsg(convId) {
  if (!convId) return false;
  const msgs = state.getMessages(convId) || [];
  return msgs.some((m) => m._isLocalStream && m.status === 'streaming');
}

function _handleRemoteAgentDelta(convId, text) {
  if (!convId) return;
  // 发送方自己处理，跳过
  if (_hasLocalStreamingMsg(convId)) return;

  let stream = _remoteStreamingMsgs.get(convId);
  if (!stream) {
    // 首次 delta：创建接收方 streaming 占位消息（显示三点跳动 typing 气泡）
    const msgId = `agent-remote-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
    stream = { msgId, acc: '', safetyTimer: null };
    _remoteStreamingMsgs.set(convId, stream);

    state._setters.appendMessage(convId, {
      id: msgId,
      convId,
      author: 'agent',
      authorId: 'agent',
      authorName: '炎图AI助手',
      text: '',
      attachments: [],
      status: 'streaming',
      ts: Date.now(),
      createdAt: Date.now(),
      isAgent: true,
    });

    // 安全超时：显示超时提示（气泡不消失，状态变 complete）
    stream.safetyTimer = setTimeout(() => {
      const s = _remoteStreamingMsgs.get(convId);
      if (!s || s.msgId !== msgId) return;
      _remoteStreamingMsgs.delete(convId);
      state._setters.updateMessage(convId, msgId, {
        text: s.acc || '抱歉，AI 回复超时，请稍后再试',
        status: 'complete',
      });
      state._setters.notify();
    }, 90000);
  }

  stream.acc += text || '';
  state._setters.updateMessage(convId, stream.msgId, { text: stream.acc });
  state._setters.notify();
}

function _handleRemoteAgentFinal(convId, text, attachments) {
  if (!convId) return;
  // 发送方自己处理，跳过（仅清理接收方跟踪表）
  if (_hasLocalStreamingMsg(convId)) {
    _remoteStreamingMsgs.delete(convId);
    return;
  }

  const stream = _remoteStreamingMsgs.get(convId);
  const rawText = text || (stream ? stream.acc : '');
  const pushedAttachments = attachments || [];
  const decoded = _decodeAgentMessage(rawText, true, pushedAttachments);

  // 对齐 origin-vanilla：空回复也保留气泡，状态变 complete（不消失）
  if (!decoded.text.trim() && (!decoded.attachments || decoded.attachments.length === 0)) {
    if (stream) {
      if (stream.safetyTimer) { clearTimeout(stream.safetyTimer); stream.safetyTimer = null; }
      state._setters.updateMessage(convId, stream.msgId, {
        text: '',
        status: 'complete',
      });
      _remoteStreamingMsgs.delete(convId);
      state._setters.notify();
    }
    return;
  }

  if (stream) {
    // 终结已有的 streaming 气泡
    if (stream.safetyTimer) { clearTimeout(stream.safetyTimer); stream.safetyTimer = null; }
    state._setters.updateMessage(convId, stream.msgId, {
      text: decoded.text,
      attachments: decoded.attachments,
      agentDeliveryId: decoded.agentDeliveryId || undefined,
      status: 'complete',
    });
    _remoteStreamingMsgs.delete(convId);
  } else {
    // 没有 streaming 占位（可能 delta 丢失或 C++ 只推了 final），直接创建完整消息
    state._setters.appendMessage(convId, {
      id: `agent-remote-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
      convId,
      author: 'agent',
      authorId: 'agent',
      authorName: '炎图AI助手',
      text: decoded.text,
      attachments: decoded.attachments,
      agentDeliveryId: decoded.agentDeliveryId || undefined,
      ts: Date.now(),
      createdAt: Date.now(),
      status: 'complete',
      isAgent: true,
    });
  }
  state._setters.notify();
}

function _handleRemoteAgentError(convId, message) {
  if (!convId) return;
  if (_hasLocalStreamingMsg(convId)) {
    _remoteStreamingMsgs.delete(convId);
    return;
  }
  const stream = _remoteStreamingMsgs.get(convId);
  if (!stream) return; // 没有 streaming 占位就不显示错误（避免无端冒出错误气泡）
  if (stream.safetyTimer) { clearTimeout(stream.safetyTimer); stream.safetyTimer = null; }
  // 对齐 origin-vanilla onError：显示错误文本，状态变 complete（气泡不消失）
  state._setters.updateMessage(convId, stream.msgId, {
    text: `抱歉，AI 回复失败：${message || '未知错误'}`,
    status: 'complete',
  });
  _remoteStreamingMsgs.delete(convId);
  state._setters.notify();
}

// ================================================================
// 协同协议指令处理（对齐 origin-vanilla chatStore.js:1395-1545）
// 接收 c:agentchat.collaboration 消息后按 action 分发：
// - device.open_content -> 添加系统消息 + webview 卡片
// - device.speak        -> 添加系统消息 + 浏览器 TTS
// - ai.skill_result     -> 提取 webview 附件 + 系统消息
// - ai.task_status      -> 仅日志
// - interactive.resource_event -> 系统消息
// ================================================================

function _collabCleanupDedupe(now) {
  for (const [key, expiresAt] of _collabDedupeKeys) {
    if (now > expiresAt) _collabDedupeKeys.delete(key);
  }
}

function _collabMarkDedupe(instruction, now) {
  _collabCleanupDedupe(now);
  const key = String(instruction.dedupeKey || instruction.dispatchId || '').trim();
  if (!key) return false;
  if (_collabDedupeKeys.has(key)) return true;
  _collabDedupeKeys.set(key, now + _COLLAB_DEDUPE_TTL_MS);
  return false;
}

function _handleCollaborationInstruction(convId, instruction, messageId, createdAt) {
  if (!instruction || instruction.protocol !== 'agentchat.collaboration') return;

  const now = Date.now();

  // 过期检查
  if (isCollabExpired(instruction, now)) {
    console.log('[bridge] collab expired', instruction.action);
    return;
  }

  // 去重
  if (_collabMarkDedupe(instruction, now)) {
    console.log('[bridge] collab deduplicated', instruction.action, instruction.dedupeKey);
    return;
  }

  const action = instruction.action;

  if (action === COLLABORATION_ACTIONS.DEVICE_OPEN_CONTENT) {
    _handleCollabOpenContent(convId, instruction, messageId, createdAt);
  } else if (action === COLLABORATION_ACTIONS.DEVICE_SPEAK) {
    _handleCollabSpeak(convId, instruction, messageId, createdAt);
  } else if (action === COLLABORATION_ACTIONS.AI_SKILL_RESULT) {
    _handleCollabSkillResult(convId, instruction, messageId, createdAt);
  } else if (action === COLLABORATION_ACTIONS.AI_TASK_STATUS) {
    console.log('[bridge] collab ai.task_status', instruction.payload?.status);
  } else if (action === COLLABORATION_ACTIONS.INTERACTIVE_RESOURCE_EVENT) {
    _handleCollabInteractiveEvent(convId, instruction, messageId, createdAt);
  } else {
    console.log('[bridge] collab unsupported action:', action);
  }
}

function _handleCollabOpenContent(convId, instruction, messageId, createdAt) {
  const payload = instruction.payload || {};
  const contentType = String(payload.contentType || '').trim();
  const url = String(payload.url || payload.resourceUrl || '').trim();
  const title = String(payload.title || '内容').trim();
  const summary = String(payload.summary || '').trim();

  if (contentType === 'webview' && url) {
    _addSystemMessage(convId, `投递到设备：${title}`, [{
      type: 'webview',
      url,
      title,
      summary: summary || undefined,
      postId: payload.postId,
    }], messageId, createdAt);
  }
}

function _handleCollabSpeak(convId, instruction, messageId, createdAt) {
  const payload = instruction.payload || {};
  const text = String(payload.text || '').trim();
  const displayText = String(payload.displayText || text).trim();
  if (!text) return;

  _addSystemMessage(convId, `播报：${displayText}`, [], messageId, createdAt);

  // 浏览器本地 TTS（如果支持）
  if ('speechSynthesis' in window) {
    try {
      const voice = payload.voice || {};
      const utterance = new SpeechSynthesisUtterance(text);
      utterance.lang = voice.lang || 'zh-CN';
      if (voice.rate) utterance.rate = Number(voice.rate);
      if (voice.pitch) utterance.pitch = Number(voice.pitch);
      window.speechSynthesis.speak(utterance);
    } catch (e) {
      console.warn('[bridge] TTS failed:', e);
    }
  }
}

function _handleCollabSkillResult(convId, instruction, messageId, createdAt) {
  const payload = instruction.payload || {};
  const outputs = Array.isArray(payload.outputs) ? payload.outputs : [];
  const summary = String(payload.summary || '').trim();
  const status = String(payload.status || 'done').trim();

  const attachments = [];
  for (const output of outputs) {
    if (!output || typeof output !== 'object') continue;
    const type = String(output.type || '').trim();
    if (type === 'webview') {
      const url = String(output.url || '').trim();
      if (url && /^https?:\/\//i.test(url)) {
        attachments.push({
          type: 'webview',
          url,
          title: String(output.title || '').trim() || undefined,
          summary: String(output.summary || '').trim() || undefined,
          taskNo: payload.taskNo,
          providerId: payload.providerId,
          skillId: payload.skillId,
        });
      }
    }
  }

  _addSystemMessage(convId, summary || 'AI 结果已返回', attachments, messageId, createdAt);
}

function _handleCollabInteractiveEvent(convId, instruction, messageId, createdAt) {
  const payload = instruction.payload || {};
  const summary = String(payload.summary || '').trim();
  if (summary) {
    _addSystemMessage(convId, summary, [], messageId, createdAt);
  }
}

function _addSystemMessage(convId, text, attachments, messageId, createdAt) {
  const ts = createdAt || Date.now();
  state._setters.appendMessage(convId, {
    id: messageId || `sys-${ts}-${Math.random().toString(36).slice(2, 8)}`,
    convId,
    author: 'system',
    authorName: '系统',
    text,
    attachments: attachments || [],
    status: 'complete',
    delivery: 'sent',
    ts,
    createdAt: ts,
  });
  state._setters.notify();
}

// ================================================================
// 语音转文字通道（原 ChatApi.transcribeAudio -> tts-bridge）
// ================================================================
async function transcribeAudio(audioBlob, lang = 'zh-CN') {
  // 注意：postMessage 无法直接传递 Blob，C++ 侧约定通过 ArrayBuffer 或 base64
  // 占位：返回 mock 文本
  return core.request('tts.transcribe', { lang });
}

// ================================================================
// 交互资源 Bridge（H5 卡片 <-> JS postMessage 双向通信）
// ----------------------------------------------------------------
// 对齐 origin-vanilla chatStore.js 的 _interactiveBridgeState 管理：
// - openInteractiveResource: 注册 message 监听 + 解析 context + 标记历史消息
// - onInteractiveFrameLoaded: iframe load 后发送 bridge_context
// - _handleInteractiveMessage: 接收 H5 的 interactive.ready / send_message
// - _forwardPendingMessages: 把会话新消息转发给 iframe（conversation_message / skill_result）
// ================================================================
function getInteractiveBridgeState() {
  // 只暴露页面渲染需要的字段（避免内部 Set/Map 被外部误改）
  return {
    active: _interactiveBridgeState.active,
    bridgeReady: _interactiveBridgeState.bridgeReady,
    context: _interactiveBridgeState.context,
  };
}

async function openInteractiveResource(input, getFrameWindow) {
  const viewer = {
    userId: state.getUserId() || undefined,
    phone: state.getPhone() || undefined,
    displayName: state.getPhone() || undefined,
  };

  const taskPayload = {
    type: 'webview',
    url: input.url,
    title: input.title,
    summary: input.summary,
    postId: input.postId,
    taskNo: input.taskNo,
    providerId: input.providerId,
    skillId: input.skillId,
    sourceSkillId: input.sourceSkillId,
    objectKind: input.objectKind,
    artifactType: input.artifactType,
    mimeType: input.mimeType,
    input: input.input,
  };

  const context = resolveInteractiveResourceContext(taskPayload, input.conversationId, viewer);
  if (!context) {
    console.warn('[bridge] openInteractiveResource: context is null');
    return false;
  }

  // 旧监听器先清掉，避免泄漏
  if (_interactiveBridgeState._messageHandler) {
    window.removeEventListener('message', _interactiveBridgeState._messageHandler);
  }

  _interactiveBridgeState = {
    active: true,
    context,
    getFrameWindow,
    bridgeReady: false,
    forwardedMessageIds: new Set(),
    suppressedTexts: new Map(),
    _messageHandler: (event) => _handleInteractiveMessage(event),
  };

  // 标记已有消息为已转发（不重发历史消息给 H5）
  const existingMsgs = state.getMessages(input.conversationId) || [];
  existingMsgs.forEach((m) => _interactiveBridgeState.forwardedMessageIds.add(m.id));

  // 注册 message 监听（必须在设置 iframe.src 之前完成，
  // 避免 H5 的 interactive.ready 早于监听注册到达导致丢失）
  window.addEventListener('message', _interactiveBridgeState._messageHandler);

  // 同时通知 C++ 侧（用于统计/日志，C++ 不参与桥接逻辑）
  try { await core.request('interactive.open', input); } catch (e) {
    console.warn('[bridge] interactive.open notify failed:', e?.message || e);
  }

  console.log('[bridge] interactive bridge opened', {
    conversationId: context.conversationId,
    resourceId: context.resourceId,
    instanceId: context.instanceId,
    origin: context.origin,
    hasInput: Boolean(context.input),
    inputText: context.input?.text?.slice(0, 80),
  });
  return true;
}

async function onInteractiveFrameLoaded(getFrameWindow) {
  const ctx = _interactiveBridgeState.context;
  if (!ctx || !_interactiveBridgeState.active) return;

  // 更新 getFrameWindow 回调，确保后续发送使用最新的 iframe.contentWindow
  // （iframe 重载或 ready 先于 onload 到达时仍能取到 contentWindow）
  _interactiveBridgeState.getFrameWindow = getFrameWindow;
  _sendBridgeContext();
  _forwardPendingMessages();
}

async function closeInteractiveResource() {
  if (_interactiveBridgeState._messageHandler) {
    window.removeEventListener('message', _interactiveBridgeState._messageHandler);
  }
  _interactiveBridgeState = {
    active: false,
    context: null,
    getFrameWindow: null,
    bridgeReady: false,
    forwardedMessageIds: new Set(),
    suppressedTexts: new Map(),
    _messageHandler: null,
  };
  // 通知 C++ 侧（用于统计/日志）
  try { await core.request('interactive.close', {}); } catch (e) {
    console.warn('[bridge] interactive.close notify failed:', e?.message || e);
  }
  console.log('[bridge] interactive bridge closed');
}

// ── 内部辅助函数 ──

/**
 * 把 agent-chat-vanilla 内部消息格式归一化为 interactiveResourceBridge 期望的格式
 * （origin-vanilla 用 message.content / message.conversationId / message.author；
 *  agent-chat-vanilla 用 message.text / message.convId，且 agent 消息可能没有 author 字段）
 */
function _normalizeMessageForBridge(msg, convId) {
  if (!msg) return null;
  const text = String(msg.text ?? msg.content ?? '').trim();
  const isAgent = msg.author === 'agent' || msg.authorId === 'agent' || msg.isAgent === true;
  const author = msg.author || (isAgent ? 'agent' : (msg.authorId === state.getUserId() ? 'user' : 'other_user'));
  return {
    id: msg.id,
    conversationId: msg.conversationId || msg.convId || convId,
    author,
    authorId: msg.authorId,
    authorName: msg.authorName,
    content: text,
    attachments: Array.isArray(msg.attachments) ? msg.attachments : [],
    status: msg.status,
    createdAt: msg.createdAt || msg.ts,
    serverMessageId: msg.serverMessageId,
  };
}

function _sendBridgeContext() {
  const ctx = _interactiveBridgeState.context;
  if (!ctx) return;
  const frameWindow = _interactiveBridgeState.getFrameWindow ? _interactiveBridgeState.getFrameWindow() : null;
  const payload = buildHostBridgeContextMessage(ctx);
  if (frameWindow) {
    frameWindow.postMessage(payload, ctx.origin);
    console.log('[bridge] post bridge_context', {
      conversationId: payload.conversationId,
      resourceId: payload.resourceId,
      hasInput: Boolean(payload.resource?.input),
    });
  }
}

function _forwardPendingMessages() {
  const ctx = _interactiveBridgeState.context;
  if (!ctx || !_interactiveBridgeState.bridgeReady) return;
  const frameWindow = _interactiveBridgeState.getFrameWindow ? _interactiveBridgeState.getFrameWindow() : null;
  if (!frameWindow) return;

  const convId = ctx.conversationId;
  const msgs = state.getMessages(convId) || [];

  for (const raw of msgs) {
    if (_interactiveBridgeState.forwardedMessageIds.has(raw.id)) continue;
    const message = _normalizeMessageForBridge(raw, convId);
    if (!message) continue;

    // 抑制 H5 回写的消息（避免 echo）
    const text = message.content;
    if (text && _interactiveBridgeState.suppressedTexts.has(text)) {
      const expiresAt = _interactiveBridgeState.suppressedTexts.get(text);
      if (Date.now() > expiresAt) {
        _interactiveBridgeState.suppressedTexts.delete(text);
      } else {
        _interactiveBridgeState.suppressedTexts.delete(text);
        _interactiveBridgeState.forwardedMessageIds.add(raw.id);
        continue;
      }
    }

    let didForward = false;

    // 转发用户消息（协议 §4.2.2 agentchat.conversation_message）
    if (message.author === 'user' || message.author === 'other_user') {
      const convPayload = buildHostConversationMessage(ctx, message);
      if (convPayload) {
        frameWindow.postMessage(convPayload, ctx.origin);
        didForward = true;
      }
    }

    // 转发 Agent 结果（协议 §4.2.3 agentchat.skill_result）
    if (message.author === 'agent' && message.status !== 'streaming') {
      const skillPayload = buildHostSkillResultMessage(ctx, message);
      if (skillPayload) {
        frameWindow.postMessage(skillPayload, ctx.origin);
        didForward = true;
      }
    }

    if (didForward) {
      _interactiveBridgeState.forwardedMessageIds.add(raw.id);
    }
  }
}

function _handleInteractiveMessage(event) {
  const ctx = _interactiveBridgeState.context;
  if (!ctx || !_interactiveBridgeState.active) return;
  const frameWindow = _interactiveBridgeState.getFrameWindow ? _interactiveBridgeState.getFrameWindow() : null;
  const parsed = parseInteractiveResourceEvent(event, ctx, frameWindow, _interactiveBridgeState.bridgeReady);
  if (!parsed) return;

  if (parsed.type === 'ready') {
    _interactiveBridgeState.bridgeReady = true;
    console.log('[bridge] received interactive.ready', {
      conversationId: ctx.conversationId,
      resourceId: ctx.resourceId,
    });
    _sendBridgeContext();
    _forwardPendingMessages();
    return;
  }

  if (parsed.type === 'send_message') {
    console.log('[bridge] received interactive.send_message', {
      conversationId: parsed.conversationId,
      text: parsed.text?.slice(0, 50),
    });
    // 抑制回写消息的转发（避免 echo）
    _interactiveBridgeState.suppressedTexts.set(parsed.text, Date.now() + 5000);
    // 发送到会话
    void sendUserMessage(parsed.text, parsed.conversationId);
  }
}

/**
 * state 变化时被调用：把新消息转发给已打开的 iframe
 */
function _notifyInteractiveBridge() {
  if (!_interactiveBridgeState.active || !_interactiveBridgeState.bridgeReady) return;
  _forwardPendingMessages();
}

// ================================================================
// 协同分发（原 dispatchContentToDevice / dispatchSpeakToDevice）
// ================================================================
async function dispatchContentToDevice({ url, title, conversationId, attachment }) {
  return core.request('device.dispatch_content', { url, title, conversationId, attachment });
}
async function dispatchSpeakToDevice({ text, conversationId }) {
  return core.request('device.dispatch_speak', { text, conversationId });
}

// ================================================================
// 通知
// ================================================================
function getGroupInvitationNotifications() { return state.getGroupInvitationNotifications(); }
function getPushNotifications() { return state.getPushNotifications(); }
function getLastSeenNotificationsAt() { return state.getLastSeenNotificationsAt(); }
function setLastSeenNotificationsAt(ts) { state.setLastSeenNotificationsAt(ts); state._setters.notify(); }

// ================================================================
// 消息解析工具（纯本地，保留原功能）
// ================================================================
function parseSharedPostMessageContent(raw, convId) {
  return state.parseSharedPostMessageContent(raw, convId);
}

/**
 * 对齐 agent 项目 postShareEnvelope.ts encodeForwardAttachmentContent：
 * 将转发附件编码为 HTML 注释信封格式，附加到显示文本后。
 * 格式：displayText\n<!-- agent-chat:forward-attachment <base64> -->
 * 接收方通过 parseSharedPostMessageContent 解析信封，还原 displayText 和 attachments。
 * 使用 HTML 注释 + base64 编码，避免服务端文本透传时 JSON 被截断或转义。
 */
const FORWARD_ATTACHMENT_START = '<!-- agent-chat:forward-attachment ';
const FORWARD_ATTACHMENT_END = ' -->';

function _toBase64Utf8(value) {
  const bytes = new TextEncoder().encode(value);
  let binary = '';
  for (let i = 0; i < bytes.length; i++) { binary += String.fromCharCode(bytes[i]); }
  return btoa(binary);
}

function _fromBase64Utf8(value) {
  const binary = atob(value);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) { bytes[i] = binary.charCodeAt(i); }
  return new TextDecoder().decode(bytes);
}

function encodeForwardAttachment(displayText, title, attachments) {
  const envelope = { version: 1, title, attachments };
  const token = _toBase64Utf8(JSON.stringify(envelope));
  return displayText + '\n' + FORWARD_ATTACHMENT_START + token + FORWARD_ATTACHMENT_END;
}

// ================================================================
// 草稿持久化（输入框内容本地存储）
// ================================================================
function saveComposerDrafts(drafts) {
  chatHistoryStore.saveDrafts(drafts);
}
function loadComposerDrafts() {
  return chatHistoryStore.loadDrafts();
}

// ================================================================
// 重置（登出/切换账号时清理）
// ================================================================
async function resetForAuthChange() {
  try { await disconnect(); } catch (e) {}
  chatHistoryStore.clearAllMessages();
  chatHistoryStore.clearDrafts();
  chatHistoryStore.clearKickedRooms();
  state._setters.resetAll();
  state._setters.notify();
}

function subscribe(fn) { return state.subscribe(fn); }
function onPush(handler) { return core.onPush(handler); }
function isMockMode() { return false; }

export default {
  init,
  // auth
  sendSmsCode, login, logout, isLoggedIn, getPhone, getUserId, getSessionId, applyInjectedAuth,
  // chat
  connect, disconnect, loadConversations, getConversations, getActiveConversationId,
  getActiveConversation, setActiveConversation, clearMarkRead, getMessages, getActiveMessages,
  getStatus, getConversationUnreadCount, getTotalUnreadCount, sendUserMessage, retrySend,
  loadConversationHistory, createGroupConversation, deleteConversationFromList,
  // posts
  loadGroupPosts, loadAllGroupPosts, getPosts, createGroupPost, closeGroupPost,
  respondToGroupPost, submitHomeworkPostResponse,
  // members
  getRoomMembers, inviteRoomMember, removeRoomMember,
  // topics
  loadTopics, getTopics, createTopic, joinTopic, leaveTopic,
  // tasks
  getPersonalTasksForCurrentUser, loadPersonalTasksFromServer, createPersonalTask,
  completePersonalTask, cancelPersonalTask, reschedulePersonalTask,
  // devices
  refreshBoundDevices, getBoundDevices, getPendingDeviceSessions, getBindableConversations,
  parseDeviceBindPayload, createDeviceBindSession, getDeviceBindSession, confirmDeviceBind, unbindDevice,
  // agent / workspace
  getAgentSkills, isSkillEnabled, toggleSkill, enableAllSkills, disableAllSkills,
  getExecutionHistory, getWorkspaceDraftActions, publishWorkspaceDraft, getAgentSession,
  onAgentStream,
  // tts
  transcribeAudio,
  // interactive
  getInteractiveBridgeState, openInteractiveResource, onInteractiveFrameLoaded, closeInteractiveResource,
  // dispatch
  dispatchContentToDevice, dispatchSpeakToDevice,
  // notifications
  getGroupInvitationNotifications, getPushNotifications, getLastSeenNotificationsAt, setLastSeenNotificationsAt,
  // utils
  parseSharedPostMessageContent, encodeForwardAttachment, resetForAuthChange,
  // drafts
  saveComposerDrafts, loadComposerDrafts,
  // pub/sub
  subscribe, onPush, isMockMode,
};
