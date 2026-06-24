/* ================================================================
   AgentChat HTML 版 - 登录页面
   对应原版 AuthShell.vue + SmsCodeInput.vue + LoginSubmitButton.vue + TermsCheckbox.vue
   ================================================================ */

import AuthStore from '../stores/authStore.js';
import ChatStore from '../stores/chatStore.js';
import Toast from '../utils/toast.js';

const showAppHud = Toast.show.bind(Toast);

const AuthPage = (() => {
  let phone = '';
  let code = '';
  let acceptedTerms = true;
  let errorMessage = '';
  let sendingCode = false;
  let loggingIn = false;
  let cooldown = 0;
  let cooldownTimer = null;
  let lastSendPhone = '';

  function init() {
    render();
    bindEvents();
  }

  function destroy() {
    if (cooldownTimer) {
      clearInterval(cooldownTimer);
      cooldownTimer = null;
    }
  }

  /** Update only button states without full re-render (preserves input focus) */
  function updateButtonStates() {
    const canSendCode = /^1\d{10}$/.test(phone) && cooldown <= 0 && !sendingCode;
    const canSubmit = /^1\d{10}$/.test(phone) && /^\d{4,6}$/.test(code) && acceptedTerms && !loggingIn;
    const cooldownText = cooldown > 0 ? `${cooldown}s` : sendingCode ? '发送中' : '获取验证码';
    const submitText = loggingIn ? '进入中...' : '进入平台';

    const sendBtn = document.getElementById('auth-send-code-btn');
    if (sendBtn) {
      sendBtn.disabled = !canSendCode;
      sendBtn.className = `auth-send-code-btn${!canSendCode ? ' disabled' : ''}`;
      sendBtn.textContent = cooldownText;
    }

    const submitBtn = document.getElementById('auth-submit-btn');
    if (submitBtn) {
      submitBtn.disabled = !canSubmit;
      submitBtn.className = `auth-submit-btn${!canSubmit ? ' disabled' : ''}`;
      const span = submitBtn.querySelector('span');
      if (span) span.textContent = submitText;
    }
  }

  function render() {
    const el = document.getElementById('page-auth');
    if (!el) return;

    const canSendCode = /^1\d{10}$/.test(phone) && cooldown <= 0 && !sendingCode;
    const canSubmit = /^1\d{10}$/.test(phone) && /^\d{4,6}$/.test(code) && acceptedTerms && !loggingIn;
    const cooldownText = cooldown > 0 ? `${cooldown}s` : sendingCode ? '发送中' : '获取验证码';
    const submitText = loggingIn ? '进入中...' : '进入平台';

    el.innerHTML = `
    <main class="auth-page">
      <!-- Background decorations -->
      <div class="auth-bg-decor">
        <div class="auth-bg-orb auth-bg-orb-1"></div>
        <div class="auth-bg-orb auth-bg-orb-2"></div>
      </div>

      <div class="auth-container">
        <!-- Mobile logo -->
        <div class="auth-logo-mobile">
          <div class="auth-logo-icon">A</div>
          <div class="auth-logo-text">AgentChat</div>
        </div>

        <!-- Header -->
        <div class="auth-header">
          <h2 class="auth-title">开始对话</h2>
          <p class="auth-subtitle">登录以连接您的智能助手与团队网络</p>
        </div>

        <!-- Form -->
        <form class="auth-form" id="auth-form" onsubmit="return false;">
          <!-- Phone input -->
          <div class="auth-input-group">
            <div class="auth-input-wrap">
              <div class="auth-input-prefix">
                <span class="auth-country-code">+86</span>
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M6 9l6 6 6-6"/></svg>
              </div>
              <div class="auth-input-divider"></div>
              <input
                id="auth-phone-input"
                type="text"
                inputmode="numeric"
                placeholder="请输入手机号"
                value="${phone}"
                maxlength="11"
                class="auth-input"
              />
            </div>
          </div>

          <!-- Code input -->
          <div class="auth-input-group">
            <div class="auth-input-wrap">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="auth-input-icon"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0110 0v4"/></svg>
              <input
                id="auth-code-input"
                type="text"
                inputmode="numeric"
                maxlength="6"
                placeholder="6 位验证码"
                value="${code}"
                class="auth-input auth-input-code"
              />
              <div class="auth-input-divider auth-input-divider-right"></div>
              <button
                id="auth-send-code-btn"
                type="button"
                class="auth-send-code-btn ${!canSendCode ? 'disabled' : ''}"
                ${!canSendCode ? 'disabled' : ''}
              >
                ${cooldownText}
              </button>
            </div>
          </div>

          <!-- Submit button -->
          <button
            id="auth-submit-btn"
            type="button"
            class="auth-submit-btn ${!canSubmit ? 'disabled' : ''}"
            ${!canSubmit ? 'disabled' : ''}
          >
            <span>${submitText}</span>
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M5 12h14m-7-7l7 7-7 7"/></svg>
          </button>

          <!-- Terms checkbox -->
          <label class="auth-terms-label" id="auth-terms-label">
            <div class="auth-checkbox ${acceptedTerms ? 'checked' : ''}" id="auth-checkbox">
              ${acceptedTerms ? '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#fff" stroke-width="3"><path d="M20 6L9 17l-5-5"/></svg>' : ''}
            </div>
            <span class="auth-terms-text">我已阅读并同意 <a href="#" class="auth-terms-link" id="auth-terms-link">用户协议</a> 与 <a href="#" class="auth-terms-link" id="auth-privacy-link">隐私政策</a></span>
          </label>

          <!-- Error banner -->
          ${errorMessage ? `<div class="auth-error-banner" id="auth-error-banner">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 8v4m0 4h.01"/></svg>
            <span>${errorMessage}</span>
          </div>` : ''}
        </form>

        <!-- Footer -->
        <div class="auth-footer-mobile">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>
          <span>企业级安全保障</span>
        </div>
      </div>
    </main>`;
  }

  function startCooldown() {
    if (cooldownTimer) clearInterval(cooldownTimer);
    cooldown = 60;
    cooldownTimer = setInterval(() => {
      cooldown -= 1;
      if (cooldown <= 0) {
        clearInterval(cooldownTimer);
        cooldownTimer = null;
        cooldown = 0;
      }
      updateButtonStates();
    }, 1000);
  }

  function clearCooldown() {
    if (cooldownTimer) {
      clearInterval(cooldownTimer);
      cooldownTimer = null;
    }
    cooldown = 0;
  }

  async function handleSendCode() {
    if (!/^1\d{10}$/.test(phone)) {
      errorMessage = '请输入正确的手机号';
      render();
      bindEvents();
      return;
    }

    sendingCode = true;
    errorMessage = '';
    render();
    bindEvents();

    try {
      await AuthStore.sendSmsCode(phone);
      lastSendPhone = phone;
      startCooldown();
      showAppHud('验证码已发送', 'success');
    } catch (e) {
      errorMessage = e.message || '发送验证码失败';
    } finally {
      sendingCode = false;
      render();
      bindEvents();
    }
  }

  async function handleLogin() {
    if (!/^1\d{10}$/.test(phone)) {
      errorMessage = '请输入正确的手机号';
      render();
      bindEvents();
      return;
    }
    if (!/^\d{4,6}$/.test(code)) {
      errorMessage = '请输入 4 到 6 位验证码';
      render();
      bindEvents();
      return;
    }
    if (!acceptedTerms) {
      errorMessage = '请先阅读并同意用户协议与隐私政策';
      render();
      bindEvents();
      return;
    }

    loggingIn = true;
    errorMessage = '';
    render();
    bindEvents();

    try {
      // 对齐 Vue 版：先清理上一个账号的本地缓存（依赖当前账号信息），再登录新账号
      await ChatStore.resetForAuthChange();
      await AuthStore.login(phone, code);
      showAppHud('登录成功', 'success');
      // 初始化新账号的本地状态（本地瞬时完成，网络在后台并行）
      ChatStore.init();
      // 跳转到聊天页
      window.location.hash = '#/chat';
    } catch (e) {
      errorMessage = e.message || '登录失败，请稍后重试';
    } finally {
      loggingIn = false;
      render();
      bindEvents();
    }
  }

  function bindEvents() {
    const phoneInput = document.getElementById('auth-phone-input');
    const codeInput = document.getElementById('auth-code-input');
    const sendCodeBtn = document.getElementById('auth-send-code-btn');
    const submitBtn = document.getElementById('auth-submit-btn');
    const termsLabel = document.getElementById('auth-terms-label');
    const checkbox = document.getElementById('auth-checkbox');
    const termsLink = document.getElementById('auth-terms-link');
    const privacyLink = document.getElementById('auth-privacy-link');

    if (phoneInput) {
      phoneInput.addEventListener('input', (e) => {
        phone = e.target.value.replace(/\D/g, '');
        e.target.value = phone;
        if (errorMessage) errorMessage = '';
        if (lastSendPhone && phone !== lastSendPhone) clearCooldown();
        updateButtonStates();
      });
    }

    if (codeInput) {
      codeInput.addEventListener('input', (e) => {
        code = e.target.value.replace(/\D/g, '');
        e.target.value = code;
        if (errorMessage) errorMessage = '';
        updateButtonStates();
      });
    }

    if (sendCodeBtn) {
      sendCodeBtn.addEventListener('click', handleSendCode);
    }

    if (submitBtn) {
      submitBtn.addEventListener('click', handleLogin);
    }

    if (termsLabel) {
      termsLabel.addEventListener('click', (e) => {
        e.preventDefault();
        // Don't toggle if clicking on links
        if (e.target.classList.contains('auth-terms-link')) return;
        acceptedTerms = !acceptedTerms;
        render();
        bindEvents();
      });
    }

    if (termsLink) {
      termsLink.addEventListener('click', (e) => {
        e.preventDefault();
        showAppHud('用户协议暂未开放', 'warn');
      });
    }

    if (privacyLink) {
      privacyLink.addEventListener('click', (e) => {
        e.preventDefault();
        showAppHud('隐私政策暂未开放', 'warn');
      });
    }
  }

  return { init, destroy };
})();

export default AuthPage;
