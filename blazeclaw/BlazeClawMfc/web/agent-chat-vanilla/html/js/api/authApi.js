/* ================================================================
   AgentChat HTML 版 - 认证 API
   对应原版 authService.ts 的 Web 模式实现

   核心逻辑：
   - Web 调试模式（webSmsLoginEnabled=true）：跳过真实 SMS，输入固定验证码即可登录
   - 非调试模式：调用真实后端 API（https://host:port/api/auth/*）
   ================================================================ */

import AppConfig from '../config.js';

const AuthApi = (() => {
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

  async function _fetchAuth(path, options = {}) {
    const baseUrl = await _getAuthUrl();
    const candidates = [];
    if (_supportsSameOriginProxy() && typeof window !== 'undefined' && window.location?.origin && window.location.origin.startsWith('http')) {
      candidates.push(`${window.location.origin}${path}`);
    }
    if (baseUrl) {
      candidates.push(`${String(baseUrl).replace(/\/$/, '')}${path}`);
    }

    let lastError = null;
    for (const url of [...new Set(candidates)]) {
      try {
        const resp = await fetch(url, options);
        if (resp.status === 404 || resp.status === 405 || resp.status === 502) {
          lastError = new Error(`HTTP ${resp.status}`);
          continue;
        }
        return resp;
      } catch (error) {
        lastError = error;
      }
    }
    throw lastError || new Error('认证服务不可用');
  }

  /**
   * 获取认证服务器地址
   * HTML 版通过本地桥接服务代理请求，避免浏览器 SSL 证书问题
   */
  async function _getAuthUrl() {
    return AppConfig.getChatConfig().httpBaseUrl;
  }

  /**
   * 解析登录响应
   * 对应原版 parseLoginFromRaw
   */
  function _parseLoginFromRaw(raw) {
    let directJson = null;
    try { directJson = JSON.parse(raw); } catch { directJson = null; }

    let payload = directJson;
    if (directJson && typeof directJson === 'object' && 'payload' in directJson) {
      payload = directJson.payload;
      if (typeof payload === 'string') {
        try { payload = JSON.parse(payload); } catch { /* keep as string */ }
      }
    }

    // 服务端返回可能是数组 [{...}]，取第一个元素
    const normalizedPayload = typeof payload === 'string'
      ? (function() { try { return JSON.parse(payload); } catch { return payload; } })()
      : payload;

    const container = (Array.isArray(normalizedPayload) && normalizedPayload.length > 0
      ? normalizedPayload[0]
      : normalizedPayload);

    if (typeof container === 'object' && container !== null) {
      // Server error response: {"error":"..."} — throw immediately
      if (container.error && typeof container.error === 'string') {
        throw new Error(container.error);
      }

      const jwt = (container.access_token ?? container.jwt ?? '').toString().trim();
      const apiKeyRaw = container.API_KEY ?? container.api_key;
      const apiKey = typeof apiKeyRaw === 'string' && apiKeyRaw.trim() ? apiKeyRaw.trim() : null;
      const userId = (container.user_id ?? container.userId ?? container.uid ?? container.uuid ?? '').toString().trim();

      // 如果响应里没有 userId，从 JWT 的 sub 字段解码
      const resolvedUserId = userId || _decodeJwtSub(jwt);

      if (jwt) {
        return { jwt, apiKey, userId: resolvedUserId };
      }
    }

    // 如果不是 JSON 或没有 jwt，当作纯文本 token
    return { jwt: raw.trim(), apiKey: null, userId: '' };
  }

  /**
   * 从 JWT 中解码 sub 字段作为 userId
   */
  function _decodeJwtSub(jwt) {
    try {
      const parts = jwt.split('.');
      if (parts.length !== 3) return '';
      const payload = JSON.parse(atob(parts[1]));
      return typeof payload === 'object' && payload !== null
        ? String(payload.sub ?? '').trim()
        : '';
    } catch {
      return '';
    }
  }

  /**
   * 检查 SMS 发送是否成功
   * 对应原版 ensureSmsSendAccepted
   */
  function _ensureSmsSendAccepted(raw) {
    const normalized = raw.trim();
    if (!normalized) throw new Error('sms send failed (empty response)');
    if (normalized.toLowerCase() === 'ok') return;

    let parsed = null;
    try { parsed = JSON.parse(normalized); } catch {}

    if (parsed && typeof parsed === 'object') {
      // Bridge wraps response as {raw: "ok"}, check raw field
      const rawField = String(parsed.raw ?? '').trim();
      if (rawField.toLowerCase() === 'ok') return;

      const status = String(parsed.status ?? parsed.code ?? '').trim();
      if (status && /^(OK|SUCCESS|SENT|ACCEPTED)$/i.test(status)) return;
    }

    throw new Error(`验证码发送失败：${normalized.slice(0, 200)}`);
  }

  /**
   * 发送短信验证码
   * 对应原版 sendSmsCode
   */
  async function sendSmsCode(phone) {
    const phoneTrimmed = phone.trim();
    if (!/^1\d{10}$/.test(phoneTrimmed)) {
      throw new Error('请输入正确的手机号');
    }

    // Web 调试模式：跳过真实 SMS 发送
    if (AppConfig.isWebSmsLoginEnabled()) {
      return;
    }

    // 真实 API 调用
    const sessionId = localStorage.getItem('auth.session_id') || '0';

    const resp = await _fetchAuth('/api/auth/send-sms', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'sessionid': sessionId,
      },
      body: JSON.stringify({ phone: phoneTrimmed }),
    });

    if (!resp.ok) {
      let errBody = '';
      try { errBody = await resp.text(); } catch {}
      throw new Error(`HTTP ${resp.status}: ${errBody.slice(0, 200)}`);
    }

    const raw = await resp.text();
    _ensureSmsSendAccepted(raw);
  }

  /**
   * 验证码登录
   * 对应原版 loginWithSms
   */
  async function loginWithSms(phone, code) {
    const phoneTrimmed = phone.trim();
    const codeTrimmed = code.trim();

    if (!/^1\d{10}$/.test(phoneTrimmed)) {
      throw new Error('请输入正确的手机号');
    }
    if (!/^\d{4,6}$/.test(codeTrimmed)) {
      throw new Error('请输入 4 到 6 位验证码');
    }

    // Web 调试模式：输入固定验证码即可登录
    if (AppConfig.isWebSmsLoginEnabled()) {
      const debugCode = AppConfig.getDebugSmsCode();
      if (codeTrimmed !== debugCode) {
        throw new Error(`Web 调试环境验证码固定为 ${debugCode}`);
      }

      const jwt = `debug-web-jwt-${Date.now()}`;
      const debugSessionId = AppConfig.getDebugSessionId();

      return {
        jwt,
        apiKey: null,
        userId: phoneTrimmed,
        phone: phoneTrimmed,
        sessionId: debugSessionId || null,
      };
    }

    // 真实 API 调用
    const sessionId = localStorage.getItem('auth.session_id') || '0';

    const resp = await _fetchAuth('/api/auth/login', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'sessionid': sessionId,
      },
      body: JSON.stringify({ phone: phoneTrimmed, code: codeTrimmed }),
    });

    if (!resp.ok) {
      let errBody = '';
      try { errBody = await resp.text(); } catch {}
      throw new Error(`HTTP ${resp.status}: ${errBody.slice(0, 200)}`);
    }

    const raw = await resp.text();
    const parsed = _parseLoginFromRaw(raw);

    if (!parsed.jwt) {
      throw new Error('登录响应缺少 access token');
    }

    // 尝试从响应中获取 sessionId
    let responseSessionId = null;
    try {
      const jsonResp = JSON.parse(raw);
      responseSessionId = jsonResp.sessionId || jsonResp.session_id || null;
    } catch {}

    return {
      jwt: parsed.jwt,
      apiKey: parsed.apiKey,
      userId: parsed.userId,
      phone: phoneTrimmed,
      sessionId: responseSessionId,
    };
  }

  /**
   * 健康检查
   */
  async function healthCheck() {
    const resp = await _fetchAuth('/api/auth/health', {
      method: 'GET',
      headers: { 'sessionid': localStorage.getItem('auth.session_id') || '0' },
    });
    if (!resp.ok) throw new Error(`健康检查失败: HTTP ${resp.status}`);
    return resp.json();
  }

  return {
    sendSmsCode,
    loginWithSms,
    healthCheck,
  };
})();

export default AuthApi;
