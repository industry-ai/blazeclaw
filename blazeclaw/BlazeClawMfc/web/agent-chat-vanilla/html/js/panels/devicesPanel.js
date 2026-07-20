/* ================================================================
   AgentChat 重构版 - 设备面板（右侧抽屉）
   ----------------------------------------------------------------
   职责：设备管理面板的渲染与事件绑定。
   - 以右侧抽屉形式展示（复用 posts-panel CSS 类）
   - 通过 mixin 合并到 ChatPage，this 指向 ChatPage
   - 看板视图：统计 / 当前在线 / 已绑定 / 待绑定会话 / 手动输入入口
   - 绑定流程：手动输入三步（input -> confirm -> result）
   - 业务逻辑通过 Bridge -> postMessage 交由 C++ 处理
   ================================================================ */

import Bridge from '../bridge/index.js';
import Toast from '../utils/toast.js';
import TimeUtils from '../utils/time.js';

const DevicesPanelMixin = {
  // ═══════════════════════════════════════════
  // 面板生命周期
  // ═══════════════════════════════════════════

  async _openDevicesPanel() {
    const conv = Bridge.getActiveConversation();
    if (!conv) return;
    this.devicesPanelOpen = true;
    // 直接打开手动输入绑定流程（对标 agent 项目"绑定电视设备"浮层）
    this._resetDevicesBindFlow();
    this.devicesBindFlowOpen = true;
    this.devicesBindStep = 'input';
    this.render();
    this._bindDevicesPanelEvents();
    // 异步刷新设备列表（绑定成功后状态需要更新）
    try {
      await Bridge.refreshBoundDevices();
    } catch (e) { /* ignore */ }
    // 自动聚焦输入框
    setTimeout(() => {
      const input = document.getElementById('dp-bind-input');
      if (input) input.focus();
    }, 50);
  },

  _closeDevicesPanel() {
    this.devicesPanelOpen = false;
    this._resetDevicesBindFlow();
    this.render();
  },

  _refreshDevicesPanel() {
    if (!this.devicesPanelOpen) return;
    const panel = document.getElementById('devices-panel');
    if (!panel) return;
    // 保存输入框值（如果在绑定流程的输入步骤）
    const inputEl = document.getElementById('dp-bind-input');
    if (inputEl) this.devicesBindManualInput = inputEl.value;
    const html = this._renderDevicesPanel();
    const wrapper = document.createElement('div');
    wrapper.innerHTML = html;
    const newPanel = wrapper.firstElementChild;
    if (newPanel) {
      panel.replaceWith(newPanel);
      this._bindDevicesPanelEvents();
      // 恢复输入框值和焦点
      if (this.devicesBindFlowOpen && this.devicesBindStep === 'input') {
        const newInput = document.getElementById('dp-bind-input');
        if (newInput) {
          newInput.value = this.devicesBindManualInput;
          newInput.focus();
          const len = newInput.value.length;
          newInput.setSelectionRange(len, len);
        }
      }
    }
  },

  _renderDevicesPanel() {
    if (!this.devicesPanelOpen) return '';
    if (this.devicesBindFlowOpen) {
      return this._renderDevicesBindFlow();
    }
    return this._renderDevicesDashboard();
  },

  _bindDevicesPanelEvents() {
    if (!this.devicesPanelOpen) return;
    if (this.devicesBindFlowOpen) {
      this._bindDevicesBindFlowEvents();
    } else {
      this._bindDevicesDashboardEvents();
    }
  },

  // ═══════════════════════════════════════════
  // 绑定流程状态管理
  // ═══════════════════════════════════════════

  _resetDevicesBindFlow() {
    this.devicesBindFlowOpen = false;
    this.devicesBindStep = 'input';
    this.devicesBindManualInput = '';
    this.devicesBindToken = '';
    this.devicesBindSession = null;
    this.devicesBindResult = null;
    this.devicesBindError = '';
    this.devicesBindConfirming = false;
  },

  _openDevicesBindFlow() {
    this.devicesBindFlowOpen = true;
    this.devicesBindStep = 'input';
    this.devicesBindManualInput = '';
    this.devicesBindToken = '';
    this.devicesBindSession = null;
    this.devicesBindResult = null;
    this.devicesBindError = '';
    this.devicesBindConfirming = false;
    this._refreshDevicesPanel();
    // 自动聚焦输入框
    setTimeout(() => {
      const input = document.getElementById('dp-bind-input');
      if (input) input.focus();
    }, 50);
  },

  _closeDevicesBindFlow() {
    // 关闭整个面板，返回聊天页（对标 agent 项目 closeFlow + bindSource=chat）
    this._closeDevicesPanel();
  },

  _setDevicesBindError(msg) {
    this.devicesBindError = msg;
    const errorEl = document.getElementById('dp-bind-error');
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
  // 看板视图
  // ═══════════════════════════════════════════

  _renderDevicesDashboard() {
    const conv = Bridge.getActiveConversation();
    const devices = Bridge.getBoundDevices();
    const pendingSessions = Bridge.getPendingDeviceSessions();
    const onlineDevices = devices.filter(d => d.online === true);
    const fullScreenStyle = this._isDesktop()
      ? 'width:min(30rem,100%);height:100%;border-radius:1.5rem 0 0 1.5rem;'
      : 'width:100%;height:100%;border-radius:0;';

    return `
    <div id="devices-panel" class="posts-panel open" style="display:flex;">
      <div class="pp-overlay" id="dp-overlay"></div>
      <div class="pp-drawer" style="${fullScreenStyle}background:var(--app-page-bg);">
        <div class="pp-header" style="padding:1rem 1rem 0.75rem;border-bottom:1px solid var(--app-border);background:var(--app-surface);">
          <div>
            <div class="pp-title">设备管理</div>
            <div style="margin-top:0.3rem;font-size:0.8rem;color:var(--app-muted);">${this._esc(conv?.name || '当前会话')} · 管理协同设备</div>
          </div>
          <div class="pp-actions">
            <button class="pp-btn pp-close-btn" id="dp-close-btn" title="关闭">&times;</button>
          </div>
        </div>
        <div class="pp-body" style="padding:1rem 1rem 1.5rem;overflow-y:auto;background:var(--app-page-bg);">

          <!-- 我的设备（统计） -->
          <section style="border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
            <div style="font-size:1rem;font-weight:600;color:var(--app-text);">我的设备</div>
            <div style="margin-top:0.35rem;font-size:0.82rem;line-height:1.6;color:var(--app-muted);">管理电视等协同节点。生成绑定码后，可在设备端扫码或手动输入完成绑定。</div>
            <div style="margin-top:0.95rem;display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0.65rem;">
              <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 0.5rem;text-align:center;">
                <div style="font-size:1.2rem;font-weight:700;color:var(--app-brand);">${onlineDevices.length}</div>
                <div style="margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">当前在线</div>
              </div>
              <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 0.5rem;text-align:center;">
                <div style="font-size:1.2rem;font-weight:700;color:var(--app-brand);">${devices.length}</div>
                <div style="margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">已绑定</div>
              </div>
              <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 0.5rem;text-align:center;">
                <div style="font-size:1.2rem;font-weight:700;color:var(--app-brand);">${pendingSessions.length}</div>
                <div style="margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">可绑定</div>
              </div>
            </div>
          </section>

          <!-- 当前在线 -->
          <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
            <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
              <div style="font-size:0.98rem;font-weight:600;color:var(--app-text);">当前在线</div>
              <div style="font-size:0.82rem;color:var(--app-muted);">${onlineDevices.length} 台</div>
            </div>
            ${onlineDevices.length === 0
              ? `<div style="margin-top:0.8rem;font-size:0.84rem;line-height:1.6;color:var(--app-text-secondary);">当前没有在线设备。绑定电视后，可在这里看到可接收投递的设备。</div>`
              : `<div style="margin-top:0.8rem;display:flex;flex-direction:column;gap:0.7rem;">
                  ${onlineDevices.map(device => `
                    <article style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.9rem;">
                      <div style="display:flex;align-items:flex-start;gap:0.6rem;">
                        <div style="flex-shrink:0;width:2.5rem;height:2.5rem;border-radius:0.8rem;background:var(--app-brand-soft);color:var(--app-brand);display:flex;align-items:center;justify-content:center;">
                          ${this._deviceIconSvg(device.deviceType)}
                        </div>
                        <div style="min-width:0;flex:1;">
                          <div style="font-size:0.9rem;font-weight:600;color:var(--app-text);">${this._esc(device.name)}</div>
                          <div style="margin-top:0.3rem;font-size:0.78rem;color:var(--app-text-secondary);">${this._deviceTypeLabel(device.deviceType)} · 可接收投屏</div>
                          <div style="margin-top:0.25rem;font-size:0.76rem;color:var(--app-muted);">投递目标：${this._esc(device.conversationName || '当前会话')}</div>
                        </div>
                      </div>
                    </article>
                  `).join('')}
                </div>`
            }
          </section>

          <!-- 已绑定 -->
          <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
            <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
              <div style="font-size:0.98rem;font-weight:600;color:var(--app-text);">已绑定</div>
              <div style="font-size:0.82rem;color:var(--app-muted);">${devices.length} 台</div>
            </div>
            ${devices.length === 0
              ? `<div style="margin-top:0.8rem;font-size:0.84rem;line-height:1.6;color:var(--app-text-secondary);">还没有设备加入。生成绑定码后，通过手动输入完成绑定。</div>`
              : `<div style="margin-top:0.8rem;display:flex;flex-direction:column;gap:0.7rem;">
                  ${devices.map(device => `
                    <article style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.9rem;">
                      <div style="display:flex;align-items:flex-start;justify-content:space-between;gap:0.75rem;">
                        <div style="display:flex;gap:0.6rem;align-items:flex-start;">
                          <div style="flex-shrink:0;width:2.5rem;height:2.5rem;border-radius:0.8rem;background:var(--app-brand-soft);color:var(--app-brand);display:flex;align-items:center;justify-content:center;">
                            ${this._deviceIconSvg(device.deviceType)}
                          </div>
                          <div style="min-width:0;flex:1;">
                            <div style="font-size:0.9rem;font-weight:600;color:var(--app-text);">${this._esc(device.name)}</div>
                            <div style="margin-top:0.3rem;font-size:0.78rem;color:var(--app-text-secondary);">${this._deviceTypeLabel(device.deviceType)} · ${device.online ? '在线' : '离线'}</div>
                            <div style="margin-top:0.25rem;font-size:0.76rem;color:var(--app-muted);">已加入群聊：${this._esc(device.conversationName || '当前会话')}</div>
                            <div style="margin-top:0.25rem;font-size:0.76rem;color:var(--app-muted);">绑定时间：${this._esc(TimeUtils.formatSmart(device.boundAt))}</div>
                          </div>
                        </div>
                        <button style="border:1px solid rgba(251,113,133,0.3);border-radius:999px;background:transparent;padding:0.42rem 0.7rem;font-size:0.76rem;color:#e11d48;flex-shrink:0;" data-dp-action="unbind-device" data-device-id="${this._esc(device.id)}">解绑</button>
                      </div>
                    </article>
                  `).join('')}
                </div>`
            }
          </section>

          <!-- 待绑定会话 -->
          <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
            <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
              <div style="font-size:0.98rem;font-weight:600;color:var(--app-text);">待绑定会话</div>
              <button id="dp-create-session-btn" style="border:1px solid var(--app-border);border-radius:999px;background:var(--app-subtle-bg);padding:0.42rem 0.8rem;font-size:0.76rem;font-weight:600;color:var(--app-text-secondary);">生成绑定码</button>
            </div>
            <div style="margin-top:0.25rem;font-size:0.8rem;color:var(--app-muted);">设备端发起绑定后，可在此查看待绑定的设备会话。</div>
            ${pendingSessions.length === 0
              ? `<div style="margin-top:0.8rem;border:1px dashed var(--app-border);border-radius:1rem;padding:1rem;text-align:center;font-size:0.84rem;line-height:1.6;color:var(--app-muted);">暂无待绑定会话，先生成一个电视绑定码。</div>`
              : `<div style="margin-top:0.8rem;display:flex;flex-direction:column;gap:0.7rem;">
                  ${pendingSessions.map(session => `
                    <article style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.9rem;">
                      <div style="font-size:0.9rem;font-weight:600;color:var(--app-text);">${this._esc(session.name || session.deviceName || '电视设备')}</div>
                      <div style="margin-top:0.3rem;font-size:0.78rem;color:var(--app-muted);">状态：${this._esc(session.status)} · 过期：${this._esc(TimeUtils.formatSmart(session.expiresAt || session.expireAt))}</div>
                      <div style="margin-top:0.55rem;font-size:0.76rem;color:var(--app-muted);word-break:break-all;">bind_token：${this._esc(session.bindToken)}</div>
                      <div style="margin-top:0.75rem;display:flex;gap:0.55rem;flex-wrap:wrap;">
                        <button style="border:1px solid var(--app-border);border-radius:999px;background:transparent;padding:0.42rem 0.7rem;font-size:0.76rem;color:var(--app-muted);" data-dp-action="copy-token" data-bind-token="${this._esc(session.bindToken)}">复制令牌</button>
                        <button style="border:1px solid var(--app-border);border-radius:999px;background:transparent;padding:0.42rem 0.7rem;font-size:0.76rem;color:var(--app-muted);" data-dp-action="copy-deeplink" data-bind-token="${this._esc(session.bindToken)}">复制链接</button>
                      </div>
                    </article>
                  `).join('')}
                </div>`
            }
          </section>

          <!-- 手动输入入口 -->
          <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
            <div style="font-size:0.98rem;font-weight:600;color:var(--app-text);">手动输入绑定</div>
            <div style="margin-top:0.25rem;font-size:0.8rem;color:var(--app-muted);">在电视端获取绑定码后，手动输入完成绑定。</div>
            <button id="dp-manual-bind-btn" style="margin-top:0.85rem;width:100%;border:none;border-radius:1rem;background:var(--app-brand);padding:0.8rem;font-size:0.88rem;font-weight:600;color:#fff;">手动输入绑定码</button>
          </section>

        </div>
      </div>
    </div>`;
  },

  _bindDevicesDashboardEvents() {
    // 关闭按钮
    const closeBtn = document.getElementById('dp-close-btn');
    if (closeBtn) closeBtn.onclick = () => this._closeDevicesPanel();

    // 遮罩点击关闭
    const overlay = document.getElementById('dp-overlay');
    if (overlay) overlay.onclick = () => this._closeDevicesPanel();

    // 生成绑定码
    const createBtn = document.getElementById('dp-create-session-btn');
    if (createBtn) createBtn.onclick = async () => {
      try {
        const name = prompt('请输入设备名称', 'Living Room TV');
        if (name === null) return;
        const session = await Bridge.createDeviceBindSession(name || 'Living Room TV');
        this._refreshDevicesPanel();
        Toast.success(`已生成绑定码：${session.bindToken}`);
      } catch (e) {
        Toast.warn(e.message || '生成绑定码失败');
      }
    };

    // 手动输入绑定
    const manualBtn = document.getElementById('dp-manual-bind-btn');
    if (manualBtn) manualBtn.onclick = () => this._openDevicesBindFlow();

    // 复制令牌
    this.container.querySelectorAll('[data-dp-action="copy-token"]').forEach(el => {
      el.onclick = async () => {
        const token = el.dataset.bindToken;
        try {
          await navigator.clipboard.writeText(token || '');
          Toast.success('bind_token 已复制');
        } catch {
          Toast.warn('复制失败，请手动复制');
        }
      };
    });

    // 复制 Deep Link
    this.container.querySelectorAll('[data-dp-action="copy-deeplink"]').forEach(el => {
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

    // 解绑设备
    this.container.querySelectorAll('[data-dp-action="unbind-device"]').forEach(el => {
      el.onclick = async () => {
        const deviceId = el.dataset.deviceId;
        if (!deviceId) return;
        if (!confirm('确定解绑该设备吗？')) return;
        try {
          await Bridge.unbindDevice(deviceId);
          this._refreshDevicesPanel();
          Toast.success('设备已解绑');
        } catch (e) {
          Toast.warn(e.message || '解绑失败');
        }
      };
    });
  },

  // ═══════════════════════════════════════════
  // 绑定流程视图（手动输入三步：input -> confirm -> result）
  // ═══════════════════════════════════════════

  _renderDevicesBindFlow() {
    const step = this.devicesBindStep;
    const title = step === 'input' ? '绑定电视设备'
      : step === 'confirm' ? '确认加入群聊'
      : '绑定完成';
    const fullScreenStyle = this._isDesktop()
      ? 'width:min(30rem,100%);height:100%;border-radius:1.5rem 0 0 1.5rem;'
      : 'width:100%;height:100%;border-radius:0;';

    let body = '';
    if (step === 'input') body = this._renderBindInputStep();
    else if (step === 'confirm') body = this._renderBindConfirmStep();
    else if (step === 'result') body = this._renderBindResultStep();

    return `
    <div id="devices-panel" class="posts-panel open" style="display:flex;">
      <div class="pp-overlay" id="dp-overlay"></div>
      <div class="pp-drawer" style="${fullScreenStyle}background:var(--app-page-bg);">
        <div class="pp-header" style="padding:1rem 1rem 0.75rem;border-bottom:1px solid var(--app-border);background:var(--app-surface);">
          <div style="display:flex;align-items:center;gap:0.5rem;">
            <button id="dp-bind-back-btn" style="border:none;background:transparent;color:var(--app-text);padding:0.2rem;display:flex;align-items:center;cursor:pointer;">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
            </button>
            <div class="pp-title">${title}</div>
          </div>
          <div class="pp-actions">
            <button class="pp-btn pp-close-btn" id="dp-close-btn" title="关闭">&times;</button>
          </div>
        </div>
        <div class="pp-body" style="padding:1rem 1rem 1.5rem;overflow-y:auto;background:var(--app-page-bg);">

          <!-- 错误提示 -->
          <div id="dp-bind-error" style="${this.devicesBindError ? 'display:flex' : 'display:none'};margin-bottom:0.8rem;align-items:flex-start;gap:0.6rem;border:1px solid rgba(251,113,133,0.3);border-radius:1rem;background:rgba(251,113,133,0.08);padding:0.7rem 0.9rem;font-size:0.82rem;line-height:1.5;color:#e11d48;">
            ${this.devicesBindError ? this._esc(this.devicesBindError) : ''}
          </div>

          ${body}

        </div>
      </div>
    </div>`;
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
          id="dp-bind-input"
          type="text"
          placeholder="例如 bt_8f3k2"
          value="${this._esc(this.devicesBindManualInput)}"
          style="margin-top:0.9rem;display:block;width:100%;box-sizing:border-box;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 1rem;font-size:0.88rem;color:var(--app-text);outline:none;"
        />
        <button
          id="dp-bind-submit-btn"
          style="margin-top:0.9rem;width:100%;border:none;border-radius:1rem;background:var(--app-brand);padding:0.85rem;font-size:0.9rem;font-weight:600;color:#fff;"
        >继续确认</button>
      </section>

      <!-- 底部提示 -->
      <div style="margin-top:0.9rem;border:1px dashed var(--app-border);border-radius:1rem;padding:0.8rem 0.9rem;font-size:0.8rem;line-height:1.6;color:var(--app-text-secondary);">
        在电视端打开绑定页面，输入屏幕上显示的绑定码即可完成绑定。也支持直接粘贴 bind_token 或 agentchat:// 链接。
      </div>
    `;
  },

  // 步骤 2：确认加入群聊
  _renderBindConfirmStep() {
    const session = this.devicesBindSession || {};
    const conv = Bridge.getActiveConversation();
    const deviceName = session.name || session.deviceName || '电视设备';
    const bindToken = this.devicesBindToken || session.bindToken || '';

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

        <!-- 目标群聊（锁定为当前会话） -->
        <div style="margin-top:1.2rem;font-size:0.8rem;font-weight:600;letter-spacing:0.1em;text-transform:uppercase;color:var(--app-muted);">目标群聊</div>
        <div style="margin-top:0.5rem;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem 1rem;font-size:0.9rem;color:var(--app-text);">
          ${this._esc(conv?.name || '当前会话')}
        </div>

        <!-- 确认按钮 -->
        <button
          id="dp-confirm-bind-btn"
          style="margin-top:1.2rem;width:100%;min-height:3rem;border:none;border-radius:1rem;background:var(--app-brand);padding:0.85rem;font-size:0.92rem;font-weight:600;color:#fff;${this.devicesBindConfirming ? 'opacity:0.6;' : ''}"
          ${this.devicesBindConfirming ? 'disabled' : ''}
        >${this.devicesBindConfirming ? '绑定中…' : '确认绑定'}</button>
      </section>
    `;
  },

  // 步骤 3：绑定成功
  _renderBindResultStep() {
    const result = this.devicesBindResult || {};
    const deviceName = result.name || result.deviceName || '设备';
    const deviceId = result.id || result.deviceId || '';
    const convName = result.conversationName || result.boundConversationName || '当前会话';

    return `
      <!-- 成功卡片 -->
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

      <!-- 设备详情 -->
      <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1.2rem;box-shadow:0 10px 26px rgba(61,74,122,0.05);">
        <div style="font-size:0.8rem;font-weight:600;letter-spacing:0.1em;text-transform:uppercase;color:var(--app-muted);">设备 ID</div>
        <div style="margin-top:0.3rem;font-size:0.9rem;word-break:break-all;color:var(--app-text);">${this._esc(deviceId)}</div>
        <div style="margin-top:1rem;font-size:0.8rem;font-weight:600;letter-spacing:0.1em;text-transform:uppercase;color:var(--app-muted);">绑定群聊</div>
        <div style="margin-top:0.3rem;font-size:0.9rem;color:var(--app-text);">${this._esc(convName)}</div>
        <button
          id="dp-finish-bind-btn"
          style="margin-top:1.2rem;width:100%;min-height:3rem;border:none;border-radius:1rem;background:var(--app-brand);padding:0.85rem;font-size:0.92rem;font-weight:600;color:#fff;"
        >完成</button>
      </section>
    `;
  },

  _bindDevicesBindFlowEvents() {
    // 关闭按钮（关闭整个面板）
    const closeBtn = document.getElementById('dp-close-btn');
    if (closeBtn) closeBtn.onclick = () => this._closeDevicesPanel();

    // 遮罩点击关闭
    const overlay = document.getElementById('dp-overlay');
    if (overlay) overlay.onclick = () => this._closeDevicesPanel();

    // 返回按钮（返回看板）
    const backBtn = document.getElementById('dp-bind-back-btn');
    if (backBtn) backBtn.onclick = () => this._closeDevicesBindFlow();

    // 按步骤绑定事件
    if (this.devicesBindStep === 'input') {
      const input = document.getElementById('dp-bind-input');
      if (input) {
        input.oninput = (e) => { this.devicesBindManualInput = e.target.value; };
        input.onkeydown = (e) => {
          if (e.key === 'Enter') { e.preventDefault(); this._submitDevicesBindToken(); }
        };
      }
      const submitBtn = document.getElementById('dp-bind-submit-btn');
      if (submitBtn) submitBtn.onclick = () => this._submitDevicesBindToken();
    } else if (this.devicesBindStep === 'confirm') {
      const confirmBtn = document.getElementById('dp-confirm-bind-btn');
      if (confirmBtn) confirmBtn.onclick = () => this._confirmDevicesBind();
    } else if (this.devicesBindStep === 'result') {
      const finishBtn = document.getElementById('dp-finish-bind-btn');
      if (finishBtn) finishBtn.onclick = () => this._finishDevicesBind();
    }
  },

  // ═══════════════════════════════════════════
  // 绑定流程业务操作（调用 Bridge）
  // ═══════════════════════════════════════════

  // 步骤 1 -> 2：解析输入并校验会话
  async _submitDevicesBindToken() {
    const raw = (this.devicesBindManualInput || '').trim();
    if (!raw) {
      this._setDevicesBindError('请先输入 bind_token');
      return;
    }

    // 解析多种格式（纯 token / deep link / JSON）
    const parsed = Bridge.parseDeviceBindPayload(raw);
    const bindToken = parsed?.bindToken || raw;

    // 按钮加载状态
    const btn = document.getElementById('dp-bind-submit-btn');
    if (btn) { btn.disabled = true; btn.textContent = '校验中…'; }

    try {
      const session = await Bridge.getDeviceBindSession(bindToken);
      // 校验会话状态
      if (!session) {
        this._setDevicesBindError('绑定会话不存在');
        if (btn) { btn.disabled = false; btn.textContent = '继续确认'; }
        return;
      }
      if (session.status === 'expired') {
        this._setDevicesBindError('绑定会话已过期');
        if (btn) { btn.disabled = false; btn.textContent = '继续确认'; }
        return;
      }
      if (session.status === 'bound') {
        this._setDevicesBindError('该设备已完成绑定');
        if (btn) { btn.disabled = false; btn.textContent = '继续确认'; }
        return;
      }
      // 校验通过，进入确认步骤
      this.devicesBindToken = bindToken;
      this.devicesBindSession = session;
      this.devicesBindStep = 'confirm';
      this.devicesBindError = '';
      this._refreshDevicesPanel();
    } catch (e) {
      this._setDevicesBindError(e.message || '校验失败，请检查绑定码');
      if (btn) { btn.disabled = false; btn.textContent = '继续确认'; }
    }
  },

  // 步骤 2 -> 3：确认绑定
  async _confirmDevicesBind() {
    const conv = Bridge.getActiveConversation();
    if (!conv) {
      this._setDevicesBindError('请先进入会话');
      return;
    }

    this.devicesBindConfirming = true;
    const btn = document.getElementById('dp-confirm-bind-btn');
    if (btn) { btn.disabled = true; btn.textContent = '绑定中…'; }

    try {
      const session = this.devicesBindSession || {};
      const deviceName = session.name || session.deviceName;
      const result = await Bridge.confirmDeviceBind({
        bindToken: this.devicesBindToken,
        conversationId: conv.id,
        conversationName: conv.name,
        deviceName,
      });
      // Bridge.confirmDeviceBind 返回 { ok: true, data: dev } 或直接 dev
      this.devicesBindResult = (result && result.data) ? result.data : result;
      this.devicesBindStep = 'result';
      this.devicesBindConfirming = false;
      this.devicesBindError = '';
      this._refreshDevicesPanel();
      Toast.success('设备已加入当前群聊');
    } catch (e) {
      this.devicesBindConfirming = false;
      if (btn) { btn.disabled = false; btn.textContent = '确认绑定'; }
      this._setDevicesBindError(e.message || '绑定失败，请稍后重试');
    }
  },

  // 步骤 3：完成，关闭面板返回聊天页
  _finishDevicesBind() {
    this._closeDevicesPanel();
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

  _deviceIconSvg(type) {
    if (type === 'tv') {
      return '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>';
    }
    return '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>';
  },
};

export default DevicesPanelMixin;
