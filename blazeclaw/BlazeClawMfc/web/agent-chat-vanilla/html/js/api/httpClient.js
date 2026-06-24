/* ================================================================
   AgentChat HTML 版 - HTTP 客户端
   封装 fetch：统一超时、会话头、JSON 解析
   ================================================================ */

import AppConfig from '../config.js';

const AppHttp = (() => {
  function _supportsSameOriginProxy() {
    try {
      const { protocol, hostname, port } = window.location;
      if (!/^https?:$/i.test(protocol)) return false;
      if (!/^(localhost|127\.0\.0\.1)$/i.test(hostname)) return false;
      return port === '3000';
    } catch {
      return false;
    }
  }

  function _defaultBaseUrl() {
    if (_supportsSameOriginProxy()) return '/api/chat';
    return AppConfig.getChatConfig().httpBaseUrl || 'http://localhost:8787';
  }

  let API_BASE = _defaultBaseUrl();

  /**
   * 设置 API 基础地址（用于指向桥接服务）
   */
  function setBaseUrl(url) {
    API_BASE = url;
  }

  function readSessionId() {
    try { return localStorage.getItem('auth.session_id')?.trim() || ''; }
    catch { return ''; }
  }

  function buildHeaders(extra = {}) {
    return {
      'Content-Type': 'application/json',
      'sessionid': readSessionId() || '0',
      ...extra,
    };
  }

  async function request(method, path, body, opts = {}) {
    const { timeoutMs = 10000, headers: extraHeaders = {}, signal: extSignal } = opts;

    const controller = new AbortController();
    let didTimeout = false;
    const timer = timeoutMs > 0 ? setTimeout(() => {
      didTimeout = true;
      controller.abort();
    }, timeoutMs) : null;

    if (extSignal) {
      if (extSignal.aborted) { controller.abort(); }
      else { extSignal.addEventListener('abort', () => controller.abort(), { once: true }); }
    }

    try {
      const fetchOpts = {
        method,
        headers: buildHeaders(extraHeaders),
        signal: controller.signal,
      };
      if (body && method !== 'GET') {
        fetchOpts.body = JSON.stringify(body);
      }

      const url = path.startsWith('http') ? path : `${API_BASE}${path}`;
      const resp = await fetch(url, fetchOpts);

      if (!resp.ok) {
        let errBody = '';
        try { errBody = await resp.text(); } catch {}
        const err = new Error(`HTTP ${resp.status}: ${resp.statusText}`);
        err.status = resp.status;
        err.body = errBody.slice(0, 500);
        throw err;
      }

      const text = await resp.text();
      if (!text) return undefined;
      try { return JSON.parse(text); }
      catch { return text; }
    } catch (error) {
      if (error?.name === 'AbortError') {
        if (didTimeout) {
          const err = new Error(`请求超时（${timeoutMs / 1000}s）：${method} ${path} — 请确认桥接服务 ${API_BASE} 是否正常运行`);
          err.name = 'RequestTimeoutError';
          throw err;
        }
        throw error;
      }
      // 网络不通时给出明确提示
      if (error?.name === 'TypeError' && error?.message?.includes('fetch')) {
        const err = new Error(`无法连接桥接服务 ${API_BASE}，请确认已启动 chat-bridge`);
        err.name = 'BridgeUnreachableError';
        throw err;
      }
      throw error;
    } finally {
      if (timer) clearTimeout(timer);
    }
  }

  return {
    get(path, opts) { return request('GET', path, null, opts); },
    post(path, body, opts) { return request('POST', path, body, opts); },
    put(path, body, opts) { return request('PUT', path, body, opts); },
    delete(path, opts) { return request('DELETE', path, null, opts); },

    /** SSE 流式请求 */
    async postSSE(path, body, callbacks, opts = {}) {
      const { timeoutMs = 0, headers: extraHeaders = {} } = opts;
      const controller = new AbortController();
      const timer = timeoutMs > 0 ? setTimeout(() => controller.abort(), timeoutMs) : null;

      try {
        const url = path.startsWith('http') ? path : `${API_BASE}${path}`;
        const resp = await fetch(url, {
          method: 'POST',
          headers: buildHeaders({ ...extraHeaders, 'Accept': 'text/event-stream' }),
          body: JSON.stringify(body),
          signal: controller.signal,
        });

        if (!resp.ok) {
          const err = new Error(`SSE HTTP ${resp.status}`);
          err.status = resp.status;
          throw err;
        }

        const reader = resp.body.getReader();
        const decoder = new TextDecoder();
        let buffer = '';

        while (true) {
          const { done, value } = await reader.read();
          if (done) break;

          buffer += decoder.decode(value, { stream: true });
          const lines = buffer.split('\n');
          buffer = lines.pop() || '';

          for (const line of lines) {
            if (line.startsWith('data: ')) {
              const data = line.slice(6).trim();
              if (data === '[DONE]') { callbacks.onDone?.(); return; }
              try {
                const json = JSON.parse(data);
                callbacks.onData?.(json);
              } catch { callbacks.onData?.(data); }
            }
          }
        }
        callbacks.onDone?.();
      } catch (err) {
        callbacks.onError?.(err);
      } finally {
        if (timer) clearTimeout(timer);
      }
    },

    getBaseUrl() { return API_BASE; },
    setBaseUrl,
  };
})();

export default AppHttp;
