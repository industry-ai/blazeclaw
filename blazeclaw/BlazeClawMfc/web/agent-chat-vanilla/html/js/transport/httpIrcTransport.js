/* ================================================================
   AgentChat HTML 版 - HTTP IRC 聊天 Transport
   对应原版 httpIrcChatTransportClient.ts
   通过 Node.js 桥接服务与 chat-server 通信
   ================================================================ */

import ChatApi from '../api/chatApi.js';
import AppConfig from '../config.js';

// 对齐 Vue 版 NativeTcpChatTransportClient.AGENT_TYPING_MARKER：
// 打字指示器 fallback 通过 AGENT_BROADCAST 广播的 marker，接收方需拦截并解析
const _AGENT_TYPING_MARKER = '[::AGENT_TYPING::]';

class HttpIrcChatTransport {
  constructor(options = {}) {
    this.handler = null;
    this.connected = false;
    this.joinedChannels = new Set();
    this.pollTimer = null;
    this.lastPollTs = 0;
    this.polling = false;
    // ── 重连相关 ──
    this.reconnecting = false;
    this.reconnectTimer = null;
    this.reconnectAttempts = 0;
    this.options = {
      pollIntervalMs: 3000,
      pollTimeoutMs: 25000,
      reconnectBaseDelayMs: 1500,
      reconnectMaxDelayMs: 15000,
      ...options,
    };
  }

  _getSessionId() {
    try {
      return localStorage.getItem('auth.session_id')?.trim() || '0';
    } catch {
      return '0';
    }
  }

  _emit(event) {
    if (this.handler) {
      this.handler(event);
    }
  }

  _getSelfIdentities() {
    try {
      return new Set([
        localStorage.getItem('auth.session_id')?.trim() || '',
        localStorage.getItem('auth.user_id')?.trim() || '',
        localStorage.getItem('auth.phone')?.trim() || '',
      ].filter(Boolean));
    } catch {
      return new Set([this._getSessionId()].filter(Boolean));
    }
  }

  _extractConversationId(evt) {
    return evt.channel || evt.room_id || evt.conversation_id || evt.conversationId || '';
  }

  _extractMessageText(evt) {
    return String(
      evt.message?.content ||
      evt.message?.text ||
      evt.message?.message ||
      evt.message ||
      evt.text ||
      evt.content ||
      '',
    ).trim();
  }

  _extractAuthorId(evt) {
    return String(
      evt.user_id ||
      evt.userId ||
      evt.author_id ||
      evt.authorId ||
      evt.from ||
      evt.sender ||
      '',
    ).trim();
  }

  _extractAuthorName(evt) {
    return String(
      evt.author_name ||
      evt.authorName ||
      evt.sender_name ||
      evt.senderName ||
      evt.from_name ||
      evt.fromName ||
      evt.from ||
      '未知用户',
    ).trim();
  }

  _isOwnMessageEvent(evt) {
    const authorId = this._extractAuthorId(evt);
    if (!authorId) return false;
    return this._getSelfIdentities().has(authorId);
  }

  // 对齐 Vue 版 isServerAgentPayload / isServerAiSenderPayload：
  // 识别服务端广播的 agent 消息（session_id=0 或 from 包含炎图/agent）
  _isAgentBroadcastEvent(evt) {
    const sessionId = String(evt.session_id ?? evt.sessionId ?? '').trim();
    if (sessionId === '0') return true;
    const from = String(evt.from || evt.sender_name || '').toLowerCase();
    if (from.includes('agent') || from.includes('炎图')) return true;
    const senderType = String(evt.sender_type ?? evt.senderType ?? '').trim().toLowerCase();
    if (senderType === 'ai' || senderType === 'agent') return true;
    return false;
  }

  async connect() {
    this._emit({ type: 'transport_status', status: 'connecting' });
    await this._tryConnect();
  }

  /**
   * 尝试连接桥接服务：成功则 emit connected 并启动轮询；
   * 失败则 emit reconnecting 并按指数退避重试（对齐移动端 TransportStatus 行为）。
   */
  async _tryConnect() {
    try {
      // 健康检查桥接服务
      await ChatApi.healthCheck();
      this.connected = true;
      this.reconnecting = false;
      this.reconnectAttempts = 0;
      this._clearReconnectTimer();
      this._emit({ type: 'transport_status', status: 'connected' });
      // 启动长轮询（接收消息 + 检测新频道邀请）
      this._startPolling();
    } catch (e) {
      this.connected = false;
      this._emit({
        type: 'transport_status',
        status: 'reconnecting',
        errorMessage: `Chat bridge unreachable: ${e.message}`,
      });
      // 桥接服务不可用，按指数退避自动重连
      console.warn('[HTTP IRC] Bridge unavailable, scheduling reconnect');
      this._scheduleReconnect();
    }
  }

  _scheduleReconnect() {
    this._clearReconnectTimer();
    this.reconnecting = true;
    this.reconnectAttempts += 1;
    const delay = Math.min(
      this.options.reconnectBaseDelayMs * Math.pow(2, this.reconnectAttempts - 1),
      this.options.reconnectMaxDelayMs,
    );
    this.reconnectTimer = setTimeout(() => {
      this.reconnecting = false;
      this._tryConnect();
    }, delay);
  }

  _clearReconnectTimer() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  disconnect() {
    // 主动断开：阻止自动重连
    this.reconnecting = true;
    this._clearReconnectTimer();
    this.connected = false;
    this.joinedChannels.clear();
    this._stopPolling();
    this._emit({ type: 'transport_status', status: 'disconnected' });
  }

  joinConversation(conversationId) {
    if (!this.connected) return;
    const channel = conversationId.trim();
    if (!channel) return;
    const normalizedChannel = channel.startsWith('#') ? channel : `#${channel}`;

    if (this.joinedChannels.has(normalizedChannel)) return;
    // 对齐 Vue 版 NativeTcpChatTransportClient.joinConversationUnlocked：
    // 先标记防止并发重复 JOIN，JOIN 失败时清除标记以允许重试。
    this.joinedChannels.add(normalizedChannel);

    const sessionId = this._getSessionId();
    ChatApi.joinChannel(normalizedChannel, sessionId)
      .then(() => {
        console.log('[HTTP IRC] Joined channel:', normalizedChannel);
      })
      .catch((e) => {
        // JOIN 失败：清除标记，下次 joinConversation 调用会重试
        this.joinedChannels.delete(normalizedChannel);
        console.warn('[HTTP IRC] Join channel failed (will retry):', normalizedChannel, e);
      });
  }

  sendMessage(payload) {
    if (!this.connected) return;

    const sessionId = this._getSessionId();
    const channel = payload.conversationId.startsWith('#')
      ? payload.conversationId
      : `#${payload.conversationId}`;

    ChatApi.sendPrivmsg(channel, payload.text, sessionId)
      .then((resp) => {
        // 发送成功后，触发 send_ack
        this._emit({
          type: 'send_ack',
          conversationId: payload.conversationId,
          status: resp?.ok ? 'sent' : 'sent',
        });
      })
      .catch((e) => {
        console.error('[HTTP IRC] Send failed:', e);
        this._emit({
          type: 'transport_error',
          conversationId: payload.conversationId,
          message: e.message,
        });
      });
  }

  onEvent(handler) {
    this.handler = handler;
    return () => { this.handler = null; };
  }

  _startPolling() {
    this._stopPolling();
    this.polling = true;
    this._pollLoop();
  }

  _stopPolling() {
    this.polling = false;
    if (this.pollTimer) {
      clearTimeout(this.pollTimer);
      this.pollTimer = null;
    }
  }

  async _pollLoop() {
    if (!this.polling || !this.connected) return;

    try {
      await this._pollMessages();
    } catch (e) {
      console.warn('[HTTP IRC] Poll error:', e.message);
    }

    // 继续下一轮轮询
    if (this.polling && this.connected) {
      this.pollTimer = setTimeout(() => this._pollLoop(), this.options.pollIntervalMs);
    }
  }

  async _pollMessages() {
    if (this.joinedChannels.size === 0) return;

    const sessionId = this._getSessionId();
    const channels = [...this.joinedChannels];

    try {
      const resp = await ChatApi.pollMessages(
        sessionId,
        channels,
        this.lastPollTs,
        this.options.pollTimeoutMs
      );

      if (resp && resp.events && Array.isArray(resp.events)) {
        for (const evt of resp.events) {
          this._handleServerEvent(evt);
        }
      }

      this.lastPollTs = Date.now();
    } catch (e) {
      // 轮询超时是正常的（长轮询等待消息），不打印日志
      const msg = e?.message || '';
      const isTimeout = msg.includes('timeout') || msg.includes('超时') || msg.includes('abort') || e?.name === 'RequestTimeoutError' || e?.name === 'AbortError';
      if (!isTimeout) {
        console.warn('[HTTP IRC] Poll failed:', e);
      }
    }

    // 定期检查是否有新房间（每 10 秒检查一次）
    if (Date.now() - (this._lastRoomCheck || 0) > 10000) {
      await this._checkNewRooms(sessionId);
      this._lastRoomCheck = Date.now();
    }
  }

  // ── 检查新房间（用户被邀请加入的群聊） ─
  async _checkNewRooms() {
    try {
      const resp = await ChatApi.listConversations();
      const conversations = resp?.conversations || resp || [];

      if (Array.isArray(conversations)) {
        for (const conv of conversations) {
          const convId = conv.id || conv.channel || conv.room_id;
          if (!convId) continue;

          const normalizedId = convId.startsWith('#') ? convId : `#${convId}`;

          // 如果用户还没有加入这个频道，触发 conversation_signal
          if (!this.joinedChannels.has(normalizedId)) {
            console.log('[HTTP IRC] New room detected:', normalizedId);
            this._emit({
              type: 'conversation_signal',
              conversationId: normalizedId,
              reason: 'user_invited_to_room',
            });
          }
        }
      }
    } catch (e) {
      // 忽略检查错误
    }
  }

  _handleServerEvent(evt) {
    if (!evt || !evt.event) return;

    switch (evt.event) {
      case 'IRC_MESSAGE':
        this._emit({
          type: 'message_received',
          conversationId: evt.channel,
          messageId: evt.message?.id,
          author: evt.message?.authorId === this._getSessionId() ? 'user' : 'other_user',
          authorId: evt.message?.authorId,
          authorName: evt.message?.authorName || '未知用户',
          text: evt.message?.content || '',
          createdAt: evt.message?.createdAt || Date.now(),
          attachments: evt.message?.attachments,
        });
        break;

      case 'MESSAGE':
      case 'PRIVMSG': {
        const text = this._extractMessageText(evt);
        if (!text) return;
        const conversationId = this._extractConversationId(evt);

        // 对齐 Vue 版 nativeTcpChatTransportClient.sendAgentTypingUnlocked + onFrame：
        // b 端会通过 AGENT_BROADCAST 广播 [::AGENT_TYPING::]{"s":"started",...} 信封作为
        // 打字指示器的 fallback 通道，服务端将其转成 PRIVMSG 推送。接收方必须拦截该 marker，
        // 解析后 emit agent_typing 事件，绝不能当成普通文本消息显示（否则显示为乱码）。
        if (text.startsWith(_AGENT_TYPING_MARKER)) {
          try {
            const jsonStr = text.slice(_AGENT_TYPING_MARKER.length);
            const parsed = JSON.parse(jsonStr);
            this._emit({
              type: 'agent_typing',
              conversationId,
              agentRequestId: parsed.r || undefined,
              isTyping: parsed.s === 'started',
              ttlMs: typeof parsed.t === 'number' ? parsed.t : undefined,
            });
          } catch {
            // 解析失败时静默丢弃，不作为消息显示
          }
          return;
        }

        const baseEvent = {
          conversationId,
          messageId: evt.message_id || evt.messageId || evt.seq,
          authorId: this._extractAuthorId(evt),
          authorName: this._extractAuthorName(evt),
          text,
          createdAt: evt.created_at || evt.createdAt || Date.now(),
          attachments: evt.attachments || evt.message?.attachments,
        };
        this._emit(
          this._isOwnMessageEvent(evt)
            ? {
                type: 'message_echo',
                ...baseEvent,
              }
            : {
                type: 'message_received',
                author: evt.author || (this._isAgentBroadcastEvent(evt) ? 'agent' : 'other_user'),
                ...baseEvent,
              },
        );
        break;
      }

      case 'SEND_ACK':
        this._emit({
          type: 'send_ack',
          conversationId: evt.channel,
          seq: evt.seq,
          status: evt.status || 'sent',
          ackEvent: evt.ack_event,
          bucketKey: evt.bucket_key,
        });
        break;

      case 'AGENT_TYPING':
        this._emit({
          type: 'agent_typing',
          conversationId: evt.channel || evt.conversation_id,
          agentRequestId: evt.agent_request_id,
          isTyping: evt.is_typing !== false,
          ttlMs: evt.ttl_ms,
        });
        break;

      case 'AGENT_BROADCAST':
        this._emit({
          type: 'message_received',
          conversationId: evt.channel || evt.conversation_id,
          messageId: evt.message?.id,
          author: 'agent',
          authorName: evt.message?.authorName || '炎图AI助手',
          text: evt.message?.content || '',
          createdAt: evt.message?.createdAt || Date.now(),
          status: evt.message?.status || 'complete',
          attachments: evt.attachments || evt.message?.attachments,
        });
        break;

      // ── 群聊邀请通知：对齐 Vue 版 mapNativeTcpGroupInvitedToEvent ──
      case 'GROUP_INVITED': {
        const rawRoomId = String(evt.room_id || evt.roomId || evt.channel || '').trim();
        if (!rawRoomId) break;
        const conversationId = rawRoomId.startsWith('#') ? rawRoomId : `#${rawRoomId}`;
        const roomName = String(evt.room_name || evt.roomName || '').trim() || undefined;
        const inviter = String(evt.inviter || '').trim() || undefined;
        const inviterPhone = String(evt.inviter_phone || evt.inviterPhone || '').trim() || undefined;
        // 解析 ISO 8601 时间戳
        let createdAt;
        const tsRaw = String(evt.ts || '').trim();
        if (tsRaw) {
          const parsed = Date.parse(tsRaw);
          if (Number.isFinite(parsed)) createdAt = parsed;
        }
        console.log('[HTTP IRC] GROUP_INVITED:', conversationId, roomName, inviterPhone);
        this._emit({
          type: 'group_invited',
          conversationId,
          roomName,
          inviter,
          inviterPhone,
          createdAt: createdAt || Date.now(),
        });
        break;
      }

      // ── 新会话信号：用户被邀请/加入群聊 ──
      case 'JOIN':
      case 'INVITE':
      case 'ROOM_JOINED':
      case 'CHANNEL_JOINED':
        this._emit({
          type: 'conversation_signal',
          conversationId: evt.channel || evt.room_id || evt.conversation_id || evt.conversationId,
          reason: evt.event || 'user_joined_room',
          userId: evt.user_id || evt.userId,
        });
        break;

      case 'ERROR':
        this._emit({
          type: 'transport_error',
          conversationId: evt.channel,
          code: evt.code,
          message: evt.message || 'Server error',
        });
        break;

      default:
        console.log('[HTTP IRC] Unknown event:', evt.event, evt);
    }
  }
}

export default HttpIrcChatTransport;
