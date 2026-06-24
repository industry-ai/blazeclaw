/* ================================================================
   AgentChat HTML 版 - 设备管理页面
   ================================================================ */

import ChatStore from '../stores/chatStore.js';
import Toast from '../utils/toast.js';
import TimeUtils from '../utils/time.js';

const DevicesPage = {
  container: null,
  unsub: null,
  selectedConversationId: '',

  init() {
    this.container = document.getElementById('page-devices');
    this.unsub = ChatStore.subscribe(() => this.render());
    void ChatStore.refreshBoundDevices();
    this.render();
  },

  render() {
    if (!this.container) return;
    const conversations = ChatStore.getBindableConversations();
    if (!this.selectedConversationId && conversations[0]) {
      this.selectedConversationId = conversations[0].id;
    }
    const devices = ChatStore.getBoundDevices();
    const pendingSessions = ChatStore.getPendingDeviceSessions();
    const onlineDevices = devices.filter(device => device.status === 'online');

    this.container.innerHTML = `
    <div class="devices-page">
      <div class="devices-header">
        <button class="devices-back-btn" id="devices-back-btn">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
        </button>
        <span class="devices-header-title">设备管理</span>
        <button class="devices-add-btn" id="devices-add-btn" title="生成绑定码">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 5v14M5 12h14"/></svg>
        </button>
      </div>

      <div style="padding:1rem 1rem calc(5rem + env(safe-area-inset-bottom));overflow-y:auto;">
        <section style="border:1px solid var(--app-border);border-radius:1.35rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
          <div style="font-size:1rem;font-weight:600;color:var(--app-text);">我的设备</div>
          <div style="margin-top:0.35rem;font-size:0.82rem;line-height:1.6;color:var(--app-muted);">这里管理电视等协同节点。生成绑定码后，可在设备端扫码，再把设备绑定到当前工作空间或群聊。</div>
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

        <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.35rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
          <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
            <div>
              <div style="font-size:1rem;font-weight:600;color:var(--app-text);">绑定入口</div>
              <div style="margin-top:0.25rem;font-size:0.8rem;color:var(--app-muted);">先选择目标会话，再生成绑定码或输入 bind_token。</div>
            </div>
            <button id="devices-refresh-btn" class="btn btn-secondary" style="padding:0.45rem 0.8rem;">刷新</button>
          </div>
          <label style="display:block;margin-top:0.85rem;">
            <select id="devices-target-conv" style="width:100%;height:2.65rem;border:1px solid var(--app-border);border-radius:0.95rem;background:var(--app-subtle-bg);padding:0 0.85rem;font-size:0.82rem;font-weight:600;color:var(--app-text);outline:none;">
              ${conversations.map(item => `<option value="${this._esc(item.id)}" ${item.id === this.selectedConversationId ? 'selected' : ''}>绑定到：${this._esc(item.name)}</option>`).join('')}
            </select>
          </label>
          <div style="margin-top:0.85rem;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:0.7rem;">
            <button id="devices-create-session-btn" class="btn btn-primary">生成电视绑定码</button>
            <button id="scan-bind-btn" class="btn btn-secondary">输入绑定码</button>
          </div>
        </section>

        <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.35rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
          <div style="font-size:1rem;font-weight:600;color:var(--app-text);">待绑定会话</div>
          <div style="margin-top:0.25rem;font-size:0.8rem;color:var(--app-muted);">在设备端扫码后，可回到这里确认绑定到目标会话。</div>
          ${pendingSessions.length === 0
            ? `<div style="margin-top:0.95rem;border:1px dashed var(--app-border);border-radius:1rem;padding:1rem;text-align:center;font-size:0.84rem;line-height:1.6;color:var(--app-muted);">暂无待绑定会话，先生成一个电视绑定码。</div>`
            : `<div style="margin-top:0.95rem;display:flex;flex-direction:column;gap:0.7rem;">
                ${pendingSessions.map(session => `
                  <article style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.9rem;">
                    <div style="font-size:0.9rem;font-weight:600;color:var(--app-text);">${this._esc(session.deviceName || '电视设备')}</div>
                    <div style="margin-top:0.3rem;font-size:0.78rem;color:var(--app-muted);">状态：${this._esc(session.status)} · 过期时间：${this._esc(TimeUtils.formatSmart(session.expireAt))}</div>
                    <div style="margin-top:0.55rem;font-size:0.76rem;color:var(--app-muted);word-break:break-all;">bind_token：${this._esc(session.bindToken)}</div>
                    <div style="margin-top:0.75rem;display:flex;gap:0.55rem;flex-wrap:wrap;">
                      <button class="btn btn-secondary" data-action="copy-token" data-bind-token="${this._esc(session.bindToken)}">复制令牌</button>
                      <button class="btn btn-primary" data-action="confirm-bind" data-bind-token="${this._esc(session.bindToken)}">绑定到当前会话</button>
                    </div>
                  </article>
                `).join('')}
              </div>`
          }
        </section>

        <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.35rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
          <div style="font-size:1rem;font-weight:600;color:var(--app-text);">已绑定设备</div>
          <div style="margin-top:0.25rem;font-size:0.8rem;color:var(--app-muted);">已绑定设备会在这里展示，并同步到工作台与 AI 空间。</div>
          ${devices.length === 0
            ? `<div style="margin-top:0.95rem;border:1px dashed var(--app-border);border-radius:1rem;padding:1rem;text-align:center;font-size:0.84rem;line-height:1.6;color:var(--app-muted);">暂无绑定设备，可先生成绑定码并完成确认绑定。</div>`
            : `<div style="margin-top:0.95rem;display:flex;flex-direction:column;gap:0.7rem;">
                ${devices.map(device => `
                  <article style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.9rem;">
                    <div style="display:flex;align-items:flex-start;justify-content:space-between;gap:0.75rem;">
                      <div>
                        <div style="font-size:0.9rem;font-weight:600;color:var(--app-text);">${this._esc(device.deviceName)}</div>
                        <div style="margin-top:0.3rem;font-size:0.78rem;color:var(--app-muted);">${this._esc(device.boundConversationName || '当前会话')} · ${device.status === 'online' ? '在线' : '离线'}</div>
                        <div style="margin-top:0.25rem;font-size:0.76rem;color:var(--app-muted);">绑定时间：${this._esc(TimeUtils.formatSmart(device.boundAt))}</div>
                      </div>
                      <button class="btn btn-secondary" data-action="unbind-device" data-device-id="${this._esc(device.deviceId)}">解绑</button>
                    </div>
                  </article>
                `).join('')}
              </div>`
          }
        </section>
      </div>
    </div>`;
    this._bindEvents();
  },

  _bindEvents() {
    document.getElementById('devices-back-btn').onclick = () => { window.location.hash = '#/chat'; };
    document.getElementById('devices-refresh-btn').onclick = async () => {
      await ChatStore.refreshBoundDevices();
      Toast.success('设备列表已刷新');
    };
    document.getElementById('devices-target-conv').onchange = (event) => {
      this.selectedConversationId = event.target.value;
    };
    document.getElementById('devices-add-btn').onclick = () => this._createBindSession();
    document.getElementById('devices-create-session-btn').onclick = () => this._createBindSession();
    document.getElementById('scan-bind-btn').onclick = () => this._confirmFromInput();

    this.container.querySelectorAll('[data-action="copy-token"]').forEach(el => {
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

    this.container.querySelectorAll('[data-action="confirm-bind"]').forEach(el => {
      el.onclick = async () => {
        await this._confirmBind(el.dataset.bindToken);
      };
    });

    this.container.querySelectorAll('[data-action="unbind-device"]').forEach(el => {
      el.onclick = async () => {
        const deviceId = el.dataset.deviceId;
        if (!deviceId) return;
        if (!confirm('确定解绑该设备吗？')) return;
        try {
          await ChatStore.unbindDevice(deviceId);
          Toast.success('设备已解绑');
        } catch (e) {
          Toast.warn(e.message || '解绑失败');
        }
      };
    });
  },

  async _createBindSession() {
    try {
      const name = prompt('请输入设备名称', 'Living Room TV');
      if (name === null) return;
      const session = await ChatStore.createDeviceBindSession(name || 'Living Room TV');
      Toast.success(`已生成绑定码：${session.bindToken}`);
    } catch (e) {
      Toast.warn(e.message || '生成绑定码失败');
    }
  },

  async _confirmFromInput() {
    const raw = prompt('请输入扫码结果或 bind_token');
    if (!raw) return;
    const parsed = ChatStore.parseDeviceBindPayload(raw);
    const bindToken = parsed?.bindToken || String(raw).trim();
    await this._confirmBind(bindToken);
  },

  async _confirmBind(bindToken) {
    const target = ChatStore.getBindableConversations().find(item => item.id === this.selectedConversationId);
    if (!target) {
      Toast.warn('请先选择目标会话');
      return;
    }
    try {
      await ChatStore.getDeviceBindSession(bindToken);
      await ChatStore.confirmDeviceBind({
        bindToken,
        conversationId: target.id,
        conversationName: target.name,
      });
      Toast.success(`设备已绑定到 ${target.name}`);
    } catch (e) {
      Toast.warn(e.message || '设备绑定失败');
    }
  },

  _esc(str) {
    return String(str || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  },

  destroy() {
    if (this.unsub) {
      this.unsub();
      this.unsub = null;
    }
  },
};

export default DevicesPage;
