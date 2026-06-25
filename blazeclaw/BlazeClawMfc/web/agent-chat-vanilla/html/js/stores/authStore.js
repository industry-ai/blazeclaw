/* ================================================================
   AgentChat HTML 版 - 认证状态管理
   对应原版 authService.ts + authStore.ts 的 Web 模式实现
   ================================================================ */

import AuthApi from '../api/authApi.js';
import { mapAuthErrorMessage } from '../utils/authErrorMessage.js';

const AuthStore = (() => {
  // ── 状态 ──
  let _isLoggedIn = false;
  let _phone = '';
  let _userId = '';
  let _loading = false;
  let _error = '';
  const listeners = new Set();

  const STORAGE_KEYS = {
    jwt: 'auth.jwt',
    sessionId: 'auth.session_id',
    apiKey: 'auth.api_key',
    userId: 'auth.user_id',
    phone: 'auth.phone',
    host: 'auth.host',
    port: 'auth.port',
  };

  function _notify() {
    for (const fn of listeners) fn(getState());
  }

  function getState() {
    return {
      isLoggedIn: _isLoggedIn,
      phone: _phone,
      userId: _userId,
      loading: _loading,
      error: _error,
    };
  }

  // ── Storage helpers ──
  async function _storageGet(key) {
    return localStorage.getItem(key) || '';
  }
  async function _storageSet(key, value) {
    localStorage.setItem(key, String(value));
  }
  async function _storageRemove(key) {
    localStorage.removeItem(key);
  }
  async function _getJwt() { return _storageGet(STORAGE_KEYS.jwt); }
  async function _getSessionId() { return _storageGet(STORAGE_KEYS.sessionId); }

  // ── 初始化 / Bootstrap ──
  async function init() {
    try {
      const storedJwt = await _getJwt();
      _isLoggedIn = Boolean(storedJwt.trim());
      if (_isLoggedIn) {
        _phone = (await _storageGet(STORAGE_KEYS.phone)).trim();
        _userId = (await _storageGet(STORAGE_KEYS.userId)).trim();
      } else {
        _phone = '';
        _userId = '';
      }
    } catch {
      _isLoggedIn = false;
      _phone = '';
      _userId = '';
    }
    _loading = false;
    _error = '';
    _notify();
  }

  // ── 发送短信验证码 ──
  async function sendSmsCode(phone) {
    _loading = true;
    _error = '';
    _notify();

    try {
      await AuthApi.sendSmsCode(phone);
      return true;
    } catch (e) {
      _error = mapAuthErrorMessage(e, '验证码发送失败，请稍后重试');
      _notify();
      throw e;
    } finally {
      _loading = false;
      _notify();
    }
  }

  // ── 验证码登录 ──
  async function login(phone, code) {
    _loading = true;
    _error = '';
    _notify();

    try {
      const result = await AuthApi.loginWithSms(phone, code);

      // 持久化登录信息
      await Promise.all([
        _storageSet(STORAGE_KEYS.jwt, result.jwt),
        result.apiKey ? _storageSet(STORAGE_KEYS.apiKey, result.apiKey) : _storageRemove(STORAGE_KEYS.apiKey),
        _storageSet(STORAGE_KEYS.phone, result.phone),
        result.userId ? _storageSet(STORAGE_KEYS.userId, result.userId) : Promise.resolve(),
      ]);

      // 保存 sessionId（如果有）
      if (result.sessionId) {
        await _storageSet(STORAGE_KEYS.sessionId, result.sessionId);
      }

      // 更新状态
      _isLoggedIn = true;
      _phone = result.phone;
      _userId = result.userId;
      _notify();

      return true;
    } catch (e) {
      _error = mapAuthErrorMessage(e, '登录失败，请稍后重试');
      _notify();
      throw e;
    } finally {
      _loading = false;
      _notify();
    }
  }

  // ── 登出 ──
  async function logout() {
    await Promise.all(Object.values(STORAGE_KEYS).map(k => _storageRemove(k)));
    _isLoggedIn = false;
    _phone = '';
    _userId = '';
    _loading = false;
    _error = '';
    _notify();
  }

  // ── 清除错误 ──
  function clearError() {
    _error = '';
    _notify();
  }

  // ── Getters ──
  function isLoggedIn() { return _isLoggedIn; }
  function getPhone() { return _phone; }
  function getUserId() { return _userId; }
  function isLoading() { return _loading; }
  function getError() { return _error; }
  async function getJwt() { return _getJwt(); }
  async function getSessionId() { return _getSessionId(); }

  return {
    getState,
    init,
    sendSmsCode,
    login,
    logout,
    clearError,
    isLoggedIn,
    getPhone,
    getUserId,
    isLoading,
    getError,
    getJwt,
    getSessionId,
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); },
  };
})();

export default AuthStore;
