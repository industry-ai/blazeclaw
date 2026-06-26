/* ================================================================
   AgentChat HTML 版 - 聊天主页面 (精确匹配原 Vue 版样式)
   ================================================================ */

import ChatStore from '../stores/chatStore.js';
import UiStore from '../stores/uiStore.js';
import AuthStore from '../stores/authStore.js';
import ChatApi from '../api/chatApi.js';
import Toast from '../utils/toast.js';
import TimeUtils from '../utils/time.js';
import MarkdownRenderer from '../utils/markdown.js';
import AvatarSwatch from '../utils/avatar.js';

const ChatPage = {
  searchQuery: '',
  searchOpen: false,
  container: null,
  unsubChat: null,
  unsubUi: null,
  activeTab: '聊天',
  groupMembers: [],
  groupMembersLoading: false,
  groupMembersLoaded: false,
  groupMembersError: '',
  groupMembersNotice: '',
  invitePhone: '',
  invitePanelOpen: false,
  postsPanelOpen: false,
  postsPanelView: 'board',
  postsLoading: false,
  postsError: '',
  postCreatorOpen: false,
  postCreatorSubmitting: false,
  postCreatorType: 'homework',
  postCreatorTitle: '',
  postCreatorSummary: '',
  postCreatorDeadline: '',
  postCreatorResourceUrl: '',
  postCreatorFeedbackEnabled: true,
  postDetailOpen: false,
  postDetailPost: null,
  postDetailResponseText: '',
  postDetailSelectedFile: null,
  workspacePublishTargetId: '',
  composerDrafts: {},
  pendingScrollToBottom: false,
  pendingScrollConvId: null,
  _lastRenderedConvId: null,
  suppressComposerFocusRestore: false,
  createGroupSubmitting: false,
  // 语音输入状态（对齐 Vue 版 useVoiceTextInput）
  isVoiceMode: false,
  isPreparingVoiceMode: false,
  voiceState: 'idle', // 'idle' | 'starting' | 'listening' | 'processing'
  voiceErrorMessage: '',
  voiceElapsedSeconds: 0,
  createGroupError: '',
  createGroupDialogOpen: false,
  createGroupName: '',
  // 删除群聊确认弹窗状态（对齐 Vue 版 onDeleteConversation）
  deleteConversationDialogOpen: false,
  deleteConversationTargetId: '',
  deleteConversationTargetName: '',

  init() {
    this.container = document.getElementById('page-chat');
    this._scheduleRender = this._debounce(() => {
      if (this.createGroupDialogOpen || this.postCreatorOpen || this.postDetailOpen || this.postsPanelOpen) return;
      this.render();
    }, 80);
    this.unsubChat = ChatStore.subscribe(() => this._scheduleRender());
    this.unsubUi = UiStore.subscribe(() => this._scheduleRender());
    const activeId = ChatStore.getActiveConversationId();
    if (activeId && UiStore.getActiveConversationId() !== activeId) {
      UiStore.setActiveConversation(activeId, true);
      UiStore.resetToChatView();
    }
    // 创建群聊弹窗挂载到 body（对齐 Vue teleport：不受页面 innerHTML 重绘影响）
    this._ensureDialogElement();
    this.render();
  },

  _ensureDialogElement() {
    if (!document.getElementById('create-group-dialog')) {
      const dialog = document.createElement('div');
      dialog.id = 'create-group-dialog';
      dialog.className = 'modal-overlay';
      dialog.style.display = 'none';
      document.body.appendChild(dialog);
    }
    if (!document.getElementById('delete-conversation-dialog')) {
      const dialog = document.createElement('div');
      dialog.id = 'delete-conversation-dialog';
      dialog.className = 'modal-overlay';
      dialog.style.display = 'none';
      document.body.appendChild(dialog);
    }
  },

  _activateConversation(conversationId, options = {}) {
    const id = String(conversationId || '').trim();
    if (!id) return;
    ChatStore.setActiveConversation(id);
    UiStore.setActiveConversation(id, options.preventNavigation === true);
    if (!options.skipMarkRead) {
      ChatStore.clearMarkRead(id);
    }
  },

  /** 防抖：短时间内多次变化只触发一次 render，避免 innerHTML 频繁销毁 DOM */
  _debounce(fn, delay) {
    let timer = null;
    return function() {
      if (timer) clearTimeout(timer);
      timer = setTimeout(() => { timer = null; fn.call(this); }, delay);
    };
  },

  /** 立即渲染（跳过防抖），用于用户操作后的即时反馈 */
  _renderNow() {
    this.render();
  },

  // ── 主渲染入口 ──
  render() {
    if (!this.container) return;
    const isDesktop = this._isDesktop();
    // 检测会话切换：上次渲染的会话与当前不同时，标记需要滚动到底部
    const currentConvId = ChatStore.getActiveConversationId();
    if (this._lastRenderedConvId && this._lastRenderedConvId !== currentConvId) {
      this.pendingScrollConvId = currentConvId;
    }
    const composerState = this._captureComposerState();
    const postsPanelState = this._capturePostsPanelState();
    const scrollState = this._captureScrollState();

    this.container.innerHTML = `
      <div style="position:relative;display:flex;height:100%;min-height:0;width:100%;flex-direction:column;overflow:hidden;color:var(--app-text);">
        <div class="backdrop-glow">
          <div style="left:-12%;top:4%;width:28rem;height:28rem;border-radius:50%;background:radial-gradient(circle,rgba(109,61,247,0.16),transparent 62%);filter:blur(64px);position:absolute;"></div>
          <div style="bottom:-10%;right:-10%;width:24rem;height:24rem;border-radius:50%;background:radial-gradient(circle,rgba(91,46,240,0.12),transparent 60%);filter:blur(64px);position:absolute;"></div>
        </div>
        ${isDesktop ? this._renderDesktopShell() : this._renderMobileShell()}
        ${this._renderPostsPanel()}
        ${this._renderPostCreator()}
        ${this._renderTaskDetail()}
      </div>
    `;

    this._bindDesktopEvents();
    this._bindMobileEvents();
    this._bindComposerEvents();
    this._bindMessagePostCards();
    this._bindRetryButtons();
    this._bindPostsPanelEvents();
    this._bindPostCreatorEvents();
    this._bindTaskDetailEvents();
    this._restoreComposerState(composerState);
    this._restorePostsPanelState(postsPanelState);
    this._restoreScrollState(scrollState);
    this._lastRenderedConvId = ChatStore.getActiveConversationId();

    // 对齐 Vue 版 ui.openTask：检查是否有待打开的任务详情
    const pendingTask = UiStore.consumePendingTask();
    if (pendingTask && pendingTask.type === 'native_post' && pendingTask.postId) {
      this._openPostDetail(pendingTask.postId);
    }
  },

  _captureScrollState() {
    const msgStream = this.container?.querySelector('.msg-stream');
    const convList = this.container?.querySelector('.conv-list');
    return {
      msgStream: msgStream ? msgStream.scrollTop : 0,
      convList: convList ? convList.scrollTop : 0,
    };
  },

  _restoreScrollState(state) {
    if (!state) return;
    // 发送消息后自动滚动到底部
    if (this.pendingScrollToBottom) {
      this.pendingScrollToBottom = false;
      const msgStream = this.container?.querySelector('.msg-stream');
      if (msgStream) {
        requestAnimationFrame(() => { msgStream.scrollTop = msgStream.scrollHeight; });
        return;
      }
    }
    // 切换会话后滚动到底部（消息可能异步加载，需多次尝试）
    if (this.pendingScrollConvId) {
      const msgStream = this.container?.querySelector('.msg-stream');
      if (msgStream) {
        const tryScroll = () => {
          if (msgStream.scrollHeight > msgStream.clientHeight) {
            msgStream.scrollTop = msgStream.scrollHeight;
            this.pendingScrollConvId = null;
          }
        };
        requestAnimationFrame(tryScroll);
        // 消息异步加载，二次 RAF 确保内容已渲染
        requestAnimationFrame(() => requestAnimationFrame(tryScroll));
      }
      if (state.convList != null) {
        const el = this.container?.querySelector('.conv-list');
        if (el) el.scrollTop = state.convList;
      }
      return;
    }
    if (state.msgStream != null) {
      const el = this.container?.querySelector('.msg-stream');
      if (el) el.scrollTop = state.msgStream;
    }
    if (state.convList != null) {
      const el = this.container?.querySelector('.conv-list');
      if (el) el.scrollTop = state.convList;
    }
  },

  // ═══════════════════════════════════════════
  // DESKTOP SHELL (match ChatShellDesktopPane)
  // ═══════════════════════════════════════════
  _renderDesktopShell() {
    const convs = ChatStore.getConversations();
    const activeId = ChatStore.getActiveConversationId();
    const activeConv = ChatStore.getActiveConversation();
    const messages = ChatStore.getActiveMessages();
    const isAgent = this._isAgentConv(activeConv);
    const isGroup = activeConv?.type === 'group';

    return `
    <div class="desktop-shell" style="position:relative;z-index:10;margin:1rem;height:calc(100dvh - 2rem);display:grid;grid-template-columns:21.5rem minmax(0,1fr) 0;overflow:hidden;border-radius:2rem;border:1px solid rgba(255,255,255,0.45);background:var(--app-surface);box-shadow:0 28px 70px rgba(95,73,170,0.12);backdrop-filter:blur(24px);">
      <!-- Sidebar -->
      <aside class="ds-sidebar" style="display:flex;flex-direction:column;min-height:0;border-right:1px solid rgba(226,232,240,0.7);background:var(--app-surface-strong);">
        ${this._renderSidebar(convs, activeId)}
      </aside>

      <!-- Main -->
      <main class="ds-main" style="display:flex;flex-direction:column;min-height:0;min-width:0;background:var(--app-page-bg);">
        ${activeConv ? this._renderDesktopHeader(activeConv, isAgent, isGroup) : this._emptyMain()}
        ${activeConv ? this._renderMessageStream(messages, activeConv) : ''}
        ${activeConv ? this._renderDesktopComposer(activeConv) : ''}
      </main>
    </div>`;
  },

  _renderDesktopHeader(conv, isAgent, isGroup) {
    const iconBg = isAgent
      ? 'background:linear-gradient(135deg,#eff1ff,#ded6ff);'
      : isGroup
        ? 'background:linear-gradient(135deg,#edf8ff,#dff1ff);'
        : 'background:linear-gradient(135deg,#eef6ff,#e2eaff);';
    const iconColor = isAgent ? 'color:var(--app-brand);' : isGroup ? 'color:#0284c7;' : 'color:#475569;';
    const subtitle = this._convSubtitle(conv);

    return `
    <header class="ds-header" style="flex-shrink:0;border-bottom:1px solid rgba(226,232,240,0.7);padding:1.75rem 1.75rem 1rem 1.75rem;">
      <div class="ds-header-inner" style="display:flex;align-items:flex-start;justify-content:space-between;gap:1.5rem;">
        <div style="min-width:0;">
          <div style="display:flex;align-items:center;gap:1rem;">
            <div class="ds-conv-icon" style="${iconBg}">
              ${isAgent ? `<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="${iconColor}"><rect x="3" y="3" width="18" height="18" rx="3"/><path d="M9 10a3 3 0 016 0v2a3 3 0 01-6 0V10z"/><circle cx="12" cy="17" r="1.5"/></svg>` : isGroup ? `<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="${iconColor}"><line x1="4" y1="9" x2="20" y2="9"/><line x1="4" y1="15" x2="20" y2="15"/><line x1="10" y1="3" x2="8" y2="21"/><line x1="16" y1="3" x2="14" y2="21"/></svg>` : `<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="${iconColor}"><path d="M20 21v-2a4 4 0 00-4-4H8a4 4 0 00-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>`}
              <span class="online-dot"></span>
            </div>
            <div style="min-width:0;">
              <div class="ds-title" style="font-size:1.4rem;font-weight:600;letter-spacing:-0.04em;color:#152047;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(conv.name)}</div>
              <div class="ds-subtitle" style="margin-top:0.25rem;display:flex;align-items:center;gap:0.75rem;font-size:0.85rem;color:var(--app-muted);">
                <span>${subtitle}</span>
                ${this._renderConnectionLabel()}
              </div>
            </div>
          </div>
        </div>
        <div class="ds-actions" style="display:flex;flex-shrink:0;align-items:center;gap:0.75rem;padding-top:0.25rem;">
          ${this._dsActionBtn(`<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/></svg>`, 'group-board-btn')}
          ${this._dsActionBtn(`<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>`, 'group-bind-btn')}
        </div>
      </div>
    </header>`;
  },

  _emptyMain() {
    return `<div style="display:flex;align-items:center;justify-content:center;flex:1;font-size:0.9rem;color:var(--app-muted);">选择一个会话开始聊天</div>`;
  },

  _dsActionBtn(icon, id = '') {
    return `<button class="ds-action-btn" ${id ? `id="${id}"` : ''}>${icon}</button>`;
  },

  // ═══════════════════════════════════════════
  // MOBILE SHELL (match ChatShellMobilePane)
  // ═══════════════════════════════════════════

  /**
   * 设备在线状态指示器（对齐移动端 AppMobilePrimaryHeader 的 status-dot/status-text）：
   * - connected → 绿色圆点
   * - reconnecting → 琥珀色"重连中"徽章 + 转圈
   * - 其他（connecting/disconnected/error）→ 不显示
   */
  _renderStatusIndicator() {
    const status = ChatStore.getStatus();
    if (status === 'connected') {
      return `<span class="status-dot connected" aria-hidden="true"></span>`;
    }
    if (status === 'reconnecting') {
      return `
        <span class="status-badge warning">
          <svg class="status-spin" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><path d="M21 12a9 9 0 11-6.219-8.56"/></svg>
          <span>重连中</span>
        </span>`;
    }
    return '';
  },

  /**
   * 桌面 header 副标题区域的连接状态标签：
   * - connected → 绿点 + "在线"
   * - reconnecting → 琥珀色点（转圈）+ "重连中"
   * - connecting → 灰点 + "连接中"
   * - disconnected/error → 灰点 + "离线"
   */
  _renderConnectionLabel() {
    const status = ChatStore.getStatus();
    const map = {
      connected: { color: '#10b981', text: '在线', spin: false },
      reconnecting: { color: 'var(--app-warning)', text: '重连中', spin: true },
      connecting: { color: 'var(--app-muted)', text: '连接中', spin: true },
      disconnected: { color: 'var(--app-muted)', text: '离线', spin: false },
      error: { color: 'var(--app-muted)', text: '离线', spin: false },
    };
    const cfg = map[status] || map.disconnected;
    const spinSvg = cfg.spin
      ? `<svg class="status-spin" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" style="display:inline-flex;"><path d="M21 12a9 9 0 11-6.219-8.56"/></svg>`
      : `<span style="display:inline-flex;width:8px;height:8px;border-radius:50%;background:${cfg.color};"></span>`;
    return `
      <span style="display:inline-flex;align-items:center;gap:0.375rem;color:${cfg.color};">
        ${spinSvg}
        <span>${cfg.text}</span>
      </span>`;
  },

  _renderMobileShell() {
    const convs = ChatStore.getConversations();
    const activeId = UiStore.getActiveConversationId() || ChatStore.getActiveConversationId();
    const activeConv = convs.find(c => c.id === activeId) || ChatStore.getActiveConversation();
    const messages = activeConv ? ChatStore.getMessages(activeConv.id) : [];
    const currentView = UiStore.getView();
    console.log('[ChatPage] _renderMobileShell:', { convCount: convs.length, convIds: convs.map(c => c.id), activeId, currentView, hasActiveConv: !!activeConv });

    if (currentView === 'chat' && activeConv) {
      return this._renderMobileChat(activeConv, messages);
    }
    return this._renderMobileList(convs, activeId);
  },

  _renderMobileList(convs, activeId) {
    return `
    <div style="position:relative;z-index:10;display:flex;flex-direction:column;flex:1;min-height:0;overflow:hidden;background:var(--app-surface-strong);">
      <!-- Primary Header -->
      <div class="mobile-primary-header">
        <div class="leading-icon">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 8V4H8"/><rect width="16" height="12" x="4" y="8" rx="2"/><path d="M2 14h2"/><path d="M20 14h2"/><path d="M15 13v2"/><path d="M9 13v2"/></svg>
        </div>
        <div class="title">AgentChat</div>
        ${this._renderStatusIndicator()}
        <button class="mph-action" id="mobile-search-btn" title="搜索">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><path d="M21 21l-4.35-4.35"/></svg>
        </button>
        <button class="mph-action" id="mobile-plus-btn" title="新建">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 5v14M5 12h14"/></svg>
        </button>
      </div>
      <!-- Search box -->
      <div id="mobile-search-panel" style="display:none;padding:0.75rem 1rem;border-bottom:1px solid var(--app-border);background:var(--app-surface);">
        <div class="sidebar-search-box" style="margin:0;">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><path d="M21 21l-4.35-4.35"/></svg>
          <input type="text" id="mobile-search-input" placeholder="搜索会话、成员或消息">
          <button id="mobile-search-clear" class="search-clear-btn" style="display:none;">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 6L6 18M6 6l12 12"/></svg>
          </button>
          <button id="mobile-search-cancel" class="search-cancel-btn">取消</button>
        </div>
      </div>
      <!-- Conv list -->
      <div style="flex:1;min-height:0;overflow:hidden;background:var(--app-subtle-bg);padding-bottom:calc(5rem+env(safe-area-inset-bottom));">
        <div class="conv-list" style="height:100%;overflow-y:auto;">
          ${this._renderConversationList(convs, activeId)}
        </div>
      </div>
    </div>`;
  },

  _renderMobileChat(conv, messages) {
    const isAgent = this._isAgentConv(conv);
    const isGroup = conv.type === 'group';
    const isWorkspace = conv.scope === 'personal_workspace';
    return `
    <div style="position:relative;z-index:10;display:flex;flex-direction:column;flex:1;min-height:0;overflow:hidden;background:var(--app-page-bg);">
      <!-- Secondary Header -->
      <div class="mobile-secondary-header">
        <button class="msh-back" id="mobile-chat-back" title="返回">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
        </button>
        <div class="msh-title" style="overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(conv.name)}</div>
        <button class="msh-action" id="mobile-board-btn" title="${isWorkspace?'AI 空间':'群组看板'}">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/></svg>
        </button>
        <button class="msh-action" id="mobile-bind-btn" title="${isWorkspace?'扫码':'绑定设备'}">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>
        </button>
      </div>
      <!-- Messages -->
      <div style="position:relative;flex:1;min-height:0;">
        ${this._renderMessageStream(messages, conv)}
      </div>
      <!-- Composer -->
      <div style="flex-shrink:0;padding:0.75rem 1rem 0.75rem;padding-bottom:max(0.75rem,env(safe-area-inset-bottom));">
        ${this._renderComposer(conv, false)}
      </div>
    </div>`;
  },

  // ═══════════════════════════════════════════
  // SIDEBAR (match ConversationSidebar)
  // ═══════════════════════════════════════════
  _renderSidebar(convs, activeId) {
    const unread = ChatStore.getTotalUnreadCount();
    return `
    <div class="sidebar-header">
      <div style="display:flex;align-items:center;gap:0.5rem;">
        <div class="leading-icon">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 8V4H8"/><rect width="16" height="12" x="4" y="8" rx="2"/><path d="M2 14h2"/><path d="M20 14h2"/><path d="M15 13v2"/><path d="M9 13v2"/></svg>
        </div>
        <span class="sidebar-title">AgentChat</span>
        ${this._renderStatusIndicator()}
      </div>
      <div class="sidebar-actions" style="display:flex;align-items:center;gap:4px;">
        <button class="sidebar-action-btn" id="sidebar-search-btn" title="搜索">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><path d="M21 21l-4.35-4.35"/></svg>
        </button>
        <button class="sidebar-action-btn" id="sidebar-new-btn" title="新建群组">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 5v14M5 12h14"/></svg>
        </button>
      </div>
    </div>

    <!-- Search -->
    <div id="sidebar-search-panel" style="display:${this.searchOpen?'block':'none'};padding:0 1rem 0.75rem;border-bottom:1px solid var(--app-border);flex-shrink:0;">
      <div class="sidebar-search-box" style="margin:0;">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><path d="M21 21l-4.35-4.35"/></svg>
        <input type="text" id="sidebar-search-input" placeholder="搜索会话、成员或消息" value="${this.searchQuery}">
        <button id="sidebar-search-clear" class="search-clear-btn" style="display:${this.searchQuery?'flex':'none'};">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 6L6 18M6 6l12 12"/></svg>
        </button>
        <button id="sidebar-search-cancel" class="search-cancel-btn">取消</button>
      </div>
    </div>

    <!-- Conversation List -->
    <div class="conv-list" style="flex:1;min-height:0;overflow-y:auto;">
      ${this._renderConversationList(convs, activeId)}
      ${convs.length===0 ? `<div style="text-align:center;padding:2.5rem 1rem;color:var(--app-muted);font-size:0.85rem;">暂无会话</div>` : ''}
    </div>

    <!-- Profile Card -->
    <div class="sidebar-profile" id="sidebar-profile-btn">
      <div class="sp-avatar" style="background:${AvatarSwatch.getSwatch(AuthStore.getUserId()||'me').bg};color:${AvatarSwatch.getSwatch(AuthStore.getUserId()||'me').fg};">
        ${(AuthStore.getPhone()||'用户').slice(-2)}
      </div>
      <div style="flex:1;min-width:0;">
        <div class="sp-name">${this._esc(AuthStore.getPhone()||'未登录')}</div>
        <div class="sp-status">AI 在线</div>
      </div>
      <svg class="sp-arrow" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg>
    </div>`;
  },

  _renderConversationList(convs, activeId) {
    let filtered = convs;
    if (this.searchQuery) {
      filtered = convs.filter(c => c.name.toLowerCase().includes(this.searchQuery.toLowerCase()));
    }

    // Sort: personal_workspace first, then by latest message time
    const sorted = [...filtered].sort((a, b) => {
      const aPinned = a.scope === 'personal_workspace';
      const bPinned = b.scope === 'personal_workspace';
      if (aPinned !== bPinned) return aPinned ? -1 : 1;
      const aMsgs = ChatStore.getMessages(a.id);
      const bMsgs = ChatStore.getMessages(b.id);
      const aLatest = aMsgs.length > 0 ? aMsgs[aMsgs.length - 1].createdAt : 0;
      const bLatest = bMsgs.length > 0 ? bMsgs[bMsgs.length - 1].createdAt : 0;
      if (aLatest !== bLatest) return bLatest - aLatest;
      return 0;
    });

    const renderItem = (c) => {
      const msgs = ChatStore.getMessages(c.id);
      const lastMsg = msgs.length > 0 ? msgs[msgs.length - 1] : null;
      const preview = lastMsg ? lastMsg.content.slice(0, 50) : '';
      const isActive = c.id === activeId;
      const isAgent = c.type === 'agent';
      const isGroup = c.type === 'group';
      const isPinned = c.scope === 'personal_workspace';
      const unread = ChatStore.getConversationUnreadCount ? ChatStore.getConversationUnreadCount(c.id) : (c.unread || 0);
      const timeLabel = this._convTimeLabel(c.id);

      // Avatar rendering
      let avatarHtml = '';
      if (isPinned) {
        // Personal workspace: briefcase icon with gradient
        avatarHtml = `<div class="conv-avatar conv-avatar-workspace">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 7h-3a2 2 0 01-2-2V3"/><path d="M9 18a2 2 0 01-2-2V4a2 2 0 012-2h7l4 4v10a2 2 0 01-2 2z"/><path d="M3 7.6v12.8A1.6 1.6 0 004.6 22h9.8"/><path d="M3 12h12"/></svg>
        </div>`;
      } else if (isAgent) {
        // Agent: bot icon with brand-soft bg
        avatarHtml = `<div class="conv-avatar conv-avatar-agent">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="3"/><path d="M9 10a3 3 0 016 0v2a3 3 0 01-6 0V10z"/><circle cx="12" cy="17" r="1.5"/></svg>
        </div>`;
      } else {
        // Group: participant avatars grid or fallback
        const avatars = this._getParticipantAvatars(c.id);
        if (avatars.length > 0) {
          avatarHtml = `<div class="conv-avatar conv-avatar-group">
            ${avatars.map(a => `<div class="conv-avatar-cell" style="background:${a.bg};color:${a.fg};">${a.label}</div>`).join('')}
          </div>`;
        } else {
          const swatch = AvatarSwatch.getSwatch(c.id);
          const fallback = c.name.length >= 2 ? c.name.slice(0, 2) : '群';
          avatarHtml = `<div class="conv-avatar conv-avatar-fallback" style="background:${swatch.bg};color:${swatch.fg};">${fallback}</div>`;
        }
      }

      // Badges
      let badgesHtml = '';
      if (isPinned) {
        badgesHtml = `<span class="conv-badge-label conv-badge-private">私有</span><span class="conv-badge-label conv-badge-ai">AI</span>`;
      } else if (isAgent) {
        badgesHtml = `<span class="conv-badge-label conv-badge-ai">AI</span>`;
      } else if (isGroup) {
        badgesHtml = `<span class="conv-badge-label conv-badge-group">群</span>`;
      }

      // Preview text
      let previewText = '';
      if (isPinned) {
        previewText = this._getWorkspaceStatus();
      } else if (preview) {
        previewText = preview;
      } else if (isAgent) {
        previewText = c.topic || '协助撰写任务、总结信息与发布公告';
      } else if (isGroup) {
        previewText = c.topic || '群组协作与任务推进';
      } else {
        previewText = c.topic || '暂无最新消息';
      }

      return `
      <div class="conv-item ${isActive ? 'active' : ''}" data-conv-id="${c.id}">
        ${avatarHtml}
        <div class="conv-body">
          <div class="conv-name-row">
            <span class="conv-name">${this._esc(c.name)}</span>
            ${badgesHtml}
          </div>
          <div class="conv-preview">${this._esc(previewText)}</div>
        </div>
        <div class="conv-meta">
          ${timeLabel ? `<span class="conv-time">${timeLabel}</span>` : ''}
          ${unread > 0 ? `<span class="conv-badge">${unread > 99 ? '99+' : unread}</span>` : ''}
        </div>
        ${isGroup && !isPinned ? `<button class="conv-delete-btn" data-conv-id="${c.id}" title="删除群聊" aria-label="删除群聊">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h18"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6"/><path d="M8 6V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>
        </button>` : ''}
      </div>`;
    };

    return sorted.map(renderItem).join('');
  },

  // Get participant avatars for a group conversation (max 4)
  _getParticipantAvatars(convId) {
    const msgs = ChatStore.getMessages(convId);
    const seen = new Map();
    msgs.forEach(msg => {
      if (msg.author === 'system') return;
      const key = String(msg.authorId || msg.authorName || msg.id);
      if (!key || seen.has(key)) return;
      const swatch = AvatarSwatch.getSwatch(key);
      const rawName = String(msg.authorName || msg.authorId || '').trim();
      seen.set(key, {
        label: rawName ? rawName.slice(-2) : '#',
        bg: swatch.bg,     
        fg: swatch.fg,
      });
    });
    return Array.from(seen.values()).slice(0, 4);
  },

  // Get workspace status text
  _getWorkspaceStatus() {
    const devices = ChatStore.getBoundDevices();
    const onlineCount = devices.filter(device => device.status === 'online').length;
    const workspace = ChatStore.getConversations().find(c => c.scope === 'personal_workspace');
    const draftCount = ChatStore.getWorkspaceDraftActions(workspace?.id).length;
    return `AI 在线 · ${onlineCount} 台设备在线 · ${draftCount} 条待确认`;
  },

  // Format time label for conversation list
  _convTimeLabel(convId) {
    const msgs = ChatStore.getMessages(convId);
    const lastMsg = msgs.length > 0 ? msgs[msgs.length - 1] : null;
    return TimeUtils.formatListTime(lastMsg?.createdAt);
  },

  // ═══════════════════════════════════════════
  // MESSAGE STREAM
  // ═══════════════════════════════════════════
  _renderMessageStream(messages, conv) {
    if (messages.length === 0) {
      return `<div class="msg-stream" id="msg-stream" style="display:flex;align-items:center;justify-content:center;font-size:0.9rem;color:var(--app-muted);">暂无消息，开始聊天吧</div>`;
    }
    return `<div class="msg-stream" id="msg-stream">${messages.map(m => this._renderBubble(m, conv)).join('')}</div>`;
  },

  // 对齐 Vue 版 parseSharedPostMessageContent：解析消息中的帖子分享信封，得到干净文本与帖子数据
  _resolveMessageDisplay(msg, convId) {
    const rawContent = String(msg.content ?? '');
    const parsed = ChatStore.parseSharedPostMessageContent(rawContent, convId);
    const attachments = (msg.attachments && msg.attachments.length)
      ? msg.attachments
      : (parsed.attachments || []);
    return {
      displayContent: parsed.content || rawContent,
      post: parsed.post || null,
      attachments,
    };
  },

  // 对齐 Vue 版 MessageAttachmentCard / buildConversationPostAttachments：渲染可点击的任务卡片
  _renderPostAttachmentCards(attachments, conv) {
    if (!attachments || !attachments.length) return '';
    return attachments.map(att => {
      if (att.type === 'native_post') {
        const templateLabel = att.template === 'homework' ? '家庭作业' : att.template === 'event' ? '群活动' : '群资讯';
        const deadline = att.deadlineAt ? this._formatDeadline(att.deadlineAt) : '长期有效';
        return `
        <button class="msg-post-card" data-post-id="${this._esc(att.postId)}" data-conv-id="${this._esc(att.conversationId || conv?.id || '')}" type="button" style="display:flex;width:100%;align-items:flex-start;gap:0.65rem;margin-top:0.5rem;border:1px solid var(--app-border);border-radius:0.9rem;background:var(--app-surface);padding:0.7rem 0.8rem;text-align:left;cursor:pointer;">
          <span style="display:flex;height:2.2rem;width:2.2rem;flex-shrink:0;align-items:center;justify-content:center;border-radius:0.7rem;background:var(--app-brand-soft);color:var(--app-brand);">${this._postIconSvg({ template: att.template, status: att.status })}</span>
          <span style="min-width:0;flex:1;">
            <span style="display:block;font-size:0.84rem;font-weight:600;color:var(--app-text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(att.title || '任务')}</span>
            <span style="display:block;margin-top:0.15rem;font-size:0.72rem;color:var(--app-muted);">${this._esc(templateLabel)} · 截止 ${this._esc(deadline)}</span>
          </span>
          <span style="display:flex;align-items:center;align-self:center;color:var(--app-brand);font-size:0.74rem;font-weight:600;">查看</span>
        </button>`;
      }
      if (att.type === 'webview') {
        const cardTitle = att.title || 'H5 卡片';
        const cardSummary = att.summary || att.url || '';
        const cardDomain = (() => { try { return new URL(att.url || '').host; } catch { return ''; } })();
        const cardIdx = att._cardIdx || 0;
        return `
        <article class="msg-post-card msg-post-card-webview" data-resource-url="${this._esc(att.url || '')}" data-card-idx="${cardIdx}" style="position:relative;display:flex;width:100%;flex-direction:column;margin-top:0.5rem;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-surface-elevated);padding:0;overflow:hidden;box-shadow:0 8px 18px rgba(86,74,132,0.05);">
          <button class="msg-post-card-main" data-resource-url="${this._esc(att.url || '')}" type="button" style="display:flex;width:100%;flex-direction:column;border:0;background:transparent;padding:0;text-align:left;cursor:pointer;color:inherit;">
            <div style="position:relative;display:flex;width:100%;flex-direction:column;gap:0.75rem;padding:0.95rem 3rem 1rem 1.05rem;background:linear-gradient(135deg,color-mix(in srgb,var(--app-brand) 10%,transparent),transparent 54%);">
              <div style="position:absolute;left:0;top:0;bottom:0;width:0.25rem;background:var(--app-brand);"></div>
              <div style="display:flex;min-width:0;align-items:flex-start;gap:0.75rem;">
                <span style="display:flex;width:2.65rem;height:2.65rem;flex:0 0 auto;align-items:center;justify-content:center;border-radius:0.8rem;background:var(--app-brand-soft);color:var(--app-brand);">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 2l2.4 7.4H22l-6.2 4.5 2.4 7.4L12 16.8l-6.2 4.5 2.4-7.4L2 9.4h7.6z"/></svg>
                </span>
                <span style="min-width:0;flex:1;">
                  <span style="display:block;font-size:0.78rem;color:var(--app-muted);">H5 卡片</span>
                  <span style="display:block;margin-top:0.18rem;font-size:1.02rem;font-weight:800;color:var(--app-text);line-height:1.28;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(cardTitle)}</span>
                  ${cardDomain ? `<span style="display:block;margin-top:0.18rem;font-size:0.8rem;color:var(--app-text-secondary);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(cardDomain)}</span>` : ''}
                </span>
              </div>
              ${cardSummary ? `<span style="display:-webkit-box;overflow:hidden;-webkit-box-orient:vertical;-webkit-line-clamp:2;color:var(--app-text-secondary);font-size:0.86rem;line-height:1.5;">${this._esc(cardSummary)}</span>` : ''}
              <div style="display:flex;align-items:center;">
                <span style="display:inline-flex;align-items:center;border-radius:0.7rem;background:var(--app-brand);padding:0.42rem 0.72rem;color:#fff;font-size:0.8rem;font-weight:700;line-height:1.2;">查看卡片</span>
              </div>
            </div>
          </button>
          <button class="msg-post-card-forward" type="button" data-action="forward" data-resource-url="${this._esc(att.url || '')}" data-card-title="${this._esc(att.title || 'H5 卡片')}" aria-label="转发资源" style="position:absolute;right:0.8rem;bottom:1rem;display:flex;width:2.1rem;height:2.1rem;align-items:center;justify-content:center;border:0;border-radius:999px;background:transparent;color:var(--app-muted);cursor:pointer;">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 2L11 13"/><path d="M22 2l-7 20-4-9-9-4 20-7z"/></svg>
          </button>
          <button class="msg-post-card-more" type="button" data-action="more" data-resource-url="${this._esc(att.url || '')}" aria-label="更多操作" style="position:absolute;top:0.7rem;right:0.7rem;display:flex;width:2rem;height:2rem;align-items:center;justify-content:center;border:0;border-radius:999px;background:color-mix(in srgb,var(--app-surface-elevated) 86%,transparent);color:var(--app-muted);cursor:pointer;">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="5" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="12" cy="19" r="1"/></svg>
          </button>
        </article>`;
      }
      return '';
    }).join('');
  },

  _renderBubble(msg, conv) {
    const isAgent = msg.author === 'agent';
    const isMine = msg.author === 'user';
    const isOther = msg.author === 'other_user';
    const isSystem = msg.author === 'system';

    if (isSystem) {
      const isPlain = !/[#*_`>\[\]()]/.test(msg.content);
      const html = isPlain ? this._esc(msg.content) : MarkdownRenderer.render(msg.content);
      return `<div class="msg-system-row"><div class="msg-system-bubble"><div class="whitespace-pre-wrap" style="word-break:break-word;">${html.replace(/\n/g,'<br>')}</div></div></div>`;
    }

    if (isAgent) {
      // 对齐 Vue 版 MessageBubble：agent 消息也解析信封并渲染附件卡片
      const { displayContent, attachments } = this._resolveMessageDisplay(msg, conv?.id);
      const isStreaming = msg.status === 'streaming';
      const isTypingOnly = isStreaming && !displayContent.trim();
      const isPlain = !/[#*_`>\[\]()]/.test(displayContent);
      const html = isPlain ? this._esc(displayContent) : MarkdownRenderer.render(displayContent);
      const attachmentCards = this._renderPostAttachmentCards(attachments, conv);
      const bubbleContent = isTypingOnly
        ? '<div class="agent-typing" aria-label="助手正在输入"><span></span><span></span><span></span></div>'
        : `<div class="message-markdown">${html}</div>${attachmentCards}`;
      return `
      <div class="msg-agent-row">
        <div class="agent-avatar">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="3"/><path d="M9 10a3 3 0 016 0v2a3 3 0 01-6 0V10z"/><circle cx="12" cy="17" r="1.5"/></svg>
        </div>
        <div class="agent-msg-col">
          <div class="agent-name-row">
            <span class="agent-name">炎图AI助手</span>
            <span class="agent-ai-badge">AI</span>
          </div>
          <div class="agent-bubble">
            ${bubbleContent}
          </div>
        </div>
      </div>`;
    }

    if (isMine) {
      const swatch = AvatarSwatch.getSwatch(AuthStore.getUserId() || 'me');
      const initials = (AuthStore.getPhone()||'').slice(-2) || '我';
      const { displayContent, attachments } = this._resolveMessageDisplay(msg, conv?.id);
      const isPlain = !/[#*_`>\[\]()]/.test(displayContent);
      const html = isPlain ? this._esc(displayContent) : MarkdownRenderer.render(displayContent);
      const attachmentCards = this._renderPostAttachmentCards(attachments, conv);
      return `
      <div class="msg-self-row">
        <div class="self-msg-col">
          <div class="self-bubble">
            <div class="message-markdown" style="color:#fff;">${html}</div>
          </div>
          ${attachmentCards}
          <div class="self-msg-footer">
            ${this._renderDeliveryStatus(msg, conv?.id)}
            <div class="msg-time" style="text-align:right;">${TimeUtils.formatMdHm(msg.createdAt)}</div>
          </div>
        </div>
        <div class="self-avatar">${initials}</div>
      </div>`;
    }

    if (isOther) {
      const key = msg.authorId || msg.authorName || msg.id;
      const swatch = AvatarSwatch.getSwatch(key);
      const name = msg.authorName || '成员';
      const initials = (name||'').slice(-2) || '#';
      const { displayContent, attachments } = this._resolveMessageDisplay(msg, conv?.id);
      const isPlain = !/[#*_`>\[\]()]/.test(displayContent);
      const html = isPlain ? this._esc(displayContent) : MarkdownRenderer.render(displayContent);
      const attachmentCards = this._renderPostAttachmentCards(attachments, conv);
      return `
      <div class="msg-other-row">
        <div class="other-avatar" style="background:${swatch.bg};color:${swatch.fg};">${initials}</div>
        <div class="other-msg-col">
          <div class="other-name">${this._esc(name)}</div>
          <div class="other-bubble">
            <div class="message-markdown">${html}</div>
          </div>
          ${attachmentCards}
        </div>
      </div>`;
    }

    return '';
  },

  /**
   * 自己消息的发送状态指示器（对齐 Vue 版 MessageBubble delivery 渲染 + 微信交互）：
   * - sending → 灰色转圈图标
   * - failed → 红色感叹号按钮，点击重发
   * - sent / 无值 → 不显示
   */
  _renderDeliveryStatus(msg, convId) {
    const delivery = msg.delivery;
    if (delivery === 'sending') {
      return `<span class="msg-delivery sending" aria-label="发送中"><svg class="status-spin" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><path d="M21 12a9 9 0 11-6.219-8.56"/></svg></span>`;
    }
    if (delivery === 'failed') {
      return `<button class="msg-delivery failed" data-retry-msg-id="${this._esc(msg.id)}" data-retry-conv-id="${this._esc(convId || '')}" title="发送失败，点击重发" aria-label="发送失败，点击重发"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg></button>`;
    }
    return '';
  },

  // ═══════════════════════════════════════════
  // COMPOSER (match ChatComposer.vue)
  // ═══════════════════════════════════════════
  _renderDesktopComposer(conv) {
    // composer 样式统一：仅 agent 会话用 agent 样式，工作空间与群聊一致用 normal
    const isAgent = conv && conv.type === 'agent';
    const draft = this._getComposerDraft(conv?.id);
    const counter = draft.length;
    const hasText = draft.trim().length > 0;
    const voiceActive = this.voiceState !== 'idle';
    const voiceToggleDisabled = (this.isPreparingVoiceMode || voiceActive) ? 'disabled' : '';
    const voiceStatusText = this._getVoiceStatusText();
    const voiceElapsedLabel = this._getVoiceElapsedLabel();
    return `
    <div class="composer-wrap">
      <div class="composer-card ${isAgent?'agent':'normal'}">
        <div class="composer-inner">
          ${!isAgent ? `
          <button class="composer-sparkle" id="composer-sparkle-btn" title="召唤智能助手">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 3l1.5 5.5L19 10l-5.5 1.5L12 17l-1.5-5.5L5 10l5.5-1.5z"/><path d="M6 14l.75 2.25L9 17l-2.25.75L6 20l-.75-2.25L3 17l2.25-.75z"/></svg>
          </button>` : ''}
          ${this.isVoiceMode ? `
          <button class="composer-voice-toggle" id="composer-voice-toggle-btn" title="切换到键盘输入" ${voiceToggleDisabled}>
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M6 14h12"/></svg>
          </button>` : `
          <button class="composer-voice-toggle" id="composer-voice-toggle-btn" title="切换到语音输入" ${voiceToggleDisabled}>
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 1a3 3 0 00-3 3v8a3 3 0 006 0V4a3 3 0 00-3-3z"/><path d="M19 10v2a7 7 0 01-14 0v-2"/><path d="M12 19v4M8 23h8"/></svg>
          </button>`}
          ${this.isVoiceMode ? `
          <div class="composer-voice-area">
            <button id="composer-voice-btn" class="composer-voice-btn ${voiceActive ? 'active' : ''}" ${this.voiceState === 'processing' ? 'disabled' : ''}>
              <span>${voiceActive ? voiceStatusText : '点击 录音'}</span>
            </button>
          </div>` : `
          <div class="composer-textarea-wrap">
            <textarea id="composer-input" class="composer-textarea" rows="1" placeholder="输入消息... (Enter 发送)">${this._esc(draft)}</textarea>
          </div>`}
          <button id="composer-send-btn" class="composer-send ${hasText?'enabled':'disabled'} ${isAgent?'agent-send':''}" ${hasText ? '' : 'disabled'}>
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 2L11 13"/><path d="M22 2l-7 20-4-9-9-4 20-7z"/></svg>
          </button>
        </div>
        ${this.isVoiceMode && voiceActive ? `
        <div class="composer-voice-panel">
          <div class="composer-voice-status">${voiceStatusText}</div>
          <div class="composer-voice-timer">${voiceElapsedLabel}</div>
          <button class="composer-voice-stop" ${this.voiceState === 'processing' ? 'disabled' : ''}>${this.voiceState === 'processing' ? '请稍候' : '点击停止'}</button>
        </div>` : ''}
        <div class="composer-footer">
          <span>重要信息请自行核验</span>
          <span class="composer-counter" id="composer-counter">${counter} / 2000</span>
        </div>
      </div>
    </div>`;
  },

  _renderComposer(conv, isDesktop = true) {
    // composer 样式统一：仅 agent 会话用 agent 样式，工作空间与群聊一致用 normal
    const isAgent = conv && conv.type === 'agent';
    const wrapStyle = isDesktop ? 'composer-wrap' : '';
    const draft = this._getComposerDraft(conv?.id);
    const counter = draft.length;
    const hasText = draft.trim().length > 0;
    const voiceActive = this.voiceState !== 'idle';
    const voiceToggleDisabled = (this.isPreparingVoiceMode || voiceActive) ? 'disabled' : '';
    const voiceStatusText = this._getVoiceStatusText();
    const voiceElapsedLabel = this._getVoiceElapsedLabel();
    return `
    <div class="${wrapStyle}">
      <div class="composer-card ${isAgent?'agent':'normal'}">
        <div class="composer-inner">
          ${!isAgent ? `
          <button class="composer-sparkle" id="composer-sparkle-btn" title="召唤智能助手">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 3l1.5 5.5L19 10l-5.5 1.5L12 17l-1.5-5.5L5 10l5.5-1.5z"/><path d="M6 14l.75 2.25L9 17l-2.25.75L6 20l-.75-2.25L3 17l2.25-.75z"/></svg>
          </button>` : ''}
          ${this.isVoiceMode ? `
          <button class="composer-voice-toggle" id="composer-voice-toggle-btn" title="切换到键盘输入" ${voiceToggleDisabled}>
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M6 14h12"/></svg>
          </button>` : `
          <button class="composer-voice-toggle" id="composer-voice-toggle-btn" title="切换到语音输入" ${voiceToggleDisabled}>
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 1a3 3 0 00-3 3v8a3 3 0 006 0V4a3 3 0 00-3-3z"/><path d="M19 10v2a7 7 0 01-14 0v-2"/><path d="M12 19v4M8 23h8"/></svg>
          </button>`}
          ${this.isVoiceMode ? `
          <div class="composer-voice-area">
            <button id="composer-voice-btn" class="composer-voice-btn ${voiceActive ? 'active' : ''}" ${this.voiceState === 'processing' ? 'disabled' : ''}>
              <span>${voiceActive ? voiceStatusText : '点击 录音'}</span>
            </button>
          </div>` : `
          <div class="composer-textarea-wrap">
            <textarea id="composer-input" class="composer-textarea" rows="1" placeholder="输入消息... (Enter 发送)">${this._esc(draft)}</textarea>
          </div>`}
          <button id="composer-send-btn" class="composer-send ${hasText?'enabled':'disabled'} ${isAgent?'agent-send':''}" ${hasText ? '' : 'disabled'}>
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 2L11 13"/><path d="M22 2l-7 20-4-9-9-4 20-7z"/></svg>
          </button>
        </div>
        ${this.isVoiceMode && voiceActive ? `
        <div class="composer-voice-panel">
          <div class="composer-voice-status">${voiceStatusText}</div>
          <div class="composer-voice-timer">${voiceElapsedLabel}</div>
          <button class="composer-voice-stop" ${this.voiceState === 'processing' ? 'disabled' : ''}>${this.voiceState === 'processing' ? '请稍候' : '点击停止'}</button>
        </div>` : ''}
        <div class="composer-footer">
          <span>重要信息请自行核验</span>
          <span class="composer-counter" id="composer-counter">${counter} / 2000</span>
        </div>
      </div>
    </div>`;
  },

  _getComposerDraft(conversationId) {
    if (!conversationId) return '';
    return this.composerDrafts[conversationId] || '';
  },

  _setComposerDraft(conversationId, value) {
    if (!conversationId) return;
    const next = String(value || '');
    if (next) this.composerDrafts[conversationId] = next;
    else delete this.composerDrafts[conversationId];
  },

  _captureComposerState() {
    const activeElement = document.activeElement;
    const input = document.getElementById('composer-input');
    if (!input) return null;
    const conversationId = ChatStore.getActiveConversationId();
    this._setComposerDraft(conversationId, input.value);
    return {
      focused: activeElement === input,
      selectionStart: input.selectionStart ?? input.value.length,
      selectionEnd: input.selectionEnd ?? input.value.length,
    };
  },

  _restoreComposerState(state) {
    const input = document.getElementById('composer-input');
    if (!input) return;
    input.style.height = 'auto';
    input.style.height = Math.min(input.scrollHeight, 120) + 'px';
    if (this.suppressComposerFocusRestore) {
      this.suppressComposerFocusRestore = false;
      return;
    }
    if (state?.focused) {
      input.focus();
      try {
        input.setSelectionRange(state.selectionStart, state.selectionEnd);
      } catch {}
    }
  },

  _captureViewportState() {
    const stream = document.getElementById('msg-stream');
    const conversationId = ChatStore.getActiveConversationId();
    if (!stream || !conversationId) {
      return {
        conversationId,
        messageCount: ChatStore.getActiveMessages().length,
        scrollTop: 0,
        isNearBottom: true,
      };
    }
    const maxTop = Math.max(0, stream.scrollHeight - stream.clientHeight);
    const distanceFromBottom = maxTop - stream.scrollTop;
    return {
      conversationId,
      messageCount: ChatStore.getActiveMessages().length,
      scrollTop: stream.scrollTop,
      isNearBottom: distanceFromBottom <= 48,
    };
  },

  _restoreViewportState(state) {
    const stream = document.getElementById('msg-stream');
    const conversationId = ChatStore.getActiveConversationId();
    if (!stream) return;

    const scrollToBottom = () => {
      stream.scrollTop = stream.scrollHeight;
    };

    if (this.pendingScrollToBottom) {
      this.pendingScrollToBottom = false;
      requestAnimationFrame(scrollToBottom);
      return;
    }

    if (!state || state.conversationId !== conversationId) {
      requestAnimationFrame(scrollToBottom);
      return;
    }

    const currentCount = ChatStore.getActiveMessages().length;
    if (currentCount !== state.messageCount) {
      if (state.isNearBottom) {
        requestAnimationFrame(scrollToBottom);
      } else {
        requestAnimationFrame(() => {
          const maxTop = Math.max(0, stream.scrollHeight - stream.clientHeight);
          stream.scrollTop = Math.min(state.scrollTop, maxTop);
        });
      }
      return;
    }

    requestAnimationFrame(() => {
      const maxTop = Math.max(0, stream.scrollHeight - stream.clientHeight);
      stream.scrollTop = Math.min(state.scrollTop, maxTop);
    });
  },

  _capturePostsPanelState() {
    const panelBody = this.container?.querySelector('#posts-panel .pp-body');
    const conversationId = ChatStore.getActiveConversationId();
    return {
      open: this.postsPanelOpen,
      conversationId,
      view: this.postsPanelView || 'board',
      scrollTop: panelBody ? panelBody.scrollTop : 0,
    };
  },

  _restorePostsPanelState(state) {
    if (!state?.open) return;
    const conversationId = ChatStore.getActiveConversationId();
    if (state.conversationId !== conversationId) return;
    if ((this.postsPanelView || 'board') !== state.view) return;
    const panelBody = this.container?.querySelector('#posts-panel .pp-body');
    if (!panelBody) return;
    requestAnimationFrame(() => {
      const maxTop = Math.max(0, panelBody.scrollHeight - panelBody.clientHeight);
      panelBody.scrollTop = Math.min(state.scrollTop, maxTop);
    });
  },

  // ═══════════════════════════════════════════
  // EVENT BINDING
  // ═══════════════════════════════════════════
  _bindDesktopEvents() {
    // Desktop tab buttons
    this.container.querySelectorAll('.ds-tab-btn').forEach(btn => {
      btn.onclick = () => {
        this.activeTab = btn.dataset.dsTab;
        this.render();
      };
    });

    // Desktop board button → opens posts panel (same as Vue mobile board button)
    const gbBtn = document.getElementById('group-board-btn');
    if (gbBtn) gbBtn.onclick = () => this._openPostsPanel();

    // Desktop bind-device button → open devices page
    const bindBtn = document.getElementById('group-bind-btn');
    if (bindBtn) bindBtn.onclick = () => this._handleBindDeviceAction();

    // Desktop tab = 文件/任务 show placeholder
    if (this.activeTab !== '聊天') {
      const main = this.container.querySelector('.ds-main');
      if (main && ChatStore.getActiveConversation()) {
        main.innerHTML = `
          <header class="ds-header">
            <div class="ds-header-inner"><div class="ds-title">${this.activeTab}</div></div>
          </header>
          <div style="flex:1;display:flex;align-items:center;justify-content:center;color:var(--app-muted);font-size:0.9rem;">
            ${this.activeTab}功能开发中
          </div>`;
      }
    }
  },

  _bindMobileEvents() {
    // Back button
    const backBtn = document.getElementById('mobile-chat-back');
    if (backBtn) backBtn.onclick = () => {
      UiStore.setActiveConversation(null);
      ChatStore.setActiveConversation('');
    };

    // Board button → opens group board / workspace hub
    const boardBtn = document.getElementById('mobile-board-btn');
    if (boardBtn) boardBtn.onclick = () => this._openPostsPanel();

    // Bind device button
    const bindBtn = document.getElementById('mobile-bind-btn');
    if (bindBtn) bindBtn.onclick = () => this._handleBindDeviceAction();

    // Search
    const searchBtn = document.getElementById('mobile-search-btn');
    const searchPanel = document.getElementById('mobile-search-panel');
    const searchInput = document.getElementById('mobile-search-input');
    const searchClear = document.getElementById('mobile-search-clear');
    const searchCancel = document.getElementById('mobile-search-cancel');

    if (searchBtn && searchPanel) {
      searchBtn.onclick = () => {
        searchPanel.style.display = 'block';
        searchInput?.focus();
      };
    }
    if (searchInput) {
      searchInput.addEventListener('input', () => {
        this.searchQuery = searchInput.value;
        if (searchClear) searchClear.style.display = this.searchQuery ? 'flex' : 'none';
        this.render();
      });
    }
    if (searchClear) searchClear.onclick = () => {
      this.searchQuery = '';
      if (searchInput) searchInput.value = '';
      searchClear.style.display = 'none';
      this.render();
    };
    if (searchCancel) searchCancel.onclick = () => {
      this.searchQuery = '';
      if (searchInput) searchInput.value = '';
      if (searchPanel) searchPanel.style.display = 'none';
      this.render();
    };

    // Plus button → 打开创建群聊对话框（对齐 Vue 版 MobileCreateMenu → 发起群聊 → CreateGroupDialog）
    const plusBtn = document.getElementById('mobile-plus-btn');
    if (plusBtn) plusBtn.onclick = () => this._openCreateGroupDialog();
  },

  _bindComposerEvents() {
    const input = document.getElementById('composer-input');
    const sendBtn = document.getElementById('composer-send-btn');
    const counter = document.getElementById('composer-counter');
    const conversationId = ChatStore.getActiveConversationId();

    // 语音模式切换按钮（对齐 Vue 版 toggleVoiceMode）
    const voiceToggleBtn = document.getElementById('composer-voice-toggle-btn');
    if (voiceToggleBtn) {
      voiceToggleBtn.onclick = () => this._toggleVoiceMode();
    }

    // 语音录音按钮（对齐 Vue 版 onVoiceClick）
    const voiceBtn = document.getElementById('composer-voice-btn');
    if (voiceBtn) {
      voiceBtn.onclick = () => this._onVoiceClick();
    }
    const voiceStopBtn = document.querySelector('.composer-voice-stop');
    if (voiceStopBtn) {
      voiceStopBtn.onclick = () => this._onVoiceClick();
    }

    // 语音模式下没有 textarea，跳过 input 相关绑定
    if (!input) return;

    const toggleSendBtn = () => {
      const hasText = input.value.trim().length > 0;
      if (sendBtn) {
        sendBtn.disabled = !hasText;
        sendBtn.className = `composer-send ${hasText?'enabled':'disabled'} ${ChatStore.getActiveConversation()?.type==='agent'?'agent-send':''}`;
      }
      if (counter) {
        counter.textContent = `${input.value.length} / 2000`;
      }
    };

    input.addEventListener('input', () => {
      this._setComposerDraft(conversationId, input.value);
      input.style.height = 'auto';
      input.style.height = Math.min(input.scrollHeight, 120) + 'px';
      toggleSendBtn();
    });

    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        this._doSend();
      }
    });

    if (sendBtn) {
      sendBtn.onclick = () => this._doSend();
    }

    // Sidebar search
    const sbSearchBtn = document.getElementById('sidebar-search-btn');
    if (sbSearchBtn) {
      sbSearchBtn.onclick = () => {
        this.searchOpen = true;
        this.render();
        setTimeout(() => {
          const si = document.getElementById('sidebar-search-input');
          if (si) si.focus();
        }, 50);
      };
    }

    const sbSearchCancel = document.getElementById('sidebar-search-cancel');
    if (sbSearchCancel) {
      sbSearchCancel.onclick = () => {
        this.searchOpen = false;
        this.searchQuery = '';
        this.render();
      };
    }

    const sbSearchClear = document.getElementById('sidebar-search-clear');
    const sbSearchInput = document.getElementById('sidebar-search-input');
    if (sbSearchInput) {
      sbSearchInput.addEventListener('input', () => {
        this.searchQuery = sbSearchInput.value;
        if (sbSearchClear) sbSearchClear.style.display = this.searchQuery ? 'flex' : 'none';
        this.render();
      });
    }
    if (sbSearchClear) {
      sbSearchClear.onclick = () => {
        this.searchQuery = '';
        this.render();
        setTimeout(() => {
          const si = document.getElementById('sidebar-search-input');
          if (si) si.focus();
        }, 50);
      };
    }

    // Conversation list clicks
    this.container.querySelectorAll('.conv-item').forEach(el => {
      el.onclick = (e) => {
        // 对齐 Vue 版：删除按钮点击不触发会话激活
        if (e.target.closest('.conv-delete-btn')) return;
        const id = el.dataset.convId;
        ChatStore.setActiveConversation(id);
        UiStore.setActiveConversation(id);
        ChatStore.clearMarkRead(id);
        this.searchOpen = false;
        this.searchQuery = '';
        // Also close mobile search
        const msp = document.getElementById('mobile-search-panel');
        if (msp) msp.style.display = 'none';
      };
    });

    // 会话列表删除按钮点击（对齐 Vue 版长按删除入口）
    this.container.querySelectorAll('.conv-delete-btn').forEach(btn => {
      btn.onclick = (e) => {
        e.stopPropagation();
        const id = btn.dataset.convId;
        if (id) this._openDeleteConversationDialog(id);
      };
    });

    // Profile → me page
    const profBtn = document.getElementById('sidebar-profile-btn');
    if (profBtn) profBtn.onclick = () => { window.location.hash = '#/me'; };

    // Sparkle → 插入 @炎图AI助手 召唤智能助手（对齐 Vue 版 summonAgent）
    const sparkleBtn = document.getElementById('composer-sparkle-btn');
    if (sparkleBtn) sparkleBtn.onclick = () => {
      const input = document.getElementById('composer-input');
      if (!input) return;
      const mentionText = '@炎图AI助手 ';
      const currentText = input.value;
      const needsSeparator = currentText.length > 0 && !/\s$/.test(currentText);
      input.value = `${currentText}${needsSeparator ? ' ' : ''}${mentionText}`.slice(0, 2000);
      input.focus();
      input.selectionStart = input.value.length;
      input.selectionEnd = input.value.length;
      // 触发 input 事件以更新计数器和发送按钮状态
      input.dispatchEvent(new Event('input', { bubbles: true }));
      // 自动调整高度
      input.style.height = 'auto';
      input.style.height = Math.min(input.scrollHeight, 72) + 'px';
    };

    // New group button
    const newBtn = document.getElementById('sidebar-new-btn');
    if (newBtn) newBtn.onclick = () => this._openCreateGroupDialog();

  },

  // ── Create Group Dialog Events ─
  _bindCreateGroupDialogEvents() {
    // Close button
    const closeBtn = document.getElementById('cg-close-btn');
    if (closeBtn) closeBtn.onclick = () => this._closeCreateGroupDialog();

    // Cancel button
    const cancelBtn = document.getElementById('cg-cancel-btn');
    if (cancelBtn) cancelBtn.onclick = () => this._closeCreateGroupDialog();

    // Confirm button
    const confirmBtn = document.getElementById('cg-confirm-btn');
    if (confirmBtn) confirmBtn.onclick = () => this._handleCreateGroupConfirm();

    // Enter key to confirm / input tracking
    const input = document.getElementById('cg-name-input');
    if (input) {
      input.addEventListener('input', () => {
        this.createGroupName = input.value;
      });
      input.onkeydown = (e) => {
        if (e.key === 'Enter') {
          this._handleCreateGroupConfirm();
        } else if (e.key === 'Escape') {
          this._closeCreateGroupDialog();
        }
      };
    }

    // Click overlay to close
    const overlay = document.getElementById('create-group-dialog');
    if (overlay) {
      overlay.onclick = (e) => {
        if (e.target === overlay) {
          this._closeCreateGroupDialog();
        }
      };
    }
  },

  // ── Send message ──
  _doSend() {
    const input = document.getElementById('composer-input');
    if (!input) return;
    const text = input.value.trim();
    if (!text) return;

    const convId = ChatStore.getActiveConversationId();
    if (!convId) { Toast.warn('请先选择一个会话'); return; }

    this._setComposerDraft(convId, '');
    this.pendingScrollToBottom = true;
    this.suppressComposerFocusRestore = true;
    input.value = '';
    input.style.height = 'auto';

    const sendBtn = document.getElementById('composer-send-btn');
    if (sendBtn) {
      sendBtn.disabled = true;
      sendBtn.className = `composer-send disabled ${ChatStore.getActiveConversation()?.type==='agent'?'agent-send':''}`;
    }
    const counter = document.getElementById('composer-counter');
    if (counter) counter.textContent = '0 / 2000';

    ChatStore.sendUserMessage(text, convId);
  },

  // ── 语音输入（对齐 Vue 版 useVoiceTextInput） ──

  // 对齐 Vue 版 toggleVoiceMode：进入语音模式前先检查麦克风权限
  async _toggleVoiceMode() {
    if (this.isPreparingVoiceMode) return;
    if (this.isVoiceMode) {
      this.isVoiceMode = false;
      this._cancelVoiceInput();
      this.render();
      return;
    }
    if (!this._isVoiceSupported()) {
      Toast.show('当前环境不支持语音转文字，请使用新版 Chrome 或 Android WebView', 'warn');
      return;
    }
    this.isPreparingVoiceMode = true;
    this.render();
    const prepared = await this._prepareVoiceInput();
    this.isPreparingVoiceMode = false;
    if (!prepared) {
      if (this.voiceErrorMessage) Toast.show(this.voiceErrorMessage, 'warn');
      this.render();
      return;
    }
    this.isVoiceMode = true;
    this.render();
  },

  _isVoiceSupported() {
    return (
      typeof navigator !== 'undefined' &&
      Boolean(navigator.mediaDevices?.getUserMedia) &&
      typeof MediaRecorder !== 'undefined'
    );
  },

  // 对齐 Vue 版 prepare → prepareWeb → requestWebMicrophoneAccess
  async _prepareVoiceInput() {
    this.voiceErrorMessage = '';
    if (!this._canUseWebRecorder()) {
      this.voiceErrorMessage = '当前环境无法录音转文字';
      return false;
    }
    const permission = await this._queryMicrophonePermission();
    if (permission === 'denied') {
      this.voiceErrorMessage = '麦克风权限未开启，请在浏览器设置中允许麦克风';
      return false;
    }
    let permissionStream = null;
    try {
      permissionStream = await navigator.mediaDevices.getUserMedia({ audio: true });
      const [track] = permissionStream.getAudioTracks();
      if (!track) throw new Error('没有检测到麦克风设备');
      if (track.readyState === 'ended' || !track.enabled) {
        throw new Error('麦克风不可用，请检查权限或设备设置');
      }
      return true;
    } catch (error) {
      if (error instanceof DOMException) {
        if (error.name === 'NotAllowedError' || error.name === 'SecurityError') {
          this.voiceErrorMessage = '麦克风权限未开启';
        } else if (error.name === 'NotFoundError') {
          this.voiceErrorMessage = '没有检测到麦克风设备';
        } else if (error.name === 'NotReadableError') {
          this.voiceErrorMessage = '麦克风被占用，请关闭其它录音应用后再试';
        } else {
          this.voiceErrorMessage = error.message || '麦克风权限检查失败';
        }
      } else {
        this.voiceErrorMessage = error.message || '麦克风权限检查失败';
      }
      return false;
    } finally {
      if (permissionStream) {
        for (const track of permissionStream.getTracks()) track.stop();
      }
    }
  },

  async _queryMicrophonePermission() {
    if (typeof navigator === 'undefined' || !navigator.permissions?.query) return 'unknown';
    try {
      return (await navigator.permissions.query({ name: 'microphone' })).state;
    } catch {
      return 'unknown';
    }
  },

  _getVoiceStatusText() {
    if (this.voiceState === 'processing') return '正在整理文字';
    if (this.voiceState === 'listening') return '正在听你说';
    if (this.voiceState === 'starting') return '正在申请麦克风权限';
    return '点击录音';
  },

  _getVoiceElapsedLabel() {
    const minutes = Math.floor(this.voiceElapsedSeconds / 60);
    const seconds = this.voiceElapsedSeconds % 60;
    return `${minutes}:${String(seconds).padStart(2, '0')}`;
  },

  _canUseWebRecorder() {
    return (
      typeof navigator !== 'undefined' &&
      Boolean(navigator.mediaDevices?.getUserMedia) &&
      typeof MediaRecorder !== 'undefined'
    );
  },

  _selectRecorderMimeType() {
    if (typeof MediaRecorder === 'undefined' || !MediaRecorder.isTypeSupported) return '';
    const types = ['audio/webm;codecs=opus', 'audio/webm', 'audio/mp4', 'audio/ogg;codecs=opus'];
    return types.find((m) => MediaRecorder.isTypeSupported(m)) ?? '';
  },

  _onVoiceClick() {
    if (this.voiceState === 'listening' || this.voiceState === 'starting') {
      void this._stopVoiceInput();
    } else if (this.voiceState === 'idle') {
      void this._startVoiceInput();
    }
  },

  async _startVoiceInput() {
    if (this.voiceState !== 'idle') return;
    if (!this._canUseWebRecorder()) {
      this.voiceErrorMessage = '当前环境无法录音转文字';
      Toast.show(this.voiceErrorMessage, 'error');
      return;
    }

    this.voiceState = 'starting';
    this.voiceErrorMessage = '';
    this.voiceElapsedSeconds = 0;
    this.render();

    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      const [track] = stream.getAudioTracks();
      if (!track) throw new Error('没有检测到麦克风设备');
      if (track.readyState === 'ended' || !track.enabled) {
        throw new Error('麦克风不可用，请检查权限或设备设置');
      }

      this._voiceStream = stream;
      this._startVoiceInputMeter(stream);
      this._voiceRecorderMimeType = this._selectRecorderMimeType();
      const recorder = this._voiceRecorderMimeType
        ? new MediaRecorder(stream, { mimeType: this._voiceRecorderMimeType })
        : new MediaRecorder(stream);
      this._voiceRecorderMimeType = recorder.mimeType || this._voiceRecorderMimeType || 'audio/webm';
      this._voiceRecordedChunks = [];

      recorder.ondataavailable = (event) => {
        if (event.data.size > 0) this._voiceRecordedChunks.push(event.data);
      };
      recorder.onerror = () => {
        this.voiceErrorMessage = '录音失败，请再试一次';
      };
      recorder.start(250);
      this._voiceRecorder = recorder;

      this.voiceState = 'listening';
      this._voiceRecordingStartedAt = Date.now();
      this._voiceElapsedTimer = setInterval(() => {
        this.voiceElapsedSeconds += 1;
        this.render();
      }, 1000);
      this._voiceMaxTimer = setTimeout(() => {
        void this._stopVoiceInput();
      }, 60000);
      this.render();
    } catch (error) {
      this._cleanupVoiceRecorder();
      this.voiceState = 'idle';
      if (error instanceof DOMException) {
        if (error.name === 'NotAllowedError' || error.name === 'SecurityError') {
          this.voiceErrorMessage = '麦克风权限未开启';
        } else if (error.name === 'NotFoundError') {
          this.voiceErrorMessage = '没有检测到麦克风设备';
        } else if (error.name === 'NotReadableError') {
          this.voiceErrorMessage = '麦克风被占用，请关闭其它录音应用后再试';
        } else {
          this.voiceErrorMessage = error.message || '录音启动失败';
        }
      } else {
        this.voiceErrorMessage = error.message || '录音启动失败';
      }
      Toast.show(this.voiceErrorMessage, 'error');
      this.render();
    }
  },

  async _stopVoiceInput() {
    if (this.voiceState !== 'listening') return;
    if (this._voiceElapsedTimer) { clearInterval(this._voiceElapsedTimer); this._voiceElapsedTimer = null; }
    if (this._voiceMaxTimer) { clearTimeout(this._voiceMaxTimer); this._voiceMaxTimer = null; }
    this.voiceState = 'processing';
    this.render();

    try {
      // 最短录音时长保护
      const minMs = 650;
      if (this._voiceRecordingStartedAt) {
        const remaining = minMs - (Date.now() - this._voiceRecordingStartedAt);
        if (remaining > 0) await new Promise(r => setTimeout(r, remaining));
      }

      const recorder = this._voiceRecorder;
      let audio;
      if (recorder && recorder.state !== 'inactive') {
        audio = await new Promise((resolve) => {
          let settled = false;
          const fallback = setTimeout(() => {
            if (!settled) {
              settled = true;
              resolve(new Blob(this._voiceRecordedChunks, { type: this._voiceRecorderMimeType || 'audio/webm' }));
            }
          }, 5000);
          recorder.onstop = () => {
            if (settled) return;
            settled = true;
            clearTimeout(fallback);
            resolve(new Blob(this._voiceRecordedChunks, { type: this._voiceRecorderMimeType || 'audio/webm' }));
          };
          try { recorder.requestData(); } catch {}
          try { recorder.stop(); } catch {
            if (!settled) {
              settled = true;
              clearTimeout(fallback);
              resolve(new Blob(this._voiceRecordedChunks, { type: this._voiceRecorderMimeType || 'audio/webm' }));
            }
          }
        });
      } else {
        audio = new Blob(this._voiceRecordedChunks || [], { type: this._voiceRecorderMimeType || 'audio/webm' });
      }

      const inputMeter = this._stopVoiceInputMeter();
      this._cleanupVoiceRecorder();

      if (!audio.size) throw new Error('没有录到声音，请再试一次');
      if (inputMeter.available && inputMeter.maxLevel < 4) {
        throw new Error('麦克风没有收到声音，请检查权限或靠近麦克风');
      }

      const text = await ChatApi.transcribeAudio(audio, 'zh-CN');
      this.voiceState = 'idle';
      this.voiceErrorMessage = '';

      // 语音识别结果追加到输入框已有内容后面，而非覆盖（用户先打字再语音时，已有文字应保留）
      this.isVoiceMode = false;
      const convId = ChatStore.getActiveConversationId();
      const existingDraft = this._getComposerDraft(convId);
      const separator = existingDraft && !existingDraft.endsWith(' ') ? ' ' : '';
      const combined = (existingDraft + separator + text).slice(0, 2000);
      this._setComposerDraft(convId, combined);
      this.render();
      setTimeout(() => {
        const input = document.getElementById('composer-input');
        if (input) {
          input.focus();
          input.selectionStart = input.selectionEnd = input.value.length;
        }
      }, 50);
    } catch (error) {
      this._cleanupVoiceRecorder();
      this.voiceState = 'idle';
      this.voiceErrorMessage = error.message || '语音转文字失败';
      Toast.show(this.voiceErrorMessage, 'error');
      this.render();
    }
  },

  _cancelVoiceInput() {
    if (this._voiceElapsedTimer) { clearInterval(this._voiceElapsedTimer); this._voiceElapsedTimer = null; }
    if (this._voiceMaxTimer) { clearTimeout(this._voiceMaxTimer); this._voiceMaxTimer = null; }
    this._cleanupVoiceRecorder();
    this.voiceState = 'idle';
    this.voiceErrorMessage = '';
    this.voiceElapsedSeconds = 0;
  },

  _cleanupVoiceRecorder() {
    if (this._voiceRecorder) {
      this._voiceRecorder.ondataavailable = null;
      this._voiceRecorder.onerror = null;
      this._voiceRecorder.onstop = null;
      if (this._voiceRecorder.state !== 'inactive') {
        try { this._voiceRecorder.stop(); } catch {}
      }
      this._voiceRecorder = null;
    }
    this._cleanupVoiceInputMeter();
    if (this._voiceStream) {
      for (const track of this._voiceStream.getTracks()) track.stop();
      this._voiceStream = null;
    }
    this._voiceRecordedChunks = [];
    this._voiceRecorderMimeType = '';
    this._voiceRecordingStartedAt = 0;
  },

  // 对齐 Vue 版 useVoiceTextInput 音频输入电平检测
  _startVoiceInputMeter(stream) {
    this._cleanupVoiceInputMeter();
    if (typeof window === 'undefined') return;
    const AudioContextCtor = window.AudioContext || window.webkitAudioContext;
    if (!AudioContextCtor) return;
    try {
      this._voiceAudioContext = new AudioContextCtor();
      this._voiceAudioSource = this._voiceAudioContext.createMediaStreamSource(stream);
      this._voiceAudioAnalyser = this._voiceAudioContext.createAnalyser();
      this._voiceAudioAnalyser.fftSize = 512;
      this._voiceAudioSource.connect(this._voiceAudioAnalyser);
      this._voiceInputMeterAvailable = true;
      this._voiceMaxInputLevel = 0;
      const samples = new Uint8Array(this._voiceAudioAnalyser.fftSize);
      const measure = () => {
        if (!this._voiceAudioAnalyser) return;
        this._voiceAudioAnalyser.getByteTimeDomainData(samples);
        let peak = 0;
        for (const sample of samples) {
          peak = Math.max(peak, Math.abs(sample - 128));
        }
        this._voiceMaxInputLevel = Math.max(this._voiceMaxInputLevel, peak);
        this._voiceMeterRafId = requestAnimationFrame(measure);
      };
      measure();
    } catch {
      this._cleanupVoiceInputMeter();
    }
  },

  _stopVoiceInputMeter() {
    const result = {
      available: this._voiceInputMeterAvailable || false,
      maxLevel: this._voiceMaxInputLevel || 0,
    };
    this._cleanupVoiceInputMeter();
    return result;
  },

  _cleanupVoiceInputMeter() {
    if (this._voiceMeterRafId) {
      cancelAnimationFrame(this._voiceMeterRafId);
      this._voiceMeterRafId = 0;
    }
    try { this._voiceAudioSource?.disconnect(); } catch {}
    try { this._voiceAudioAnalyser?.disconnect(); } catch {}
    if (this._voiceAudioContext) {
      this._voiceAudioContext.close().catch(() => {});
    }
    this._voiceAudioSource = null;
    this._voiceAudioAnalyser = null;
    this._voiceAudioContext = null;
    this._voiceInputMeterAvailable = false;
    this._voiceMaxInputLevel = 0;
  },

  // ── Helpers ──
  _isAgentConv(conv) {
    return conv && (conv.type==='agent' || conv.scope==='personal_workspace');
  },
  _convSubtitle(conv) {
    if (!conv) return '';
    if (conv.type==='agent') return 'AI 智能助手';
    if (conv.scope==='personal_workspace') return 'AI 个人工作空间';
    if (conv.type==='group') return '群组会话';
    return '私聊';
  },
  _isDesktop() {
    return window.matchMedia('(min-width: 640px)').matches;
  },
  _parseDeliveryTarget(raw) {
    if (!raw) return { type: 'workspace' };
    if (typeof raw === 'string') {
      try {
        const parsed = JSON.parse(raw);
        return parsed && typeof parsed === 'object' ? parsed : { type: 'workspace' };
      } catch {
        return { type: 'workspace' };
      }
    }
    return raw;
  },
  _isWorkspaceTask(task, conversationId) {
    const target = this._parseDeliveryTarget(task?.deliveryTarget);
    return (
      task?.sourceConversationId === conversationId ||
      target?.conversationId === conversationId ||
      target?.type === 'workspace'
    );
  },
  _workspaceSourceLabel(task, conversationId) {
    if (!task?.sourceConversationId || task.sourceConversationId === conversationId) return 'AI 空间创建';
    const source = ChatStore.getConversations().find(c => c.id === task.sourceConversationId);
    return `${source?.name || '来源会话'}同步到空间`;
  },
  _workspaceDeliveryLabel(task) {
    const target = this._parseDeliveryTarget(task?.deliveryTarget);
    if (target?.type === 'workspace') return '仅在 AI 空间';
    if (target?.type === 'both') return '群聊与 AI 空间';
    if (target?.type === 'conversation') return '来自会话';
    return '未设置';
  },
  _formatWorkspaceDueAt(value) {
    if (!value) return '无截止时间';
    return TimeUtils.formatSmart(value);
  },
  _recentWorkspaceSummary(conversationId) {
    const msgs = ChatStore.getMessages(conversationId) || [];
    for (let i = msgs.length - 1; i >= 0; i -= 1) {
      const content = String(msgs[i]?.content || '').trim();
      if (content) return content;
    }
    return '';
  },
  _openDevicesPage() {
    window.location.hash = '#/devices';
  },
  _handleBindDeviceAction() {
    const conv = ChatStore.getActiveConversation();
    if (!conv) {
      Toast.warn('请先进入会话再绑定设备');
      return;
    }
    if (conv.scope !== 'personal_workspace' && conv.type !== 'group') {
      Toast.warn('请先进入群聊再绑定设备');
      return;
    }
    this._openDevicesPage();
  },
  _esc(str) {
    return String(str||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
  },

  // ── 创建群聊对话框 ─
  _openCreateGroupDialog() {
    this.createGroupDialogOpen = true;
    this.createGroupName = '';
    this.createGroupError = '';
    this.createGroupSubmitting = false;
    this._renderDialogContent();
    const dialog = document.getElementById('create-group-dialog');
    if (dialog) dialog.style.display = 'flex';
    setTimeout(() => {
      const input = document.getElementById('cg-name-input');
      if (input) input.focus();
    }, 10);
  },

  _closeCreateGroupDialog() {
    this.createGroupDialogOpen = false;
    this.createGroupError = '';
    this.createGroupSubmitting = false;
    this.createGroupName = '';
    const dialog = document.getElementById('create-group-dialog');
    if (dialog) dialog.style.display = 'none';
  },

  /** 仅更新弹窗内容，不触发全页重绘 */
  _renderDialogContent() {
    const dialog = document.getElementById('create-group-dialog');
    if (!dialog) return;
    const submitting = this.createGroupSubmitting;
    const error = this.createGroupError;
    const errorHtml = error ? '<p class="cg-error">' + this._esc(error) + '</p>' : '<p class="hint">群聊 ID 将由服务端自动生成</p>';
    const nameValue = this._esc(this.createGroupName);
    dialog.innerHTML =
      '<div class="modal-card">' +
        '<div class="modal-header">' +
          '<h3>新建群聊</h3>' +
          '<button id="cg-close-btn" class="icon-btn"' + (submitting ? ' disabled' : '') + '>&times;</button>' +
        '</div>' +
        '<div class="modal-body">' +
          '<label>群聊名称</label>' +
          '<input id="cg-name-input" type="text" maxlength="30" placeholder="输入群聊名称" value="' + nameValue + '"' + (submitting ? ' disabled' : '') + '>' +
          errorHtml +
        '</div>' +
        '<div class="modal-footer">' +
          '<button id="cg-cancel-btn" class="btn-secondary"' + (submitting ? ' disabled' : '') + '>取消</button>' +
          '<button id="cg-confirm-btn" class="btn-primary"' + (submitting ? ' disabled' : '') + '>' + (submitting ? '创建中...' : '确认创建') + '</button>' +
        '</div>' +
      '</div>';
    this._bindCreateGroupDialogEvents();
  },

  /**
   * 创建群聊确认（对齐 Vue 版 onCreateGroupConfirm）
   */
  async _handleCreateGroupConfirm() {
    const input = document.getElementById('cg-name-input');
    if (!input) return;
    const name = input.value.trim();
    if (!name) return;

    this.createGroupName = input.value;
    this.createGroupSubmitting = true;
    this.createGroupError = '';
    this._renderDialogContent();

    try {
      const conversationId = await ChatStore.createGroupConversation(name);
      this._closeCreateGroupDialog();
      this._activateConversation(conversationId);
      Toast.success('群聊已创建');
    } catch (e) {
      this.createGroupError = e.message || '创建群聊失败';
      this.createGroupSubmitting = false;
      this._renderDialogContent();
      return;
    }
    this.createGroupSubmitting = false;
  },

  _renderCreateGroupDialog() {
    // 弹窗已通过 _ensureDialogElement 挂载到 body，不受 render() → innerHTML 影响
    return '';
  },

  // ── 删除群聊确认弹窗（对齐 Vue 版 onDeleteConversation） ──
  _openDeleteConversationDialog(conversationId) {
    const conv = ChatStore.getConversations().find(c => c.id === conversationId);
    if (!conv) return;
    // 对齐 Vue 版：个人工作空间不能删除
    if (conv.scope === 'personal_workspace') {
      Toast.warn('个人工作空间不能删除');
      return;
    }
    if (conv.type === 'agent') {
      Toast.warn('该会话不能删除');
      return;
    }
    this.deleteConversationDialogOpen = true;
    this.deleteConversationTargetId = conversationId;
    this.deleteConversationTargetName = conv.name || conversationId.replace(/^#/, '');
    this._renderDeleteConversationDialogContent();
    const dialog = document.getElementById('delete-conversation-dialog');
    if (dialog) dialog.style.display = 'flex';
  },

  _closeDeleteConversationDialog() {
    this.deleteConversationDialogOpen = false;
    this.deleteConversationTargetId = '';
    this.deleteConversationTargetName = '';
    const dialog = document.getElementById('delete-conversation-dialog');
    if (dialog) dialog.style.display = 'none';
  },

  _renderDeleteConversationDialogContent() {
    const dialog = document.getElementById('delete-conversation-dialog');
    if (!dialog) return;
    const name = this._esc(this.deleteConversationTargetName);
    // 文案对齐 Vue 版 window.confirm，样式对齐设计规范：危险操作弹窗
    dialog.innerHTML =
      '<div class="modal-card dc-modal">' +
        '<div class="dc-icon-wrap">' +
          '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h18"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6"/><path d="M8 6V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>' +
        '</div>' +
        '<div class="dc-content">' +
          '<h3 class="dc-title">确定删除群聊「<span class="dc-name">' + name + '</span>」？</h3>' +
          '<p class="dc-hint">删除后本机不再显示这个会话，也不会被新消息自动恢复。</p>' +
        '</div>' +
        '<div class="modal-footer dc-footer">' +
          '<button id="dc-cancel-btn" class="btn btn-secondary">取消</button>' +
          '<button id="dc-confirm-btn" class="btn btn-danger">确定删除</button>' +
        '</div>' +
      '</div>';
    this._bindDeleteConversationDialogEvents();
  },

  _bindDeleteConversationDialogEvents() {
    const cancelBtn = document.getElementById('dc-cancel-btn');
    if (cancelBtn) cancelBtn.onclick = () => this._closeDeleteConversationDialog();
    const confirmBtn = document.getElementById('dc-confirm-btn');
    if (confirmBtn) confirmBtn.onclick = () => this._handleDeleteConversationConfirm();
    const overlay = document.getElementById('delete-conversation-dialog');
    if (overlay) {
      overlay.onclick = (e) => {
        if (e.target === overlay) this._closeDeleteConversationDialog();
      };
    }
  },

  _handleDeleteConversationConfirm() {
    const id = this.deleteConversationTargetId;
    if (!id) return;
    // 对齐 Vue 版 onDeleteConversation：调用 deleteConversationFromList（纯本地删除）
    const wasActive = ChatStore.getActiveConversationId() === id;
    const deleted = ChatStore.deleteConversationFromList(id);
    if (!deleted) {
      Toast.warn('会话删除失败');
      this._closeDeleteConversationDialog();
      return;
    }
    // 对齐 Vue 版：删除的是当前活跃会话时，切换到下一个可用会话
    if (wasActive) {
      const convs = ChatStore.getConversations();
      const nextConv = convs.find(c => c.scope === 'personal_workspace')
        || convs.find(c => c.type === 'agent')
        || convs[0]
        || null;
      if (nextConv) {
        ChatStore.setActiveConversation(nextConv.id);
        UiStore.setActiveConversation(nextConv.id, true);
      } else {
        UiStore.resetToListView();
      }
    }
    this._closeDeleteConversationDialog();
    Toast.success('会话已删除');
  },

  _memberRoleLabel(member) {
    const role = member.role || 'member';
    if (role === 'owner') return '群主';
    if (role === 'admin') return '管理员';
    if (role === 'assistant') return '助手';
    return '成员';
  },

  _memberRoleClass(member) {
    const role = member.role || 'member';
    if (role === 'owner') return 'role-owner';
    if (role === 'admin') return 'role-admin';
    if (role === 'assistant') return 'role-assistant';
    return 'role-member';
  },

  _memberSubtitle(member) {
    if (member.memberKind === 'user') return member.phone ? '手机号成员' : '成员';
    if (member.memberKind === 'phone') return '手机号成员';
    const nodeType = member.node?.nodeType || 'node';
    return `${nodeType}`;
  },

  _memberTitle(member) {
    if (member.memberKind === 'user') return member.phone || member.userId || '未知成员';
    if (member.memberKind === 'phone') return member.phone || '手机号成员';
    return member.node?.name || `节点 ${member.nodeId || ''}`.trim();
  },

  _memberInitial(member) {
    const title = this._memberTitle(member);
    return title.length >= 2 ? title.slice(-2) : title || '成员';
  },

  _memberAvatarMarkup(member, fallbackText) {
    if (member.memberKind === 'node' && member.node?.nodeType === 'AI_AGENT') {
      return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="3"/><path d="M9 10a3 3 0 016 0v2a3 3 0 01-6 0V10z"/><circle cx="12" cy="17" r="1.5"/></svg>`;
    }
    if (member.memberKind === 'node') {
      return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>`;
    }
    return this._esc(fallbackText);
  },

  _isSelfMember(member) {
    const userId = AuthStore.getUserId() || '';
    const phone = AuthStore.getPhone() || '';
    if (!userId && !phone) return false;
    if (member.memberKind === 'user') {
      return Boolean((userId && member.userId === userId) || (phone && (member.phone === phone || member.userId === phone)));
    }
    if (member.memberKind === 'phone') {
      return Boolean(phone && member.phone === phone);
    }
    return false;
  },

  _getDisplayedRoomMembers(conversationId) {
    if (!conversationId) return [];
    return Array.isArray(this.groupMembers) ? this.groupMembers : [];
  },

  _memberStatusText(members, loading, loaded, error) {
    if (members.length > 0) {
      if (loading) return `${members.length} 人 · 更新中`;
      return `${members.length} 人`;
    }
    if (error) return '同步失败';
    if (loading || !loaded) return '同步中';
    return '暂无成员';
  },

  // ── Posts Panel ─
  _renderPostsPanel() {
    const conv = ChatStore.getActiveConversation();
    const isWorkspace = conv && conv.scope === 'personal_workspace';
    const isGroup = conv && conv.type === 'group';
    const posts = isGroup ? ChatStore.getPosts(conv.id) : [];
    const loading = this.postsLoading;
    const error = this.postsError;
    const panelView = this.postsPanelView || 'board';
    const members = this._getDisplayedRoomMembers(conv?.id);
    const membersLoading = this.groupMembersLoading;
    const membersLoaded = this.groupMembersLoaded;
    const membersError = this.groupMembersError;
    const membersNotice = this.groupMembersNotice;
    const inviteOpen = this.invitePanelOpen;
    const invitePhone = this.invitePhone;

    const orderedPosts = [...posts].sort((a, b) => {
      const aScore = a.deadlineAt || a.createdAt;
      const bScore = b.deadlineAt || b.createdAt;
      return bScore - aScore;
    });

    const memberCount = members.length;
    const memberPreview = members.slice(0, 5);
    const memberStatusText = this._memberStatusText(members, membersLoading, membersLoaded, membersError);

    if (isWorkspace) {
      const allTasks = ChatStore.getPersonalTasksForCurrentUser();
      const workspaceTasks = allTasks.filter(task => this._isWorkspaceTask(task, conv.id));
      const outsideWorkspaceTaskCount = allTasks.filter(task => !this._isWorkspaceTask(task, conv.id)).length;
      const pendingTasks = workspaceTasks.filter(task => task.status !== 'done' && task.status !== 'canceled');
      const completedTasks = workspaceTasks.filter(task => task.status === 'done');
      const canceledTasks = workspaceTasks.filter(task => task.status === 'canceled');
      const targetGroups = ChatStore.getConversations().filter(c => c.type === 'group' && c.scope !== 'personal_workspace');
      const draftActions = ChatStore.getWorkspaceDraftActions(conv.id);
      const boundDevices = ChatStore.getBoundDevices();
      const onlineDevices = boundDevices.filter(device => device.status === 'online');
      const selectedPublishTarget = targetGroups.find(item => item.id === this.workspacePublishTargetId) || targetGroups[0] || null;
      if (!this.workspacePublishTargetId && targetGroups[0]) {
        this.workspacePublishTargetId = targetGroups[0].id;
      }
      const recentSummary = this._recentWorkspaceSummary(conv.id);
      const fullScreenStyle = this._isDesktop()
        ? 'width:min(30rem,100%);height:100%;border-radius:1.5rem 0 0 1.5rem;'
        : 'width:100%;height:100%;border-radius:0;';

      return `
      <div id="posts-panel" class="posts-panel ${this.postsPanelOpen ? 'open' : ''}" style="display:${this.postsPanelOpen ? 'flex' : 'none'};">
        <div class="pp-overlay" id="pp-overlay"></div>
        <div class="pp-drawer" style="${fullScreenStyle}background:var(--app-page-bg);">
          <div class="pp-header" style="padding:1rem 1rem 0.75rem;border-bottom:1px solid var(--app-border);background:var(--app-surface);">
            <div>
              <div class="pp-title">${this._esc(conv.name || '我的 AI 工作空间')}</div>
              <div style="margin-top:0.3rem;font-size:0.8rem;color:var(--app-muted);">${this._esc(conv.topic || '文件、任务和设备同步')}</div>
            </div>
            <div class="pp-actions">
              <button class="pp-btn pp-close-btn" id="pp-close-btn" title="关闭">&times;</button>
            </div>
          </div>
          <div class="pp-body" style="padding:1rem 1rem 1.5rem;overflow-y:auto;background:var(--app-page-bg);">
            <section style="border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
              <div style="display:flex;align-items:center;gap:0.75rem;">
                <div style="display:flex;height:2.75rem;width:2.75rem;align-items:center;justify-content:center;border-radius:0.9rem;background:var(--app-brand-soft);color:var(--app-brand);">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>
                </div>
                <div style="min-width:0;flex:1;">
                  <div style="font-size:1rem;font-weight:600;color:var(--app-text);">正在处理</div>
                  <div style="font-size:0.8rem;color:var(--app-muted);">当前 AI 空间</div>
                </div>
                <span style="display:inline-flex;height:0.65rem;width:0.65rem;border-radius:999px;background:#10b981;"></span>
              </div>
              <div style="margin-top:0.95rem;display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0.7rem;">
                <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.85rem 0.5rem;text-align:center;">
                  <div style="font-size:1.2rem;font-weight:700;color:var(--app-brand);">${pendingTasks.length}</div>
                  <div style="margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">空间待办</div>
                </div>
                <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.85rem 0.5rem;text-align:center;">
                  <div style="font-size:1.2rem;font-weight:700;color:var(--app-brand);">${draftActions.length}</div>
                  <div style="margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">待确认</div>
                </div>
                <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.85rem 0.5rem;text-align:center;">
                  <div style="font-size:1.2rem;font-weight:700;color:var(--app-brand);">${onlineDevices.length}</div>
                  <div style="margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">设备在线</div>
                </div>
              </div>
              <div style="margin-top:0.95rem;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.85rem 1rem;">
                <div style="font-size:0.8rem;font-weight:600;color:var(--app-text);">最近上下文</div>
                <div style="margin-top:0.35rem;font-size:0.78rem;line-height:1.6;color:var(--app-muted);">${this._esc(recentSummary || '这里会显示当前 AI 空间最近生成的摘要、草稿或提醒。')}</div>
              </div>
              <button type="button" id="pp-workspace-open-ai-btn" style="margin-top:0.95rem;display:flex;width:100%;align-items:center;justify-content:center;gap:0.45rem;border-radius:1rem;border:1px solid var(--app-border);background:var(--app-subtle-bg);padding:0.8rem 1rem;font-size:0.86rem;font-weight:600;color:var(--app-brand);">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>
                打开工作台
              </button>
            </section>

            <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
              <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
                <div>
                  <div style="font-size:1rem;font-weight:600;color:var(--app-text);">快捷操作</div>
                  <div style="margin-top:0.25rem;font-size:0.8rem;color:var(--app-muted);">基于当前 AI 空间继续处理</div>
                </div>
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="var(--app-brand)" stroke-width="2"><path d="M12 3l2.5 5.5L20 10l-5.5 2.5L12 18l-2.5-5.5L4 10l5.5-2.5z"/></svg>
              </div>
              <div style="margin-top:0.95rem;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:0.7rem;">
                <button type="button" data-workspace-action="create-task" style="display:flex;min-height:4.2rem;align-items:center;gap:0.7rem;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem;text-align:left;">
                  <span style="display:flex;height:2.25rem;width:2.25rem;align-items:center;justify-content:center;border-radius:0.85rem;background:var(--app-brand-soft);color:var(--app-brand);">
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 11l3 3L22 4"/><path d="M21 12v7a2 2 0 01-2 2H5a2 2 0 01-2-2V5a2 2 0 012-2h11"/></svg>
                  </span>
                  <span><span style="display:block;font-size:0.86rem;font-weight:600;color:var(--app-text);">新建任务</span><span style="display:block;margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">创建空间提醒</span></span>
                </button>
                <button type="button" data-workspace-action="summarize" style="display:flex;min-height:4.2rem;align-items:center;gap:0.7rem;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem;text-align:left;">
                  <span style="display:flex;height:2.25rem;width:2.25rem;align-items:center;justify-content:center;border-radius:0.85rem;background:var(--app-brand-soft);color:var(--app-brand);">
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15a2 2 0 01-2 2H7l-4 4V5a2 2 0 012-2h14a2 2 0 012 2z"/></svg>
                  </span>
                  <span><span style="display:block;font-size:0.86rem;font-weight:600;color:var(--app-text);">总结对话</span><span style="display:block;margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">整理当前上下文</span></span>
                </button>
                <button type="button" data-workspace-action="publish-draft-quick" style="display:flex;min-height:4.2rem;align-items:center;gap:0.7rem;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem;text-align:left;">
                  <span style="display:flex;height:2.25rem;width:2.25rem;align-items:center;justify-content:center;border-radius:0.85rem;background:var(--app-brand-soft);color:var(--app-brand);">
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 2L11 13"/><path d="M22 2l-7 20-4-9-9-4 20-7z"/></svg>
                  </span>
                  <span><span style="display:block;font-size:0.86rem;font-weight:600;color:var(--app-text);">发布到群</span><span style="display:block;margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">${this._esc(selectedPublishTarget?.name || '请选择目标群')}</span></span>
                </button>
                <button type="button" data-workspace-action="open-devices" style="display:flex;min-height:4.2rem;align-items:center;gap:0.7rem;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.8rem;text-align:left;">
                  <span style="display:flex;height:2.25rem;width:2.25rem;align-items:center;justify-content:center;border-radius:0.85rem;background:var(--app-brand-soft);color:var(--app-brand);">
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>
                  </span>
                  <span><span style="display:block;font-size:0.86rem;font-weight:600;color:var(--app-text);">投到设备</span><span style="display:block;margin-top:0.2rem;font-size:0.72rem;color:var(--app-muted);">电视或笔记本</span></span>
                </button>
              </div>
            </section>

            <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
              <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
                <div>
                  <div style="font-size:1rem;font-weight:600;color:var(--app-text);">待确认</div>
                  <div style="margin-top:0.25rem;font-size:0.8rem;color:var(--app-muted);">AI 生成但还没有发布的草稿</div>
                </div>
              </div>
              ${draftActions.length ? `
                ${targetGroups.length ? `
                  <label style="display:block;margin-top:0.9rem;">
                    <select id="pp-workspace-target-select" style="width:100%;height:2.6rem;border:1px solid var(--app-border);border-radius:0.9rem;background:var(--app-subtle-bg);padding:0 0.9rem;font-size:0.82rem;font-weight:600;color:var(--app-text);outline:none;">
                      ${targetGroups.map(item => `<option value="${this._esc(item.id)}" ${this.workspacePublishTargetId === item.id ? 'selected' : ''}>发布目标：${this._esc(item.name)}</option>`).join('')}
                    </select>
                  </label>
                ` : ''}
                <div style="margin-top:0.95rem;display:flex;flex-direction:column;gap:0.7rem;">
                  ${draftActions.map(action => `
                    <article style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.9rem;">
                      <div style="font-size:0.94rem;font-weight:600;line-height:1.5;color:var(--app-text);">${this._esc(action.draft.title || '待确认草稿')}</div>
                      <div style="margin-top:0.35rem;font-size:0.78rem;line-height:1.6;color:var(--app-muted);">${this._esc(action.draft.summary || '确认内容后，可继续发布到指定群。')}</div>
                      <div style="margin-top:0.55rem;display:flex;flex-wrap:wrap;gap:0.4rem;">
                        <span style="border-radius:999px;background:#eef2ff;padding:0.24rem 0.55rem;font-size:0.72rem;color:var(--app-muted);">${this._esc(action.skillId)}</span>
                        <span style="border-radius:999px;background:#eef2ff;padding:0.24rem 0.55rem;font-size:0.72rem;color:var(--app-muted);">未发布</span>
                      </div>
                      ${targetGroups.length ? `
                        <button type="button" data-workspace-action="publish-draft" data-skill-id="${this._esc(action.skillId)}" style="margin-top:0.75rem;border:none;border-radius:999px;background:var(--app-brand);padding:0.42rem 0.8rem;font-size:0.76rem;font-weight:600;color:#fff;">
                          发布到 ${this._esc(targetGroups.find(item => item.id === this.workspacePublishTargetId)?.name || targetGroups[0].name)}
                        </button>
                      ` : '<div style="margin-top:0.75rem;font-size:0.78rem;color:var(--app-muted);">暂无可发布的群聊</div>'}
                    </article>
                  `).join('')}
                </div>
              ` : `
                <div style="margin-top:0.95rem;border:1px dashed var(--app-border);border-radius:1rem;padding:1rem;text-align:center;font-size:0.84rem;line-height:1.6;color:var(--app-muted);">
                  暂无待确认草稿。让 AI 继续生成任务、公告或活动后，会先出现在这里。
                </div>
              `}
            </section>

            <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
              <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
                <div>
                  <div style="font-size:1rem;font-weight:600;color:var(--app-text);">AI 空间任务</div>
                  <div style="margin-top:0.25rem;font-size:0.8rem;color:var(--app-muted);">待办 ${pendingTasks.length} · 已完成 ${completedTasks.length} · 已取消 ${canceledTasks.length}</div>
                </div>
              </div>
   
              ${workspaceTasks.length ? `
                <div style="margin-top:0.95rem;display:flex;flex-direction:column;gap:0.7rem;">
                  ${workspaceTasks.slice(0, 3).map(task => {
                    const isDone = task.status === 'done';
                    const isCanceled = task.status === 'canceled';
                    const iconBg = isDone
                      ? 'background:rgba(16,185,129,0.12);color:#059669;'
                      : isCanceled
                        ? 'background:rgba(244,63,94,0.12);color:#e11d48;'
                        : 'background:var(--app-brand-soft);color:var(--app-brand);';
                    const iconSvg = isDone
                      ? '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg>'
                      : isCanceled
                        ? '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M15 9l-6 6M9 9l6 6"/></svg>'
                        : '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/></svg>';
                    const statusBadge = isDone
                      ? '<span style="border-radius:999px;background:rgba(16,185,129,0.12);padding:0.24rem 0.55rem;font-size:0.72rem;font-weight:600;color:#059669;">已完成</span>'
                      : isCanceled
                        ? '<span style="border-radius:999px;background:rgba(244,63,94,0.12);padding:0.24rem 0.55rem;font-size:0.72rem;font-weight:600;color:#e11d48;">已取消</span>'
                        : '<span style="border-radius:999px;background:var(--app-brand-soft);padding:0.24rem 0.55rem;font-size:0.72rem;font-weight:600;color:var(--app-brand);">待办</span>';
                    return `
                    <article style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.9rem;">
                      <div style="display:flex;align-items:flex-start;gap:0.75rem;">
                        <div style="margin-top:0.125rem;flex-shrink:0;width:2rem;height:2rem;border-radius:0.8rem;display:flex;align-items:center;justify-content:center;${iconBg}">${iconSvg}</div>
                        <div style="flex:1;min-width:0;">
                          <div style="font-size:0.94rem;font-weight:600;line-height:1.5;color:var(--app-text);">${this._esc(task.title)}</div>
                          ${task.summary ? `<div style="margin-top:0.35rem;font-size:0.78rem;line-height:1.6;color:var(--app-muted);">${this._esc(task.summary)}</div>` : ''}
                          <div style="margin-top:0.55rem;display:flex;flex-wrap:wrap;gap:0.4rem;align-items:center;">
                            ${statusBadge}
                            <span style="border-radius:999px;background:#eef2ff;padding:0.24rem 0.55rem;font-size:0.72rem;color:var(--app-muted);">${this._esc(this._formatWorkspaceDueAt(task.dueAt))}</span>
                            <span style="border-radius:999px;background:#eef2ff;padding:0.24rem 0.55rem;font-size:0.72rem;color:var(--app-muted);">${this._esc(this._workspaceSourceLabel(task, conv.id))}</span>
                            <span style="border-radius:999px;background:#eef2ff;padding:0.24rem 0.55rem;font-size:0.72rem;color:var(--app-muted);">${this._esc(this._workspaceDeliveryLabel(task))}</span>
                          </div>
                          ${task.sourceConversationId && task.sourceConversationId !== conv.id ? `
                            <button type="button" data-workspace-action="open-task-source" data-task-id="${this._esc(task.id)}" style="margin-top:0.7rem;display:inline-flex;align-items:center;gap:0.35rem;border:1px solid var(--app-border);border-radius:999px;background:transparent;padding:0.42rem 0.8rem;font-size:0.76rem;font-weight:600;color:var(--app-muted);">
                              回到来源群
                            </button>
                          ` : ''}
                        </div>
                      </div>
                      ${task.status !== 'done' && task.status !== 'canceled' ? `
                        <div style="margin-top:0.75rem;display:flex;justify-content:flex-end;gap:0.5rem;">
                          <button type="button" data-workspace-action="reschedule-task" data-task-id="${this._esc(task.id)}" style="border:1px solid var(--app-border);border-radius:999px;background:transparent;padding:0.42rem 0.7rem;font-size:0.76rem;color:var(--app-muted);">延后1小时</button>
                          <button type="button" data-workspace-action="cancel-task" data-task-id="${this._esc(task.id)}" style="border:1px solid rgba(251,113,133,0.3);border-radius:999px;background:transparent;padding:0.42rem 0.7rem;font-size:0.76rem;color:#e11d48;">取消</button>
                          <button type="button" data-workspace-action="complete-task" data-task-id="${this._esc(task.id)}" style="border:none;border-radius:999px;background:var(--app-brand);padding:0.42rem 0.8rem;font-size:0.76rem;font-weight:600;color:#fff;">完成</button>
                        </div>
                      ` : ''}
                    </article>
                  `;}).join('')}
                  ${workspaceTasks.length > 3 ? `
                    <button type="button" id="ws-view-more-tasks" style="margin-top:0.35rem;display:flex;align-items:center;justify-content:center;gap:0.35rem;width:100%;border:1px solid var(--app-border);border-radius:0.75rem;background:transparent;padding:0.6rem;font-size:0.8rem;font-weight:600;color:var(--app-brand);cursor:pointer;">
                      查看全部 ${workspaceTasks.length} 条任务
                      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
                    </button>
                  ` : ''}
                </div>
              ` : `
                <div style="margin-top:0.95rem;border:1px dashed var(--app-border);border-radius:1rem;padding:1rem;text-align:center;font-size:0.84rem;line-height:1.6;color:var(--app-muted);">
                  这里暂时没有 AI 空间任务。其他群里的个人提醒会留在“任务”页，不会混进当前空间。
                </div>
              `}
            </section>
            <section style="margin-top:1rem;border:1px solid var(--app-border);border-radius:1.2rem;background:var(--app-surface);padding:1rem;box-shadow:0 10px 24px rgba(36,47,90,0.05);">
              <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
                <div>
                  <div style="font-size:1rem;font-weight:600;color:var(--app-text);">可用设备</div>
                  <div style="margin-top:0.25rem;font-size:0.8rem;color:var(--app-muted);">设备绑定入口与 Vue 移动端保持一致</div>
                </div>
                <button type="button" data-workspace-action="open-devices" style="border:1px solid var(--app-border);border-radius:999px;background:var(--app-subtle-bg);padding:0.45rem 0.8rem;font-size:0.78rem;font-weight:600;color:var(--app-muted);">管理</button>
              </div>
              ${boundDevices.length ? `
                <div style="margin-top:0.95rem;display:flex;flex-direction:column;gap:0.7rem;">
                  ${boundDevices.slice(0, 4).map(device => `
                    <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.85rem 0.95rem;">
                      <div style="font-size:0.88rem;font-weight:600;color:var(--app-text);">${this._esc(device.deviceName)}</div>
                      <div style="margin-top:0.25rem;font-size:0.76rem;color:var(--app-muted);">${this._esc(device.boundConversationName || '当前会话')} · ${device.status === 'online' ? '在线' : '离线'}</div>
                    </div>
                  `).join('')}
                </div>
              ` : `
                <div style="margin-top:0.95rem;border:1px dashed var(--app-border);border-radius:1rem;padding:1rem;text-align:center;font-size:0.84rem;line-height:1.6;color:var(--app-muted);">
                  暂无可用设备。可通过“扫码/绑定设备”或此处“管理”进入设备页继续绑定。
                </div>
              `}
            </section>
          </div>
        </div>
      </div>`;
    }

    // ── Board View ──
    if (panelView === 'board') {
      return `
      <div id="posts-panel" class="posts-panel ${this.postsPanelOpen ? 'open' : ''}" style="display:${this.postsPanelOpen ? 'flex' : 'none'};">
        <div class="pp-overlay" id="pp-overlay"></div>
        <div class="pp-drawer">
          <div class="pp-header">
            <div class="pp-title">${this._esc(conv?.name ? `${conv.name} 看板` : '群组看板')}</div>
            <div class="pp-actions">
              <button class="pp-btn pp-close-btn" id="pp-close-btn" title="关闭">&times;</button>
            </div>
          </div>
          ${isGroup ? `
          <div class="pp-body">
            <!-- Banner -->
            <div class="pp-board-banner">
              <div class="pp-board-banner-bg"></div>
              <div class="pp-board-banner-content">
                <div class="pp-board-banner-text">
                  <div class="pp-board-banner-name">${this._esc(conv.name)}</div>
                  <div class="pp-board-banner-topic">${this._esc(conv.topic || '作业、活动与群管理')}</div>
                </div>
                <button class="pp-board-create-btn" id="pp-banner-create-btn" title="发布新任务">
                  <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 5v14M5 12h14"/></svg>
                  发布新任务
                </button>
              </div>
            </div>

            <!-- Member Preview Section -->
            <div class="pp-member-preview" id="pp-member-preview-btn">
              <div class="pp-member-preview-main">
                <div class="pp-member-preview-icon">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M16 21v-2a4 4 0 00-4-4H6a4 4 0 00-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M22 21v-2a4 4 0 00-3-3.87M16 3.13a4 4 0 010 7.75"/></svg>
                </div>
                <div class="pp-member-preview-info">
                  <div class="pp-member-preview-header">
                    <span class="pp-member-preview-label">群成员</span>
                    <span class="pp-member-preview-count">${memberStatusText}</span>
                  </div>
                  <div class="pp-member-avatars">
                    ${memberPreview.length > 0 ? memberPreview.map((m, i) => `
                      <div class="pp-member-avatar" style="z-index:${memberPreview.length - i}" title="${this._esc(this._memberTitle(m))}">
                        ${this._memberAvatarMarkup(m, this._memberInitial(m))}
                      </div>
                    `).join('') : !membersLoading ? `<span class="pp-member-empty-hint">暂无成员</span>` : ''}
                    ${membersLoading && memberPreview.length === 0 ? `
                      <div class="pp-member-avatar pp-member-avatar-skel"></div>
                      <div class="pp-member-avatar pp-member-avatar-skel"></div>
                      <div class="pp-member-avatar pp-member-avatar-skel"></div>
                      <div class="pp-member-avatar pp-member-avatar-skel"></div>
                    ` : ''}
                  </div>
                </div>
              </div>
              <button class="pp-member-invite-btn" id="pp-member-invite-btn" title="邀请成员">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 8v6m3-3h-6"/><circle cx="11" cy="11" r="8"/></svg>
              </button>
            </div>
            <!-- Tasks Section -->
            <div class="pp-section">
              <div class="pp-section-header">
                <h2 class="pp-section-title">活跃任务</h2>
              </div>
              ${loading ? `<div class="pp-loading">正在加载任务...</div>` : error ? `<div class="pp-error">${this._esc(error)}</div>` : orderedPosts.length === 0 ? `
                <div class="pp-empty">当前群组还没有任务，点击上方"发布新任务"开始创建。</div>
              ` : orderedPosts.map(p => this._renderPostCard(p)).join('')}
            </div>
          </div>` : `
          <div class="pp-body">
            <div class="pp-empty">仅群聊支持查看任务</div>
          </div>`}
        </div>
      </div>`;
    }

    // ── Members View ──
    return `
    <div id="posts-panel" class="posts-panel ${this.postsPanelOpen ? 'open' : ''}" style="display:${this.postsPanelOpen ? 'flex' : 'none'};">
      <div class="pp-overlay" id="pp-overlay"></div>
      <div class="pp-drawer">
        <div class="pp-header">
          <button class="pp-back-btn" id="pp-back-to-board-btn" title="返回">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
          </button>
          <div class="pp-title">成员管理</div>
          <div class="pp-actions">
            <button class="pp-btn pp-close-btn" id="pp-close-btn" title="关闭">&times;</button>
          </div>
        </div>
        <div class="pp-body">
          <div style="margin-bottom:0.95rem;display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
            <div style="min-width:0;">
              <div style="overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:0.9rem;color:var(--app-muted);">${this._esc(conv?.name || '当前群组')} · ${memberCount} 人</div>
            </div>
            <div style="display:flex;align-items:center;gap:0.55rem;">
              <button class="pp-members-invite-toggle-btn" id="pp-members-invite-toggle-btn" title="邀请成员">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M16 21v-2a4 4 0 00-4-4H6a4 4 0 00-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M19 8v6m3-3h-6"/></svg>
                <span>邀请</span>
              </button>
            </div>
          </div>

          <!-- Invite Card -->
          ${inviteOpen ? `
          <div class="pp-invite-card">
            <div class="pp-invite-card-title">邀请成员</div>
            <div class="pp-invite-input-wrap">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 16.92v3a2 2 0 01-2.18 2 19.79 19.79 0 01-8.63-3.07 19.5 19.5 0 01-6-6 19.79 19.79 0 01-3.07-8.67A2 2 0 014.11 2h3a2 2 0 012 1.72 12.84 12.84 0 00.7 2.81 2 2 0 01-.45 2.11L8.09 9.91a16 16 0 006 6l1.27-1.27a2 2 0 012.11-.45 12.84 12.84 0 002.81.7A2 2 0 0122 16.92z"/></svg>
              <input id="pp-invite-input" type="tel" placeholder="输入手机号" value="${this._esc(invitePhone)}">
            </div>
            <button class="pp-invite-confirm-btn" id="pp-invite-confirm-btn" ${!invitePhone.trim() ? 'disabled' : ''}>确认邀请</button>
          </div>` : ''}

          ${membersError ? `<div class="pp-error">${this._esc(membersError)}</div>` : membersNotice ? `<div class="pp-error" style="color:#a16207;background:#fffbeb;border-color:#fde68a;">${this._esc(membersNotice)}</div>` : ''}

          <!-- Members List -->
          <div class="pp-section">
            <div class="pp-section-header">
              <span class="pp-section-title">当前成员</span>
              <span class="pp-count">${memberCount}</span>
            </div>
            ${membersLoading && members.length === 0 ? `
              <div class="pp-loading">正在加载成员...</div>
            ` : members.length === 0 ? `
              <div class="pp-empty">暂无可展示成员</div>
            ` : members.map(m => this._renderPostPanelMemberItem(m)).join('')}
          </div>
        </div>
      </div>
    </div>`;
  },

  _renderPostPanelMemberItem(member) {
    const isSelf = this._isSelfMember(member);
    const initials = this._memberInitial(member);
    const roleLabel = this._memberRoleLabel(member);
    const roleClass = this._memberRoleClass(member);
    const canRemove = !isSelf && member.memberKind !== 'phone' && (member.memberKind === 'user' || member.memberKind === 'node');
    const key = member.memberKind === 'user' ? `user:${member.userId}` : member.memberKind === 'phone' ? `phone:${member.phone}` : `node:${member.nodeId}`;

    return `
    <div class="pp-member-item" data-member-key="${key}">
      <div class="pp-member-item-avatar">${this._memberAvatarMarkup(member, initials)}</div>
      <div class="pp-member-item-info">
        <div class="pp-member-item-name">
          <span>${this._esc(this._memberTitle(member))}</span>
          <span class="pp-role-badge ${roleClass}">${roleLabel}</span>
          ${isSelf ? '<span class="pp-self-badge">我</span>' : ''}
        </div>
        <div class="pp-member-item-sub">${this._memberSubtitle(member)}</div>
      </div>
      ${member.memberKind === 'user' || member.memberKind === 'node' ? `
        <button class="pp-member-remove-btn" data-member-kind="${member.memberKind}" data-member-id="${this._esc(member.memberKind === 'user' ? member.userId : member.nodeId)}" title="移除" ${canRemove ? '' : 'disabled'}>
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 6h18M8 6V4a2 2 0 012-2h4a2 2 0 012 2v2m3 0v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6h14z"/></svg>
        </button>
      ` : ''}
    </div>`;
  },

  _renderPostCard(post) {
    const statusLabel = this._postStatusLabel(post);
    const statusClass = this._postStatusClass(post);
    const iconSvg = this._postIconSvg(post);
    const progress = this._postProgress(post);
    const deadline = post.deadlineAt ? this._formatDeadline(post.deadlineAt) : '长期有效';

    return `
    <div class="pp-post-card" data-post-id="${post.id}">
      <div class="pp-post-icon">${iconSvg}</div>
      <div class="pp-post-body">
        <div class="pp-post-header">
          <span class="pp-post-status ${statusClass}">${statusLabel}</span>
        </div>
        <div class="pp-post-title">${this._esc(post.title)}</div>
        <div class="pp-post-summary">${this._esc(post.summary || '')}</div>
        <div class="pp-post-deadline">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="4" width="18" height="18" rx="2"/><path d="M16 2v4M8 2v4M3 10h18"/></svg>
          <span>截止：${deadline}</span>
        </div>
        <div class="pp-post-progress">
          <span class="pp-progress-pct">${progress}%</span>
          <div class="pp-progress-bar"><div class="pp-progress-fill" style="width:${progress}%"></div></div>
        </div>
        <div class="pp-post-action">${post.status === 'closed' ? '查看详情' : '继续任务'}</div>
      </div>
    </div>`;
  },

  _postStatusLabel(post) {
    if (post.status === 'closed') return '已完成';
    if (post.status === 'draft') return '待开始';
    return '进行中';
  },

  _postStatusClass(post) {
    if (post.status === 'closed') return 'status-closed';
    if (post.status === 'draft') return 'status-draft';
    return 'status-active';
  },

  _postIconSvg(post) {
    if (post.template === 'news') return `<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 7v10a2 2 0 002 2h14a2 2 0 002-2V9a2 2 0 00-2-2h-6l-2-2H5a2 2 0 00-2 2z"/></svg>`;
    if (post.template === 'event') return `<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="4" width="18" height="18" rx="2"/><path d="M16 2v4M8 2v4M3 10h18M8 14h.01M12 14h.01M16 14h.01M8 18h.01M12 18h.01"/></svg>`;
    if (post.status === 'closed') return `<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 11.08V12a10 10 0 11-5.93-9.14"/><path d="M22 4L12 14.01l-3-3"/></svg>`;
    return `<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6M16 13H8M16 17H8M10 9H8"/></svg>`;
  },

  _postProgress(post) {
    const responses = (post.responses || []).length;
    if (post.status === 'closed') return 100;
    if (responses <= 0) return post.status === 'draft' ? 20 : 40;
    return Math.min(100, 40 + responses * 20);
  },

  _postDefaultSubmitLabel(post) {
    if (!post) return '标记完成';
    if (post.template === 'news') return '我知道了';
    if (post.actionType === 'upload') return '标记已读';
    if (post.actionType === 'read') return '标记已读';
    return '标记完成';
  },

  _postResourceActionLabel(post) {
    if (!post?.resourceUrl) return '';
    if (post.template === 'homework') return '打开作业';
    if (post.template === 'news') return '查看资讯';
    return '查看说明';
  },

  _postResourceSubtitle(post) {
    if (!post?.resourceUrl) return '';
    if (post.resourceType === 'html') return '在线任务资源';
    if (post.resourceType === 'link') return '链接资源';
    return '任务附件';
  },

  _resolvePostResponsePayload(post, requestedResponseType) {
    const actionType = post?.actionType || 'read';
    if (actionType === 'confirm') {
      const confirmation = requestedResponseType === 'not_going' ? 'not_going' : 'going';
      return {
        responseType: confirmation === 'not_going' ? 'reject' : 'accept',
        confirmation,
      };
    }
    return {
      responseType: requestedResponseType || actionType || 'read',
      confirmation: '',
    };
  },

  _postResponseDisplayText(post, response) {
    const content = String(response?.content || '').trim();
    if (content) return content;
    if (post?.actionType === 'confirm') {
      if (response?.confirmation === 'not_going' || response?.responseType === 'reject') return '无法参加';
      return '确认参加';
    }
    if (post?.actionType === 'upload') return '已提交作业';
    if (post?.actionType === 'read') return '已查看';
    return '已响应';
  },

  _creatorTemplateMeta(template) {
    if (template === 'event') {
      return {
        label: '群活动',
        desc: '组织签到、报名或参与确认',
        defaultSummary: '请确认是否参加，并提前安排时间。',
        resourceLabel: '活动说明链接',
        resourcePlaceholder: 'https://example.com/event',
      };
    }
    if (template === 'news') {
      return {
        label: '群资讯',
        desc: '发布通知、新闻或群内公告',
        defaultSummary: '请大家及时查看并知悉。',
        resourceLabel: '资讯链接',
        resourcePlaceholder: 'https://example.com/news',
      };
    }
    return {
      label: '家庭作业',
      desc: '下发互动课本、练习题或作业任务链接',
      defaultSummary: '请按要求完成作业并按时提交。',
      resourceLabel: '作业链接',
      resourcePlaceholder: 'https://works.blazegraph.site/works/8/homework/index.html',
    };
  },

  _creatorPostTransportMeta(template, resourceUrl = '') {
    // 对齐 Vue 版 resolveConversationPostDraft + inferResourceType
    const url = String(resourceUrl || '').trim();
    let resourceType;
    if (!url) {
      resourceType = 'none';
    } else if (/\.html?(?:[?#].*)?$/i.test(url)) {
      resourceType = 'html';
    } else if (/\.(avif|bmp|gif|jpe?g|png|svg|webp)(?:[?#].*)?$/i.test(url)) {
      resourceType = 'image';
    } else {
      resourceType = 'link';
    }
    if (template === 'news') return { actionType: 'read', resourceType };
    if (template === 'event') return { actionType: 'confirm', resourceType };
    return { actionType: 'upload', resourceType };
  },

  _openPostResource(post) {
    if (!post?.resourceUrl) return;
    window.open(post.resourceUrl, '_blank', 'noopener');
  },

  _formatDeadline(ts) {
    if (!ts) return '长期有效';
    const d = new Date(ts);
    const month = d.getMonth() + 1;
    const day = d.getDate();
    const hour = String(d.getHours()).padStart(2, '0');
    const min = String(d.getMinutes()).padStart(2, '0');
    return `${month}月${day}日 ${hour}:${min}`;
  },

  async _openPostsPanel() {
    const conv = ChatStore.getActiveConversation();
    if (!conv) return;

    this.postsPanelOpen = true;
    this.postsPanelView = 'board'; // board | members
    this.postsLoading = true;
    this.postsError = '';
    this.groupMembers = [];
    this.groupMembersLoading = true;
    this.groupMembersLoaded = false;
    this.groupMembersError = '';
    this.groupMembersNotice = '';
    this.invitePanelOpen = false;
    this.invitePhone = '';
    this.render();

    if (conv.scope === 'personal_workspace') {
      this.postsLoading = false;
      this.groupMembersLoading = false;
      this.groupMembersLoaded = false;
      this.render();
      this._bindPostsPanelEvents();
      return;
    }

    if (conv.type === 'group') {
      try {
        // Parallel load: posts + members
        const [postsResult, membersResult] = await Promise.allSettled([
          ChatStore.loadGroupPosts(conv.id),
          ChatStore.getRoomMembers(conv.id).catch(() => []),
        ]);

        if (postsResult.status === 'rejected') {
          this.postsError = postsResult.reason?.message || '加载失败';
        }
        if (membersResult.status === 'fulfilled') {
          this.groupMembers = membersResult.value;
          this.groupMembersLoaded = true;
          this.groupMembersNotice = '';
        } else {
          this.groupMembersError = membersResult.reason?.message || '成员列表加载失败';
          this.groupMembersNotice = '';
        }
      } catch (e) {
        console.error('[ChatPage] Failed to load posts panel:', e);
      } finally {
        this.postsLoading = false;
        this.groupMembersLoading = false;
        this.render();
        this._bindPostsPanelEvents();
      }
    } else {
      this.postsLoading = false;
      this.groupMembersLoading = false;
      this.groupMembersLoaded = false;
      this.render();
    }
  },

  _closePostsPanel() {
    this.postsPanelOpen = false;
    this.postsPanelView = 'board';
    this.invitePanelOpen = false;
    this.groupMembersNotice = '';
    this.render();
  },

  // 刷新 posts panel 内容（任务操作后更新卡片状态）
  // 由于 _scheduleRender 在 postsPanelOpen 时会跳过，需手动重渲染
  _refreshPostsPanel() {
    if (!this.postsPanelOpen) return;
    const panel = document.getElementById('posts-panel');
    if (!panel) return;
    const html = this._renderPostsPanel();
    // 重建 panel 节点以更新内容
    const wrapper = document.createElement('div');
    wrapper.innerHTML = html;
    const newPanel = wrapper.firstElementChild;
    if (newPanel) {
      panel.replaceWith(newPanel);
      this._bindPostsPanelEvents();
    }
  },

  _switchPostsPanelToMembers() {
    this.postsPanelView = 'members';
    this.invitePanelOpen = false;
    this.render();
    this._bindPostsPanelEvents();
  },

  _switchPostsPanelToBoard() {
    this.postsPanelView = 'board';
    this.invitePanelOpen = false;
    this.render();
    this._bindPostsPanelEvents();
  },

  // 对齐 Vue 版 MessageAttachmentCard：点击聊天中的任务卡片打开任务详情
  _bindMessagePostCards() {
    const cards = this.container.querySelectorAll('.msg-post-card');
    cards.forEach(card => {
      // 主卡片点击：打开资源或任务详情
      const mainBtn = card.querySelector('.msg-post-card-main') || card;
      if (mainBtn) {
        mainBtn.onclick = (e) => {
          e.preventDefault();
          const resourceUrl = mainBtn.dataset.resourceUrl || card.dataset.resourceUrl;
          const postId = card.dataset.postId;
          if (card.classList.contains('msg-post-card-webview') && resourceUrl) {
            window.open(resourceUrl, '_blank', 'noopener');
            return;
          }
          if (postId) this._openPostDetail(postId);
        };
      }

      // 转发按钮（对齐 Vue 版 triggerSecondaryShare → openForwardModal）
      const forwardBtn = card.querySelector('[data-action="forward"]');
      if (forwardBtn) {
        forwardBtn.onclick = (e) => {
          e.preventDefault();
          e.stopPropagation();
          const resourceUrl = forwardBtn.dataset.resourceUrl || card.dataset.resourceUrl;
          const cardTitle = forwardBtn.dataset.cardTitle || 'H5 卡片';
          if (resourceUrl) {
            this._showForwardModal(resourceUrl, cardTitle);
          }
        };
      }

      // 更多操作按钮（对齐 Vue 版 requestAttachmentActions）
      const moreBtn = card.querySelector('[data-action="more"]');
      if (moreBtn) {
        moreBtn.onclick = (e) => {
          e.preventDefault();
          e.stopPropagation();
          const resourceUrl = moreBtn.dataset.resourceUrl || card.dataset.resourceUrl;
          this._showAttachmentActionMenu(resourceUrl, e);
        };
      }
    });
  },

  // 对齐 Vue 版 ForwardModal：转发资源到其他会话
  _showForwardModal(resourceUrl, cardTitle) {
    if (!resourceUrl) return;
    const existing = document.querySelector('.forward-modal-overlay');
    if (existing) existing.remove();

    const conversations = ChatStore.getConversations().filter(c => c.type !== 'agent');
    if (!conversations.length) {
      Toast.show('没有可转发的会话', 'info');
      return;
    }

    const overlay = document.createElement('div');
    overlay.className = 'forward-modal-overlay';
    overlay.style.cssText = 'position:fixed;inset:0;z-index:9999;display:flex;align-items:center;justify-content:center;padding:1rem;background:rgba(15,23,42,0.6);backdrop-filter:blur(4px);';

    const modal = document.createElement('div');
    modal.style.cssText = 'position:relative;width:100%;max-width:24rem;border-radius:1.5rem;background:var(--app-surface-elevated);padding:1.5rem;box-shadow:0 20px 40px rgba(0,0,0,0.15);';

    const header = document.createElement('div');
    header.style.cssText = 'display:flex;align-items:center;justify-content:space-between;margin-bottom:1.5rem;';
    header.innerHTML = '<h3 style="font-size:1.1rem;font-weight:700;color:var(--app-text);">转发到...</h3>';
    const closeBtn = document.createElement('button');
    closeBtn.type = 'button';
    closeBtn.style.cssText = 'display:flex;width:2rem;height:2rem;align-items:center;justify-content:center;border:0;border-radius:999px;background:var(--app-surface);color:var(--app-muted);cursor:pointer;';
    closeBtn.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 6L6 18M6 6l12 12"/></svg>';
    closeBtn.onclick = () => overlay.remove();
    header.appendChild(closeBtn);
    modal.appendChild(header);

    const list = document.createElement('div');
    list.style.cssText = 'max-height:60vh;overflow-y:auto;display:flex;flex-direction:column;gap:0.5rem;';

    conversations.forEach(conv => {
      const item = document.createElement('button');
      item.type = 'button';
      item.style.cssText = 'display:flex;width:100%;align-items:center;justify-content:space-between;border:1px solid var(--app-border);border-radius:1rem;padding:0.9rem;background:transparent;cursor:pointer;transition:background 0.15s;';
      const isGroup = conv.type === 'group';
      const iconColor = isGroup ? 'var(--app-brand)' : 'var(--app-muted)';
      const iconName = isGroup
        ? '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 9h16M4 15h16M10 3L8 21M16 3l-2 18"/></svg>'
        : '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 21v-2a4 4 0 00-4-4H8a4 4 0 00-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>';
      item.innerHTML = `
        <span style="display:flex;align-items:center;gap:0.75rem;min-width:0;">
          <span style="display:flex;width:2rem;height:2rem;align-items:center;justify-content:center;border-radius:0.6rem;background:var(--app-brand-soft);color:${iconColor};">${iconName}</span>
          <span style="font-size:0.9rem;font-weight:600;color:var(--app-text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(conv.name || conv.id)}</span>
        </span>
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="var(--app-brand)" stroke-width="2"><path d="M22 2L11 13M22 2l-7 20-4-9-9-4 20-7z"/></svg>
      `;
      item.onmouseenter = () => { item.style.background = 'var(--app-brand-soft)'; };
      item.onmouseleave = () => { item.style.background = 'transparent'; };
      item.onclick = () => {
        // 对齐 Vue 版 forwardTo：发送消息 + 附件到目标会话
        ChatStore.sendUserMessage(`[转发卡片] ${cardTitle}`, conv.id, {
          attachments: [{ type: 'webview', url: resourceUrl, title: cardTitle }],
        });
        Toast.show(`已转发到 ${conv.name || conv.id}`, 'success');
        overlay.remove();
      };
      list.appendChild(item);
    });

    modal.appendChild(list);
    overlay.appendChild(modal);
    overlay.onclick = (e) => { if (e.target === overlay) overlay.remove(); };
    document.body.appendChild(overlay);
  },

  // 对齐 Vue 版 MessageAttachmentActionMenu：卡片更多操作菜单
  _showAttachmentActionMenu(resourceUrl, evt) {
    if (!resourceUrl) return;
    const existing = document.querySelector('.attachment-action-menu');
    if (existing) existing.remove();

    const menu = document.createElement('div');
    menu.className = 'attachment-action-menu';
    menu.style.cssText = 'position:fixed;z-index:9999;background:var(--app-surface-elevated);border:1px solid var(--app-border);border-radius:0.8rem;box-shadow:0 8px 24px rgba(0,0,0,0.12);padding:0.3rem 0;min-width:10rem;';
    const x = Math.min(evt.clientX || 100, window.innerWidth - 180);
    const y = Math.min(evt.clientY || 100, window.innerHeight - 200);
    menu.style.left = `${x}px`;
    menu.style.top = `${y}px`;

    const actions = [
      { id: 'open', label: '打开卡片', icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M15 3h6v6"/><path d="M10 14L21 3"/><path d="M18 13v6a2 2 0 01-2 2H5a2 2 0 01-2-2V8a2 2 0 012-2h6"/></svg>' },
      { id: 'copy_link', label: '复制链接', icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1"/></svg>' },
      { id: 'save_local', label: '保存到本地', icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>' },
    ];

    actions.forEach(action => {
      const item = document.createElement('button');
      item.type = 'button';
      item.style.cssText = 'display:flex;width:100%;align-items:center;gap:0.6rem;padding:0.6rem 0.9rem;border:0;background:transparent;color:var(--app-text);font-size:0.86rem;text-align:left;cursor:pointer;';
      item.innerHTML = `${action.icon}<span>${action.label}</span>`;
      item.onmouseenter = () => { item.style.background = 'var(--app-brand-soft)'; };
      item.onmouseleave = () => { item.style.background = 'transparent'; };
      item.onclick = (e) => {
        e.preventDefault();
        menu.remove();
        if (action.id === 'open') {
          window.open(resourceUrl, '_blank', 'noopener');
        } else if (action.id === 'copy_link') {
          navigator.clipboard?.writeText(resourceUrl).then(() => {
            Toast.show('链接已复制', 'success');
          }).catch(() => Toast.show('复制失败', 'error'));
        } else if (action.id === 'save_local') {
          // 对齐 Vue 版 saveAttachmentResource：提取扩展名 + fetch blob 下载
          const ext = (() => {
            try {
              const match = new URL(resourceUrl).pathname.match(/\.([a-z0-9]{2,8})$/i);
              return match ? `.${match[1].toLowerCase()}` : '';
            } catch { return ''; }
          })();
          const baseName = (() => {
            try {
              const last = new URL(resourceUrl).pathname.split('/').filter(Boolean).pop() || 'resource';
              return decodeURIComponent(last).replace(/\.html?$/i, '') || 'resource';
            } catch { return 'resource'; }
          })();
          const filename = ext && !baseName.toLowerCase().endsWith(ext) ? `${baseName}${ext}` : baseName;
          Toast.show('正在保存...', 'info');
          fetch(resourceUrl)
            .then(res => {
              if (!res.ok) throw new Error(`HTTP ${res.status}`);
              return res.blob();
            })
            .then(blob => {
              const blobUrl = URL.createObjectURL(blob);
              const a = document.createElement('a');
              a.href = blobUrl;
              a.download = filename;
              document.body.appendChild(a);
              a.click();
              a.remove();
              setTimeout(() => URL.revokeObjectURL(blobUrl), 1000);
              Toast.show('已开始保存', 'success');
            })
            .catch(() => {
              // 降级：直接用 a 标签打开
              const a = document.createElement('a');
              a.href = resourceUrl;
              a.download = filename;
              a.target = '_blank';
              a.rel = 'noopener noreferrer';
              document.body.appendChild(a);
              a.click();
              a.remove();
              Toast.show('已尝试保存', 'info');
            });
        }
      };
      menu.appendChild(item);
    });

    document.body.appendChild(menu);
    const closeHandler = (e2) => {
      if (!menu.contains(e2.target)) {
        menu.remove();
        document.removeEventListener('click', closeHandler, true);
      }
    };
    setTimeout(() => document.addEventListener('click', closeHandler, true), 0);
  },

  // 绑定失败消息的重发按钮（对齐微信交互：点击红色感叹号重发）
  _bindRetryButtons() {
    const btns = this.container.querySelectorAll('.msg-delivery.failed[data-retry-msg-id]');
    btns.forEach(btn => {
      btn.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        const msgId = btn.dataset.retryMsgId;
        const convId = btn.dataset.retryConvId;
        if (!msgId || !convId) return;
        ChatStore.retrySend(msgId, convId);
      };
    });
  },

  _bindPostsPanelEvents() {
    const conv = ChatStore.getActiveConversation();
    const isWorkspace = conv && conv.scope === 'personal_workspace';
    const panelView = this.postsPanelView || 'board';

    // Close button
    const closeBtn = document.getElementById('pp-close-btn');
    if (closeBtn) closeBtn.onclick = () => this._closePostsPanel();

    // Overlay click to close
    const overlay = document.getElementById('pp-overlay');
    if (overlay) overlay.onclick = () => this._closePostsPanel();

    if (isWorkspace) {
      const openAiBtn = document.getElementById('pp-workspace-open-ai-btn');
      if (openAiBtn) openAiBtn.onclick = () => { window.location.hash = '#/ai'; };
      const viewMoreTasksBtn = document.getElementById('ws-view-more-tasks');
      if (viewMoreTasksBtn) viewMoreTasksBtn.onclick = () => {
        this._closePostsPanel();
        window.location.hash = '#/tasks';
      };
      const targetSelect = document.getElementById('pp-workspace-target-select');
      if (targetSelect) {
        targetSelect.onchange = (event) => {
          this.workspacePublishTargetId = event.target.value;
          this.render();
          this._bindPostsPanelEvents();
        };
      }

      this.container.querySelectorAll('[data-workspace-action]').forEach(btn => {
        btn.onclick = async () => {
          const action = btn.dataset.workspaceAction;
          const taskId = btn.dataset.taskId;
          const skillId = btn.dataset.skillId;
          if (!conv) return;

          if (action === 'create-task') {
            const task = await ChatStore.createPersonalTask({
              title: '新任务',
              summary: '在 AI 空间快捷创建',
              sourceConversationId: conv.id,
              deliveryTarget: { type: 'conversation', conversationId: conv.id },
            });
            if (task) {
              Toast.success('任务已创建，可在任务中心查看');
              this._closePostsPanel();
              window.location.hash = '#/tasks';
            } else {
              Toast.warn('任务创建失败，请稍后再试');
            }
            return;
          }

          if (action === 'summarize') {
            this._closePostsPanel();
            ChatStore.setActiveConversation(conv.id);
            UiStore.setActiveConversation(conv.id);
            ChatStore.sendUserMessage('请总结当前会话的主要内容', conv.id);
            return;
          }

          if (action === 'open-devices') {
            this._closePostsPanel();
            this._openDevicesPage();
            return;
          }

          if (action === 'publish-draft-quick' || action === 'publish-draft') {
            const targetId = this.workspacePublishTargetId || ChatStore.getConversations().find(item => item.type === 'group' && item.scope !== 'personal_workspace')?.id;
            const publishSkillId = skillId || draftActions[0]?.skillId;
            if (!publishSkillId || !targetId) {
              Toast.warn(targetId ? '暂无可发布的草稿' : '请先选择目标群聊');
              return;
            }
            try {
              await ChatStore.publishWorkspaceDraft(publishSkillId, targetId, conv.id);
              const targetName = ChatStore.getConversations().find(item => item.id === targetId)?.name || '目标群聊';
              Toast.success(`已发布到 ${targetName}`);
            } catch (e) {
              Toast.warn(e.message || '发布失败，请稍后再试');
            }
            return;
          }

          if (!taskId) return;

          if (action === 'complete-task') {
            const ok = await ChatStore.completePersonalTask(taskId);
            if (!ok) { Toast.warn('任务完成失败，请稍后再试'); return; }
            Toast.success('任务已完成');
            this._refreshPostsPanel();
            return;
          }

          if (action === 'cancel-task') {
            const ok = await ChatStore.cancelPersonalTask(taskId);
            if (!ok) { Toast.warn('任务取消失败，请稍后再试'); return; }
            Toast.success('任务已取消');
            this._refreshPostsPanel();
            return;
          }

          if (action === 'reschedule-task') {
            const task = ChatStore.getPersonalTasksForCurrentUser().find(item => item.id === taskId);
            const base = task?.dueAt && task.dueAt > Date.now() ? task.dueAt : Date.now();
            const ok = await ChatStore.reschedulePersonalTask(taskId, base + 60 * 60 * 1000);
            if (!ok) { Toast.warn('任务延期失败，请稍后再试'); return; }
            Toast.success('任务已延后1小时');
            this._refreshPostsPanel();
            return;
          }

          if (action === 'open-task-source') {
            const task = ChatStore.getPersonalTasksForCurrentUser().find(item => item.id === taskId);
            const target = this._parseDeliveryTarget(task?.deliveryTarget);
            const conversationId =
              target?.type === 'conversation' || target?.type === 'both'
                ? target.conversationId
                : task?.sourceConversationId;
            if (!conversationId) {
              Toast.warn('这个任务没有绑定来源群聊');
              return;
            }
            this._closePostsPanel();
            ChatStore.setActiveConversation(conversationId);
            UiStore.setActiveConversation(conversationId);
          }
        };
      });
      return;
    }

    if (panelView === 'board') {
      // Banner create post button
      const bannerCreateBtn = document.getElementById('pp-banner-create-btn');
      if (bannerCreateBtn) bannerCreateBtn.onclick = () => this._openCreatePost();

      // Header create post button
      const createBtn = document.getElementById('pp-create-btn');
      if (createBtn) createBtn.onclick = () => this._openCreatePost();

      // Member preview section → open members view
      const memberPreviewBtn = document.getElementById('pp-member-preview-btn');
      if (memberPreviewBtn) memberPreviewBtn.onclick = (e) => {
        // Don't trigger if clicking invite button
        if (e.target.closest('#pp-member-invite-btn')) return;
        this._switchPostsPanelToMembers();
      };

      // Invite button in member preview → open members view with invite open
      const memberInviteBtn = document.getElementById('pp-member-invite-btn');
      if (memberInviteBtn) memberInviteBtn.onclick = () => {
        this.invitePanelOpen = true;
        this._switchPostsPanelToMembers();
      };

      // Post card clicks
      this.container.querySelectorAll('.pp-post-card').forEach(card => {
        card.onclick = () => {
          const postId = card.dataset.postId;
          if (postId) this._openPostDetail(postId);
        };
      });

      const viewAllBtn = document.getElementById('pp-view-all-btn');
      if (viewAllBtn) viewAllBtn.onclick = () => Toast.featureUnavailable();
    } else {
      // Members view
      // Back to board button
      const backBtn = document.getElementById('pp-back-to-board-btn');
      if (backBtn) backBtn.onclick = () => this._switchPostsPanelToBoard();

      // Invite toggle button
      const inviteToggleBtn = document.getElementById('pp-members-invite-toggle-btn');
      if (inviteToggleBtn) inviteToggleBtn.onclick = () => {
        this.invitePanelOpen = !this.invitePanelOpen;
        this.render();
        this._bindPostsPanelEvents();
      };

      const refreshInlineBtn = document.getElementById('pp-members-refresh-inline-btn');
      if (refreshInlineBtn) refreshInlineBtn.onclick = () => this._refreshPostPanelMembers();

      // Invite input
      const inviteInput = document.getElementById('pp-invite-input');
      if (inviteInput) {
        inviteInput.oninput = (e) => {
          this.invitePhone = e.target.value;
          const btn = document.getElementById('pp-invite-confirm-btn');
          if (btn) btn.disabled = !e.target.value.trim();
        };
      }

      // Invite confirm button
      const inviteConfirmBtn = document.getElementById('pp-invite-confirm-btn');
      if (inviteConfirmBtn) inviteConfirmBtn.onclick = () => this._doInviteFromPostPanel();

      // Remove member buttons
      this.container.querySelectorAll('.pp-member-remove-btn').forEach(btn => {
        btn.onclick = () => {
          const memberId = btn.dataset.memberId;
          const memberKind = btn.dataset.memberKind || 'user';
          if (memberId) this._doRemoveMemberFromPostPanel(memberId, memberKind);
        };
      });
    }
  },

  async _refreshPostPanelMembers() {
    const conv = ChatStore.getActiveConversation();
    if (!conv) return;

    this.groupMembersLoading = true;
    this.render();
    try {
      const members = await ChatStore.getRoomMembers(conv.id);
      this.groupMembers = members;
      this.groupMembersLoaded = true;
      this.groupMembersError = '';
      this.groupMembersNotice = '';
    } catch (e) {
      console.error('[ChatPage] Failed to refresh members:', e);
      this.groupMembersError = e.message || '刷新失败';
      this.groupMembersNotice = '';
    } finally {
      this.groupMembersLoading = false;
      this.render();
      this._bindPostsPanelEvents();
    }
  },

  async _doInviteFromPostPanel() {
    const conv = ChatStore.getActiveConversation();
    if (!conv) return;

    const phone = this.invitePhone.trim();
    if (!phone) {
      Toast.warn('请输入手机号');
      return;
    }

    try {
      const members = await ChatStore.inviteRoomMember({
        roomId: conv.id,
        memberKind: 'phone',
        phone,
        role: 'member',
      });
      this.groupMembers = members;
      this.groupMembersNotice = '';
      this.groupMembersLoaded = true;
      Toast.success('邀请成功');
      this.invitePhone = '';
      this.invitePanelOpen = false;
      this.render();
      this._bindPostsPanelEvents();
    } catch (e) {
      console.error('[ChatPage] Failed to invite member:', e);
      Toast.error(e.message || '邀请失败');
    }
  },

  async _doRemoveMemberFromPostPanel(memberId, memberKind = 'user') {
    const conv = ChatStore.getActiveConversation();
    if (!conv) return;

    if (!confirm('确定要移除该成员吗？')) return;

    try {
      const members = await ChatStore.removeRoomMember(conv.id, memberKind, memberId);
      this.groupMembers = members;
      this.groupMembersLoaded = true;
      this.groupMembersNotice = members.length ? '' : '服务端成员列表为空，已先展示本地会话记录。';
      Toast.success('已移除');
      this.render();
      this._bindPostsPanelEvents();
    } catch (e) {
      console.error('[ChatPage] Failed to remove member:', e);
      Toast.error(e.message || '移除失败');
    }
  },

  // ── Post Creator Modal (对齐 Vue PostCreatorFullScreen 内容) ─
  _renderPostCreator() {
    const conv = ChatStore.getActiveConversation();
    const { postCreatorOpen, postCreatorSubmitting, postCreatorType, postCreatorTitle, postCreatorSummary, postCreatorDeadline, postCreatorResourceUrl, postCreatorFeedbackEnabled } = this;

    if (!postCreatorOpen) return '';

    const templates = {
      homework: {
        label: '家庭作业',
        desc: '下发互动课本、练习题或作业任务链接',
        icon: `<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#6f44ff" stroke-width="2"><path d="M2 3h6a4 4 0 014 4v14a3 3 0 00-3-3H2z"/><path d="M22 3h-6a4 4 0 00-4 4v14a3 3 0 013-3h7z"/></svg>`,
        accent: '#7c4dff',
        accentSoft: 'rgba(124,77,255,0.12)',
      },
      event: {
        label: '群活动',
        desc: '组织签到、报名或参与确认',
        icon: `<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#ff6c1f" stroke-width="2"><rect x="3" y="4" width="18" height="18" rx="2"/><path d="M16 2v4M8 2v4M3 10h18"/></svg>`,
        accent: '#ff7a1f',
        accentSoft: 'rgba(255,122,31,0.12)',
      },
      news: {
        label: '群资讯',
        desc: '发布通知、新闻或群内公告',
        icon: `<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#19a863" stroke-width="2"><path d="M3 7v10a2 2 0 002 2h14a2 2 0 002-2V9a2 2 0 00-2-2h-6l-2-2H5a2 2 0 00-2 2z"/></svg>`,
        accent: '#21b36f',
        accentSoft: 'rgba(33,179,111,0.12)',
      },
    };

    const meta = this._creatorTemplateMeta(postCreatorType);
    const titleLen = postCreatorTitle.length;
    const summaryLen = postCreatorSummary.length;

    return `
    <div class="td-drawer-overlay" id="post-creator-page">
      <div class="td-drawer">
        <div class="td-drawer-header">
          <button id="pc-close-btn" class="td-drawer-back" title="返回">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
          </button>
          <div class="td-drawer-title">发布任务</div>
        </div>
        <div class="td-drawer-body">
        <!-- Template Picker -->
        <div class="pc-fs-section-card" style="margin-top:0;">
          <div class="pc-fs-section-label">任务类型</div>
          <div class="pc-fs-template-grid">
            ${Object.entries(templates).map(([key, tpl]) => `
              <button class="pc-fs-template-card ${postCreatorType === key ? 'selected' : ''}" data-pc-template="${key}" style="${postCreatorType === key ? `border-color:${tpl.accent};background:${tpl.accentSoft};` : ''}">
                <div class="pc-fs-template-icon" style="background:${tpl.accentSoft};color:${tpl.accent};">
                  ${tpl.icon.replace('stroke="#6f44ff"', `stroke="${tpl.accent}"`).replace('stroke="#ff6c1f"', `stroke="${tpl.accent}"`).replace('stroke="#19a863"', `stroke="${tpl.accent}"`)}
                </div>
                <div class="pc-fs-template-label">${tpl.label}</div>
                <div class="pc-fs-template-desc">${tpl.desc}</div>
                ${postCreatorType === key ? `<div class="pc-fs-template-check" style="background:${tpl.accent};">✓</div>` : ''}
              </button>
            `).join('')}
          </div>
        </div>

        <!-- Title -->
        <div class="pc-fs-section-card">
          <div class="pc-fs-section-label">任务标题 <span class="pc-fs-required">*</span></div>
          <div class="pc-fs-input-wrap">
            <input id="pc-title-input" type="text" maxlength="60" placeholder="输入任务标题" value="${this._esc(postCreatorTitle)}" style="padding-right:3.2rem;">
            <div class="pc-fs-counter" id="pc-title-counter">${titleLen}/60</div>
          </div>
        </div>

        <!-- Summary -->
        <div class="pc-fs-section-card">
          <div class="pc-fs-section-label">任务内容 <span class="pc-fs-required">*</span></div>
          <div class="pc-fs-textarea-wrap">
            <textarea id="pc-summary-input" rows="5" maxlength="2000" placeholder="${this._esc(meta.defaultSummary)}" style="padding-bottom:1.8rem;">${this._esc(postCreatorSummary)}</textarea>
            <div class="pc-fs-counter" id="pc-summary-counter">${summaryLen}/2000</div>
          </div>
        </div>

        <!-- Resource URL -->
        <div class="pc-fs-section-card">
          <div class="pc-fs-section-label">${this._esc(meta.resourceLabel)}</div>
          <div class="pc-fs-input-wrap">
            <input id="pc-resource-input" type="url" placeholder="${this._esc(meta.resourcePlaceholder)}" value="${this._esc(postCreatorResourceUrl)}">
          </div>
          <div class="pc-fs-section-hint">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/></svg>
            <span>支持链接、文档、图片等资源，方便成员查看和完成任务</span>
          </div>
        </div>

        <!-- Deadline -->
        <div class="pc-fs-section-card">
          <div class="pc-fs-section-label">截止时间 <span class="pc-fs-required">*</span></div>
          <div class="pc-fs-input-wrap">
            <input id="pc-deadline-input" type="datetime-local" value="${this._esc(postCreatorDeadline)}">
          </div>
          <div class="pc-fs-section-hint">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/></svg>
            <span>选择截止日期和时间后，任务将在该时间自动截止</span>
          </div>
        </div>

        <!-- Feedback Toggle -->
        <div class="pc-fs-section-card">
          <button id="pc-feedback-toggle-btn" type="button" style="display:flex;width:100%;align-items:center;justify-content:space-between;border:none;background:none;padding:0;cursor:pointer;">
            <span style="display:flex;align-items:center;gap:0.45rem;font-size:0.95rem;font-weight:700;letter-spacing:-0.02em;color:var(--app-text);">
              允许成员提交反馈
              <span style="display:inline-flex;height:1.1rem;width:1.1rem;align-items:center;justify-content:center;border-radius:999px;border:1px solid var(--app-border);font-size:0.68rem;color:var(--app-muted);">?</span>
            </span>
            <span style="position:relative;display:inline-flex;height:2rem;width:3.5rem;align-items:center;border-radius:999px;padding:0.25rem;transition:all .18s ease;background:${postCreatorFeedbackEnabled ? 'linear-gradient(135deg,#8a63ff 0%,#6f44ff 100%)' : '#d9deec'};">
              <span style="height:1.5rem;width:1.5rem;border-radius:999px;background:#fff;box-shadow:0 4px 12px rgba(48,66,112,0.18);transform:translateX(${postCreatorFeedbackEnabled ? '1.45rem' : '0'});transition:transform .18s ease;"></span>
            </span>
          </button>
        </div>
      </div>

      <!-- Footer -->
      <div class="td-drawer-footer">
        <button id="pc-publish-btn" class="pc-fs-publish-btn" ${postCreatorSubmitting ? 'disabled' : ''}>
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"><path d="M22 2L11 13M22 2l-7 20-4-9-9-4 20-7z"/></svg>
          ${postCreatorSubmitting ? '发布中...' : '发布任务'}
        </button>
      </div>
      </div>
    </div>`;
  },

  // ── Task Detail Drawer (对齐 Vue TaskFullScreen 内容) ─
  _renderTaskDetail() {
    const { postDetailOpen, postDetailPost, postDetailResponseText, postDetailSelectedFile } = this;
    if (!postDetailOpen || !postDetailPost) return '';

    const post = postDetailPost;
    const statusLabel = this._postStatusLabel(post);
    const statusClass = this._postStatusClass(post);
    const deadline = post.deadlineAt ? this._formatDeadline(post.deadlineAt) : '长期有效';
    const responses = post.responses || [];
    const submitLabel = this._postDefaultSubmitLabel(post);
    const resourceLabel = this._postResourceActionLabel(post);
    const resourceSubtitle = this._postResourceSubtitle(post);
    const participantPreview = responses.slice(0, 5);
    const showCloseAction = post.type === 'task' && post.status !== 'closed';

    return `
    <div class="td-drawer-overlay" id="task-detail-page">
      <div class="td-drawer-backdrop" id="td-backdrop"></div>
      <div class="td-drawer">
        <div class="td-drawer-header">
          <button id="td-back-btn" class="td-drawer-back" title="返回">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
          </button>
          <div class="td-drawer-title">任务详情</div>
        </div>
        <div class="td-drawer-body">
          <div class="td-scroll">
          <!-- Hero -->
          <div class="td-hero">
            <div class="td-hero-icon">${this._postIconSvg(post)}</div>
            <div class="td-hero-content">
              <div class="td-hero-title-row">
                <div class="td-hero-title">${this._esc(post.title)}</div>
                <span class="td-status ${statusClass}">${statusLabel}</span>
              </div>
              <div class="td-deadline">
                <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="4" width="18" height="18" rx="2"/><path d="M16 2v4M8 2v4M3 10h18"/></svg>
                <span>截止时间：${deadline}</span>
              </div>
            </div>
          </div>

          <!-- Tip -->
          <div class="td-tip">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="margin-top:0.2rem;flex-shrink:0;"><path d="M12 2l2.4 7.2h7.6l-6 4.8 2.4 7.2-6.4-4.8-6.4 4.8 2.4-7.2-6-4.8h7.6z"/></svg>
            <span>${this._esc(post.summary || '')}</span>
          </div>

          <!-- Task Content -->
          <div class="td-section">
            <div class="td-section-title">任务内容</div>
            <div class="td-section-text">${this._esc(post.summary || '')}</div>
          </div>

          <!-- Participants -->
          ${participantPreview.length ? `
          <div class="td-section">
            <div style="display:flex;align-items:center;justify-content:space-between;gap:0.75rem;">
              <div class="td-section-title">参与成员（${responses.length}人）</div>
              <button id="td-view-all-participants-btn" type="button" style="background:none;font-size:0.82rem;font-weight:600;color:var(--app-brand);">查看全部</button>
            </div>
            <div class="td-avatar-row">
              ${participantPreview.map((item, index) => `
                <span class="td-avatar">${this._esc((item.actorName || item.userName || '成员').slice(-2) || `成${index + 1}`)}</span>
              `).join('')}
              <button id="td-view-all-participants-more-btn" type="button" class="td-avatar td-avatar-more">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="5" cy="12" r="1.5"/><circle cx="12" cy="12" r="1.5"/><circle cx="19" cy="12" r="1.5"/></svg>
              </button>
            </div>
          </div>` : ''}

          <!-- Attachment -->
          ${post.resourceUrl ? `
          <div class="td-section">
            <div class="td-section-title">附件（1）</div>
            <div class="td-file-item" style="margin-top:0.85rem;">
              <div class="td-file-icon">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
              </div>
              <div style="min-width:0;">
                <div class="td-file-title">${this._esc(post.title)}</div>
                <div class="td-file-subtitle">${this._esc(resourceSubtitle)}</div>
              </div>
              <button id="td-open-resource-btn" class="td-file-action">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M15 3h6v6"/><path d="M10 14L21 3"/><path d="M18 13v6a2 2 0 01-2 2H5a2 2 0 01-2-2V8a2 2 0 012-2h6"/></svg>
                ${this._esc(resourceLabel)}
              </button>
            </div>
          </div>` : ''}
        </div>

        </div>
        <!-- Footer -->
        ${post.status === 'closed' ? `
        <div class="td-drawer-footer td-footer-ended">
          <div class="td-footer-ended">任务已结束</div>
        </div>` : post.actionType === 'confirm' ? `
        <div class="td-drawer-footer">
          <div class="td-footer-confirm">
            <button id="td-confirm-yes-btn" class="td-btn-confirm-yes">确认参加</button>
            <button id="td-confirm-no-btn" class="td-btn-confirm-no">无法参加</button>
          </div>
        </div>` : `
        <div class="td-drawer-footer">
          <div class="td-footer-actions">
            ${showCloseAction ? `
            <button id="td-close-post-btn" class="td-btn-secondary">完成任务</button>
            ` : `<div></div>`}
            <button id="td-respond-btn" class="td-btn-primary">${this._esc(submitLabel)}</button>
          </div>
        </div>`}
      </div>
    </div>`;
  },

  _openCreatePost() {
    const conv = ChatStore.getActiveConversation();
    if (!conv || conv.type !== 'group') return;
    const meta = this._creatorTemplateMeta('homework');

    this.postsPanelOpen = false;
    this.postCreatorOpen = true;
    this.postCreatorSubmitting = false;
    this.postCreatorType = 'homework';
    this.postCreatorTitle = '';
    this.postCreatorSummary = meta.defaultSummary;
    this.postCreatorDeadline = '';
    this.postCreatorResourceUrl = '';
    this.postCreatorFeedbackEnabled = true;
    this.render();
    setTimeout(() => {
      const input = document.getElementById('pc-title-input');
      if (input) input.focus();
    }, 50);
  },

  _closePostCreator() {
    this.postCreatorOpen = false;
    this.postsPanelOpen = true;
    this.render();
  },

  async _handlePostCreatorPublish() {
    const conv = ChatStore.getActiveConversation();
    if (!conv) return;

    const title = this.postCreatorTitle.trim();
    const summary = this.postCreatorSummary.trim();
    const deadline = this.postCreatorDeadline;

    if (!title) {
      Toast.warn('请填写任务标题');
      return;
    }
    if (!summary) {
      Toast.warn('请填写任务内容');
      return;
    }
    if (!deadline) {
      Toast.warn('请选择截止时间');
      return;
    }
    const deadlineAt = new Date(deadline).getTime();
    if (!deadlineAt || isNaN(deadlineAt)) {
      Toast.warn('截止时间格式不正确');
      return;
    }

    this.postCreatorSubmitting = true;
    this.render();

    try {
      const transportMeta = this._creatorPostTransportMeta(this.postCreatorType, this.postCreatorResourceUrl.trim());
      await ChatStore.createGroupPost(conv.id, {
        template: this.postCreatorType,
        title,
        summary,
        deadlineAt,
        actionType: transportMeta.actionType,
        resourceType: transportMeta.resourceType,
        resourceUrl: this.postCreatorResourceUrl.trim() || undefined,
        announceInChat: true,
      });
      Toast.success('任务已发布');
      this._closePostCreator();
      // Reload posts
      await ChatStore.loadGroupPosts(conv.id);
      this.render();
    } catch (e) {
      console.error('[ChatPage] Failed to create post:', e);
      Toast.error(e.message || '发布失败');
      this.postCreatorSubmitting = false;
      this.render();
    }
  },

  _bindPostCreatorEvents() {
    // Close button
    const closeBtn = document.getElementById('pc-close-btn');
    if (closeBtn) closeBtn.onclick = () => this._closePostCreator();

    // Publish button
    const publishBtn = document.getElementById('pc-publish-btn');
    if (publishBtn) publishBtn.onclick = () => this._handlePostCreatorPublish();

    const feedbackToggleBtn = document.getElementById('pc-feedback-toggle-btn');
    if (feedbackToggleBtn) {
      feedbackToggleBtn.onclick = () => {
        this.postCreatorFeedbackEnabled = !this.postCreatorFeedbackEnabled;
        Toast.featureUnavailable();
        this.render();
        this._bindPostCreatorEvents();
      };
    }

    // Template cards
    this.container.querySelectorAll('.pc-fs-template-card').forEach(card => {
      card.onclick = () => {
        const nextType = card.dataset.pcTemplate;
        const prevMeta = this._creatorTemplateMeta(this.postCreatorType);
        const nextMeta = this._creatorTemplateMeta(nextType);
        const currentSummary = this.postCreatorSummary.trim();
        if (!currentSummary || currentSummary === prevMeta.defaultSummary) {
          this.postCreatorSummary = nextMeta.defaultSummary;
        }
        this.postCreatorType = nextType;
        this.render();
        this._bindPostCreatorEvents();
      };
    });

    // Title input
    const titleInput = document.getElementById('pc-title-input');
    if (titleInput) {
      titleInput.oninput = () => {
        this.postCreatorTitle = titleInput.value;
        const counter = document.getElementById('pc-title-counter');
        if (counter) counter.textContent = `${titleInput.value.length}/60`;
      };
    }

    // Summary textarea
    const summaryInput = document.getElementById('pc-summary-input');
    if (summaryInput) {
      summaryInput.oninput = () => {
        this.postCreatorSummary = summaryInput.value;
        const counter = document.getElementById('pc-summary-counter');
        if (counter) counter.textContent = `${summaryInput.value.length}/2000`;
      };
    }

    // Resource URL input
    const resourceInput = document.getElementById('pc-resource-input');
    if (resourceInput) {
      resourceInput.oninput = (e) => {
        this.postCreatorResourceUrl = e.target.value;
      };
    }

    // Deadline input
    const deadlineInput = document.getElementById('pc-deadline-input');
    if (deadlineInput) {
      deadlineInput.onchange = (e) => {
        this.postCreatorDeadline = e.target.value;
      };
    }
  },

  async _openPostDetail(postId) {
    const conv = ChatStore.getActiveConversation();
    if (!conv) return;

    let posts = ChatStore.getPosts(conv.id);
    let post = posts.find(p => p.id === postId);
    // 对齐 Vue 版：本地未找到时从服务端加载帖子列表再查找
    if (!post) {
      try {
        await ChatStore.loadGroupPosts(conv.id);
        posts = ChatStore.getPosts(conv.id);
        post = posts.find(p => p.id === postId);
      } catch (e) {
        console.warn('[ChatPage] Failed to load posts for detail:', e.message);
      }
    }
    if (!post) {
      Toast.warn('任务不存在或已被删除');
      return;
    }

    this.postsPanelOpen = false;
    this.postDetailOpen = true;
    this.postDetailPost = post;
    this.postDetailResponseText = '';
    this.postDetailSelectedFile = null;
    this.render();
  },

  _closePostDetail() {
    this.postDetailOpen = false;
    this.postDetailPost = null;
    this.postDetailResponseText = '';
    this.postDetailSelectedFile = null;
    this.postsPanelOpen = true;
    this.render();
  },

  _formatSelectedFileSize(size = 0) {
    const mb = size / 1024 / 1024;
    if (mb >= 1) return `${mb.toFixed(1)} MB`;
    const kb = size / 1024;
    return `${Math.max(1, Math.round(kb))} KB`;
  },

  async _handlePostRespond(responseType) {
    const post = this.postDetailPost;
    if (!post) return;

    const text = this.postDetailResponseText.trim();
    const file = this.postDetailSelectedFile || null;
    const responsePayload = this._resolvePostResponsePayload(post, responseType);
    // if (post.actionType === 'upload' && !text && !file) {
    //   Toast.warn('请填写备注或选择文件');
    //   return;
    // }

    try {
      if (post.actionType === 'upload') {
        await ChatStore.submitHomeworkPostResponse(post.id, post.conversationId, {
          note: text,
          file,
        });
        Toast.success('响应已提交');
        this.postDetailResponseText = '';
        this.postDetailSelectedFile = null;
        await ChatStore.loadGroupPosts(post.conversationId);
        this._closePostDetail();
        return;
      }
      await ChatStore.respondToGroupPost(post.id, post.conversationId, {
        responseType: responsePayload.responseType,
        confirmation: responsePayload.confirmation,
        content: text,
      });
      Toast.success('响应已提交');
      this.postDetailResponseText = '';
      // Reload posts to get updated responses
      await ChatStore.loadGroupPosts(post.conversationId);
      if (this.postsPanelOpen) this._refreshPostsPanel();
      this._closePostDetail();
    } catch (e) {
      console.error('[ChatPage] Failed to respond to post:', e);
      Toast.error(e.message || '响应失败');
    }
  },

  async _handleClosePost() {
    const post = this.postDetailPost;
    if (!post) return;
    if (post.status === 'closed') return;

    try {
      // 对齐 Vue 版 TaskFullScreen.closeSelectedPost：直接关闭并发送完成消息
      await ChatStore.closeGroupPost(post.id, post.conversationId);
      Toast.success('任务已完成');
      await ChatStore.loadGroupPosts(post.conversationId);
      if (this.postsPanelOpen) this._refreshPostsPanel();
      this._closePostDetail();
    } catch (e) {
      console.error('[ChatPage] Failed to close post:', e);
      Toast.error(e.message || '截止失败');
    }
  },

  _bindTaskDetailEvents() {
    // Back button
    const backBtn = document.getElementById('td-back-btn');
    if (backBtn) backBtn.onclick = () => this._closePostDetail();

    // Backdrop click to close
    const backdrop = document.getElementById('td-backdrop');
    if (backdrop) backdrop.onclick = () => this._closePostDetail();

    // Respond / submit button
    const respondBtn = document.getElementById('td-respond-btn');
    if (respondBtn) respondBtn.onclick = () => this._handlePostRespond(this.postDetailPost?.actionType || 'read');

    // Confirm buttons
    const confirmYesBtn = document.getElementById('td-confirm-yes-btn');
    if (confirmYesBtn) confirmYesBtn.onclick = () => this._handlePostRespond('going');

    const confirmNoBtn = document.getElementById('td-confirm-no-btn');
    if (confirmNoBtn) confirmNoBtn.onclick = () => this._handlePostRespond('not_going');

    // Close task button ("完成任务")
    const closePostBtn = document.getElementById('td-close-post-btn');
    if (closePostBtn) closePostBtn.onclick = () => this._handleClosePost();

    // Open resource
    const openResourceBtn = document.getElementById('td-open-resource-btn');
    if (openResourceBtn) openResourceBtn.onclick = () => this._openPostResource(this.postDetailPost);

    // View all participants
    const viewAllParticipantsBtn = document.getElementById('td-view-all-participants-btn');
    if (viewAllParticipantsBtn) viewAllParticipantsBtn.onclick = () => Toast.featureUnavailable();

    const viewAllParticipantsMoreBtn = document.getElementById('td-view-all-participants-more-btn');
    if (viewAllParticipantsMoreBtn) viewAllParticipantsMoreBtn.onclick = () => Toast.featureUnavailable();
  },

  destroy() {
    if (this.unsubChat) {
      this.unsubChat();
      this.unsubChat = null;
    }
    if (this.unsubUi) {
      this.unsubUi();
      this.unsubUi = null;
    }
  },
};

export default ChatPage;
