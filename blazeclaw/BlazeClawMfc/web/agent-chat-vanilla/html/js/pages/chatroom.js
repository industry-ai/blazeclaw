/* ================================================================
   话题群聊 - 主页面（静态假数据展示版）
   ----------------------------------------------------------------
   功能：
   1. 展示当前用户参与的话题群聊列表
   2. 每个群聊展示成员列表，标记管理员
   3. 管理员可：移除成员、修改群聊标题/描述、解散群聊
   4. 当前用户可退出话题群聊
   假数据与状态管理由 irc/mock.js 提供。
   ================================================================ */

import Toast from "../utils/toast.js";
import TimeUtils from "../utils/time.js";
import AvatarSwatch from "../utils/avatar.js";
import {
  MOCK_NICK,
  MOCK_IS_GLOBAL_OP,
  mockChannels,
  mockMessages,
  getActiveChannel,
  setActiveChannel,
  subscribe,
  notify,
} from "../irc/mock.js";

const ChatroomPage = {
  container: null,
  composerText: "",
  _scrollLocked: false,
  // 弹窗
  memberMenuNick: null,
  _menuX: 0,
  _menuY: 0,
  editChannelOpen: false,
  editTitle: "",
  editDesc: "",
  dissolveConfirmOpen: false,
  leaveConfirmOpen: false,

  init() {
    this.container = document.getElementById("page-chatroom");

    this._renderLayout();
    this._bindGlobalEvents();
    this._unsub = subscribe(() => this._onStateChange());

    // 默认选中第一个频道
    if (mockChannels.length && !getActiveChannel()) {
      setActiveChannel(mockChannels[0].name);
    }
    this._renderAll();
  },

  destroy() {
    if (this._unsub) {
      this._unsub();
      this._unsub = null;
    }
    if (this._clickHandler) {
      this.container.removeEventListener("click", this._clickHandler);
      this._clickHandler = null;
    }
    this.container.innerHTML = "";
  },

  _onStateChange() {
    this._renderChannelList();
    this._renderHeader();
    this._renderMessages();
    this._renderMembersPanel();
  },

  _renderAll() {
    this._renderChannelList();
    this._renderHeader();
    this._renderMessages();
    this._renderMembersPanel();
  },

  // ================================================================
  // 布局骨架
  // ================================================================
  _renderLayout() {
    this.container.innerHTML = `
      <div class="irc-shell">
        <aside class="irc-sidebar" id="irc-sidebar"></aside>
        <section class="irc-main" id="irc-main">
          <header class="irc-header" id="irc-header"></header>
          <div class="irc-messages" id="irc-messages"></div>
          <footer class="irc-composer">
            <input type="text" class="irc-composer-input" id="irc-composer-input"
              placeholder="输入消息…" autocomplete="off" />
            <button class="irc-composer-send" id="irc-send" title="发送">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/></svg>
            </button>
          </footer>
        </section>
        <aside class="irc-aside" id="irc-aside"></aside>
      </div>
      <div id="irc-modal-root"></div>
    `;
  },

  // ================================================================
  // 左栏：话题群聊列表
  // ================================================================
  _renderChannelList() {
    const el = document.getElementById("irc-sidebar");
    if (!el) return;
    const channels = mockChannels;

    el.innerHTML = `
      <div class="irc-sidebar-head">
        <span class="irc-sidebar-title">话题群聊</span>
      </div>
      <div class="irc-channel-list">
        ${
          channels.length === 0
            ? '<div class="irc-empty-mini">暂无已加入的话题群聊</div>'
            : channels
                .map((c) => {
                  return `
          <div class="irc-channel-item ${getActiveChannel() === c.name ? "active" : ""}" data-channel="${this._esc(c.name)}">
            <div class="irc-channel-hash">#</div>
            <div class="irc-channel-info">
              <div class="irc-channel-name">${this._esc(c.title || c.name.slice(1))}</div>
              ${c.topic ? `<div class="irc-channel-topic">${this._esc(c.topic)}</div>` : ""}
            </div>
            <div class="irc-channel-meta">
              <span class="irc-channel-count">${c.members.length}</span>
            </div>
          </div>
        `;
                })
                .join("")
        }
      </div>
    `;
  },

  // ================================================================
  // 中栏头部：群聊标题/描述 + 操作按钮
  // ================================================================
  _renderHeader() {
    const el = document.getElementById("irc-header");
    if (!el) return;
    const ch = getActiveChannel() ? this._getChannel(getActiveChannel()) : null;
    if (!ch) {
      el.innerHTML = `<div class="irc-header-empty">选择一个话题群聊开始聊天</div>`;
      return;
    }
    const isAdmin = MOCK_IS_GLOBAL_OP || this._isOperator(getActiveChannel(), MOCK_NICK);

    el.innerHTML = `
      <div class="irc-header-main">
        <div class="irc-header-title">${this._esc(ch.title || ch.name.slice(1))}</div>
      </div>
      <div class="irc-header-actions">
        ${
          isAdmin
            ? `
          <button class="irc-header-btn" data-action="edit-channel" title="修改标题和描述">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20h9"/><path d="M16.5 3.5a2.121 2.121 0 013 3L7 19l-4 1 1-4L16.5 3.5z"/></svg>
          </button>
          <button class="irc-header-btn irc-header-btn-danger" data-action="dissolve" title="解散群聊">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h18"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6"/><path d="M8 6V4a2 2 0 012-2h4a2 2 0 012 2v2"/></svg>
          </button>
        `
            : `
          <button class="irc-header-btn irc-header-btn-danger" data-action="leave" title="退出群聊">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 21H5a2 2 0 01-2-2V5a2 2 0 012-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>
          </button>
        `
        }
      </div>
    `;
  },

  // ================================================================
  // 中栏消息流
  // ================================================================
  _renderMessages() {
    const el = document.getElementById("irc-messages");
    if (!el) return;
    if (!getActiveChannel()) {
      el.innerHTML = `<div class="irc-empty-state">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" style="width:48px;height:48px;margin:0 auto 12px;"><path d="M21 15a2 2 0 01-2 2H7l-4 4V5a2 2 0 012-2h14a2 2 0 012 2z"/></svg>
        <div>选择左侧话题群聊开始聊天</div>
      </div>`;
      return;
    }
    const msgs = mockMessages[getActiveChannel()] || [];
    el.innerHTML = `<div class="irc-msg-list">${msgs.map((m) => this._renderMessage(m)).join("")}</div>`;
    if (!this._scrollLocked) {
      el.scrollTop = el.scrollHeight;
    }
  },

  _renderMessage(m) {
    if (m.isSystem || m.type === "system") {
      return `<div class="irc-msg-system">${this._esc(m.text)}</div>`;
    }
    const mine = m.from === MOCK_NICK;
    const avatar = AvatarSwatch.render(m.from, m.from, 32);
    return `
      <div class="irc-msg ${mine ? "mine" : ""}">
        ${mine ? "" : avatar}
        <div class="irc-msg-body">
          <div class="irc-msg-meta">
            <span class="irc-msg-author">${this._esc(m.from)}</span>
            <span class="irc-msg-time">${TimeUtils.formatMdHm(m.ts)}</span>
          </div>
          <div class="irc-msg-text">${this._linkify(this._esc(m.text))}</div>
        </div>
      </div>
    `;
  },

  // ================================================================
  // 右栏：成员列表
  // ================================================================
  _renderMembersPanel() {
    const el = document.getElementById("irc-aside");
    if (!el) return;
    if (!getActiveChannel()) {
      el.innerHTML = "";
      return;
    }

    const ch = this._getChannel(getActiveChannel());
    const members = ((ch && ch.members) || []).slice().sort((a, b) => {
      const ra = a.mode.operator ? 0 : 1;
      const rb = b.mode.operator ? 0 : 1;
      if (ra !== rb) return ra - rb;
      return (a.joinOrder || 0) - (b.joinOrder || 0);
    });
    const isAdmin = MOCK_IS_GLOBAL_OP || this._isOperator(getActiveChannel(), MOCK_NICK);

    el.innerHTML = `
      <div class="irc-aside-head">
        <span class="irc-aside-title">成员 (${members.length})</span>
      </div>
      <div class="irc-aside-body">
        ${members
          .map((m) => {
            const isMe = m.nick === MOCK_NICK;
            const memberIsAdmin = m.mode.operator;
            return `
            <div class="irc-member-item" data-nick="${this._esc(m.nick)}">
              ${AvatarSwatch.render(m.nick, m.nick, 28)}
              <div class="irc-member-info">
                <span class="irc-member-name">${this._esc(m.nick)}${isMe ? " (我)" : ""}</span>
                ${memberIsAdmin ? '<span class="irc-member-admin-badge">管理员</span>' : ""}
              </div>
              ${
                isAdmin && !isMe
                  ? `<button class="irc-member-menu-btn" data-nick="${this._esc(m.nick)}" title="管理">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="5" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="12" cy="19" r="1"/></svg>
              </button>`
                  : ""
              }
            </div>
          `;
          })
          .join("")}
      </div>
    `;
  },

  // ================================================================
  // 弹窗
  // ================================================================
  _renderModal() {
    const root = document.getElementById("irc-modal-root");
    if (!root) return;

    // 成员管理菜单
    if (this.memberMenuNick) {
      const targetIsAdmin = this._isOperator(getActiveChannel(), this.memberMenuNick);
      const x = this._menuX || 0;
      const y = this._menuY || 0;
      root.innerHTML = `
        <div class="irc-menu-overlay" data-action="close-menu"></div>
        <div class="irc-context-menu" style="left:${x}px;top:${y}px;">
          <button class="irc-menu-item" data-action="toggle-admin" data-nick="${this._esc(this.memberMenuNick)}">${targetIsAdmin ? "取消管理员" : "设为管理员"}</button>
          <button class="irc-menu-item danger" data-action="remove-member" data-nick="${this._esc(this.memberMenuNick)}">移除成员</button>
        </div>
      `;
      return;
    }

    // 修改标题和描述
    if (this.editChannelOpen) {
      root.innerHTML = `
        <div class="modal-overlay" id="irc-modal-overlay">
          <div class="modal-card">
            <div class="modal-header">
              <h3>修改群聊信息</h3>
              <button class="icon-btn" data-action="close-modal">✕</button>
            </div>
            <div class="irc-modal-body">
              <label class="irc-field-label">群聊标题</label>
              <input type="text" class="irc-field-input" id="irc-edit-title"
                value="${this._esc(this.editTitle)}" placeholder="群聊标题" />
              <label class="irc-field-label">群聊描述</label>
              <textarea class="irc-field-input irc-field-textarea" id="irc-edit-desc"
                placeholder="群聊描述…">${this._esc(this.editDesc)}</textarea>
            </div>
            <div class="irc-modal-foot">
              <button class="btn btn-secondary" data-action="close-modal">取消</button>
              <button class="btn btn-primary" data-action="confirm-edit">保存</button>
            </div>
          </div>
        </div>
      `;
      return;
    }

    // 解散群聊确认
    if (this.dissolveConfirmOpen) {
      root.innerHTML = `
        <div class="modal-overlay" id="irc-modal-overlay">
          <div class="modal-card">
            <div class="modal-header">
              <h3>解散群聊</h3>
              <button class="icon-btn" data-action="close-modal">✕</button>
            </div>
            <div class="irc-modal-body">
              <p>确定要解散这个话题群聊吗？解散后所有成员将被移除，聊天记录将清空，此操作不可撤销。</p>
            </div>
            <div class="irc-modal-foot">
              <button class="btn btn-secondary" data-action="close-modal">取消</button>
              <button class="btn btn-danger" data-action="confirm-dissolve">确认解散</button>
            </div>
          </div>
        </div>
      `;
      return;
    }

    // 退出群聊确认
    if (this.leaveConfirmOpen) {
      root.innerHTML = `
        <div class="modal-overlay" id="irc-modal-overlay">
          <div class="modal-card">
            <div class="modal-header">
              <h3>退出群聊</h3>
              <button class="icon-btn" data-action="close-modal">✕</button>
            </div>
            <div class="irc-modal-body">
              <p>确定要退出这个话题群聊吗？退出后将不再接收该群聊的消息。</p>
            </div>
            <div class="irc-modal-foot">
              <button class="btn btn-secondary" data-action="close-modal">取消</button>
              <button class="btn btn-danger" data-action="confirm-leave">确认退出</button>
            </div>
          </div>
        </div>
      `;
      return;
    }

    root.innerHTML = "";
  },

  // ================================================================
  // 事件绑定
  // ================================================================
  _bindGlobalEvents() {
    this._clickHandler = (e) => this._onClick(e);
    this.container.addEventListener("click", this._clickHandler);

    const input = document.getElementById("irc-composer-input");
    if (input) {
      input.addEventListener("input", (e) => {
        this.composerText = e.target.value;
      });
      input.addEventListener("keydown", (e) => {
        if (e.key === "Enter" && !e.shiftKey) {
          e.preventDefault();
          this._sendMessage();
        }
      });
    }

    const sendBtn = document.getElementById("irc-send");
    if (sendBtn) sendBtn.addEventListener("click", () => this._sendMessage());

    const msgs = document.getElementById("irc-messages");
    if (msgs) {
      msgs.addEventListener("scroll", () => {
        this._scrollLocked =
          msgs.scrollTop + msgs.clientHeight < msgs.scrollHeight - 40;
      });
    }
  },

  _onClick(e) {
    // 成员菜单按钮（三点）
    const menuBtn = e.target.closest(".irc-member-menu-btn");
    if (menuBtn) {
      e.stopPropagation();
      const rect = menuBtn.getBoundingClientRect();
      this._menuX = rect.left - 140;
      this._menuY = rect.bottom + 4;
      this.memberMenuNick = menuBtn.dataset.nick;
      this._renderModal();
      return;
    }

    const target = e.target.closest("[data-action], [data-channel]");
    if (!target) return;

    // 频道切换
    if (target.dataset.channel) {
      this._scrollLocked = false;
      setActiveChannel(target.dataset.channel);
      notify();
      return;
    }

    const action = target.dataset.action;
    const nick = target.dataset.nick;

    switch (action) {
      case "edit-channel":
        this._openEditChannel();
        break;
      case "dissolve":
        this.dissolveConfirmOpen = true;
        this._renderModal();
        break;
      case "leave":
        this.leaveConfirmOpen = true;
        this._renderModal();
        break;
      case "confirm-edit":
        this._handleEditChannel();
        break;
      case "confirm-dissolve":
        this._handleDissolve();
        break;
      case "confirm-leave":
        this._handleLeave();
        break;
      case "remove-member":
        this._handleRemoveMember(nick);
        break;
      case "toggle-admin":
        this._handleToggleAdmin(nick);
        break;
      case "close-modal":
        this._closeAllModals();
        break;
      case "close-menu":
        this.memberMenuNick = null;
        this._renderModal();
        break;
      default:
        break;
    }
  },

  // ================================================================
  // 业务操作（本地假数据）
  // ================================================================
  _sendMessage() {
    const input = document.getElementById("irc-composer-input");
    const text = (input ? input.value : this.composerText).trim();
    if (!text) return;
    if (!getActiveChannel()) {
      Toast.show("请先选择话题群聊", "warn");
      return;
    }

    // 本地追加消息
    if (!mockMessages[getActiveChannel()]) mockMessages[getActiveChannel()] = [];
    mockMessages[getActiveChannel()].push({
      id: `local-${Date.now()}`,
      from: MOCK_NICK,
      text,
      ts: Date.now(),
    });
    notify();

    if (input) {
      input.value = "";
      this.composerText = "";
    }
  },

  _openEditChannel() {
    const ch = this._getChannel(getActiveChannel());
    if (!ch) return;
    this.editTitle = ch.title || ch.name.slice(1);
    this.editDesc = ch.topic || "";
    this.editChannelOpen = true;
    this._renderModal();
  },

  _handleEditChannel() {
    const title = (document.getElementById("irc-edit-title") || {}).value || "";
    const desc = (document.getElementById("irc-edit-desc") || {}).value || "";
    if (!title.trim()) {
      Toast.show("请输入标题", "warn");
      return;
    }
    const ch = this._getChannel(getActiveChannel());
    if (ch) {
      ch.title = title.trim();
      ch.topic = desc.trim();
    }
    Toast.show("群聊信息已更新", "success");
    this._closeAllModals();
    notify();
  },

  _handleDissolve() {
    const idx = mockChannels.findIndex((c) => c.name === getActiveChannel());
    if (idx >= 0) mockChannels.splice(idx, 1);
    delete mockMessages[getActiveChannel()];
    Toast.show("群聊已解散", "success");
    this._closeAllModals();
    setActiveChannel(mockChannels.length ? mockChannels[0].name : null);
    notify();
  },

  _handleLeave() {
    const idx = mockChannels.findIndex((c) => c.name === getActiveChannel());
    if (idx >= 0) mockChannels.splice(idx, 1);
    delete mockMessages[getActiveChannel()];
    Toast.show("已退出群聊", "success");
    this._closeAllModals();
    setActiveChannel(mockChannels.length ? mockChannels[0].name : null);
    notify();
  },

  _handleRemoveMember(nick) {
    const ch = this._getChannel(getActiveChannel());
    if (ch) {
      ch.members = ch.members.filter((m) => m.nick !== nick);
      ch.operators = ch.operators.filter((n) => n !== nick);
    }
    this.memberMenuNick = null;
    this._renderModal();
    Toast.show(`${nick} 已被移除`, "success");
    notify();
  },

  _handleToggleAdmin(nick) {
    const ch = this._getChannel(getActiveChannel());
    if (ch) {
      const m = ch.members.find((x) => x.nick === nick);
      if (m) {
        m.mode.operator = !m.mode.operator;
        if (m.mode.operator && !ch.operators.includes(nick)) {
          ch.operators.push(nick);
        } else if (!m.mode.operator) {
          ch.operators = ch.operators.filter((n) => n !== nick);
        }
      }
    }
    this.memberMenuNick = null;
    this._renderModal();
    const isOp = ch && ch.operators.includes(nick);
    Toast.show(isOp ? `${nick} 已设为管理员` : `${nick} 已取消管理员`, "success");
    notify();
  },

  _closeAllModals() {
    this.editChannelOpen = false;
    this.dissolveConfirmOpen = false;
    this.leaveConfirmOpen = false;
    this.memberMenuNick = null;
    this._renderModal();
  },

  // ================================================================
  // 工具
  // ================================================================
  _getChannel(name) {
    return mockChannels.find((c) => c.name === name) || null;
  },
  _isOperator(channel, nick) {
    const c = this._getChannel(channel);
    return !!(c && c.operators && c.operators.includes(nick));
  },
  _esc(s) {
    return String(s == null ? "" : s).replace(
      /[&<>"']/g,
      (c) =>
        ({
          "&": "&amp;",
          "<": "&lt;",
          ">": "&gt;",
          '"': "&quot;",
          "'": "&#39;",
        })[c],
    );
  },
  _linkify(text) {
    return text.replace(
      /(https?:\/\/[^\s<]+)/g,
      (url) => `<a href="${url}" target="_blank" rel="noopener">${url}</a>`,
    );
  },
};

export default ChatroomPage;
