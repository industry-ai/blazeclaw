/* ================================================================
   AgentChat 重构版 - 聊天主页面（UI 渲染层）
   业务逻辑（消息收发/群管理/任务/Agent/设备投递）通过 Bridge -> postMessage 交由 C++ 处理
   语音转文字前端直接调用阿里云 DashScope ASR，不走 C++ 也不依赖 chat-bridge 服务
   ================================================================ */

import Bridge from '../bridge/index.js';
import AppConfig from '../config.js';
import UiStore from '../stores/uiStore.js';
import Toast from '../utils/toast.js';
import TimeUtils from '../utils/time.js';
import MarkdownRenderer from '../utils/markdown.js';
import AvatarSwatch from '../utils/avatar.js';
import DevicesPanelMixin from '../panels/devicesPanel.js';
import PostsPanelMixin from '../panels/postsPanel.js';

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
  devicesPanelOpen: false,
  // 设备绑定流程 UI 状态（手动输入三步流程：input -> confirm -> result）
  devicesBindFlowOpen: false,
  devicesBindStep: 'input',
  devicesBindManualInput: '',
  devicesBindToken: '',
  devicesBindSession: null,
  devicesBindResult: null,
  devicesBindError: '',
  devicesBindConfirming: false,
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
  // 群聊话题（创建/加入）
  topicsLoading: false,
  topicsError: '',
  topicCreatorOpen: false,
  topicCreatorTitle: '',
  topicCreatorSummary: '',
  topicCreatorSubmitting: false,
  joinTopicConfirmOpen: false,
  joinTopicTarget: null,

  init() {
    this.container = document.getElementById('page-chat');
    // 从 localStorage 恢复草稿
    const savedDrafts = Bridge.loadComposerDrafts();
    if (savedDrafts && typeof savedDrafts === 'object') {
      this.composerDrafts = savedDrafts;
    }
    this._scheduleRender = this._debounce(() => {
      if (this.createGroupDialogOpen || this.postCreatorOpen || this.postDetailOpen || this.postsPanelOpen || this.topicCreatorOpen || this.joinTopicConfirmOpen || this.devicesPanelOpen) return;
      this.render();
    }, 80);
    this.unsubChat = Bridge.subscribe(() => this._scheduleRender());
    this.unsubUi = UiStore.subscribe(() => this._scheduleRender());
    const activeId = Bridge.getActiveConversationId();
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
    Bridge.setActiveConversation(id);
    UiStore.setActiveConversation(id, options.preventNavigation === true);
    if (!options.skipMarkRead) {
      Bridge.clearMarkRead(id);
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
    const currentConvId = Bridge.getActiveConversationId();
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
        ${this._renderDevicesPanel()}
        ${this._renderPostCreator()}
        ${this._renderTopicCreator()}
        ${this._renderJoinTopicConfirm()}
        ${this._renderTaskDetail()}
      </div>
    `;

    this._bindDesktopEvents();
    this._bindMobileEvents();
    this._bindComposerEvents();
    this._bindMessagePostCards();
    this._bindRetryButtons();
    this._bindPostsPanelEvents();
    this._bindDevicesPanelEvents();
    this._bindPostCreatorEvents();
    this._bindTopicDialogEvents();
    this._bindTaskDetailEvents();
    this._restoreComposerState(composerState);
    this._restorePostsPanelState(postsPanelState);
    this._restoreScrollState(scrollState);
    this._lastRenderedConvId = Bridge.getActiveConversationId();

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
    const convs = Bridge.getConversations();
    const activeId = Bridge.getActiveConversationId();
    const activeConv = Bridge.getActiveConversation();
    const messages = Bridge.getActiveMessages();
    const isAgent = this._isAgentConv(activeConv);
    const isGroup = activeConv?.type === 'group';

    return `
    <div class="desktop-shell" style="position:relative;z-index:10;margin:1rem;height:calc(100dvh - 2rem);display:grid;grid-template-columns:20rem minmax(0,1fr) 0;overflow:hidden;border-radius:2rem;border:1px solid rgba(255,255,255,0.45);background:var(--app-surface);box-shadow:0 28px 70px rgba(95,73,170,0.12);backdrop-filter:blur(24px);">
      <!-- Sidebar -->
      <aside class="ds-sidebar" style="display:flex;flex-direction:column;min-height:0;border-right:1px solid rgba(226,232,240,0.7);background:var(--app-surface-strong);">
        ${this._renderSidebar(convs, activeId)}
      </aside>

      <!-- Main -->
      <main class="ds-main" style="display:flex;flex-direction:column;min-height:0;min-width:0;background:var(--app-page-bg);">
        ${activeConv ? this._renderDesktopHeader(activeConv, isAgent, isGroup) : this._emptyMain()}
        ${activeConv ? this._renderMessageStream(messages, activeConv) : ''}
        ${activeConv ? (activeConv.isKicked ? this._renderKickedBanner() : this._renderDesktopComposer(activeConv)) : ''}
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

  // 被踢出群聊时，替换输入框区域，提示用户已被移除且不可发送消息
  _renderKickedBanner() {
    return `
    <div class="kicked-banner-wrap" style="flex-shrink:0;padding:0.75rem 1rem 1rem;display:flex;align-items:center;justify-content:center;">
      <div class="kicked-banner" style="display:flex;align-items:center;gap:0.5rem;padding:0.6rem 1rem;border-radius:0.75rem;background:#fef2f2;color:#b91c1c;font-size:0.85rem;border:1px solid rgba(220,38,38,0.18);">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="flex-shrink:0;"><path d="M18 6L6 18M6 6l12 12"/></svg>
        <span>您已被移除群聊，无法发送消息</span>
      </div>
    </div>`;
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
    const status = Bridge.getStatus();
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
    const status = Bridge.getStatus();
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
    const convs = Bridge.getConversations();
    const activeId = UiStore.getActiveConversationId() || Bridge.getActiveConversationId();
    const activeConv = convs.find(c => c.id === activeId) || Bridge.getActiveConversation();
    const messages = activeConv ? Bridge.getMessages(activeConv.id) : [];
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
        ${conv.isKicked ? this._renderKickedBanner() : this._renderComposer(conv, false)}
      </div>
    </div>`;
  },

  // ═══════════════════════════════════════════
  // SIDEBAR (match ConversationSidebar)
  // ═══════════════════════════════════════════
  _renderSidebar(convs, activeId) {
    const unread = Bridge.getTotalUnreadCount();
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
    <div id="sidebar-search-panel" style="display:${this.searchOpen?'block':'none'};padding:0.75rem 1rem 0.75rem;border-bottom:1px solid var(--app-border);flex-shrink:0;">
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
      <div class="sp-avatar" style="background:${AvatarSwatch.getSwatch(Bridge.getUserId()||'me').bg};color:${AvatarSwatch.getSwatch(Bridge.getUserId()||'me').fg};">
        ${(Bridge.getPhone()||'用户').slice(-2)}
      </div>
      <div style="flex:1;min-width:0;">
        <div class="sp-name">${this._esc(Bridge.getPhone()||'未登录')}</div>
        <div class="sp-status">AI 在线</div>
      </div>
      <svg class="sp-arrow" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg>
    </div>`;
  },

  // 仅刷新会话列表 DOM（不重建搜索框等，避免输入框失焦）
  _refreshConvList() {
    const convs = Bridge.getConversations();
    const activeId = UiStore.getActiveConversationId() || Bridge.getActiveConversationId();
    const html = this._renderConversationList(convs, activeId) +
      (convs.length === 0 ? '<div style="text-align:center;padding:2.5rem 1rem;color:var(--app-muted);font-size:0.85rem;">暂无会话</div>' : '');
    this.container?.querySelectorAll('.conv-list').forEach(el => {
      el.innerHTML = html;
    });
    this._bindConvListEvents();
  },

  // 绑定会话列表项事件（列表 DOM 更新后需重新绑定）
  _bindConvListEvents() {
    // Conversation list clicks
    this.container.querySelectorAll('.conv-item').forEach(el => {
      el.onclick = (e) => {
        if (e.target.closest('.conv-more-btn')) return;
        const id = el.dataset.convId;
        Bridge.setActiveConversation(id);
        UiStore.setActiveConversation(id);
        Bridge.clearMarkRead(id);
        this.searchOpen = false;
        this.searchQuery = '';
        const msp = document.getElementById('mobile-search-panel');
        if (msp) msp.style.display = 'none';
      };
    });

    // 会话列表更多操作按钮（三点菜单）
    this.container.querySelectorAll('.conv-more-btn').forEach(btn => {
      btn.onclick = (e) => {
        e.stopPropagation();
        const id = btn.dataset.convId;
        if (id) this._openConvDropdown(btn, id);
      };
    });
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
      const aMsgs = Bridge.getMessages(a.id);
      const bMsgs = Bridge.getMessages(b.id);
      const aLatest = aMsgs.length > 0 ? aMsgs[aMsgs.length - 1].createdAt : 0;
      const bLatest = bMsgs.length > 0 ? bMsgs[bMsgs.length - 1].createdAt : 0;
      if (aLatest !== bLatest) return bLatest - aLatest;
      return 0;
    });

    const renderItem = (c) => {
      const msgs = Bridge.getMessages(c.id);
      const lastMsg = msgs.length > 0 ? msgs[msgs.length - 1] : null;
      const preview = lastMsg ? (lastMsg.text || lastMsg.content || '').slice(0, 50) : '';
      const isActive = c.id === activeId;
      const isAgent = c.type === 'agent';
      const isGroup = c.type === 'group';
      const isPinned = c.scope === 'personal_workspace';
      const isKicked = !!c.isKicked;
      const unread = Bridge.getConversationUnreadCount ? Bridge.getConversationUnreadCount(c.id) : (c.unread || 0);
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
      if (isKicked) {
        badgesHtml += `<span class="conv-badge-label conv-badge-kicked">已移除</span>`;
      }

      // Preview text
      let previewText = '';
      if (isKicked) {
        previewText = '您已被移除群聊';
      } else if (isPinned) {
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
      <div class="conv-item ${isActive ? 'active' : ''} ${isKicked ? 'conv-item-kicked' : ''}" data-conv-id="${c.id}">
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
        ${isGroup && !isPinned && !isKicked ? `<button class="conv-more-btn" data-conv-id="${c.id}" title="更多操作" aria-label="更多操作">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="5" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="12" cy="19" r="1"/></svg>
        </button>` : ''}
      </div>`;
    };

    return sorted.map(renderItem).join('');
  },

  // Get participant avatars for a group conversation (max 4)
  _getParticipantAvatars(convId) {
    const msgs = Bridge.getMessages(convId);
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
    const devices = Bridge.getBoundDevices();
    const onlineCount = devices.filter(device => device.status === 'online').length;
    const workspace = Bridge.getConversations().find(c => c.scope === 'personal_workspace');
    const draftCount = Bridge.getWorkspaceDraftActions(workspace?.id).length;
    return `AI 在线 · ${onlineCount} 台设备在线 · ${draftCount} 条待确认`;
  },

  // Format time label for conversation list
  _convTimeLabel(convId) {
    const msgs = Bridge.getMessages(convId);
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
    const rawContent = String(msg.text ?? msg.content ?? '');
    const parsed = Bridge.parseSharedPostMessageContent(rawContent, convId);
    const attachments = (msg.attachments && msg.attachments.length)
      ? msg.attachments
      : ((parsed && parsed.attachments) || []);
    return {
      displayContent: (parsed && parsed.content) || rawContent,
      post: (parsed && parsed.post) || null,
      attachments,
    };
  },

  // 对齐 Vue 版 isImageResourceUrl：判断 URL 是否指向图片资源
  _isImageResourceUrl(rawUrl) {
    const value = String(rawUrl ?? '').trim();
    if (!value) return false;
    try {
      const url = new URL(value);
      if (url.protocol !== 'http:' && url.protocol !== 'https:') return false;
      return /\.(?:png|jpe?g|webp|gif|avif|bmp|svg)(?:$|[?#])/i.test(url.pathname);
    } catch {
      return false;
    }
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
        const url = att.url || '';
        const cardDomain = (() => { try { return new URL(url).host; } catch { return ''; } })();

        // 对齐 Vue 版 MessageAttachmentCard.vue isImageWebview：图片 URL 渲染为内联图片预览
        if (this._isImageResourceUrl(url)) {
          const imgTitle = att.title || '图片';
          const cardIdx = att._cardIdx || 0;
          return `
        <article class="msg-post-card msg-post-card-image" data-resource-url="${this._esc(url)}" data-card-idx="${cardIdx}" style="position:relative;display:flex;width:100%;flex-direction:column;margin-top:0.5rem;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-surface-elevated);padding:0;overflow:hidden;box-shadow:0 8px 18px rgba(86,74,132,0.05);">
          <button class="msg-post-card-main" data-resource-url="${this._esc(url)}" type="button" style="display:flex;width:100%;flex-direction:column;border:0;background:transparent;padding:0;text-align:left;cursor:pointer;color:inherit;">
            <img src="${this._esc(url)}" alt="${this._esc(imgTitle)}" loading="lazy" decoding="async" referrerpolicy="no-referrer"
              style="display:block;width:100%;max-width:100%;aspect-ratio:4/3;height:auto;max-height:22rem;object-fit:contain;background:#f9fafb;"
              onerror="this.style.display='none';this.parentElement.querySelector('.msg-image-fallback').style.display='flex';" />
            <div class="msg-image-fallback" style="display:none;width:100%;aspect-ratio:4/3;max-height:22rem;align-items:center;justify-content:center;background:var(--app-brand-soft);color:var(--app-muted);font-size:0.86rem;">图片加载失败</div>
            <div style="display:flex;width:100%;align-items:center;gap:0.75rem;overflow:hidden;padding:0.75rem 3rem 0.85rem 0.9rem;">
              <div style="min-width:0;flex:1;">
                <div style="display:flex;align-items:center;gap:0.45rem;">
                  <span style="color:var(--app-muted);font-size:0.82rem;">图片</span>
                  ${cardDomain ? `<span style="color:var(--app-muted);font-size:0.78rem;">· ${this._esc(cardDomain)}</span>` : ''}
                </div>
                <div style="margin-top:0.18rem;font-size:1rem;font-weight:700;color:var(--app-text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(imgTitle)}</div>
              </div>
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="flex-shrink:0;color:var(--app-muted);"><path d="M18 13v6a2 2 0 01-2 2H5a2 2 0 01-2-2V8a2 2 0 012-2h6"/><path d="M15 3h6v6"/><path d="M10 14L21 3"/></svg>
            </div>
          </button>
          <button class="msg-post-card-forward" type="button" data-action="forward" data-resource-url="${this._esc(url)}" data-card-title="${this._esc(imgTitle)}" aria-label="转发资源" style="position:absolute;right:0.8rem;bottom:1rem;display:flex;width:2.1rem;height:2.1rem;align-items:center;justify-content:center;border:0;border-radius:999px;background:transparent;color:var(--app-muted);cursor:pointer;">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 2L11 13"/><path d="M22 2l-7 20-4-9-9-4 20-7z"/></svg>
          </button>
          <button class="msg-post-card-more" type="button" data-action="more" data-resource-url="${this._esc(url)}" aria-label="更多操作" style="position:absolute;top:0.7rem;right:0.7rem;display:flex;width:2rem;height:2rem;align-items:center;justify-content:center;border:0;border-radius:999px;background:color-mix(in srgb,var(--app-surface-elevated) 86%,transparent);color:var(--app-muted);cursor:pointer;">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="5" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="12" cy="19" r="1"/></svg>
          </button>
        </article>`;
        }

        // 非图片：渲染为 H5 卡片 / 互动资源卡片
        const isH5Card = att.objectKind === 'h5_card' || att.sourceSkillId === 'h5-cards' || att.artifactType === 'html'
          || /\/[^/?#]*card[^/?#]*\.html?(?:[?#].*)?$/i.test(url) || /\/index\.html?(?:[?#].*)?$/i.test(url);
        const cardLabel = isH5Card ? 'H5 卡片' : '互动资源';
        const cardBtnText = isH5Card ? '查看卡片' : '打开';
        const cardTitle = att.title || cardLabel;
        console.log('[chat] cardTitle 来源:', { cardTitle, attTitle: att.title, cardLabel, url: att.url, fullAtt: att });
        const cardSummary = att.summary || '';
        const cardIdx = att._cardIdx || 0;
        // 提取附件元数据用于 postMessage bridge（对齐 Vue 版 taskMetadataFromAttachment）
        const attInput = att.input || {};
        const attReq = attInput.requester || {};
        const attMetaAttrs = [
          `data-att-input-text="${this._esc(attInput.text || '')}"`,
          `data-att-input-user-id="${this._esc(attReq.userId || '')}"`,
          `data-att-input-phone="${this._esc(attReq.phone || '')}"`,
          `data-att-input-display-name="${this._esc(attReq.displayName || '')}"`,
          `data-att-task-no="${this._esc(att.taskNo || '')}"`,
          `data-att-provider-id="${this._esc(att.providerId || '')}"`,
          `data-att-skill-id="${this._esc(att.skillId || '')}"`,
          `data-att-source-skill-id="${this._esc(att.sourceSkillId || '')}"`,
          `data-att-object-kind="${this._esc(att.objectKind || '')}"`,
          `data-att-artifact-type="${this._esc(att.artifactType || '')}"`,
          `data-att-mime-type="${this._esc(att.mimeType || '')}"`,
          `data-att-summary="${this._esc(cardSummary)}"`,
        ].join(' ');
        return `
        <article class="msg-post-card msg-post-card-webview" data-resource-url="${this._esc(url)}" data-card-title="${this._esc(cardTitle)}" data-card-idx="${cardIdx}" ${attMetaAttrs} style="position:relative;display:flex;width:100%;flex-direction:column;margin-top:0.5rem;border:1px solid var(--app-border);border-radius:1rem;background:var(--app-surface-elevated);padding:0;overflow:hidden;box-shadow:0 8px 18px rgba(86,74,132,0.05);">
          <button class="msg-post-card-main" data-resource-url="${this._esc(url)}" type="button" style="display:flex;width:100%;flex-direction:column;border:0;background:transparent;padding:0;text-align:left;cursor:pointer;color:inherit;">
            <div style="position:relative;display:flex;width:100%;flex-direction:column;gap:0.75rem;padding:0.95rem 3rem 1rem 1.05rem;background:linear-gradient(135deg,color-mix(in srgb,var(--app-brand) 10%,transparent),transparent 54%);">
              <div style="position:absolute;left:0;top:0;bottom:0;width:0.25rem;background:var(--app-brand);"></div>
              <div style="display:flex;min-width:0;align-items:flex-start;gap:0.75rem;">
                <span style="display:flex;width:2.65rem;height:2.65rem;flex:0 0 auto;align-items:center;justify-content:center;border-radius:0.8rem;background:var(--app-brand-soft);color:var(--app-brand);">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 2l2.4 7.4H22l-6.2 4.5 2.4 7.4L12 16.8l-6.2 4.5 2.4-7.4L2 9.4h7.6z"/></svg>
                </span>
                <span style="min-width:0;flex:1;">
                  <span style="display:block;font-size:0.78rem;color:var(--app-muted);">${this._esc(cardLabel)}</span>
                  <span style="display:block;margin-top:0.18rem;font-size:1.02rem;font-weight:800;color:var(--app-text);line-height:1.28;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(cardTitle)}</span>
                  ${cardDomain ? `<span style="display:block;margin-top:0.18rem;font-size:0.8rem;color:var(--app-text-secondary);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(cardDomain)}</span>` : ''}
                </span>
              </div>
              ${cardSummary ? `<span style="display:-webkit-box;overflow:hidden;-webkit-box-orient:vertical;-webkit-line-clamp:2;color:var(--app-text-secondary);font-size:0.86rem;line-height:1.5;">${this._esc(cardSummary)}</span>` : ''}
              <div style="display:flex;align-items:center;">
                <span style="display:inline-flex;align-items:center;border-radius:0.7rem;background:var(--app-brand);padding:0.42rem 0.72rem;color:#fff;font-size:0.8rem;font-weight:700;line-height:1.2;">${this._esc(cardBtnText)}</span>
              </div>
            </div>
          </button>
          <button class="msg-post-card-forward" type="button" data-action="forward" data-resource-url="${this._esc(url)}" data-card-title="${this._esc(cardTitle)}" aria-label="转发资源" style="position:absolute;right:0.8rem;bottom:1rem;display:flex;width:2.1rem;height:2.1rem;align-items:center;justify-content:center;border:0;border-radius:999px;background:transparent;color:var(--app-muted);cursor:pointer;">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 2L11 13"/><path d="M22 2l-7 20-4-9-9-4 20-7z"/></svg>
          </button>
          <button class="msg-post-card-more" type="button" data-action="more" data-resource-url="${this._esc(url)}" aria-label="更多操作" style="position:absolute;top:0.7rem;right:0.7rem;display:flex;width:2rem;height:2rem;align-items:center;justify-content:center;border:0;border-radius:999px;background:color-mix(in srgb,var(--app-surface-elevated) 86%,transparent);color:var(--app-muted);cursor:pointer;">
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
      const isPlain = !/[#*_`>[\]()]/.test(msg.text || msg.content || '');
      const html = isPlain ? this._esc(msg.text || msg.content || '') : MarkdownRenderer.render(msg.text || msg.content || '');
      return `<div class="msg-system-row"><div class="msg-system-bubble"><div class="whitespace-pre-wrap" style="word-break:break-word;">${html.replace(/\n/g,'<br>')}</div></div></div>`;
    }

    if (isAgent) {
      // 对齐 Vue 版 MessageBubble：agent 消息也解析信封并渲染附件卡片
      const { displayContent, attachments } = this._resolveMessageDisplay(msg, conv?.id);
      const isStreaming = msg.status === 'streaming';
      const isTypingOnly = isStreaming && !displayContent.trim();
      const isPlain = !/[#*_`>[\]()]/.test(displayContent);
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
      const swatch = AvatarSwatch.getSwatch(Bridge.getUserId() || 'me');
      const initials = (Bridge.getPhone()||'').slice(-2) || '我';
      const { displayContent, attachments } = this._resolveMessageDisplay(msg, conv?.id);
      const isPlain = !/[#*_`>[\]()]/.test(displayContent);
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
      const isPlain = !/[#*_`>[\]()]/.test(displayContent);
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
    // 防抖持久化草稿到 localStorage
    if (this._draftSaveTimer) clearTimeout(this._draftSaveTimer);
    this._draftSaveTimer = setTimeout(() => {
      Bridge.saveComposerDrafts(this.composerDrafts);
    }, 500);
  },

  _captureComposerState() {
    const activeElement = document.activeElement;
    const input = document.getElementById('composer-input');
    if (!input) return null;
    const conversationId = Bridge.getActiveConversationId();
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
    const conversationId = Bridge.getActiveConversationId();
    if (!stream || !conversationId) {
      return {
        conversationId,
        messageCount: Bridge.getActiveMessages().length,
        scrollTop: 0,
        isNearBottom: true,
      };
    }
    const maxTop = Math.max(0, stream.scrollHeight - stream.clientHeight);
    const distanceFromBottom = maxTop - stream.scrollTop;
    return {
      conversationId,
      messageCount: Bridge.getActiveMessages().length,
      scrollTop: stream.scrollTop,
      isNearBottom: distanceFromBottom <= 48,
    };
  },

  _restoreViewportState(state) {
    const stream = document.getElementById('msg-stream');
    const conversationId = Bridge.getActiveConversationId();
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

    const currentCount = Bridge.getActiveMessages().length;
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
      if (main && Bridge.getActiveConversation()) {
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
      Bridge.setActiveConversation('');
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
        this._refreshConvList();
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
    const conversationId = Bridge.getActiveConversationId();

    // ── 侧边栏事件（与会话列表/搜索/新建群聊相关，不依赖 composer input）──
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
        this._refreshConvList();
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

    // 会话列表项事件
    this._bindConvListEvents();

    // Profile -> me page
    const profBtn = document.getElementById('sidebar-profile-btn');
    if (profBtn) profBtn.onclick = () => { window.location.hash = '#/me'; };

    // New group button
    const newBtn = document.getElementById('sidebar-new-btn');
    if (newBtn) newBtn.onclick = () => this._openCreateGroupDialog();

    // Sparkle → 插入 @炎图AI助手
    const sparkleBtn = document.getElementById('composer-sparkle-btn');
    if (sparkleBtn) sparkleBtn.onclick = () => {
      const inp = document.getElementById('composer-input');
      if (!inp) return;
      const mentionText = '@炎图AI助手 ';
      const currentText = inp.value;
      const needsSeparator = currentText.length > 0 && !/\s$/.test(currentText);
      inp.value = `${currentText}${needsSeparator ? ' ' : ''}${mentionText}`.slice(0, 2000);
      inp.focus();
      inp.selectionStart = inp.value.length;
      inp.selectionEnd = inp.value.length;
      inp.dispatchEvent(new Event('input', { bubbles: true }));
      inp.style.height = 'auto';
      inp.style.height = Math.min(inp.scrollHeight, 72) + 'px';
    };

    // 语音模式切换按钮
    const voiceToggleBtn = document.getElementById('composer-voice-toggle-btn');
    if (voiceToggleBtn) {
      voiceToggleBtn.onclick = () => this._toggleVoiceMode();
    }

    // 语音录音按钮
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
        sendBtn.className = `composer-send ${hasText?'enabled':'disabled'} ${Bridge.getActiveConversation()?.type==='agent'?'agent-send':''}`;
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

    const convId = Bridge.getActiveConversationId();
    if (!convId) { Toast.warn('请先选择一个会话'); return; }

    // 被踢出的群聊禁止发送消息
    const activeConv = Bridge.getActiveConversation();
    if (activeConv && activeConv.isKicked) {
      Toast.warn('您已被移除群聊，无法发送消息');
      return;
    }

    this._setComposerDraft(convId, '');
    this.pendingScrollToBottom = true;
    this.suppressComposerFocusRestore = true;
    input.value = '';
    input.style.height = 'auto';

    const sendBtn = document.getElementById('composer-send-btn');
    if (sendBtn) {
      sendBtn.disabled = true;
      sendBtn.className = `composer-send disabled ${Bridge.getActiveConversation()?.type==='agent'?'agent-send':''}`;
    }
    const counter = document.getElementById('composer-counter');
    if (counter) counter.textContent = '0 / 2000';

    Bridge.sendUserMessage(text, convId);
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

      const text = await this._transcribeAudio(audio, 'zh-CN');
      this.voiceState = 'idle';
      this.voiceErrorMessage = '';

      // 语音识别结果追加到输入框已有内容后面，而非覆盖（用户先打字再语音时，已有文字应保留）
      this.isVoiceMode = false;
      const convId = Bridge.getActiveConversationId();
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

  // 语音转文字：前端直接调用阿里云 DashScope ASR，不走 C++ 也不依赖 chat-bridge 服务
  // 对齐服务端 _speechTranscribeDashScope 的 OpenAI 兼容路径（qwen3-asr-flash，无需 ffmpeg）
  async _transcribeAudio(audioBlob, lang = 'zh-CN') {
    const apiKey = AppConfig.getDashscopeApiKey();
    if (!apiKey) throw new Error('语音转文字未配置：请设置 dashscopeApiKey');

    const model = AppConfig.getDashscopeAsrModel();
    const dataUrl = await new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(reader.result);
      reader.onerror = () => reject(new Error('音频读取失败'));
      reader.readAsDataURL(audioBlob);
    });

    const resp = await fetch('https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions', {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${apiKey}`,
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        model,
        messages: [{
          role: 'user',
          content: [{ type: 'input_audio', input_audio: { data: dataUrl } }],
        }],
      }),
    });
    const raw = await resp.text();
    if (!resp.ok) throw new Error(raw || 'DashScope ASR 识别失败');
    const data = JSON.parse(raw);
    const text = data?.choices?.[0]?.message?.content || '';
    const transcript = (typeof text === 'string' ? text : '').replace(/\s+/g, ' ').trim();
    if (!transcript) throw new Error('没有识别到文字，请再说一遍');
    return transcript.slice(0, 2000);
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
  _openDevicesPage() {
    window.location.hash = '#/devices';
  },
  _handleBindDeviceAction() {
    const conv = Bridge.getActiveConversation();
    if (!conv) {
      Toast.warn('请先进入会话再绑定设备');
      return;
    }
    if (conv.scope !== 'personal_workspace' && conv.type !== 'group') {
      Toast.warn('请先进入群聊再绑定设备');
      return;
    }
    this._openDevicesPanel();
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
      const conversationId = await Bridge.createGroupConversation(name);
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
    const conv = Bridge.getConversations().find(c => c.id === conversationId);
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
    const wasActive = Bridge.getActiveConversationId() === id;
    const deleted = Bridge.deleteConversationFromList(id);
    if (!deleted) {
      Toast.warn('会话删除失败');
      this._closeDeleteConversationDialog();
      return;
    }
    // 对齐 Vue 版：删除的是当前活跃会话时，切换到下一个可用会话
    if (wasActive) {
      const convs = Bridge.getConversations();
      const nextConv = convs.find(c => c.scope === 'personal_workspace')
        || convs.find(c => c.type === 'agent')
        || convs[0]
        || null;
      if (nextConv) {
        Bridge.setActiveConversation(nextConv.id);
        UiStore.setActiveConversation(nextConv.id, true);
      } else {
        UiStore.resetToListView();
      }
    }
    this._closeDeleteConversationDialog();
    Toast.success('会话已删除');
  },

  // ── 会话更多操作下拉菜单 ──
  _openConvDropdown(btn, convId) {
    this._closeConvDropdown();
    const conv = Bridge.getConversations().find(c => c.id === convId);
    if (!conv) return;
    if (conv.scope === 'personal_workspace' || conv.type === 'agent') return;

    const rect = btn.getBoundingClientRect();
    const menu = document.createElement('div');
    menu.className = 'conv-dropdown-menu';
    menu.id = 'conv-dropdown-menu';
    menu.style.position = 'fixed';
    menu.style.left = rect.right - 160 + 'px';
    menu.style.top = rect.bottom + 4 + 'px';
    menu.innerHTML =
      '<button class="conv-dropdown-item" data-action="hide-conv" data-conv-id="' + this._esc(convId) + '">' +
        '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17.94 17.94A10.07 10.07 0 0112 20c-7 0-11-8-11-8a18.45 18.45 0 015.06-5.94M9.9 4.24A9.12 9.12 0 0112 4c7 0 11 8 11 8a18.5 18.5 0 01-2.16 3.19m-6.72-1.07a3 3 0 11-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/></svg>' +
        '<span>不显示该聊天</span>' +
      '</button>' +
      '<button class="conv-dropdown-item conv-dropdown-danger" data-action="remove-conv" data-conv-id="' + this._esc(convId) + '">' +
        '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h18"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6"/><path d="M8 6V4a2 2 0 012-2h4a2 2 0 012 2v2"/></svg>' +
        '<span>删除该聊天</span>' +
      '</button>';
    document.body.appendChild(menu);

    // 点击外部关闭
    const closeHandler = (e) => {
      if (!menu.contains(e.target)) {
        this._closeConvDropdown();
      }
    };
    setTimeout(() => document.addEventListener('click', closeHandler, true), 0);
    this._convDropdownCloseHandler = closeHandler;

    // 绑定菜单项点击
    menu.querySelectorAll('.conv-dropdown-item').forEach(item => {
      item.onclick = (e) => {
        e.stopPropagation();
        const action = item.dataset.action;
        const id = item.dataset.convId;
        this._closeConvDropdown();
        if (action === 'hide-conv') {
          this._openHideConversationDialog(id);
        } else if (action === 'remove-conv') {
          this._openRemoveConversationDialog(id);
        }
      };
    });
  },

  _closeConvDropdown() {
    const menu = document.getElementById('conv-dropdown-menu');
    if (menu) menu.remove();
    if (this._convDropdownCloseHandler) {
      document.removeEventListener('click', this._convDropdownCloseHandler, true);
      this._convDropdownCloseHandler = null;
    }
  },

  // "不显示该聊天"确认弹窗
  _openHideConversationDialog(convId) {
    const conv = Bridge.getConversations().find(c => c.id === convId);
    if (!conv) return;
    const name = conv.name || convId.replace(/^#/, '');
    const dialog = document.getElementById('delete-conversation-dialog');
    if (!dialog) return;
    this.deleteConversationTargetId = convId;
    this.deleteConversationTargetName = name;
    dialog.innerHTML =
      '<div class="modal-card dc-modal">' +
        '<div class="dc-icon-wrap">' +
          '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17.94 17.94A10.07 10.07 0 0112 20c-7 0-11-8-11-8a18.45 18.45 0 015.06-5.94M9.9 4.24A9.12 9.12 0 0112 4c7 0 11 8 11 8a18.5 18.5 0 01-2.16 3.19m-6.72-1.07a3 3 0 11-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/></svg>' +
        '</div>' +
        '<div class="dc-content">' +
          '<h3 class="dc-title">不显示该聊天？</h3>' +
        '</div>' +
        '<div class="modal-footer dc-footer">' +
          '<button id="dc-cancel-btn" class="btn btn-secondary">取消</button>' +
          '<button id="dc-confirm-btn" class="btn btn-danger">不显示</button>' +
        '</div>' +
      '</div>';
    dialog.style.display = 'flex';
    const cancelBtn = document.getElementById('dc-cancel-btn');
    if (cancelBtn) cancelBtn.onclick = () => { dialog.style.display = 'none'; };
    const confirmBtn = document.getElementById('dc-confirm-btn');
    if (confirmBtn) confirmBtn.onclick = () => {
      dialog.style.display = 'none';
      this._handleHideConversation(convId);
    };
    dialog.onclick = (e) => {
      if (e.target === dialog) dialog.style.display = 'none';
    };
  },

  // "不显示该聊天"：调用已有得本地删除逻辑
  _handleHideConversation(convId) {
    const conv = Bridge.getConversations().find(c => c.id === convId);
    if (!conv) return;
    const wasActive = Bridge.getActiveConversationId() === convId;
    const deleted = Bridge.deleteConversationFromList(convId);
    if (!deleted) {
      Toast.warn('操作失败');
      return;
    }
    if (wasActive) {
      const convs = Bridge.getConversations();
      const nextConv = convs.find(c => c.scope === 'personal_workspace')
        || convs.find(c => c.type === 'agent')
        || convs[0]
        || null;
      if (nextConv) {
        Bridge.setActiveConversation(nextConv.id);
        UiStore.setActiveConversation(nextConv.id, true);
      } else {
        UiStore.resetToListView();
      }
    }
    Toast.success('已不显示该聊天');
  },

  // "删除该聊天"确认弹窗（真实接口待对接）
  _openRemoveConversationDialog(convId) {
    const conv = Bridge.getConversations().find(c => c.id === convId);
    if (!conv) return;
    const name = conv.name || convId.replace(/^#/, '');
    const dialog = document.getElementById('delete-conversation-dialog');
    if (!dialog) return;
    this.deleteConversationTargetId = convId;
    this.deleteConversationTargetName = name;
    dialog.innerHTML =
      '<div class="modal-card dc-modal">' +
        '<div class="dc-icon-wrap">' +
          '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h18"/><path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6"/><path d="M8 6V4a2 2 0 012-2h4a2 2 0 012 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>' +
        '</div>' +
        '<div class="dc-content">' +
          '<h3 class="dc-title">确定删除聊天「<span class="dc-name">' + this._esc(name) + '</span>」？</h3>' +
          '<p class="dc-hint">删除后聊天记录将被清除，且无法恢复。此操作不可撤销。</p>' +
        '</div>' +
        '<div class="modal-footer dc-footer">' +
          '<button id="dc-cancel-btn" class="btn btn-secondary">取消</button>' +
          '<button id="dc-confirm-btn" class="btn btn-danger">确定删除</button>' +
        '</div>' +
      '</div>';
    dialog.style.display = 'flex';
    const cancelBtn = document.getElementById('dc-cancel-btn');
    if (cancelBtn) cancelBtn.onclick = () => { dialog.style.display = 'none'; };
    const confirmBtn = document.getElementById('dc-confirm-btn');
    if (confirmBtn) confirmBtn.onclick = () => {
      dialog.style.display = 'none';
      // TODO: 对接真实删除接口
      console.log('[chat] 删除该聊天:', convId, name);
      Toast.show('删除接口待对接', 'warn');
    };
    dialog.onclick = (e) => {
      if (e.target === dialog) dialog.style.display = 'none';
    };
  },



  // ── 创建话题弹窗 ──
  _renderTopicCreator() {
    if (!this.topicCreatorOpen) return '';
    const { topicCreatorTitle, topicCreatorSummary, topicCreatorSubmitting } = this;
    return `
    <div class="modal-overlay" id="topic-creator-overlay">
      <div class="modal-card" style="max-width:26rem;">
        <div class="modal-header">
          <h3>创建话题</h3>
          <button class="icon-btn" id="tc-close-btn" title="关闭">&times;</button>
        </div>
        <div class="irc-modal-body">
          <label class="irc-field-label">话题标题 <span style="color:var(--app-danger);">*</span></label>
          <input type="text" class="irc-field-input" id="tc-title-input"
            value="${this._esc(topicCreatorTitle)}" placeholder="输入话题标题" maxlength="60" autocomplete="off" />
          <label class="irc-field-label">话题描述</label>
          <textarea class="irc-field-input irc-field-textarea" id="tc-summary-input"
            placeholder="简要描述话题内容（可选）" maxlength="500">${this._esc(topicCreatorSummary)}</textarea>
          <div class="irc-field-check" style="margin-top:0.75rem;cursor:default;">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="var(--app-muted)" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/></svg>
            <span style="font-size:0.78rem;color:var(--app-muted);">创建后将自动加入，可在"聊天室"中继续讨论</span>
          </div>
        </div>
        <div class="irc-modal-foot">
          <button class="btn btn-secondary" id="tc-cancel-btn">取消</button>
          <button class="btn btn-primary" id="tc-confirm-btn" ${(!topicCreatorTitle.trim() || topicCreatorSubmitting) ? 'disabled' : ''}>
            ${topicCreatorSubmitting ? '创建中...' : '创建话题'}
          </button>
        </div>
      </div>
    </div>`;
  },

  // ── 申请加入话题确认弹窗 ──
  _renderJoinTopicConfirm() {
    if (!this.joinTopicConfirmOpen || !this.joinTopicTarget) return '';
    const topic = this.joinTopicTarget;
    return `
    <div class="modal-overlay" id="join-topic-overlay">
      <div class="modal-card" style="max-width:24rem;">
        <div class="modal-header">
          <h3>申请加入话题</h3>
          <button class="icon-btn" id="jt-close-btn" title="关闭">&times;</button>
        </div>
        <div class="irc-modal-body">
          <div style="display:flex;gap:0.75rem;align-items:flex-start;">
            <div style="display:flex;align-items:center;justify-content:center;width:2.75rem;height:2.75rem;border-radius:0.9rem;background:var(--app-brand-soft);color:var(--app-brand);flex-shrink:0;">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15a2 2 0 01-2 2H7l-4 4V5a2 2 0 012-2h14a2 2 0 012 2z"/></svg>
            </div>
            <div style="min-width:0;flex:1;">
              <div style="font-size:1rem;font-weight:600;color:var(--app-text);">${this._esc(topic.title)}</div>
              ${topic.summary ? `<div style="margin-top:0.3rem;font-size:0.82rem;color:var(--app-muted);line-height:1.55;">${this._esc(topic.summary)}</div>` : ''}
              <div style="margin-top:0.5rem;font-size:0.76rem;color:var(--app-text-muted);">by ${this._esc(topic.creator || '匿名')} · ${topic.memberCount || 0} 人参与</div>
            </div>
          </div>
          <div style="margin-top:0.95rem;padding:0.75rem 0.9rem;border-radius:0.75rem;background:var(--app-subtle-bg);font-size:0.8rem;color:var(--app-text-secondary);line-height:1.55;">
            加入后将跳转到"聊天室"中对应的话题群聊，可与其他成员实时讨论。
          </div>
        </div>
        <div class="irc-modal-foot">
          <button class="btn btn-secondary" id="jt-cancel-btn">取消</button>
          <button class="btn btn-primary" id="jt-confirm-btn">确认加入</button>
        </div>
      </div>
    </div>`;
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
          const cardTitle = mainBtn.dataset.cardTitle || card.dataset.cardTitle || 'WebView';
          const postId = card.dataset.postId;
          // 图片和 H5 卡片统一在 iframe overlay 中打开
          if ((card.classList.contains('msg-post-card-image') || card.classList.contains('msg-post-card-webview')) && resourceUrl) {
            const attMeta = this._extractAttMetaFromCard(card, resourceUrl, cardTitle);
            this._openWebviewOverlay(attMeta);
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
            // 提取完整附件元数据（对齐 Vue 版 attachment 全字段传递）
            const attMeta = this._extractAttMetaFromCard(card, resourceUrl, cardTitle);
            const attachment = { type: 'webview', ...attMeta };
            this._showForwardModal(attachment, cardTitle);
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
          this._showAttachmentActionMenu(resourceUrl, e, card);
        };
      }
    });
  },

  // 从卡片 DOM 元素提取附件元数据（对齐 Vue 版 taskMetadataFromAttachment）
  _extractAttMetaFromCard(card, url, title) {
    const inputText = card.dataset.attInputText || '';
    const inputUserId = card.dataset.attInputUserId || '';
    const inputPhone = card.dataset.attInputPhone || '';
    const inputDisplayName = card.dataset.attInputDisplayName || '';

    const input = {};
    if (inputText) input.text = inputText;
    if (inputUserId || inputPhone || inputDisplayName) {
      input.requester = {};
      if (inputUserId) input.requester.userId = inputUserId;
      if (inputPhone) input.requester.phone = inputPhone;
      if (inputDisplayName) input.requester.displayName = inputDisplayName;
    }

    return {
      url,
      title,
      summary: card.dataset.attSummary || undefined,
      taskNo: card.dataset.attTaskNo || undefined,
      providerId: card.dataset.attProviderId || undefined,
      skillId: card.dataset.attSkillId || undefined,
      sourceSkillId: card.dataset.attSourceSkillId || undefined,
      objectKind: card.dataset.attObjectKind || undefined,
      artifactType: card.dataset.attArtifactType || undefined,
      mimeType: card.dataset.attMimeType || undefined,
      input: (input.text || input.requester) ? input : undefined,
    };
  },

  // 对齐 Vue 版 ForwardModal：转发资源到其他会话
  _showForwardModal(attachment, cardTitle) {
    if (!attachment || !attachment.url) return;
    const existing = document.querySelector('.forward-modal-overlay');
    if (existing) existing.remove();

    const conversations = Bridge.getConversations().filter(c => c.type !== 'agent');
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
        // 对齐 Vue 版 forwardTo：将完整附件编码为信封，发送到目标会话
        const displayText = `[转发卡片] ${cardTitle}`;
        const encodedText = Bridge.encodeForwardAttachment(displayText, cardTitle, [attachment]);
        Bridge.sendUserMessage(encodedText, conv.id, {
          attachments: [attachment],
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
  _showAttachmentActionMenu(resourceUrl, evt, card) {
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
      // 对齐协议 §5.3：用户显式投递到设备
      { id: 'device_open', label: '投递到设备', icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>' },
      { id: 'device_speak', label: '播报到设备', icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 5L6 9H2v6h4l5 4V5z"/><path d="M19.07 4.93a10 10 0 010 14.14M15.54 8.46a5 5 0 010 7.07"/></svg>' },
      { id: 'save_local', label: '保存到本地', icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>' },
      { id: 'forward', label: '转发', icon: '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 2L11 13"/><path d="M22 2l-7 20-4-9-9-4 20-7z"/></svg>' },
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
          // 图片和 H5 卡片统一在 iframe overlay 中打开
          const cardTitle = card?.dataset?.cardTitle || 'H5 卡片';
          const attMeta = card
            ? this._extractAttMetaFromCard(card, resourceUrl, cardTitle)
            : { url: resourceUrl, title: cardTitle };
          this._openWebviewOverlay(attMeta);
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
        } else if (action.id === 'device_open') {
          // 对齐协议 §5.3 + §6：投递内容到设备
          this._dispatchContentToDevice(resourceUrl, 'H5 卡片');
        } else if (action.id === 'device_speak') {
          // 对齐协议 §5.3 + §7：播报到设备
          this._dispatchSpeakToDevice(resourceUrl);
        } else if (action.id === 'forward') {
          // 对齐 Vue 版 executeAttachmentAction -> triggerSecondaryShare -> openForwardModal
          const cardTitle = card?.dataset?.cardTitle || 'H5 卡片';
          const attMeta = card
            ? this._extractAttMetaFromCard(card, resourceUrl, cardTitle)
            : { url: resourceUrl, title: cardTitle };
          const attachment = { type: 'webview', ...attMeta };
          this._showForwardModal(attachment, cardTitle);
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

  // ── WebView iframe overlay + postMessage Bridge（对齐协议 §4.2）──

  /**
   * 在 iframe overlay 中打开 H5 页面，并建立 postMessage Bridge
   * 对齐协议 §4.2.1：页面加载后由宿主发送 bridge_context
   * @param {object|string} attMeta - 附件元数据对象或 URL 字符串（向后兼容）
   */
  _openWebviewOverlay(attMeta) {
    // 兼容字符串参数
    const meta = typeof attMeta === 'string' ? { url: attMeta } : (attMeta || {});
    const url = meta.url || '';
    const title = meta.title || 'WebView';

    // 关闭已有的 overlay
    this._closeWebviewOverlay();

    const convId = Bridge.getActiveConversationId();
    if (!convId) {
      Toast.show('请先选择一个会话', 'info');
      return;
    }

    const overlay = document.createElement('div');
    overlay.id = 'webview-overlay';
    overlay.className = 'webview-overlay';
    overlay.style.cssText = 'position:fixed;inset:0;z-index:9999;display:flex;flex-direction:column;background:var(--app-page-bg);';

    // Header
    const header = document.createElement('div');
    header.style.cssText = 'display:flex;align-items:center;justify-content:space-between;padding:0.75rem 1rem;border-bottom:1px solid var(--app-border);background:var(--app-surface);min-height:3rem;';
    header.innerHTML = `
      <div style="display:flex;align-items:center;gap:0.5rem;min-width:0;">
        <button type="button" id="webview-overlay-back" style="display:flex;align-items:center;justify-content:center;width:2rem;height:2rem;border:0;border-radius:0.5rem;background:transparent;color:var(--app-text);cursor:pointer;">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
        </button>
        <span style="font-size:0.9rem;font-weight:600;color:var(--app-text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${this._esc(title || 'WebView')}</span>
      </div>
      <div style="display:flex;gap:0.4rem;">
        <button type="button" id="webview-overlay-open-external" title="在外部打开" style="display:flex;align-items:center;justify-content:center;width:2rem;height:2rem;border:0;border-radius:0.5rem;background:transparent;color:var(--app-muted);cursor:pointer;">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M15 3h6v6"/><path d="M10 14L21 3"/><path d="M18 13v6a2 2 0 01-2 2H5a2 2 0 01-2-2V8a2 2 0 012-2h6"/></svg>
        </button>
        <button type="button" id="webview-overlay-device" title="投递到设备" style="display:flex;align-items:center;justify-content:center;width:2rem;height:2rem;border:0;border-radius:0.5rem;background:transparent;color:var(--app-muted);cursor:pointer;">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>
        </button>
      </div>
    `;
    overlay.appendChild(header);

    // 内容容器
    const contentContainer = document.createElement('div');
    contentContainer.style.cssText = 'flex:1;min-height:0;position:relative;';

    const isImage = this._isImageResourceUrl(url);

    // 对齐 Vue 版 TaskFloatingWindow.vue webviewUrl：
    // 为 iframe URL 注入 phone 参数，使 H5 医疗卡片等页面能识别当前用户并加载其数据
    const frameUrl = (() => {
      const phone = Bridge.getPhone();
      if (!phone) return url;
      try {
        const parsed = new URL(url);
        if (!parsed.searchParams.has('phone')) {
          parsed.searchParams.set('phone', phone);
        }
        return parsed.toString();
      } catch {
        const sep = url.includes('?') ? '&' : '?';
        return `${url}${sep}phone=${encodeURIComponent(phone)}`;
      }
    })();

    let iframe = null;

    if (isImage) {
      // 图片用 <img> 渲染，避免 iframe 跨域限制导致无法显示
      const imgWrap = document.createElement('div');
      imgWrap.style.cssText = 'width:100%;height:100%;display:flex;align-items:center;justify-content:center;overflow:auto;background:#f9fafb;';
      const imgEl = document.createElement('img');
      imgEl.src = url;
      imgEl.alt = title || '图片';
      imgEl.style.cssText = 'max-width:100%;max-height:100%;object-fit:contain;';
      imgEl.referrerPolicy = 'no-referrer';
      imgEl.onerror = () => {
        imgWrap.innerHTML = '<div style="color:var(--app-muted);font-size:0.86rem;">图片加载失败</div>';
      };
      imgWrap.appendChild(imgEl);
      contentContainer.appendChild(imgWrap);
    } else {
      // H5 页面用 iframe 渲染（先不设置 src，等 bridge 监听器注册后再加载，
      // 避免 H5 页面 interactive.ready 先于 message 监听到达导致丢失）
      iframe = document.createElement('iframe');
      iframe.style.cssText = 'width:100%;height:100%;border:0;';
      iframe.setAttribute('allow', 'microphone; camera; autoplay; fullscreen');
      iframe.setAttribute('sandbox', 'allow-scripts allow-same-origin allow-forms allow-popups allow-modals');
      contentContainer.appendChild(iframe);
    }
    overlay.appendChild(contentContainer);

    // Bridge status bar（仅 H5 页面需要）
    let statusBar = null;
    if (!isImage) {
      statusBar = document.createElement('div');
      statusBar.id = 'webview-bridge-status';
      statusBar.style.cssText = 'padding:0.4rem 1rem;font-size:0.72rem;color:var(--app-muted);border-top:1px solid var(--app-border);background:var(--app-surface);display:flex;align-items:center;gap:0.4rem;';
      statusBar.innerHTML = '<span style="display:inline-flex;width:6px;height:6px;border-radius:50%;background:var(--app-warning);"></span> Bridge 连接中...';
      overlay.appendChild(statusBar);
    }

    document.body.appendChild(overlay);

    // 初始化交互资源 Bridge（仅 H5 页面）
    if (!isImage && iframe) {
      // 对齐 Vue 版 useInteractiveResourceBridge onMounted：
      // 先注册 message 监听器，再设置 iframe.src 开始加载，
      // 避免 H5 页面 interactive.ready 先于监听注册到达导致丢失
      Bridge.openInteractiveResource({
        url,
        title,
        conversationId: convId,
        summary: meta.summary,
        taskNo: meta.taskNo,
        providerId: meta.providerId,
        skillId: meta.skillId,
        sourceSkillId: meta.sourceSkillId,
        objectKind: meta.objectKind,
        artifactType: meta.artifactType,
        mimeType: meta.mimeType,
        input: meta.input,
      }, () => iframe.contentWindow);

      // iframe 加载完成时通知 bridge
      iframe.onload = () => {
        Bridge.onInteractiveFrameLoaded(() => iframe.contentWindow);
      };

      // 监听器注册后再设置 src，开始加载 H5 页面（使用注入 phone 参数的 URL）
      iframe.src = frameUrl;
    }

    // 绑定按钮事件
    document.getElementById('webview-overlay-back').onclick = () => this._closeWebviewOverlay();
    document.getElementById('webview-overlay-open-external').onclick = () => {
      window.open(frameUrl, '_blank', 'noopener');
    };
    document.getElementById('webview-overlay-device').onclick = async () => {
      await this._dispatchContentToDevice(frameUrl, title);
    };

    // 监听 bridge 状态更新（仅 H5 页面）
    if (!isImage && statusBar) {
      this._webviewBridgeTimer = setInterval(() => {
        const state = Bridge.getInteractiveBridgeState();
        if (state.bridgeReady) {
          statusBar.innerHTML = '<span style="display:inline-flex;width:6px;height:6px;border-radius:50%;background:#10b981;"></span> Bridge 已连接';
        }
      }, 1000);
    }
  },

  _closeWebviewOverlay() {
    const overlay = document.getElementById('webview-overlay');
    if (overlay) overlay.remove();
    Bridge.closeInteractiveResource();
    if (this._webviewBridgeTimer) {
      clearInterval(this._webviewBridgeTimer);
      this._webviewBridgeTimer = null;
    }
  },

  /**
   * 投递内容到设备（对齐协议 §5.3 + §6）
   */
  async _dispatchContentToDevice(url, title) {
    const convId = Bridge.getActiveConversationId();
    if (!convId) {
      Toast.show('请先选择一个会话', 'info');
      return;
    }
    Toast.show('正在投递到设备...', 'info');
    const result = await Bridge.dispatchContentToDevice({
      url,
      title,
      conversationId: convId,
      attachment: { url, title, type: 'webview' },
    });
    if (result.ok) {
      Toast.show('已投递，等待设备响应', 'success');
    } else {
      Toast.show(`投递失败：${result.error || '未知错误'}`, 'error');
    }
  },

  /**
   * 播报到设备（对齐协议 §5.3 + §7）
   */
  async _dispatchSpeakToDevice(resourceUrl) {
    const convId = Bridge.getActiveConversationId();
    if (!convId) {
      Toast.show('请先选择一个会话', 'info');
      return;
    }
    Toast.show('正在发送播报指令...', 'info');
    const result = await Bridge.dispatchSpeakToDevice({
      text: '请查看大屏上的内容。',
      conversationId: convId,
    });
    if (result.ok) {
      Toast.show('播报指令已发送', 'success');
    } else {
      Toast.show(`播报失败：${result.error || '未知错误'}`, 'error');
    }
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
        Bridge.retrySend(msgId, convId);
      };
    });
  },


  // ── Post Creator Modal (对齐 Vue PostCreatorFullScreen 内容) ─


  // ── 话题弹窗事件绑定 ──
  _bindTopicDialogEvents() {
    // 创建话题弹窗
    const tcClose = document.getElementById('tc-close-btn');
    if (tcClose) tcClose.onclick = () => this._closeCreateTopic();
    const tcCancel = document.getElementById('tc-cancel-btn');
    if (tcCancel) tcCancel.onclick = () => this._closeCreateTopic();
    const tcOverlay = document.getElementById('topic-creator-overlay');
    if (tcOverlay) tcOverlay.onclick = (e) => { if (e.target === tcOverlay) this._closeCreateTopic(); };
    const tcConfirm = document.getElementById('tc-confirm-btn');
    if (tcConfirm) tcConfirm.onclick = () => this._submitCreateTopic();
    const tcTitle = document.getElementById('tc-title-input');
    if (tcTitle) {
      tcTitle.oninput = () => {
        this.topicCreatorTitle = tcTitle.value;
        const btn = document.getElementById('tc-confirm-btn');
        if (btn) btn.disabled = !tcTitle.value.trim() || this.topicCreatorSubmitting;
      };
      tcTitle.onkeydown = (e) => { if (e.key === 'Enter') { e.preventDefault(); this._submitCreateTopic(); } };
    }
    const tcSummary = document.getElementById('tc-summary-input');
    if (tcSummary) tcSummary.oninput = () => { this.topicCreatorSummary = tcSummary.value; };

    // 加入话题确认弹窗
    const jtClose = document.getElementById('jt-close-btn');
    if (jtClose) jtClose.onclick = () => this._closeJoinTopicConfirm();
    const jtCancel = document.getElementById('jt-cancel-btn');
    if (jtCancel) jtCancel.onclick = () => this._closeJoinTopicConfirm();
    const jtOverlay = document.getElementById('join-topic-overlay');
    if (jtOverlay) jtOverlay.onclick = (e) => { if (e.target === jtOverlay) this._closeJoinTopicConfirm(); };
    const jtConfirm = document.getElementById('jt-confirm-btn');
    if (jtConfirm) jtConfirm.onclick = () => this._confirmJoinTopic();
  },

  _openCreateTopic() {
    const conv = Bridge.getActiveConversation();
    if (!conv || conv.type !== 'group') return;
    this.topicCreatorOpen = true;
    this.topicCreatorTitle = '';
    this.topicCreatorSummary = '';
    this.topicCreatorSubmitting = false;
    this.render();
    setTimeout(() => {
      const input = document.getElementById('tc-title-input');
      if (input) input.focus();
    }, 50);
  },

  _closeCreateTopic() {
    this.topicCreatorOpen = false;
    this.topicCreatorSubmitting = false;
    this.render();
  },

  async _submitCreateTopic() {
    const conv = Bridge.getActiveConversation();
    if (!conv) return;
    const title = this.topicCreatorTitle.trim();
    if (!title) { Toast.warn('请输入话题标题'); return; }
    this.topicCreatorSubmitting = true;
    this.render();
    try {
      const r = await Bridge.createTopic(conv.id, title, this.topicCreatorSummary.trim());
      if (r && r.ok !== false) {
        Toast.success('话题已创建');
        this.topicCreatorOpen = false;
        this.topicCreatorSubmitting = false;
        this.render();
      } else {
        Toast.warn((r && r.error && r.error.message) || '创建失败');
        this.topicCreatorSubmitting = false;
        this.render();
      }
    } catch (e) {
      Toast.error(e.message || '创建失败');
      this.topicCreatorSubmitting = false;
      this.render();
    }
  },

  _openJoinTopicConfirm(topic) {
    this.joinTopicConfirmOpen = true;
    this.joinTopicTarget = topic;
    this.render();
  },

  _closeJoinTopicConfirm() {
    this.joinTopicConfirmOpen = false;
    this.joinTopicTarget = null;
    this.render();
  },

  async _confirmJoinTopic() {
    const conv = Bridge.getActiveConversation();
    const topic = this.joinTopicTarget;
    if (!conv || !topic) return;
    try {
      const r = await Bridge.joinTopic(conv.id, topic.id);
      if (r && r.ok !== false) {
        // 话题加入成功，跳转到"聊天室"对应的话题群聊
        const channelName = (r.data && r.data.channelName) || topic.channelName || `#topic-${topic.id}`;
        this.joinTopicConfirmOpen = false;
        this.joinTopicTarget = null;
        this._navigateToChatroom(channelName, topic);
      } else {
        Toast.warn((r && r.error && r.error.message) || '加入失败');
      }
    } catch (e) {
      Toast.error(e.message || '加入失败');
    }
  },

  // 跳转到聊天室，同时传递话题信息（标题/描述等）使聊天室话题列表与群聊面板一致
  _navigateToChatroom(channelName, topic) {
    try {
      sessionStorage.setItem('irc_pending_channel', channelName);
      if (topic) {
        sessionStorage.setItem('irc_pending_topic', JSON.stringify({
          id: topic.id,
          title: topic.title,
          content: topic.summary || '',
          creator: topic.creator || '匿名',
        }));
      }
      // 存储父群聊信息到频道映射表（每个频道对应各自的父群聊）
      const conv = Bridge.getActiveConversation();
      if (conv) {
        try {
          const parents = JSON.parse(sessionStorage.getItem('irc_channel_parents') || '{}');
          parents[channelName] = { convId: conv.id, convName: conv.name };
          sessionStorage.setItem('irc_channel_parents', JSON.stringify(parents));
        } catch (e2) {}
      }
    } catch (e) {}
    this.postsPanelOpen = false;
    this.render();
    window.location.hash = '#/chatroom';
  },

  async _openPostDetail(postId) {
    const conv = Bridge.getActiveConversation();
    if (!conv) return;

    let posts = Bridge.getPosts(conv.id);
    let post = posts.find(p => p.id === postId);
    // 对齐 Vue 版：本地未找到时从服务端加载帖子列表再查找
    if (!post) {
      try {
        await Bridge.loadGroupPosts(conv.id);
        posts = Bridge.getPosts(conv.id);
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
        await Bridge.submitHomeworkPostResponse(post.id, post.conversationId, {
          note: text,
          file,
        });
        Toast.success('响应已提交');
        this.postDetailResponseText = '';
        this.postDetailSelectedFile = null;
        await Bridge.loadGroupPosts(post.conversationId);
        this._closePostDetail();
        return;
      }
      await Bridge.respondToGroupPost(post.id, post.conversationId, {
        responseType: responsePayload.responseType,
        confirmation: responsePayload.confirmation,
        content: text,
      });
      Toast.success('响应已提交');
      this.postDetailResponseText = '';
      // Reload posts to get updated responses
      await Bridge.loadGroupPosts(post.conversationId);
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
      await Bridge.closeGroupPost(post.id, post.conversationId);
      Toast.success('任务已完成');
      await Bridge.loadGroupPosts(post.conversationId);
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

  ...DevicesPanelMixin,
  ...PostsPanelMixin,
};

export default ChatPage;
