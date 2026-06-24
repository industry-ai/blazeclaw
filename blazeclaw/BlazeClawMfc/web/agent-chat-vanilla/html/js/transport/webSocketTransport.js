/* ================================================================
   AgentChat HTML 版 - WebSocket 聊天 Transport
   对应原版 webSocketTransportClient.ts
   ================================================================ */

import AppConfig from '../config.js';

class WebSocketChatTransport {
  constructor(options = {}) {
    this.url = options.url || this._buildWsUrl();
    this.handler = null;
    this.ws = null;
    this.status = 'disconnected';
    this.reconnecting = false;
    this.reconnectTimer = null;
    this.heartbeatTimer = null;
    this.lastReceivedAt = 0;
    this.joinedChannels = new Set();
    this.seq = 0;

    this.options = {
      heartbeatIntervalMs: 8000,
      heartbeatTimeoutMs: 22000,
      reconnectBaseDelayMs: 700,
      reconnectMaxDelayMs: 10000,
      ...options,
    };
  }

  _buildWsUrl() {
    const cfg = AppConfig.getChatConfig();
    const authCfg = AppConfig.getAuthConfig();
    const host = authCfg.host || '139.224.189.70';
    const port = authCfg.port || 9443;
    return `wss://${host}:${port}/ws/chat`;
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

  _setStatus(status, errorMessage) {
    this.status = status;
    this._emit({ type: 'transport_status', status, errorMessage });
  }

  async connect() {
    this._setStatus('connecting');
    try {
      this.ws = new WebSocket(this.url);

      this.ws.onopen = () => {
        this.reconnecting = false;
        this._setStatus('connected');
        this._startHeartbeat();
        // 重新加入之前加入过的频道
        for (const channel of this.joinedChannels) {
          this._joinChannel(channel);
        }
      };

      this.ws.onmessage = (event) => {
        this.lastReceivedAt = Date.now();
        try {
          const data = JSON.parse(event.data);
          this._handleMessage(data);
        } catch (e) {
          console.warn('[WS] Failed to parse message:', e);
        }
      };

      this.ws.onclose = (event) => {
        this._stopHeartbeat();
        if (!this.reconnecting) {
          this._setStatus('disconnected');
          this._scheduleReconnect();
        }
      };

      this.ws.onerror = (event) => {
        console.error('[WS] Connection error:', event);
      };
    } catch (e) {
      this._setStatus('error', e.message);
      this._scheduleReconnect();
    }
  }

  disconnect() {
    this.reconnecting = true;
    this._stopHeartbeat();
    this._clearReconnectTimer();
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.joinedChannels.clear();
    this._setStatus('disconnected');
  }

  _handleMessage(data) {
    const type = data.type;

    switch (type) {
      case 'message_received':
        this._emit({
          type: 'message_received',
          conversationId: data.conversation_id || data.channel,
          messageId: data.message_id || data.id,
          author: data.author || 'other_user',
          authorId: data.author_id,
          authorName: data.author_name,
          text: data.text || data.message || '',
          createdAt: data.created_at || data.ts || Date.now(),
          attachments: data.attachments,
        });
        break;

      case 'message_echo':
        this._emit({
          type: 'message_echo',
          conversationId: data.conversation_id || data.channel,
          messageId: data.message_id || data.id,
          text: data.text || data.message || '',
          createdAt: data.created_at || data.ts || Date.now(),
        });
        break;

      case 'send_ack':
        this._emit({
          type: 'send_ack',
          conversationId: data.conversation_id || data.channel,
          status: data.status,
          ackEvent: data.ack_event,
          bucketKey: data.bucket_key,
        });
        break;

      case 'agent_typing':
        this._emit({
          type: 'agent_typing',
          conversationId: data.conversation_id || data.channel,
          agentRequestId: data.agent_request_id,
          isTyping: data.is_typing !== false,
          ttlMs: data.ttl_ms,
        });
        break;

      case 'conversation_signal':
        this._emit({
          type: 'conversation_signal',
          conversationId: data.conversation_id || data.channel,
          reason: data.reason,
          event: data.event,
        });
        break;

      case 'transport_error':
        this._emit({
          type: 'transport_error',
          conversationId: data.conversation_id,
          code: data.code,
          message: data.message || 'Unknown error',
        });
        break;

      default:
        console.log('[WS] Unknown message type:', type, data);
    }
  }

  sendMessage(payload) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.warn('[WS] Cannot send message: not connected');
      return;
    }

    const message = {
      type: 'send_message',
      conversation_id: payload.conversationId,
      text: payload.text,
      session_id: this._getSessionId(),
      seq: ++this.seq,
    };

    this.ws.send(JSON.stringify(message));
  }

  joinConversation(conversationId) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.warn('[WS] Cannot join conversation: not connected');
      return;
    }
    this._joinChannel(conversationId);
  }

  _joinChannel(conversationId) {
    const channel = conversationId.startsWith('#') ? conversationId : `#${conversationId}`;
    this.joinedChannels.add(channel);

    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify({
        type: 'join',
        channel,
        session_id: this._getSessionId(),
        seq: ++this.seq,
      }));
    }
  }

  broadcastAgentReply(payload) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;

    this.ws.send(JSON.stringify({
      type: 'agent_broadcast',
      conversation_id: payload.conversationId,
      message: payload.message,
      attachments: payload.attachments,
      session_id: this._getSessionId(),
      seq: ++this.seq,
    }));
  }

  broadcastAgentTyping(payload) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;

    this.ws.send(JSON.stringify({
      type: 'agent_typing',
      conversation_id: payload.conversationId,
      is_typing: payload.isTyping,
      agent_request_id: payload.agentRequestId,
      ttl_ms: payload.ttlMs,
      session_id: this._getSessionId(),
      seq: ++this.seq,
    }));
  }

  onEvent(handler) {
    this.handler = handler;
    return () => { this.handler = null; };
  }

  _startHeartbeat() {
    this._stopHeartbeat();
    this.heartbeatTimer = setInterval(() => {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        const now = Date.now();
        if (now - this.lastReceivedAt > this.options.heartbeatTimeoutMs) {
          console.warn('[WS] Heartbeat timeout, reconnecting...');
          this.ws.close();
          return;
        }
        this.ws.send(JSON.stringify({
          type: 'ping',
          ts: now,
          seq: ++this.seq,
        }));
      }
    }, this.options.heartbeatIntervalMs);
  }

  _stopHeartbeat() {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  _scheduleReconnect() {
    if (this.reconnecting) return;
    this.reconnecting = true;

    const delay = Math.min(
      this.options.reconnectBaseDelayMs * Math.pow(2, this._getReconnectAttempts()),
      this.options.reconnectMaxDelayMs
    );

    this.reconnectTimer = setTimeout(() => {
      this.reconnecting = false;
      this.connect();
    }, delay);
  }

  _getReconnectAttempts() {
    // Simple counter based on time since last disconnect
    return Math.floor((Date.now() - this.lastReceivedAt) / 10000);
  }

  _clearReconnectTimer() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }
}

export default WebSocketChatTransport;
