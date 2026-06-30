/* ================================================================
   AgentChat HTML 版 - 全局配置
   对应原版 .env 和 getAuthConfig() / getChatConfig()
   ================================================================ */

const AppConfig = (() => {
  // ── 服务器配置 ──
  const defaults = {
    authHost: '139.224.189.70',
    authPort: 9443,
    chatHttpBase: 'http://localhost:8787',
    blazeclawBridgeUrl: 'http://localhost:8788',
    openclawBridgeUrl: 'http://localhost:8788',
    webSmsLoginEnabled: false,
    debugSmsCode: '123456',
    debugSessionId: '',
    forceMockChat: false,
    agentRuntimeMode: 'legacy',
    enableNativeAgentBridge: false,
    agentBridgeTransport: 'http',
    enableHttpFallbackOnNativeBridgeError: true,
    nativeBridgeHostStarted: false,
    nativeRunnerStarted: false,
    nativeModeDegraded: false,
    agentBridgeReachabilityHint: 'unknown',
  };

  function _env(key, fallback) {
    // HTML 版从 <meta> 标签或 window.__APP_CONFIG__ 读取配置
    if (window.__APP_CONFIG__ && window.__APP_CONFIG__[key] !== undefined) {
      return window.__APP_CONFIG__[key];
    }
    const meta = document.querySelector(`meta[name="app-${key.replace(/_/g, '-').toLowerCase()}"]`);
    if (meta) return meta.content;
    return fallback;
  }

  function getAuthConfig() {
    return {
      host: _env('authHost', defaults.authHost),
      port: Number(_env('authPort', defaults.authPort)),
      webSmsLoginEnabled: _env('webSmsLoginEnabled', defaults.webSmsLoginEnabled),
      debugSmsCode: _env('debugSmsCode', defaults.debugSmsCode),
      debugSessionId: _env('debugSessionId', defaults.debugSessionId),
    };
  }

  function getChatConfig() {
    const authCfg = getAuthConfig();
    const host = authCfg.host || defaults.authHost;
    const port = authCfg.port || defaults.authPort;
    return {
      httpBaseUrl: _env('chatHttpBase', defaults.chatHttpBase),
      blazeclawBridgeUrl: _env('blazeclawBridgeUrl', defaults.blazeclawBridgeUrl),
      openclawBridgeUrl: _env('openclawBridgeUrl', defaults.openclawBridgeUrl),
      wsUrl: _env('chatWsUrl', ''), // 默认为空，使用 HTTP IRC 模式
      forceMockChat: _env('forceMockChat', defaults.forceMockChat),
      agentRuntimeMode: _env('agentRuntimeMode', defaults.agentRuntimeMode),
      enableNativeAgentBridge: _env('enableNativeAgentBridge', defaults.enableNativeAgentBridge),
      agentBridgeTransport: _env('agentBridgeTransport', defaults.agentBridgeTransport),
      enableHttpFallbackOnNativeBridgeError: _env(
        'enableHttpFallbackOnNativeBridgeError',
        defaults.enableHttpFallbackOnNativeBridgeError,
      ),
      nativeBridgeHostStarted: _env('nativeBridgeHostStarted', defaults.nativeBridgeHostStarted),
      nativeRunnerStarted: _env('nativeRunnerStarted', defaults.nativeRunnerStarted),
      nativeModeDegraded: _env('nativeModeDegraded', defaults.nativeModeDegraded),
      agentBridgeReachabilityHint: _env('agentBridgeReachabilityHint', defaults.agentBridgeReachabilityHint),
    };
  }

  // ── 便捷方法 ──
  function isWebSmsLoginEnabled() {
    return _env('webSmsLoginEnabled', defaults.webSmsLoginEnabled);
  }

  function getDebugSmsCode() {
    return _env('debugSmsCode', defaults.debugSmsCode);
  }

  function getDebugSessionId() {
    return _env('debugSessionId', defaults.debugSessionId);
  }

  /**
   * 更新服务器配置（持久化到 localStorage）
   */
  async function setServerConfig(host, port) {
    localStorage.setItem('auth.host', host.trim());
    localStorage.setItem('auth.port', String(port));
  }

  async function getServerConfig() {
    const host = (localStorage.getItem('auth.host') || '').trim() || defaults.authHost;
    const port = Number(localStorage.getItem('auth.port')) || defaults.authPort;
    return { host, port };
  }

  return {
    defaults,
    getAuthConfig,
    getChatConfig,
    isWebSmsLoginEnabled,
    getDebugSmsCode,
    getDebugSessionId,
    setServerConfig,
    getServerConfig,
  };
})();

export default AppConfig;
