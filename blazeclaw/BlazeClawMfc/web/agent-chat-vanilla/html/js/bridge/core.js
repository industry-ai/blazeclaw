/* ================================================================
   postMessage 桥接核心（会话层）
   ----------------------------------------------------------------
   对齐 origin-vanilla/agentBridgeTransport.js 的 C++ 通信方式：
   - WebView -> C++  : channel "agentchat.bridge.request" (chrome.webview.postMessage)
   - C++ -> WebView  : CustomEvent "agentchat.bridge.message"（统一派发点）
     - detail.channel = "agentchat.bridge.response"  -> 响应
     - detail.channel = "agentchat.bridge.push"       -> 推送
     - detail.channel = "agentchat.bridge.stream.*"   -> Agent 流式（由其他模块处理）

   信封结构（对齐 agentBridgeTransport.js）：
   {
     channel: "agentchat.bridge.request" | "agentchat.bridge.response" | "agentchat.bridge.push",
     requestId?: string,
     kind?: string,
     payload?: any,
     ok?: boolean,
     error?: { code, message },
     ts: number
   }
   ================================================================ */

const CH_REQ = 'agentchat.bridge.request';
const CH_RES = 'agentchat.bridge.response';
const CH_PUSH = 'agentchat.bridge.push';
const RESPONSE_EVENT = 'agentchat.bridge.message';

let _requestCounter = 0;
const _pending = new Map(); // requestId -> { resolve, reject, timeout }
const _pushHandlers = new Set();

/**
 * 处理来自原生宿主的消息
 */
function _onMessage(msg) {
  if (!msg || typeof msg !== 'object') return;

  // 响应：匹配 pending request
  if (msg.channel === CH_RES && msg.requestId) {
    const p = _pending.get(msg.requestId);
    if (!p) return;
    clearTimeout(p.timeout);
    _pending.delete(msg.requestId);
    if (msg.error || msg.ok === false) {
      const err = new Error((msg.error && msg.error.message) || 'bridge error');
      err.code = msg.error && msg.error.code;
      p.reject(err);
    } else {
      p.resolve(msg.payload);
    }
    return;
  }

  // 推送：派发给所有订阅者
  if (msg.channel === CH_PUSH) {
    for (const h of _pushHandlers) {
      try { h(msg); } catch (e) { console.warn('[bridge] push handler error', e); }
    }
  }
}

// C++ 通过 agentchat.bridge.message CustomEvent 统一派发 response / push
window.addEventListener(RESPONSE_EVENT, (e) => _onMessage(e.detail));

/**
 * 发起一次 postMessage 请求并等待响应
 * @param {string} kind 消息类型，见 bridge/index.js 各 domain 方法定义
 * @param {object} payload 业务参数
 * @param {number} timeoutMs 超时（默认 30s）
 * @returns {Promise<any>} payload 响应
 */
function request(kind, payload = {}, timeoutMs = 30000) {
  return new Promise((resolve, reject) => {
    const requestId = `pm-${++_requestCounter}-${Date.now()}`;

    const timeout = setTimeout(() => {
      if (_pending.has(requestId)) {
        _pending.delete(requestId);
        reject(new Error(`postMessage 请求超时: ${kind}`));
      }
    }, timeoutMs);

    _pending.set(requestId, { resolve, reject, timeout });

    // postMessage 模式：发送给 C++ 原生宿主
    try {
      window.chrome.webview.postMessage({
        channel: CH_REQ,
        requestId,
        kind,
        payload,
      });
    } catch (e) {
      clearTimeout(timeout);
      _pending.delete(requestId);
      reject(e);
    }
  });
}

/**
 * 订阅原生宿主的主动推送事件
 * @param {(msg)=>void} handler
 * @returns {() => void} 取消订阅
 */
function onPush(handler) {
  _pushHandlers.add(handler);
  return () => _pushHandlers.delete(handler);
}

/**
 * 初始化
 */
function init() {
  console.log('[bridge] init');
}

export default {
  CH_REQ, CH_RES, CH_PUSH,
  init,
  request,
  onPush,
};
