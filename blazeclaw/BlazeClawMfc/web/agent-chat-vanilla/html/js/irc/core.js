/* ================================================================
   postMessage 桥接核心（话题群聊层）
   ----------------------------------------------------------------
   职责：为 irc/api.js 提供统一的 request / onPush 接口。
   委托给 chatroomTransport.js 已封装好的 API：
   - request  -> chatroomBridgeRequest(kind, payload)
   - push     -> onChatroomPush(handler)
   ================================================================ */

import { chatroomBridgeRequest, onChatroomPush } from '../transport/chatroomTransport.js';

const _pushHandlers = new Set();

/**
 * 将推送消息归一化为 irc/api.js _handlePush 期望的 { kind, payload } 格式。
 * 来源：chatroomTransport onChatroomPush: { eventType, payload, sessionId, timestampMs }
 */
function _normalizePush(msg) {
  const eventType = String(msg.eventType || msg.kind || msg.type || msg.event || '').toLowerCase();
  const rawPayload = msg.payload || {};

  let kind = eventType;
  const payload = { ...rawPayload };

  // 群聊消息事件：兼容多种事件类型名称
  const isMessageEvent = eventType === 'privmsg' || eventType === 'irc_message_resp' ||
    eventType === 'message' || eventType === 'chat_message' || eventType === 'privmsg_resp' ||
    eventType === 'msg' || eventType === 'text';

  if (isMessageEvent) {
    kind = 'privmsg';
    // 兼容多种字段名，归一化到 from/text/ts
    if (rawPayload.sender && !payload.from) payload.from = rawPayload.sender;
    if (rawPayload.nick && !payload.from) payload.from = rawPayload.nick;
    if (rawPayload.authorId && !payload.from) payload.from = rawPayload.authorId;
    if (rawPayload.author_id && !payload.from) payload.from = rawPayload.author_id;
    if (rawPayload.message && !payload.text) payload.text = rawPayload.message;
    if (rawPayload.content && !payload.text) payload.text = rawPayload.content;
    if (rawPayload.timestamp_ms && !payload.ts) payload.ts = rawPayload.timestamp_ms;
    if (rawPayload.timestamp && !payload.ts) payload.ts = rawPayload.timestamp;
  } else if (['join', 'part', 'kick', 'ban', 'quit'].includes(eventType)) {
    kind = 'member_event';
    payload.event = eventType.toUpperCase();
    if (rawPayload.sender && !payload.nick) payload.nick = rawPayload.sender;
    if (rawPayload.from && !payload.nick) payload.nick = rawPayload.from;
  }

  return { kind, payload, ts: msg.timestampMs || msg.ts || Date.now() };
}

function _dispatchPush(normalized) {
  for (const h of _pushHandlers) {
    try { h(normalized); } catch (e) { console.warn('[irc/bridge] push handler error', e); }
  }
}

/**
 * 发起请求，委托给 chatroomTransport.js 的 chatroomBridgeRequest
 */
function request(kind, payload = {}, timeoutMs = 30000) {
  return chatroomBridgeRequest(kind, payload, '0', timeoutMs);
}

function onPush(handler) {
  _pushHandlers.add(handler);
  return () => _pushHandlers.delete(handler);
}

function init() {
  console.log('[irc/bridge] init');
  // 使用 chatroomTransport.js 监听 C++ 推送
  onChatroomPush((push) => {
    _dispatchPush(_normalizePush(push));
  });
}

export default {
  init, request, onPush,
};
