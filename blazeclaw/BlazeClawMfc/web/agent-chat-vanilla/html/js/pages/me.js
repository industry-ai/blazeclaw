/* ================================================================
   AgentChat 重构版 - 个人中心页面 (纯 UI，精确匹配原 MeShell)
   ----------------------------------------------------------------
   主题切换、账号信息、设备入口等 UI 渲染与事件绑定保持原样。
   UiStore（纯本地 UI 状态）保留原调用方式。
   ================================================================ */

import Bridge from '../bridge/index.js';
import UiStore from '../stores/uiStore.js';

const MePage = {
  currentPanel: 'home',
  container: null,

  init() {
    this.container = document.getElementById('page-me');
    this.currentPanel = 'home';
    this.render();
  },

  render() {
    if (!this.container) return;
    const isDark = UiStore.isDark();
    const isLoggedIn = Bridge.isLoggedIn();
    const phoneText = isLoggedIn ? (Bridge.getPhone() || '--') : '未登录';
    const accountInitials = this._accountInitials();

    this.container.innerHTML = `
    <div class="me-main">
      <div class="backdrop-glow" style="display:${isDark?'none':'block'};">
        <div style="left:-10%;top:-4%;width:22rem;height:22rem;border-radius:50%;background:radial-gradient(circle,rgba(121,88,255,0.18),transparent 62%);filter:blur(64px);position:absolute;"></div>
        <div style="bottom:-8%;right:-10%;width:18rem;height:18rem;border-radius:50%;background:radial-gradient(circle,rgba(84,112,255,0.12),transparent 62%);filter:blur(64px);position:absolute;"></div>
      </div>
      <div class="me-inner">
        ${this.currentPanel === 'home' ? this._renderHome({ isDark, isLoggedIn, phoneText, accountInitials }) : ''}
        ${this.currentPanel === 'appearance' ? this._renderAppearance() : ''}
        ${this.currentPanel === 'account' ? this._renderAccount({ isLoggedIn, phoneText, accountInitials }) : ''}
      </div>
    </div>`;
    this._bindEvents();
  },

  _renderHome({ isDark, isLoggedIn, phoneText, accountInitials }) {
    const themeSummary = this._themeSummary();
    return `
    <div class="me-header">
      <span class="me-header-title">我的</span>
    </div>

    <div class="me-scroll">
      <div class="me-profile-card" style="padding:1.25rem 1.25rem 1.35rem;border-radius:1.6rem;">
        <div class="me-profile-avatar" style="background:linear-gradient(135deg,#c33cf2,#6b38f6);color:#fff;box-shadow:0 10px 22px rgba(107,56,246,0.22);">${this._esc(accountInitials)}</div>
        <div class="me-profile-info">
          <div class="me-profile-name" style="font-size:1.2rem;letter-spacing:-0.04em;">${this._esc(phoneText)}</div>
          <div class="me-profile-id" style="display:flex;align-items:center;gap:0.45rem;margin-top:0.35rem;font-size:0.88rem;color:var(--app-text-secondary);">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="color:#10b981;"><path d="M9 12l2 2 4-4"/><path d="M21 12c0 1.66-.43 3.22-1.18 4.57-.94 1.7-2.38 3.07-4.11 3.93A9.96 9.96 0 0112 21a9.96 9.96 0 01-3.71-.7 9.99 9.99 0 01-4.11-3.93A9.96 9.96 0 013 12c0-1.66.43-3.22 1.18-4.57A9.99 9.99 0 018.29 3.5 9.96 9.96 0 0112 3c1.3 0 2.55.25 3.71.7a9.99 9.99 0 014.11 3.93C20.57 8.78 21 10.34 21 12z"/></svg>
            <span>${isLoggedIn ? '已通过短信验证' : '当前账号未完成登录'}</span>
          </div>
        </div>
      </div>

      <div class="me-menu" style="padding:0.75rem;border-radius:1.6rem;">
        <div class="me-menu-item" data-panel="appearance" style="gap:0.9rem;padding:0.95rem 1rem;border-radius:1.1rem;">
          <span class="me-menu-icon" style="width:2.5rem;height:2.5rem;justify-content:center;border-radius:0.9rem;background:var(--app-brand-soft);color:var(--app-brand);"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="5"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/></svg></span>
          <span class="me-menu-label" style="font-size:0.94rem;font-weight:600;color:var(--app-text);">外观与主题</span>
          <span class="me-menu-value" style="font-size:0.8rem;color:var(--app-muted);">${themeSummary}</span>
          <span class="me-menu-arrow"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg></span>
        </div>
        <div class="me-menu-item" id="me-devices-btn" style="gap:0.9rem;padding:0.95rem 1rem;border-radius:1.1rem;margin-top:0.15rem;">
          <span class="me-menu-icon" style="width:2.5rem;height:2.5rem;justify-content:center;border-radius:0.9rem;background:var(--app-surface-elevated);color:var(--app-text-secondary);"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg></span>
          <span class="me-menu-label" style="font-size:0.94rem;font-weight:600;color:var(--app-text);">我的设备</span>
          <span class="me-menu-value" style="font-size:0.8rem;color:var(--app-muted);">电视、笔记本、手机与投递目标</span>
          <span class="me-menu-arrow"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg></span>
        </div>
        <div class="me-menu-item" data-panel="account" style="gap:0.9rem;padding:0.95rem 1rem;border-radius:1.1rem;margin-top:0.15rem;">
          <span class="me-menu-icon" style="width:2.5rem;height:2.5rem;justify-content:center;border-radius:0.9rem;background:var(--app-surface-elevated);color:var(--app-text-secondary);"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0110 0v4"/></svg></span>
          <span class="me-menu-label" style="font-size:0.94rem;font-weight:600;color:var(--app-text);">账号与安全</span>
          <span class="me-menu-value" style="font-size:0.8rem;color:var(--app-muted);">手机号、登录方式与安全验证</span>
          <span class="me-menu-arrow"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg></span>
        </div>
      </div>
    </div>`;
  },

  _renderAppearance() {
    const theme = UiStore.getTheme();
    return `
    <div class="me-panel-back">
      <button class="me-panel-back-btn" id="appearance-back-btn">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
      </button>
      <span class="me-panel-back-title">外观与主题</span>
    </div>
    <div class="me-scroll">
      <div class="me-menu" style="margin-top:1rem;padding:1.25rem 1.25rem 1rem;border-radius:1.6rem;">
        <div style="font-size:1.25rem;font-weight:600;color:var(--app-text);">主题模式</div>
        <div style="margin-top:1.15rem;display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0.75rem;">
          ${[
            { value: 'light', label: '浅色', desc: '明亮舒适', icon: '<circle cx="12" cy="12" r="5"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/>', iconBg: 'background:var(--app-surface-elevated);box-shadow:0 10px 24px rgba(98,85,160,0.08);', iconColor: 'color:#f59e0b;' },
            { value: 'dark', label: '深色', desc: '暗色护眼', icon: '<path d="M21 12.79A9 9 0 1111.21 3c0 4.97 4.03 9 9 9 .27 0 .53-.01.79-.03a.5.5 0 0 1 .52.82z"/>', iconBg: 'background:linear-gradient(135deg,#161d38,#26305f);box-shadow:0 10px 24px rgba(32,42,86,0.22);', iconColor: 'color:#fff;' },
            { value: 'system', label: '跟随系统', desc: '自动切换', icon: '<rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/>', iconBg: 'background:var(--app-surface-elevated);box-shadow:0 10px 24px rgba(98,85,160,0.08);', iconColor: 'color:var(--app-text-secondary);' },
          ].map(option => `
            <button class="theme-option" data-theme="${option.value}" style="position:relative;border:1px solid ${theme === option.value ? 'rgba(107,56,246,0.4)' : 'var(--app-border)'};border-radius:1.35rem;background:${theme === option.value ? 'var(--app-brand-soft)' : 'var(--app-surface-elevated)'};padding:0.9rem 0.65rem 0.85rem;text-align:center;color:${theme === option.value ? 'var(--app-brand)' : 'var(--app-text)'};box-shadow:0 10px 24px rgba(98,85,160,0.05);">
              ${theme === option.value ? '<span style="position:absolute;right:0.7rem;top:0.7rem;display:flex;width:1.5rem;height:1.5rem;align-items:center;justify-content:center;border-radius:999px;background:#7b58ff;font-size:0.76rem;font-weight:700;color:#fff;">✓</span>' : ''}
              <span style="display:flex;width:4rem;height:4rem;align-items:center;justify-content:center;margin:0 auto;border-radius:1.05rem;${option.iconBg}">
                <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="${option.iconColor}">${option.icon}</svg>
              </span>
              <span style="display:block;margin-top:0.7rem;font-size:0.98rem;font-weight:600;">${option.label}</span>
              <span style="display:block;margin-top:0.18rem;font-size:0.8rem;color:var(--app-muted);">${option.desc}</span>
            </button>
          `).join('')}
        </div>
        <div style="margin-top:0.95rem;display:flex;align-items:center;gap:0.45rem;font-size:0.86rem;color:var(--app-muted);">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="5"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/></svg>
          <span>主题切换直接影响聊天页和全局背景表现。</span>
        </div>
      </div>
    </div>`;
  },

  _renderAccount({ isLoggedIn, phoneText, accountInitials }) {
    return `
    <div class="me-panel-back">
      <button class="me-panel-back-btn" id="account-back-btn">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
      </button>
      <span class="me-panel-back-title">账号与安全</span>
    </div>
    <div class="me-scroll">
      <div class="me-menu" style="margin-top:1rem;padding:1.25rem;border-radius:1.6rem;">
        <div style="font-size:1.25rem;font-weight:600;color:var(--app-text);">账号信息</div>
        <div style="margin-top:1.15rem;border:1px solid var(--app-border);border-radius:1.35rem;background:var(--app-surface-elevated);padding:1rem 1.15rem;box-shadow:0 10px 24px rgba(98,85,160,0.05);">
          <div style="display:flex;align-items:center;gap:1rem;">
            <div style="display:flex;width:3.5rem;height:3.5rem;align-items:center;justify-content:center;border-radius:999px;background:linear-gradient(135deg,#c33cf2,#6b38f6);font-size:1.2rem;font-weight:700;color:#fff;">${this._esc(accountInitials)}</div>
            <div style="min-width:0;flex:1;">
              <div style="overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:1.15rem;font-weight:600;letter-spacing:-0.04em;color:var(--app-text);">${this._esc(phoneText)}</div>
              <div style="display:flex;align-items:center;gap:0.45rem;margin-top:0.35rem;font-size:0.88rem;color:var(--app-muted);">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="color:#10b981;"><path d="M9 12l2 2 4-4"/><path d="M21 12c0 1.66-.43 3.22-1.18 4.57-.94 1.7-2.38 3.07-4.11 3.93A9.96 9.96 0 0112 21a9.96 9.96 0 01-3.71-.7 9.99 9.99 0 01-4.11-3.93A9.96 9.96 0 013 12c0-1.66.43-3.22 1.18-4.57A9.99 9.99 0 018.29 3.5 9.96 9.96 0 0112 3c1.3 0 2.55.25 3.71.7a9.99 9.99 0 014.11 3.93C20.57 8.78 21 10.34 21 12z"/></svg>
                <span>${isLoggedIn ? '已通过短信验证' : '未验证，无法收发消息'}</span>
              </div>
            </div>
          </div>

          <div style="margin-top:0.9rem;border-top:1px solid var(--app-border);">
            <div style="display:flex;align-items:center;gap:0.75rem;padding:0.95rem 0 0.9rem;">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="color:var(--app-muted);"><rect x="7" y="2" width="10" height="20" rx="2" ry="2"/><path d="M11 18h2"/></svg>
              <span style="font-size:0.98rem;color:var(--app-text-secondary);">手机号</span>
              <span style="margin-left:auto;font-size:0.98rem;color:var(--app-muted);">${this._esc(phoneText)}</span>
            </div>
            <div style="display:flex;align-items:center;gap:0.75rem;padding:0.95rem 0 0.9rem;border-top:1px solid var(--app-border);">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="color:var(--app-muted);"><circle cx="8" cy="8" r="5"/><path d="M11.5 11.5L20 20M16 16h3M18 14h2"/></svg>
              <span style="font-size:0.98rem;color:var(--app-text-secondary);">登录方式</span>
              <span style="margin-left:auto;font-size:0.98rem;color:var(--app-muted);">短信验证码</span>
            </div>
            <div style="display:flex;align-items:center;gap:0.75rem;padding:0.95rem 0 0.9rem;border-top:1px solid var(--app-border);">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="color:var(--app-muted);"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><path d="M9 12l2 2 4-4"/></svg>
              <span style="font-size:0.98rem;color:var(--app-text-secondary);">安全验证</span>
              <span style="margin-left:auto;font-size:0.98rem;color:var(--app-muted);">已保护</span>
            </div>
          </div>
        </div>
        <div style="display:flex;align-items:center;gap:0.45rem;margin-top:0.95rem;font-size:0.86rem;color:var(--app-muted);">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><path d="M9 12l2 2 4-4"/></svg>
          <span>当前账号安全状态良好。</span>
        </div>
      </div>
    </div>`;
  },

  _bindEvents() {
    if (this.currentPanel === 'home') {
      document.querySelectorAll('.me-menu-item[data-panel]').forEach(el => {
        el.onclick = () => { this.currentPanel = el.dataset.panel; this.render(); };
      });
      document.getElementById('me-devices-btn').onclick = () => { window.location.hash = '#/devices'; };
    }

    if (this.currentPanel === 'appearance') {
      document.getElementById('appearance-back-btn').onclick = () => { this.currentPanel = 'home'; this.render(); };
      document.querySelectorAll('.theme-option').forEach(el => {
        el.onclick = () => { UiStore.setTheme(el.dataset.theme); this.render(); };
      });
    }

    if (this.currentPanel === 'account') {
      document.getElementById('account-back-btn').onclick = () => { this.currentPanel = 'home'; this.render(); };
    }
  },

  _themeSummary() {
    const theme = UiStore.getTheme();
    if (theme === 'light') return '浅色';
    if (theme === 'dark') return '深色';
    return '跟随系统';
  },

  _accountInitials() {
    const phone = Bridge.getPhone() || '';
    if (phone.length >= 2) return phone.slice(-2);
    return 'AC';
  },

  _esc(s) { return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;'); },
  destroy() {},
};

export default MePage;
