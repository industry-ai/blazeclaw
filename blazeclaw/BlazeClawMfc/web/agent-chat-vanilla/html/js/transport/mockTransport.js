/* ================================================================
   AgentChat HTML 版 - Mock 聊天 Transport
   对应原版 mockTransportClient.ts
   用于开发调试，模拟真实的消息收发流程
   ================================================================ */

class MockChatTransport {
  constructor() {
    this.handler = null;
    this.connected = false;
    this.disconnecting = false;
    this.timeouts = [];
    this.agentMsgId = null;
    this.typingTimer = null;
  }

  _emit(event) {
    if (this.handler) {
      this.handler(event);
    }
  }

  async connect() {
    this.connected = true;
    this.disconnecting = false;
    this._emit({ type: 'transport_status', status: 'connected' });

    // 模拟偶尔断线重连
    const dropInMs = 25000 + Math.floor(Math.random() * 15000);
    const h = setTimeout(() => {
      if (this.disconnecting || !this.connected) return;
      this._emit({ type: 'transport_status', status: 'reconnecting' });
      this.connected = false;
      this._cleanup();
      void this._reconnect();
    }, dropInMs);
    this.timeouts.push(h);
  }

  async _reconnect() {
    this._cleanup();
    await this._wait(1200);
    if (this.disconnecting) return;
    this.connected = true;
    this._emit({ type: 'transport_status', status: 'connected' });
  }

  disconnect() {
    this.disconnecting = true;
    this.connected = false;
    this._cleanup();
    this._emit({ type: 'transport_status', status: 'disconnected' });
  }

  sendMessage(payload) {
    if (!this.connected) return;

    // 立即回显用户消息
    const userMsgId = this._mkId('u');
    this._emit({
      type: 'message_echo',
      conversationId: payload.conversationId,
      messageId: userMsgId,
      text: payload.text,
    });

    // 然后模拟 Agent 流式回复
    this._simulateAgentResponse(payload.conversationId, payload.text);
  }

  joinConversation(conversationId) {
    // Mock 模式下不需要实际加入
    console.log('[Mock] Joined conversation:', conversationId);
  }

  broadcastAgentReply(payload) {
    if (!this.connected) return;
    // 模拟广播 Agent 回复
    const agentMsgId = this._mkId('a');
    this._emit({
      type: 'message_received',
      conversationId: payload.conversationId,
      messageId: agentMsgId,
      author: 'agent',
      authorName: '炎图AI助手',
      text: payload.message,
      createdAt: Date.now(),
    });
  }

  broadcastAgentTyping(payload) {
    if (!this.connected) return;
    this._emit({
      type: 'agent_typing',
      conversationId: payload.conversationId,
      agentRequestId: payload.agentRequestId,
      isTyping: payload.isTyping,
      ttlMs: payload.ttlMs,
    });
  }

  onEvent(handler) {
    this.handler = handler;
    return () => { this.handler = null; };
  }

  _simulateAgentResponse(conversationId, userText) {
    const agentMsgId = this._mkId('a');
    const full = this._generateAiReply(userText);

    const speed = 22; // ms/char
    let idx = 0;

    const emitTick = () => {
      if (!this.connected) return;
      const next = full.slice(0, idx + 1);
      const delta = next.slice(idx);
      idx += delta.length;

      if (delta) {
        this._emit({
          type: 'message_received',
          conversationId,
          messageId: agentMsgId,
          author: 'agent',
          authorName: '炎图AI助手',
          text: next,
          createdAt: Date.now(),
        });

        const h = setTimeout(emitTick, speed);
        this.timeouts.push(h);
      } else {
        // 流式完成
        this._emit({
          type: 'message_received',
          conversationId,
          messageId: agentMsgId,
          author: 'agent',
          authorName: '炎图AI助手',
          text: full,
          status: 'complete',
          createdAt: Date.now(),
        });
      }
    };

    // 延迟一点开始回复，模拟思考时间
    const h = setTimeout(emitTick, 500 + Math.random() * 500);
    this.timeouts.push(h);
  }

  _generateAiReply(text) {
    const t = text.toLowerCase();

    if (t.includes('你好') || t.includes('hi') || t.includes('hello')) {
      return '你好！有什么我可以帮你的吗？';
    }
    if (t.includes('任务') || t.includes('待办')) {
      return '好的，让我帮你整理一下当前的任务清单...\n\n1. **数学作业** - 截止明天 18:00\n2. **家长会通知** - 周五下午 3 点\n3. **阅读打卡** - 每天 30 分钟\n\n需要我帮你设置提醒吗？';
    }
    if (t.includes('数学') || t.includes('计算')) {
      return '请告诉我具体的题目，我来帮你解答。\n\n例如：\n- 长方形周长计算\n- 分数加减法\n- 应用题解析\n\n随时告诉我你的问题！';
    }
    if (t.includes('天气')) {
      return '抱歉，我暂时无法获取实时天气信息。建议你查看天气预报应用或网站获取最新天气数据。';
    }
    if (t.includes('谢谢') || t.includes('感谢')) {
      return '不客气！如果还有其他问题，随时告诉我。';
    }

    return `收到你的消息："${text}"\n\n这是一个 Mock 回复。在实际部署中，这里会连接到 AI 服务进行智能回复。\n\n你可以尝试：\n- 问我数学问题\n- 让我帮你管理任务\n- 询问学习相关问题`;
  }

  _mkId(prefix) {
    return `${prefix}_${Math.random().toString(16).slice(2)}_${Date.now().toString(16)}`;
  }

  _wait(ms) {
    return new Promise((resolve) => {
      const h = setTimeout(() => resolve(), ms);
      this.timeouts.push(h);
    });
  }

  _cleanup() {
    for (const h of this.timeouts) {
      clearTimeout(h);
    }
    this.timeouts = [];
  }
}

export default MockChatTransport;
