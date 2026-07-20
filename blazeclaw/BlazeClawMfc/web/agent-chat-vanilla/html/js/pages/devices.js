/* ================================================================
   AgentChat 重构版 - 设备管理页面（工作台入口）
   ----------------------------------------------------------------
   对标 agent 项目移动端 DevicesPage.vue 的移动端布局。
   - 页面逻辑：渲染 + 事件绑定，不直接访问网络
   - 业务逻辑：通过 Bridge -> postMessage 交由 C++ 处理
   - 看板视图：统计 / 当前在线 / 已绑定 / 可绑定 / 投递目标 / 待绑定会话
   - 绑定流程：手动输入三步（input -> confirm -> result）
   ================================================================ */

import Bridge from '../bridge/index.js';
import Toast from '../utils/toast.js';
import TimeUtils from '../utils/time.js';

const DevicesPage = {
  container: null,
  unsub: null,
  // 绑定流程 UI 状态（手动输入三步：input -> confirm -> result）
  bindFlowOpen: false,
  bindStep: 'input',
  bindManualInput: '',
  bindToken: '',
  bindSession: null,
  bindResult: null,
  bindError: '',
  bindConfirming: false,
  // 确认步骤中选中的目标群聊
  selectedConversationId: '',
  // 内联解绑确认
  unbindConfirmId: '',

  init() {
    this.container = document.getElementById('page-devices');
    this._resetBindFlow();
    this.unbindConfirmId = '';
    this.unsub = Bridge.subscribe(() => this._render());
    void Bridge.refreshBoundDevices();
    this._render();
  },

  // ═══════════════════════════════════════════
  // 渲染入口
  // ═══════════════════════════════════════════

  _render() {
    if (!this.container) return;
    if (this.bindFlowOpen) {
      this._renderBindFlow();
    } else {
      this._renderDashboard();
    }
  },

  // ═══════════════════════════════════════════
  // 看板视图（对标 agent 项目移动端 DevicesPage）
  // ═══════════════════════════════════════════

  _renderDashboard() {
    const devices = Bridge.getBoundDevices();
    const pendingSessions = Bridge.getPendingDeviceSessions();
    const onlineDevices = devices.filter(d => d.online === true);

    this.container.innerHTML = `
    <div class="devices-page">
      <div class="devices-header">
        <button class="devices-back-btn" id="devices-back-btn">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
        </button>
        <span class="devices-header-title">我的设备</span>
        <button id="devices-manual-btn" style="border:none;background:transparent;color:var(--app-brand);font-size:0.84rem;font-weight:600;padding:0.4rem 0.6rem;cursor:pointer;">手动输入</button>
      </div>

      <div style="padding:1rem 1rem calc(5rem + env(safe-area-inset-bottom));overflow-y:auto;">

        <!-- 我的设备（统计） -->
        <section style="border:1px solid var(--app-border);border-radius:1.35rem;background:var(--app-surface);padding:1.2rem;box-shadow:0 14px 34px rgba(95,73,170,0.06);">
          <div style="font-size:1.05rem;font-weight:600;letter-spacing:-0.02em;color:var(--app-text);">我的设备</div>
          <div style="margin-top:0.5rem;font-size:0.88rem;line-height:1.75;color:var(--app-text-secondary);">这里管理电视、笔记本和手机等协同节点。电视用于展示，笔记本用于执行，手机用于控制。</div>
          <div style="margin-top:1rem;display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0.6rem;">
            <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 0.5rem;text-align:center;">
              <div style="font-size:1.15rem;font-weight:600;color:var(--app-brand);">${onlineDevices.length}</div>
              <div style="margin-top:0.2rem;font-size:0.72rem;color:var(--app-text-secondary);">当前在线</div>
            </div>
            <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 0.5rem;text-align:center;">
              <div style="font-size:1.15rem;font-weight:600;color:var(--app-brand);">${devices.length}</div>
              <div style="margin-top:0.2rem;font-size:0.72rem;color:var(--app-text-secondary);">已绑定</div>
            </div>
            <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 0.5rem;text-align:center;">
              <div style="font-size:1.15rem;font-weight:600;color:var(--app-brand);">${pendingSessions.length}</div>
              <div style="margin-top:0.2rem;font-size:0.72rem;color:var(--app-text-secondary);">可绑定</div>
            </div>
          </div>
        </section>

        <!-- 当前在线 -->
        <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.45rem;background:var(--app-surface);padding:1.2rem;box-shadow:0 10px 26px rgba(61,74,122,0.05);">
          <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
            <div style="font-size:0.98rem;font-weight:600;color:var(--app-text);">当前在线</div>
            <div style="font-size:0.82rem;color:var(--app-muted);">${onlineDevices.length} 台</div>
          </div>
          ${onlineDevices.length === 0
            ? `<div style="margin-top:0.75rem;font-size:0.88rem;line-height:1.5;color:var(--app-text-secondary);">当前没有在线设备。绑定电视或笔记本后，可在这里看到可接收投递的设备。</div>`
            : `<div style="margin-top:0.75rem;display:flex;flex-direction:column;gap:0.75rem;">
                ${onlineDevices.map(device => `
                  <article style="border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-subtle-bg);padding:1rem;">
                    <div style="display:flex;align-items:flex-start;justify-content:space-between;gap:0.75rem;">
                      <div style="display:flex;gap:0.7rem;align-items:flex-start;">
                        <div style="flex-shrink:0;width:2.5rem;height:2.5rem;border-radius:0.9rem;background:var(--app-brand-soft);color:var(--app-brand);display:flex;align-items:center;justify-content:center;">
                          ${this._deviceIconSvg(device.deviceType)}
                        </div>
                        <div style="min-width:0;flex:1;">
                          <div style="font-size:0.96rem;font-weight:600;color:var(--app-text);">${this._esc(device.name)}</div>
                          <div style="margin-top:0.25rem;font-size:0.84rem;color:var(--app-text-secondary);">${this._deviceTypeLabel(device.deviceType)} · ${this._devicePurpose(device.deviceType, device.online)}</div>
                          <div style="margin-top:0.25rem;font-size:0.82rem;color:var(--app-muted);">投递目标：${this._esc(device.conversationName || '当前会话')}</div>
                        </div>
                      </div>
                    </div>
                  </article>
                `).join('')}
              </div>`
          }
        </section>

        <!-- 已绑定 -->
        <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.45rem;background:var(--app-surface);padding:1.2rem;box-shadow:0 10px 26px rgba(61,74,122,0.05);">
          <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
            <div style="font-size:0.98rem;font-weight:600;color:var(--app-text);">已绑定</div>
            <div style="font-size:0.82rem;color:var(--app-muted);">${devices.length} 台</div>
          </div>
          ${devices.length === 0
            ? `<div style="margin-top:0.75rem;font-size:0.88rem;line-height:1.5;color:var(--app-text-secondary);">还没有设备加入。电视端生成绑定码后，通过手动输入完成绑定。</div>`
            : `<div style="margin-top:0.75rem;display:flex;flex-direction:column;gap:0.75rem;">
                ${devices.map(device => `
                  <article style="border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-subtle-bg);padding:1rem;">
                    <div style="display:flex;align-items:flex-start;justify-content:space-between;gap:0.75rem;">
                      <div style="display:flex;gap:0.7rem;align-items:flex-start;">
                        <div style="flex-shrink:0;width:2.5rem;height:2.5rem;border-radius:0.9rem;background:var(--app-brand-soft);color:var(--app-brand);display:flex;align-items:center;justify-content:center;">
                          ${this._deviceIconSvg(device.deviceType)}
                        </div>
                        <div style="min-width:0;flex:1;">
                          <div style="font-size:0.96rem;font-weight:600;color:var(--app-text);">${this._esc(device.name)}</div>
                          <div style="margin-top:0.25rem;font-size:0.84rem;color:var(--app-text-secondary);">${this._deviceTypeLabel(device.deviceType)} · ${device.online ? '在线' : '离线'}</div>
                          <div style="margin-top:0.25rem;font-size:0.82rem;color:var(--app-muted);">当前用途：${this._devicePurpose(device.deviceType, device.online)}</div>
                          <div style="margin-top:0.25rem;font-size:0.82rem;color:var(--app-muted);">已加入群聊：${this._esc(device.conversationName || '当前会话')}</div>
                        </div>
                      </div>
                      ${this.unbindConfirmId === device.id ? '' : `
                        <button style="flex-shrink:0;border:1px solid rgba(251,113,133,0.3);border-radius:999px;background:transparent;padding:0.4rem 0.7rem;font-size:0.76rem;font-weight:600;color:#e11d48;cursor:pointer;" data-action="request-unbind" data-device-id="${this._esc(device.id)}">解绑设备</button>
                      `}
                    </div>
                    ${this.unbindConfirmId === device.id ? `
                      <div style="margin-top:0.75rem;display:flex;flex-wrap:wrap;align-items:center;justify-content:flex-end;gap:0.5rem;">
                        <span style="font-size:0.74rem;font-weight:600;color:#e11d48;">确定解绑该设备？</span>
                        <button style="border:1px solid var(--app-border);border-radius:999px;background:transparent;padding:0.35rem 0.7rem;font-size:0.76rem;font-weight:600;color:var(--app-text-secondary);cursor:pointer;" data-action="cancel-unbind">取消</button>
                        <button style="border:none;border-radius:999px;background:#e11d48;padding:0.35rem 0.7rem;font-size:0.76rem;font-weight:600;color:#fff;cursor:pointer;" data-action="confirm-unbind" data-device-id="${this._esc(device.id)}">确认</button>
                      </div>
                    ` : ''}
                  </article>
                `).join('')}
              </div>`
          }
        </section>

        <!-- 可绑定 -->
        <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.45rem;background:var(--app-surface);padding:1.2rem;box-shadow:0 10px 26px rgba(61,74,122,0.05);">
          <div style="font-size:0.98rem;font-weight:600;color:var(--app-text);">可绑定</div>
          <div style="margin-top:0.75rem;display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0.6rem;">
            <button data-action="open-manual-bind" style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem;text-align:center;cursor:pointer;">
              <div style="display:flex;justify-content:center;color:var(--app-brand);">${this._deviceIconSvg('tv')}</div>
              <div style="margin-top:0.4rem;font-size:0.78rem;font-weight:600;color:var(--app-text);">电视</div>
              <div style="margin-top:0.15rem;font-size:0.68rem;color:var(--app-muted);">展示端</div>
            </button>
          </div>
        </section>

        <!-- 投递目标 -->
        <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.45rem;background:var(--app-surface);padding:1.2rem;box-shadow:0 10px 26px rgba(61,74,122,0.05);">
          <div style="font-size:0.98rem;font-weight:600;color:var(--app-text);">投递目标</div>
          <div style="margin-top:0.75rem;font-size:0.88rem;line-height:1.5;color:var(--app-text-secondary);">
            当前投递目标跟随设备绑定的群聊或 AI 空间。后续可在这里设置默认电视、默认笔记本。
          </div>
        </section>

        <!-- 待绑定会话 -->
        <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.45rem;background:var(--app-surface);padding:1.2rem;box-shadow:0 10px 26px rgba(61,74,122,0.05);">
          <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
            <div style="font-size:0.98rem;font-weight:600;color:var(--app-text);">待绑定会话</div>
            <button id="devices-create-session-btn" style="border:1px solid var(--app-border);border-radius:999px;background:var(--app-subtle-bg);padding:0.4rem 0.8rem;font-size:0.76rem;font-weight:600;color:var(--app-text-secondary);cursor:pointer;">生成绑定码</button>
          </div>
          ${pendingSessions.length === 0
            ? `<div style="margin-top:0.75rem;font-size:0.88rem;line-height:1.5;color:var(--app-text-secondary);">当前还没有待绑定的设备。电视端发起绑定后，这里会显示可扫码的待绑定设备。</div>`
            : `<div style="margin-top:1rem;display:flex;flex-direction:column;gap:1rem;">
                ${pendingSessions.map(session => `
                  <div style="display:flex;flex-direction:column;gap:0.75rem;">
                    <article style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.9rem;">
                      <div style="font-size:0.9rem;font-weight:600;color:var(--app-text);">${this._esc(session.name || session.deviceName || '电视设备')}</div>
                      <div style="margin-top:0.3rem;font-size:0.78rem;color:var(--app-muted);">状态：${this._esc(session.status)} · 过期：${this._esc(TimeUtils.formatSmart(session.expiresAt || session.expireAt))}</div>
                      <div style="margin-top:0.55rem;font-size:0.76rem;color:var(--app-muted);word-break:break-all;">bind_token：${this._esc(session.bindToken)}</div>
                    </article>
                    <div style="display:flex;gap:0.5rem;flex-wrap:wrap;">
                      <button style="border:none;border-radius:999px;background:var(--app-subtle-bg);padding:0.42rem 0.8rem;font-size:0.78rem;font-weight:600;color:var(--app-text-secondary);cursor:pointer;" data-action="copy-token" data-bind-token="${this._esc(session.bindToken)}">复制 bind_token</button>
                      <button style="border:none;border-radius:999px;background:var(--app-subtle-bg);padding:0.42rem 0.8rem;font-size:0.78rem;font-weight:600;color:var(--app-text-secondary);cursor:pointer;" data-action="copy-deeplink" data-bind-token="${this._esc(session.bindToken)}">复制 Deep Link</button>
                    </div>
                  </div>
                `).join('')}
              </div>`
          }
        </section>

      </div>
    </div>`;
    this._bindDashboardEvents();
  },

  _bindDashboardEvents() {
    // 返回按钮 -> 回工作台
    const backBtn = document.getElementById('devices-back-btn');
    if (backBtn) backBtn.onclick = () => { window.location.hash = '#/ai'; };

    // 手动输入 -> 打开绑定流程
    const manualBtn = document.getElementById('devices-manual-btn');
    if (manualBtn) manualBtn.onclick = () => this._openBindFlow();

    // 生成绑定码
    const createBtn = document.getElementById('devices-create-session-btn');
    if (createBtn) createBtn.onclick = async () => {
      try {
        const name = prompt('请输入设备名称', 'Living Room TV');
        if (name === null) return;
        const session = await Bridge.createDeviceBindSession(name || 'Living Room TV');
        this._render();
        Toast.success(`已生成绑定码：${session.bindToken}`);
      } catch (e) {
        Toast.warn(e.message || '生成绑定码失败');
      }
    };

    // 可绑定 - 电视 -> 打开手动输入绑定流程
    this.container.querySelectorAll('[data-action="open-manual-bind"]').forEach(el => {
      el.onclick = () => this._openBindFlow();
    });

    // 复制 bind_token
    this.container.querySelectorAll('[data-action="copy-token"]').forEach(el => {
      el.onclick = async () => {
        try {
          await navigator.clipboard.writeText(el.dataset.bindToken || '');
          Toast.success('bind_token 已复制');
        } catch {
          Toast.warn('复制失败，请手动复制');
        }
      };
    });

    // 复制 Deep Link
    this.container.querySelectorAll('[data-action="copy-deeplink"]').forEach(el => {
      el.onclick = async () => {
        const token = el.dataset.bindToken;
        try {
          await navigator.clipboard.writeText(`agentchat://bind-device?bind_token=${token}`);
          Toast.success('Deep Link 已复制');
        } catch {
          Toast.warn('复制失败，请手动复制');
        }
      };
    });

    // 请求解绑（展示内联确认）
    this.container.querySelectorAll('[data-action="request-unbind"]').forEach(el => {
      el.onclick = () => {
        this.unbindConfirmId = el.dataset.deviceId;
        this._render();
      };
    });

    // 取消解绑
    this.container.querySelectorAll('[data-action="cancel-unbind"]').forEach(el => {
      el.onclick = () => {
        this.unbindConfirmId = '';
        this._render();
      };
    });

    // 确认解绑
    this.container.querySelectorAll('[data-action="confirm-unbind"]').forEach(el => {
      el.onclick = async () => {
        const deviceId = el.dataset.deviceId;
        if (!deviceId) return;
        try {
          await Bridge.unbindDevice(deviceId);
          this.unbindConfirmId = '';
          this._render();
          Toast.success('设备已解绑');
        } catch (e) {
          Toast.warn(e.message || '解绑失败');
        }
      };
    });
  },

  // ═══════════════════════════════════════════
  // 绑定流程状态管理
  // ═══════════════════════════════════════════

  _resetBindFlow() {
    this.bindFlowOpen = false;
    this.bindStep = 'input';
    this.bindManualInput = '';
    this.bindToken = '';
    this.bindSession = null;
    this.bindResult = null;
    this.bindError = '';
    this.bindConfirming = false;
    this.selectedConversationId = '';
  },

  _openBindFlow() {
    this.bindFlowOpen = true;
    this.bindStep = 'input';
    this.bindManualInput = '';
    this.bindToken = '';
    this.bindSession = null;
    this.bindResult = null;
    this.bindError = '';
    this.bindConfirming = false;
    // 默认选中第一个群聊
    const conversations = Bridge.getBindableConversations();
    if (conversations[0]) this.selectedConversationId = conversations[0].id;
    this._render();
    setTimeout(() => {
      const input = document.getElementById('dev-bind-input');
      if (input) input.focus();
    }, 50);
  },

  _closeBindFlow() {
    this._resetBindFlow();
    this._render();
  },

  _setBindError(msg) {
    this.bindError = msg;
    const errorEl = document.getElementById('dev-bind-error');
    if (errorEl) {
      if (msg) {
        errorEl.textContent = msg;
        errorEl.style.display = 'flex';
      } else {
        errorEl.style.display = 'none';
      }
    }
  },

  // ═══════════════════════════════════════════
  // 绑定流程视图（手动输入三步：input -> confirm -> result）
  // ═══════════════════════════════════════════

  _renderBindFlow() {
    const step = this.bindStep;
    const title = step === 'input' ? '绑定电视设备'
      : step === 'confirm' ? '确认加入群聊'
      : '绑定完成';

    let body = '';
    if (step === 'input') body = this._renderBindInputStep();
    else if (step === 'confirm') body = this._renderBindConfirmStep();
    else if (step === 'result') body = this._renderBindResultStep();

    this.container.innerHTML = `
    <div class="devices-page">
      <div class="devices-header">
        <button class="devices-back-btn" id="dev-bind-back-btn">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
        </button>
        <span class="devices-header-title">${title}</span>
        <span style="width:2rem;"></span>
      </div>

      <div style="padding:1rem 1rem calc(5rem + env(safe-area-inset-bottom));overflow-y:auto;">

        <!-- 错误提示 -->
        <div id="dev-bind-error" style="${this.bindError ? 'display:flex' : 'display:none'};margin-bottom:0.8rem;align-items:flex-start;gap:0.6rem;border:1px solid rgba(251,113,133,0.3);border-radius:1rem;background:rgba(251,113,133,0.08);padding:0.7rem 0.9rem;font-size:0.82rem;line-height:1.5;color:#e11d48;">
          ${this.bindError ? this._esc(this.bindError) : ''}
        </div>

        ${body}

      </div>
    </div>`;
    this._bindBindFlowEvents();
  },

  // 步骤 1：输入绑定码
  _renderBindInputStep() {
    return `
      <section style="border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 26px rgba(61,74,122,0.05);">
        <div style="font-size:0.95rem;font-weight:600;color:var(--app-text);">输入电视绑定码</div>
        <div style="margin-top:0.5rem;font-size:0.82rem;line-height:1.6;color:var(--app-text-secondary);">
          请在电视端打开绑定页面，输入屏幕上显示的绑定码。
        </div>
        <div style="margin-top:0.25rem;font-size:0.74rem;color:var(--app-muted);">
          也支持粘贴 agentchat://bind-device 链接。
        </div>
        <input
          id="dev-bind-input"
          type="text"
          placeholder="例如 bt_8f3k2"
          value="${this._esc(this.bindManualInput)}"
          style="margin-top:0.9rem;display:block;width:100%;box-sizing:border-box;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 1rem;font-size:0.88rem;color:var(--app-text);outline:none;"
        />
        <button
          id="dev-bind-submit-btn"
          style="margin-top:0.9rem;width:100%;min-height:3rem;border:none;border-radius:1rem;background:var(--app-brand);padding:0.85rem;font-size:0.9rem;font-weight:600;color:#fff;cursor:pointer;"
        >继续确认</button>
      </section>

      <div style="margin-top:0.9rem;border:1px dashed var(--app-border);border-radius:1rem;padding:0.8rem 0.9rem;font-size:0.8rem;line-height:1.6;color:var(--app-text-secondary);">
        在电视端打开绑定页面，输入屏幕上显示的绑定码即可完成绑定。也支持直接粘贴 bind_token 或 agentchat:// 链接。
      </div>
    `;
  },

  // 步骤 2：确认加入群聊（设备入口：可选目标群聊）
  _renderBindConfirmStep() {
    const session = this.bindSession || {};
    const conversations = Bridge.getBindableConversations();
    const deviceName = session.name || session.deviceName || '电视设备';
    const bindToken = this.bindToken || session.bindToken || '';

    return `
      <section style="border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1.2rem;box-shadow:0 10px 26px rgba(61,74,122,0.05);">
        <!-- 设备信息 -->
        <div style="display:flex;align-items:flex-start;gap:0.75rem;">
          <div style="flex-shrink:0;width:3rem;height:3rem;border-radius:1rem;background:var(--app-brand-soft);color:var(--app-brand);display:flex;align-items:center;justify-content:center;">
            ${this._deviceIconSvg(session.deviceType || 'tv')}
          </div>
          <div style="min-width:0;flex:1;">
            <div style="font-size:1rem;font-weight:600;color:var(--app-text);">${this._esc(deviceName)}</div>
            <div style="margin-top:0.3rem;font-size:0.78rem;color:var(--app-text-secondary);word-break:break-all;">bind_token：${this._esc(bindToken)}</div>
          </div>
        </div>

        <!-- 目标群聊（下拉选择，设备入口可选） -->
        <div style="margin-top:1.2rem;font-size:0.8rem;font-weight:600;letter-spacing:0.1em;text-transform:uppercase;color:var(--app-muted);">目标群聊</div>
        <select id="dev-bind-target-conv" style="margin-top:0.5rem;display:block;width:100%;box-sizing:border-box;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 1rem;font-size:0.9rem;color:var(--app-text);outline:none;">
          <option value="" disabled ${!this.selectedConversationId ? 'selected' : ''}>请选择目标群聊</option>
          ${conversations.map(c => `<option value="${this._esc(c.id)}" ${c.id === this.selectedConversationId ? 'selected' : ''}>${this._esc(c.name)}</option>`).join('')}
        </select>

        <!-- 确认按钮 -->
        <button
          id="dev-confirm-bind-btn"
          style="margin-top:1.2rem;width:100%;min-height:3rem;border:none;border-radius:1rem;background:var(--app-brand);padding:0.85rem;font-size:0.92rem;font-weight:600;color:#fff;cursor:pointer;${this.bindConfirming ? 'opacity:0.6;' : ''}"
          ${this.bindConfirming ? 'disabled' : ''}
        >${this.bindConfirming ? '绑定中…' : '确认绑定'}</button>
      </section>
    `;
  },

  // 步骤 3：绑定成功
  _renderBindResultStep() {
    const result = this.bindResult || {};
    const deviceName = result.name || result.deviceName || '设备';
    const deviceId = result.id || result.deviceId || '';
    const convName = result.conversationName || result.boundConversationName || '当前会话';

    return `
      <section style="border:1px solid rgba(16,185,129,0.3);border-radius:1.2rem;background:rgba(16,185,129,0.06);padding:1.2rem;color:#065f46;">
        <div style="display:flex;align-items:flex-start;gap:0.75rem;">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="flex-shrink:0;margin-top:0.1rem;"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/><polyline points="22 4 12 14.01 9 11.01"/></svg>
          <div>
            <div style="font-size:1.05rem;font-weight:600;">设备绑定成功</div>
            <div style="margin-top:0.3rem;font-size:0.86rem;line-height:1.6;">
              已将 ${this._esc(deviceName)} 加入 ${this._esc(convName)}
            </div>
          </div>
        </div>
      </section>

      <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1.2rem;box-shadow:0 10px 26px rgba(61,74,122,0.05);">
        <div style="font-size:0.8rem;font-weight:600;letter-spacing:0.1em;text-transform:uppercase;color:var(--app-muted);">设备 ID</div>
        <div style="margin-top:0.3rem;font-size:0.9rem;word-break:break-all;color:var(--app-text);">${this._esc(deviceId)}</div>
        <div style="margin-top:1rem;font-size:0.8rem;font-weight:600;letter-spacing:0.1em;text-transform:uppercase;color:var(--app-muted);">绑定群聊</div>
        <div style="margin-top:0.3rem;font-size:0.9rem;color:var(--app-text);">${this._esc(convName)}</div>
        <button
          id="dev-finish-bind-btn"
          style="margin-top:1.2rem;width:100%;min-height:3rem;border:none;border-radius:1rem;background:var(--app-brand);padding:0.85rem;font-size:0.92rem;font-weight:600;color:#fff;cursor:pointer;"
        >完成</button>
      </section>
    `;
  },

  _bindBindFlowEvents() {
    // 返回按钮 -> 关闭绑定流程，回到看板
    const backBtn = document.getElementById('dev-bind-back-btn');
    if (backBtn) backBtn.onclick = () => this._closeBindFlow();

    if (this.bindStep === 'input') {
      const input = document.getElementById('dev-bind-input');
      if (input) {
        input.oninput = (e) => { this.bindManualInput = e.target.value; };
        input.onkeydown = (e) => {
          if (e.key === 'Enter') { e.preventDefault(); this._submitBindToken(); }
        };
      }
      const submitBtn = document.getElementById('dev-bind-submit-btn');
      if (submitBtn) submitBtn.onclick = () => this._submitBindToken();
    } else if (this.bindStep === 'confirm') {
      const convSelect = document.getElementById('dev-bind-target-conv');
      if (convSelect) convSelect.onchange = (e) => { this.selectedConversationId = e.target.value; };
      const confirmBtn = document.getElementById('dev-confirm-bind-btn');
      if (confirmBtn) confirmBtn.onclick = () => this._confirmBind();
    } else if (this.bindStep === 'result') {
      const finishBtn = document.getElementById('dev-finish-bind-btn');
      if (finishBtn) finishBtn.onclick = () => this._closeBindFlow();
    }
  },

  // ═══════════════════════════════════════════
  // 绑定流程业务操作（调用 Bridge）
  // ═══════════════════════════════════════════

  // 步骤 1 -> 2：解析输入并校验会话
  async _submitBindToken() {
    const raw = (this.bindManualInput || '').trim();
    if (!raw) {
      this._setBindError('请先输入 bind_token');
      return;
    }

    const parsed = Bridge.parseDeviceBindPayload(raw);
    const bindToken = parsed?.bindToken || raw;

    const btn = document.getElementById('dev-bind-submit-btn');
    if (btn) { btn.disabled = true; btn.textContent = '校验中…'; }

    try {
      const session = await Bridge.getDeviceBindSession(bindToken);
      if (!session) {
        this._setBindError('绑定会话不存在');
        if (btn) { btn.disabled = false; btn.textContent = '继续确认'; }
        return;
      }
      if (session.status === 'expired') {
        this._setBindError('绑定会话已过期');
        if (btn) { btn.disabled = false; btn.textContent = '继续确认'; }
        return;
      }
      if (session.status === 'bound') {
        this._setBindError('该设备已完成绑定');
        if (btn) { btn.disabled = false; btn.textContent = '继续确认'; }
        return;
      }
      this.bindToken = bindToken;
      this.bindSession = session;
      this.bindStep = 'confirm';
      this.bindError = '';
      this._render();
    } catch (e) {
      this._setBindError(e.message || '校验失败，请检查绑定码');
      if (btn) { btn.disabled = false; btn.textContent = '继续确认'; }
    }
  },

  // 步骤 2 -> 3：确认绑定
  async _confirmBind() {
    if (!this.selectedConversationId) {
      this._setBindError('请选择目标群聊');
      return;
    }

    const conversations = Bridge.getBindableConversations();
    const target = conversations.find(c => c.id === this.selectedConversationId);
    if (!target) {
      this._setBindError('目标群聊无效');
      return;
    }

    this.bindConfirming = true;
    const btn = document.getElementById('dev-confirm-bind-btn');
    if (btn) { btn.disabled = true; btn.textContent = '绑定中…'; }

    try {
      const session = this.bindSession || {};
      const deviceName = session.name || session.deviceName;
      const result = await Bridge.confirmDeviceBind({
        bindToken: this.bindToken,
        conversationId: target.id,
        conversationName: target.name,
        deviceName,
      });
      this.bindResult = (result && result.data) ? result.data : result;
      this.bindStep = 'result';
      this.bindConfirming = false;
      this.bindError = '';
      this._render();
      Toast.success('设备已绑定');
    } catch (e) {
      this.bindConfirming = false;
      if (btn) { btn.disabled = false; btn.textContent = '确认绑定'; }
      this._setBindError(e.message || '绑定失败，请稍后重试');
    }
  },

  // ═══════════════════════════════════════════
  // 辅助方法
  // ═══════════════════════════════════════════

  _deviceTypeLabel(type) {
    if (type === 'tv') return '电视';
    if (type === 'notebook') return '笔记本';
    if (type === 'phone') return '手机';
    return '设备';
  },

  _devicePurpose(type, online) {
    if (!online) return '未使用';
    if (type === 'tv') return '可接收投屏';
    if (type === 'notebook') return '可执行任务';
    return '可协同';
  },

  _deviceIconSvg(type) {
    if (type === 'tv') {
      return '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>';
    }
    return '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>';
  },

  _esc(str) {
    return String(str || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  },

  destroy() {
    if (this.unsub) {
      this.unsub();
      this.unsub = null;
    }
    this._resetBindFlow();
    this.unbindConfirmId = '';
  },
};

export default DevicesPage;
