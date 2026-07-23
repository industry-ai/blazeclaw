/* ================================================================
   全局配置
   ----------------------------------------------------------------
   通信方式：通过 chrome.webview.postMessage 与 C++ 原生宿主通信
   ================================================================ */

const AppConfig = (() => {
  const defaults = {
    // 当前默认 AI 模型（仅用于页面展示）
    currentAi: 'openclaw',
    // 调试相关
    webSmsLoginEnabled: false,
    debugSmsCode: '123456',
    debugSessionId: '',
    // 语音转文字（DashScope ASR）
    dashscopeApiKey: 'sk-072289a981b34d9e97e606ea2a3ebe78',
    dashscopeAsrModel: 'qwen3-asr-flash',
  };

  function _env(key, fallback) {
    if (window.__APP_CONFIG__ && window.__APP_CONFIG__[key] !== undefined) {
      return window.__APP_CONFIG__[key];
    }
    const meta = document.querySelector(`meta[name="app-${key.replace(/_/g, '-').toLowerCase()}"]`);
    if (meta) return meta.content;
    return fallback;
  }

  function getCurrentAi() {
    return String(_env('currentAi', defaults.currentAi)).trim().toLowerCase();
  }

  function isWebSmsLoginEnabled() {
    return _env('webSmsLoginEnabled', defaults.webSmsLoginEnabled);
  }

  function getDebugSmsCode() {
    return _env('debugSmsCode', defaults.debugSmsCode);
  }

  function getDebugSessionId() {
    return _env('debugSessionId', defaults.debugSessionId);
  }

  function getDashscopeApiKey() {
    return String(_env('dashscopeApiKey', defaults.dashscopeApiKey)).trim();
  }

  function getDashscopeAsrModel() {
    return String(_env('dashscopeAsrModel', defaults.dashscopeAsrModel)).trim() || defaults.dashscopeAsrModel;
  }

  return { defaults, getCurrentAi, isWebSmsLoginEnabled, getDebugSmsCode, getDebugSessionId, getDashscopeApiKey, getDashscopeAsrModel };
})();

export default AppConfig;
