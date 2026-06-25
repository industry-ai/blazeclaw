/* ================================================================
   AgentChat HTML 版 - Transport 工厂
   对应原版 chatTransportDefaults.ts
   根据配置选择 WebSocket / HTTP IRC / Mock Transport
   ================================================================ */

import MockChatTransport from './mockTransport.js';
import WebSocketChatTransport from './webSocketTransport.js';
import HttpIrcChatTransport from './httpIrcTransport.js';
import AppConfig from '../config.js';
import AppHttp from '../api/httpClient.js';

/**
 * 创建默认 Transport 实例
 * 根据配置决定使用哪种 Transport：
 * - forceMockChat = true → MockTransport（开发调试）
 * - 有 wsUrl → WebSocketTransport
 * - 默认 → HttpIrcTransport（通过桥接服务）
 */
function createDefaultTransport() {
  const chatCfg = AppConfig.getChatConfig();

  // 强制 Mock 模式（开发调试）
  if (chatCfg.forceMockChat) {
    console.log('[Transport] Using MockChatTransport (force mock)');
    return new MockChatTransport();
  }

  // WebSocket 模式
  const wsUrl = chatCfg.wsUrl;
  if (wsUrl) {
    console.log('[Transport] Using WebSocketChatTransport:', wsUrl);
    return new WebSocketChatTransport({ url: wsUrl });
  }

  // HTTP IRC 模式（默认，通过桥接服务）
  const bridgeUrl = chatCfg.httpBaseUrl || 'http://localhost:8787';
  console.log('[Transport] Using HttpIrcChatTransport, bridge:', bridgeUrl);

  // 设置 HTTP 客户端基础地址
  AppHttp.setBaseUrl(bridgeUrl);

  return new HttpIrcChatTransport({
    pollIntervalMs: 3000,
    pollTimeoutMs: 25000,
  });
}

export { createDefaultTransport, MockChatTransport, WebSocketChatTransport, HttpIrcChatTransport };
export default createDefaultTransport;
