/* ================================================================
   AgentChat HTML 版 - Chatroom Native WebView Transport
   通过 chrome.webview.postMessage 与 C++ CChatRoomBridge 通信
   ================================================================ */

function _buildRequestId() {
  return `chatroom-${Date.now()}-${Math.random().toString(16).slice(2, 10)}`;
}

function _trace(requestId, stage, detail) {
  const suffix = detail ? ` ${detail}` : '';
  console.info(`[ChatroomTransport][${requestId}] ${stage}${suffix}`);
}

/**
 * 检查是否支持 Native WebView Bridge
 */
function _supportsNativeWebViewBridge() {
  return typeof window !== 'undefined'
    && !!window.chrome
    && !!window.chrome.webview
    && typeof window.chrome.webview.postMessage === 'function';
}

/**
 * 发送 Chatroom Bridge 请求到 C++
 * @param {string} kind - 请求类型
 * @param {object} payload - 请求负载
 * @param {string} sessionId - 会话 ID
 * @param {number} timeoutMs - 超时毫秒
 * @returns {Promise<object>} 响应 payload
 */
export async function chatroomBridgeRequest(kind, payload = {}, sessionId = '0', timeoutMs = 30000) {
  console.info(`[chatroomBridgeRequest] kind=${kind} sessionId=${sessionId} payload=${JSON.stringify(payload)}`);
  if (!_supportsNativeWebViewBridge()) {
    throw new Error('Native WebView Bridge not available');
  }

  const requestId = _buildRequestId();
  _trace(requestId, 'request.start', `kind=${kind}`);

  return new Promise((resolve, reject) => {
    let settled = false;
    let timeoutHandle = null;

    const cleanup = () => {
      window.removeEventListener('chatroom.bridge.response', onMessage);
      if (timeoutHandle) clearTimeout(timeoutHandle);
    };

    const settleResolve = (result) => {
      if (settled) return;
      settled = true;
      cleanup();
      _trace(requestId, 'request.resolve');
      resolve(result);
    };

    const settleReject = (error) => {
      if (settled) return;
      settled = true;
      cleanup();
      _trace(requestId, 'request.reject', error?.message);
      reject(error);
    };

    timeoutHandle = setTimeout(() => {
      settleReject(new Error('Chatroom bridge request timeout'));
    }, timeoutMs);

    const onMessage = (event) => {
      const msg = event?.detail;
      if (!msg || typeof msg !== 'object') return;
      if (String(msg.requestId || '') !== requestId) return;

      const channel = String(msg.channel || '');
      // 对齐 C++ 拆分：C++ 现在把 response 派发到 chatroom.bridge.response，
      // push 派发到 chatroom.bridge.push。response / push 监听不同的事件，避免相互串扰。
      if (channel !== 'chatroom.bridge.response') return;

      if (msg.ok === false) {
        const errorMsg = msg?.error?.message || msg?.error || 'Unknown error';
        settleReject(new Error(String(errorMsg)));
        return;
      }

      settleResolve(msg.payload || {});
    };

    window.addEventListener('chatroom.bridge.response', onMessage);

    try {
      window.chrome.webview.postMessage({
        channel: 'chatroom.bridge.request',
        requestId,
        kind,
        payload,
        sessionId,
      });
      _trace(requestId, 'request.posted');
    } catch (err) {
      settleReject(err);
    }
  });
}

// ── 高级 API ──────────────────────────────────────────────────────

/**
 * 获取频道列表
 */
export async function chatroomListConversations(sessionId = '0') {
  return chatroomBridgeRequest('list_conversations', {}, sessionId);
}

/**
 * 创建频道
 * @param {string} name - 频道名称
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomCreateConversation(name, sessionId = '0') {
  return chatroomBridgeRequest('create_conversation', { name }, sessionId);
}

/**
 * 加入频道
 * @param {string} channel - 频道名称 (带不带 # 都可以)
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomJoinChannel(channel, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('join_channel', { channel: normalizedChannel }, sessionId);
}

/**
 * 离开频道
 * @param {string} channel - 频道名称
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomPartChannel(channel, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('part_channel', { channel: normalizedChannel }, sessionId);
}

/**
 * 发送消息
 * @param {string} channel - 频道名称
 * @param {string} message - 消息内容
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomSendMessage(channel, message, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('send_message', {
    channel: normalizedChannel,
    message,
  }, sessionId);
}

/**
 * 发送 Prompt (TCP 明文传输，用于 AI 指令)
 * @param {string} channel - 频道名称
 * @param {string} message - 消息内容
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomSendPrompt(channel, message, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('send_prompt', {
    channel: normalizedChannel,
    message,
  }, sessionId);
}

/**
 * 踢出成员
 * @param {string} channel - 频道名称
 * @param {string} targetNick - 目标用户昵称
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomKickMember(channel, targetNick, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('kick_member', {
    channel: normalizedChannel,
    target: targetNick,
  }, sessionId);
}

/**
 * 封禁成员
 * @param {string} channel - 频道名称
 * @param {string} mask - 封禁 mask (如 *!user@host)
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomBanMember(channel, mask, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('ban_member', {
    channel: normalizedChannel,
    mask,
  }, sessionId);
}

/**
 * 设置频道主题
 * @param {string} channel - 频道名称
 * @param {string} topic - 主题内容
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomSetTopic(channel, topic, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('set_topic', {
    channel: normalizedChannel,
    topic,
  }, sessionId);
}

/**
 * 设置频道模式
 * @param {string} channel - 频道名称
 * @param {string} mode - 模式字符串 (如 +i, +k password)
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomSetMode(channel, mode, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('set_mode', {
    channel: normalizedChannel,
    mode,
  }, sessionId);
}

/**
 * 提升为 Operator
 * @param {string} channel - 频道名称
 * @param {string} targetNick - 目标用户昵称
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomPromoteOperator(channel, targetNick, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('promote_operator', {
    channel: normalizedChannel,
    target: targetNick,
  }, sessionId);
}

/**
 * 降级 Operator
 * @param {string} channel - 频道名称
 * @param {string} targetNick - 目标用户昵称
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomDemoteOperator(channel, targetNick, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('demote_operator', {
    channel: normalizedChannel,
    target: targetNick,
  }, sessionId);
}

/**
 * 查询用户信息
 * @param {string} nickname - 用户昵称
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomWhois(nickname, sessionId = '0') {
  return chatroomBridgeRequest('whois', { nickname }, sessionId);
}

/**
 * 获取频道成员列表
 * @param {string} channel - 频道名称
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomNames(channel, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('names', { channel: normalizedChannel }, sessionId);
}

/**
 * 获取历史消息
 * @param {string} channel - 频道名称
 * @param {number} limit - 消息数量限制
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomGetHistory(channel, limit = 50, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('get_history', {
    channel: normalizedChannel,
    limit,
  }, sessionId);
}

export async function chatroomGetRoomInfo(channel, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('get_room_info', {
    room_id: normalizedChannel,
  }, sessionId);
}

export async function chatroomListRoomMembers(channel, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('list_room_members', {
    room_id: normalizedChannel,
  }, sessionId);
}

/**
 * 邀请成员加入房间
 * 对齐 Node.js chat-bridge.mjs /room/invite 的字段：channel / member_kind / target / role
 */
export async function chatroomInviteMember(channel, memberKind, target, role = 'member', sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('invite_member', {
    channel: normalizedChannel,
    member_kind: String(memberKind || 'user').trim() || 'user',
    target: String(target || '').trim(),
    role: String(role || 'member').trim() || 'member',
  }, sessionId);
}

export async function chatroomRemoveMember(channel, memberKind, target, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('remove_member', {
    channel: normalizedChannel,
    member_kind: String(memberKind || 'user').trim() || 'user',
    target: String(target || '').trim(),
  }, sessionId);
}

// ── 话题 (Topic) API ────────────────────────────────────────────────

/**
 * 创建话题
 * @param {string} channel - 频道名称
 * @param {string} title - 话题标题
 * @param {string} content - 初始内容
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomCreateTopic(channel, title, content = '', sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('create_topic', {
    channel: normalizedChannel,
    title,
    content,
  }, sessionId);
}

/**
 * 回复话题
 * @param {string} channel - 频道名称
 * @param {string} topicId - 话题 ID
 * @param {string} content - 回复内容
 * @param {string} parentReplyId - 父回复 ID (可选，用于嵌套回复)
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomReplyTopic(channel, topicId, content, parentReplyId = '', sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('reply_topic', {
    channel: normalizedChannel,
    topic_id: topicId,
    content,
    parent_reply_id: parentReplyId,
  }, sessionId);
}

/**
 * 列出话题
 * @param {string} channel - 频道名称
 * @param {string} status - 状态过滤 (可选: open, closed, pinned, archived)
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomListTopics(channel, status = '', sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('list_topics', {
    channel: normalizedChannel,
    status,
  }, sessionId);
}

/**
 * 关闭话题
 * @param {string} channel - 频道名称
 * @param {string} topicId - 话题 ID
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomCloseTopic(channel, topicId, sessionId = '0') {
  const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;
  return chatroomBridgeRequest('close_topic', {
    channel: normalizedChannel,
    topic_id: topicId,
  }, sessionId);
}

// ── 群任务帖子 (Post) API ─────────────────────────────────────────

/**
 * 创建帖子
 * @param {object} data - 帖子数据
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomCreatePost(data, sessionId = '0') {
  return chatroomBridgeRequest('create_post', {
    conversationId: data.conversationId,
    id: data.id || '',
    title: data.title || '',
    summary: data.summary || '',
    taskKind: data.taskKind || 'notice',
    actionType: data.actionType || 'read',
    resourceType: data.resourceType || 'none',
    resourceUrl: data.resourceUrl || '',
    deadlineAt: data.deadlineAt || '',
    createdById: data.createdById || '',
    createdByName: data.createdByName || '',
  }, sessionId);
}

/**
 * 获取帖子列表
 * @param {string} conversationId - 会话 ID
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomListPosts(conversationId, sessionId = '0') {
  return chatroomBridgeRequest('list_posts', {
    conversationId,
  }, sessionId);
}

/**
 * 更新帖子
 * @param {string} postId - 帖子 ID
 * @param {string} conversationId - 会话 ID
 * @param {object} data - 更新数据
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomUpdatePost(postId, conversationId, data, sessionId = '0') {
  return chatroomBridgeRequest('update_post', {
    postId,
    conversationId,
    title: data.title || '',
    summary: data.summary || '',
  }, sessionId);
}

/**
 * 关闭帖子
 * @param {string} postId - 帖子 ID
 * @param {string} conversationId - 会话 ID
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomClosePost(postId, conversationId, sessionId = '0') {
  return chatroomBridgeRequest('close_post', {
    postId,
    conversationId,
  }, sessionId);
}

/**
 * 响应帖子（提交作业/确认阅读等）
 * @param {object} data - 响应数据
 * @param {string} sessionId - 会话 ID
 */
export async function chatroomRespondToPost(data, sessionId = '0') {
  return chatroomBridgeRequest('respond_to_post', {
    postId: data.postId || data.id || '',
    conversationId: data.conversationId || '',
    actorUserId: data.actorUserId || '',
    actorName: data.actorName || '',
    actionType: data.actionType || 'read',
    responseType: data.responseType || 'read',
    confirmation: data.confirmation || '',
    content: data.content || '',
  }, sessionId);
}

// ── 推送事件监听 ───────────────────────────────────────────────────

/**
 * 监听 Chatroom 推送事件
 * @param {function} handler - 事件处理函数 (event: { eventType, payload, sessionId })
 * @returns {function} 取消监听函数
 */
export function onChatroomPush(handler) {
  const onMessage = (event) => {
    const msg = event?.detail;
    if (!msg || typeof msg !== 'object') {
      console.log('[ChatroomTransport] onChatroomPush: invalid event.detail', event);
      return;
    }
    // C++ 现在批量下发 push 数组：detail 可能是单条对象或数组
    const items = Array.isArray(msg) ? msg : [msg];
    for (const single of items) {
      if (!single || typeof single !== 'object') continue;
      console.log('[ChatroomTransport] onChatroomPush received:', JSON.stringify(single).slice(0, 300));
      if (String(single.channel || '') !== 'chatroom.bridge.push') {
        console.log('[ChatroomTransport] onChatroomPush: wrong channel, expected chatroom.bridge.push, got', single.channel);
        continue;
      }

      // C++ 可能把真实 IRC 事件数据嵌套在 payload.raw 中（JSON 字符串），
      // 外层 payload 可能只有 channel="" 和 message="server_ack_filtered"。
      // 这里解析 raw 并合并到 payload 中。
      const basePayload = single.payload || {};
      let rawParsed = null;
      if (typeof basePayload.raw === 'string') {
        try {
          rawParsed = JSON.parse(basePayload.raw);
        } catch (e) { /* raw 不是合法 JSON，忽略 */ }
      }

      // 合并：raw 中的字段优先（覆盖外层的 server_ack_filtered 等）
      const mergedPayload = rawParsed
        ? { ...basePayload, ...rawParsed }
        : { ...basePayload };

      // 规范化 channel：前端存频道时加了 #，但 push payload 里可能没有。
      // 用浅拷贝避免污染原对象，再用 channel 字段替换。
      // 关键：过滤掉事件 channel 本身（chatroom.bridge.push 等），防止 fallback 时把它误当成 IRC channel。
      const payload = { ...mergedPayload, channel: (() => {
        let raw = mergedPayload.channel || '';
        if (!raw) return '';
        // 过滤事件 channel 名（chatroom.bridge.push / agentchat.bridge.*）
        // 这种情况说明 backend 没有提供真正的 channel，应当让上层忽略，不要 fallback 到 single.channel
        if (/^(chatroom|agentchat)\.bridge\./i.test(raw)) return '';
        if (!raw.startsWith('#')) raw = '#' + raw;
        return raw;
      })() };

      // 如果 channel 为空直接跳过，否则会把事件名暴露给上层当成群名。
      if (!payload.channel) continue;

      // eventType：外层为 "unknown" 时，尝试从 raw.event 获取
      const eventType = (single.eventType && single.eventType !== 'unknown')
        ? single.eventType
        : (rawParsed?.event || single.eventType || '');

      handler({
        eventType,
        payload,
        sessionId: single.sessionId || '0',
        timestampMs: single.timestampMs || 0,
      });
    }
  };

  window.addEventListener('chatroom.bridge.push', onMessage);
  return () => window.removeEventListener('chatroom.bridge.push', onMessage);
}

// ── 导出默认 Transport 接口 ─────────────────────────────────────────

class ChatroomNativeTransport {
  constructor(options = {}) {
    this.handler = null;
    this.sessionId = String(options.sessionId || '0');
    this._pushUnsubscribe = null;
    // 最近一次发出去的消息文本（用于 echo 判重）
    this._recentOutbound = new Map(); // channel -> Set<text>
    this._recentOutboundMax = 50;
  }

  connect() {
    // WebView Bridge 是同步就绪的，模拟连接成功
    if (this.handler) {
      this.handler({ type: 'transport_status', status: 'connected' });
    }
  }

  disconnect() {
    if (this._pushUnsubscribe) {
      this._pushUnsubscribe();
      this._pushUnsubscribe = null;
    }
    if (this.handler) {
      this.handler({ type: 'transport_status', status: 'disconnected' });
    }
  }

  sendMessage(payload) {
    const channel = String(payload?.conversationId || '').trim();
    const text = String(payload?.text || '');
    if (channel && text) {
      let set = this._recentOutbound.get(channel);
      if (!set) {
        set = new Set();
        this._recentOutbound.set(channel, set);
      }
      set.add(text);
      // 限流，避免 Set 无限增长
      if (set.size > this._recentOutboundMax) {
        const first = set.values().next().value;
        set.delete(first);
      }
    }
    // fire-and-forget，但 promise resolve 后向 chatStore 报 send_ack
    // 让 user 的 optimistic 消息从 sending → sent（避免 15s 后被超时标 failed）
    const requestPromise = chatroomSendMessage(payload.conversationId, payload.text, this.sessionId);
    Promise.resolve(requestPromise)
      .then((resp) => {
        if (this.handler) {
          const ok = resp && resp.sent !== false;
          this.handler({
            type: 'send_ack',
            conversationId: channel,
            status: ok ? 'sent' : 'failed',
          });
        }
      })
      .catch(() => {
        if (this.handler) {
          this.handler({
            type: 'transport_error',
            conversationId: channel,
            message: 'send_message_failed',
          });
        }
      });
    return requestPromise;
  }

  _consumeOutboundEcho(channel, text) {
    const set = this._recentOutbound.get(channel);
    if (!set) return false;
    if (set.has(text)) {
      set.delete(text);
      return true;
    }
    return false;
  }

  joinConversation(conversationId) {
    const channel = String(conversationId || '').trim();
    const requestPromise = chatroomJoinChannel(channel, this.sessionId);
    Promise.resolve(requestPromise)
      .then((resp) => {
        if (this.handler) {
          const ok = resp && resp.joined !== false;
          this.handler({
            type: 'send_ack',
            conversationId: channel,
            status: ok ? 'sent' : 'failed',
          });
        }
      })
      .catch(() => {
        if (this.handler) {
          this.handler({
            type: 'transport_error',
            conversationId: channel,
            message: 'join_channel_failed',
          });
        }
      });
    return requestPromise;
  }

  onEvent(handler) {
    this.handler = handler;
    // 监听推送事件
    this._pushUnsubscribe = onChatroomPush((push) => {
      console.log('[ChatroomTransport] onEvent sees push:', JSON.stringify(push).slice(0, 300));
      if (!this.handler) {
        console.log('[ChatroomTransport] onEvent: no handler set, dropping');
        return;
      }

      const eventType = String(push.eventType || '').toLowerCase();
      const payload = push.payload || {};
      const channel = String(payload.channel || '');
      const sender = String(payload.sender || '');
      const message = String(payload.message || '');
      const timestampMs = payload.timestamp_ms || push.timestampMs || Date.now();

      console.log('[ChatroomTransport] onEvent: eventType=', eventType, 'channel=', channel, 'sender=', sender, 'message=', message);

      // IRC privmsg → chatStore 认识的 message_received/message_echo
      if (eventType === 'privmsg' || eventType === 'irc_message_resp') {
        const selfIds = new Set([String(this.sessionId || ''), 'self']);
        const isSelfById = sender && selfIds.has(sender);
        const isSelfByEcho = !!channel && this._consumeOutboundEcho(channel, message);
        const isSelf = isSelfById || isSelfByEcho;

        // 修复：author 必须是 chatStore / chat.js 认识的枚举值
        // ('user' | 'agent' | 'other_user' | 'system')，否则 _renderBubble
        // 会因为所有分支都不命中而返回 ''，导致消息被加入 _messagesByConv
        // 但气泡根本不渲染（前端表现就是"消息没显示"）。
        // 原始 sender（如手机号 "17786625683"）放进 authorName 用于显示。
        const authorType = isSelf ? 'user' : 'other_user';
        // 兜底：如果 sender 为空（C++ 桥接的 sender_nick 在 JSON 异常路径下被丢掉），
        // 用 channel 末尾或通用 "成员" 作为 authorName，避免前端 lastMsg.content
        // 之外的预览信息全空。
        let senderName = sender;
        if (!senderName) {
          senderName = isSelf ? '我' : (channel && channel.startsWith('#')
            ? `${channel} 成员`
            : '成员');
        }

        const baseEvent = {
          conversationId: channel,
          author: authorType,
          authorName: senderName,
          text: message,
          messageId: `irc-${timestampMs}-${channel}`,
          createdAt: timestampMs,
          raw: payload.raw,
        };
        this.handler(isSelf
          ? { type: 'message_echo', ...baseEvent }
          : {
              type: 'message_received',
              ...baseEvent,
            });
        return;
      }

      if (eventType === 'join' || eventType === 'part' ||
          eventType === 'kick' || eventType === 'ban') {
        this.handler({
          type: 'conversation_signal',
          conversationId: channel,
          reason: eventType,
          userId: sender,
        });
        return;
      }

      if (eventType === 'topic') {
        this.handler({
          type: 'conversation_signal',
          conversationId: channel,
          reason: 'topic_changed',
        });
        return;
      }

      if (eventType === 'notice' || eventType === 'mode') {
        this.handler({
          type: 'conversation_signal',
          conversationId: channel,
          reason: eventType,
        });
        return;
      }

      // 群邀请事件 - 需要发送 chat.group_invited 消息给 bridge
      if (eventType === 'group_invited') {
        // message 字段包含 JSON: {room_id, room_name, inviter, inviter_phone, ts}
        let roomId = channel || '';
        let roomName = '';
        let inviter = sender || '';
        let inviterPhone = '';
        try {
          if (message && message.startsWith('{')) {
            const invInfo = JSON.parse(message);
            roomId = invInfo.room_id || roomId;
            roomName = invInfo.room_name || '';
            inviter = invInfo.inviter || inviter;
            inviterPhone = invInfo.inviter_phone || '';
          }
        } catch (e) {
          console.warn('[ChatroomTransport] group_invited: failed to parse message JSON', e);
        }
        // 发送 chat.group_invited 消息给 bridge (chatroomTransport 需要导入 bridge 的 dispatch 方法)
        // 由于 chatroomTransport 和 bridge 解耦，这里通过 window 事件传递
        const bridgeEvent = new CustomEvent('chat.group_invited', {
          detail: {
            payload: {
              roomId,
              roomName,
              inviter,
              inviterPhone,
              ts: message ? JSON.parse(message).ts : Date.now(),
            }
          }
        });
        window.dispatchEvent(bridgeEvent);
        
      }

      // 其它（ping/error/nick/quit）：暂不透传给 chatStore，避免误显示
    });
    return () => {
      this.handler = null;
      if (this._pushUnsubscribe) {
        this._pushUnsubscribe();
        this._pushUnsubscribe = null;
      }
    };
  }
}

export default ChatroomNativeTransport;
