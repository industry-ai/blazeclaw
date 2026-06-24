/* ================================================================
   AgentChat HTML 版 - 任务中心页面 (匹配 PersonalTaskCenterPanel)
   ================================================================ */

import ChatStore from '../stores/chatStore.js';
import UiStore from '../stores/uiStore.js';
import Toast from '../utils/toast.js';
import TimeUtils from '../utils/time.js';

const TasksPage = {
  activeSegment: 'pending',
  container: null,

  init() {
    this.container = document.getElementById('page-tasks');
    this.activeSegment = 'pending';
    this.render();
    // 对齐 Vue 版：任务页打开时预取所有群聊的帖子
    this._loadGroupPosts();
  },

  async _loadGroupPosts() {
    try {
      await ChatStore.loadAllGroupPosts();
      this.render();
    } catch (e) {
      console.warn('[TasksPage] Failed to load group posts:', e.message);
    }
  },

  render() {
    if (!this.container) return;
    const tasks = ChatStore.getPersonalTasksForCurrentUser();
    const posts = ChatStore.getPosts;
    const conversations = ChatStore.getConversations();
    const sourceNames = {};
    conversations.forEach(c => { sourceNames[c.id] = c.name; });

    // 构建任务中心条目
    const personalItems = tasks.map(t => ({
      key: `personal:${t.id}`,
      kind: 'personal',
      title: t.title,
      summary: t.summary,
      status: t.status,
      dueAt: t.dueAt,
      sourceName: t.sourceConversationId ? (sourceNames[t.sourceConversationId] || '来源会话') : '工作空间',
      task: t,
      sortAt: t.dueAt || t.createdAt,
    }));

    const groupConversations = conversations.filter(c => c.type === 'group' && c.scope !== 'personal_workspace');
    const groupItems = [];
    groupConversations.forEach(c => {
      (posts(c.id)).filter(post => post.type === 'task').forEach(post => {
        groupItems.push({
          key: `group:${post.id}`,
          kind: 'group',
          title: post.title,
          summary: post.summary,
          status: post.status === 'closed' ? 'done' : 'pending',
          dueAt: post.deadlineAt,
          sourceName: c.name,
          post,
          sortAt: post.deadlineAt || post.createdAt,
        });
      });
    });

    const sortFn = (a, b) => b.sortAt - a.sortAt;
    const activePersonal = personalItems.filter(i => i.status !== 'done' && i.status !== 'canceled');
    const activeGroup = groupItems.filter(i => i.status === 'pending');
    const completed = [...personalItems.filter(i => i.status === 'done'), ...groupItems.filter(i => i.status === 'done')].sort(sortFn);
    const activeItems = [...activePersonal, ...activeGroup].sort(sortFn);

    let visibleItems;
    if (this.activeSegment === 'personal') visibleItems = activePersonal.sort(sortFn);
    else if (this.activeSegment === 'group') visibleItems = activeGroup.sort(sortFn);
    else if (this.activeSegment === 'done') visibleItems = completed;
    else visibleItems = activeItems;

    const counts = {
      all: activeItems.length,
      pending: activeItems.length,
      personal: activePersonal.length,
      group: activeGroup.length,
      done: completed.length,
    };

    const segments = [
      { key: 'pending', label: '待处理', count: counts.pending },
      { key: 'personal', label: '个人', count: counts.personal },
      { key: 'group', label: '群任务', count: counts.group },
      { key: 'done', label: '已完成', count: counts.done },
    ];

    this.container.innerHTML = `
    <div class="hub-page">
      <div class="hub-page-bg">
        <div class="hub-glow hub-glow-1"></div>
        <div class="hub-glow hub-glow-2"></div>
      </div>
      <div class="hub-page-inner">
        <div class="hub-header">
          <span class="hub-header-title">任务</span>
 
        </div>

        <div class="hub-body">
          <!-- Segment tabs -->
          <div class="hub-segments">
            ${segments.map(seg => `
              <button class="hub-seg-btn ${this.activeSegment === seg.key ? 'active' : ''}" data-seg="${seg.key}">
                <span>${seg.label}</span>
                ${seg.count > 0 ? `<span class="hub-seg-count">${seg.count}</span>` : ''}
              </button>
            `).join('')}
          </div>

          <div class="hub-content">
            <!-- 统计卡片 -->
            <div class="hub-stats">
              <div class="hub-stat-card">
                <div class="hub-stat-num">${activeItems.length}</div>
                <div class="hub-stat-label">待处理</div>
              </div>
              <div class="hub-stat-card">
                <div class="hub-stat-num">${activePersonal.length}</div>
                <div class="hub-stat-label">个人任务</div>
              </div>
              <div class="hub-stat-card">
                <div class="hub-stat-num">${activeGroup.length}</div>
                <div class="hub-stat-label">群任务</div>
              </div>
            </div>

            ${this._renderItems(visibleItems)}
          </div>
        </div>
      </div>
    </div>`;
    this._bindEvents();
  },

  _renderItems(items) {
    if (!items.length) {
      const titles = {
        personal: '暂无个人任务', group: '暂无群任务',
        done: '暂无已完成任务', pending: '暂无待处理任务',
      };
      const descs = {
        personal: '个人提醒和 AI 空间任务会出现在这里，不混入群任务。',
        group: '各个群里的任务会聚合到这里，当前群看板仍展示该群全部任务。',
        done: '完成后的个人提醒和已关闭群任务会显示在这里。',
        pending: '待处理的个人提醒和群任务会按时间聚合到这里。',
      };
      return `
      <div class="hub-empty">
        <div class="hub-empty-icon"><svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><path d="M8 12l3 3 5-5"/></svg></div>
        <div class="hub-empty-title">${titles[this.activeSegment] || titles.pending}</div>
        <div class="hub-empty-desc">${descs[this.activeSegment] || descs.pending}</div>
      </div>`;
    }

    return `<div class="hub-item-list">${items.map(item => this._renderItem(item)).join('')}</div>`;
  },

  _renderItem(item) {
    const kindLabel = item.kind === 'group' ? '群任务' : (item.sourceName.includes('AI') || item.sourceName.includes('工作空间') ? 'AI 创建' : '个人提醒');
    const kindBg = item.kind === 'group' ? 'hub-kind-group' : 'hub-kind-personal';
    const iconBg = item.status === 'done'
      ? 'hub-icon-done'
      : item.kind === 'group' ? 'hub-icon-group' : 'hub-icon-personal';
    const iconSvg = item.status === 'done'
      ? '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg>'
      : item.kind === 'group'
        ? '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6M16 13H8M16 17H8M10 9H8"/></svg>'
        : '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 8A6 6 0 006 8c0 7-3 9-3 9h18s-3-2-3-9M13.73 21a2 2 0 01-3.46 0"/></svg>';

    const timeLabel = () => {
      if (item.kind === 'group') return item.dueAt ? `截止：${TimeUtils.formatBeijingMdHm(item.dueAt)}` : '长期有效';
      return item.dueAt ? `提醒：${TimeUtils.formatBeijingMdHm(item.dueAt)}` : '无提醒时间';
    };

    const isPersonal = item.kind === 'personal';
    const isActive = item.status !== 'done' && item.status !== 'canceled';

    // 对齐 Vue 版 PersonalTaskCenterPanel：查看来源独立于 isActive，个人任务始终显示
    let actionsHtml = '';
    if (isPersonal) {
      actionsHtml = `
        <button class="hub-task-btn hub-task-btn-back" data-action="source" data-key="${item.key}">
          查看来源<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg>
        </button>`;
    }
    if (isPersonal && isActive) {
      actionsHtml += `
        <button class="hub-task-btn hub-task-btn-icon" data-action="postpone" data-key="${item.key}" title="延期 1 小时">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M1 4v6h6"/><path d="M3.51 15a9 9 0 102.13-9.36L1 10"/></svg>
        </button>
        <button class="hub-task-btn hub-task-btn-icon hub-task-btn-danger" data-action="cancel" data-key="${item.key}" title="取消">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M15 9l-6 6M9 9l6 6"/></svg>
        </button>
        <button class="hub-task-btn hub-task-btn-primary" data-action="complete" data-key="${item.key}">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg>完成
        </button>`;
    } else if (item.kind === 'group') {
      actionsHtml = `
        <button class="hub-task-btn hub-task-btn-primary" data-action="openGroup" data-key="${item.key}">
          ${item.status === 'pending' ? '继续' : '查看'}<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 18l6-6-6-6"/></svg>
        </button>`;
    }

    return `
    <div class="hub-item-card">
      <div class="hub-item-head">
        <div class="${iconBg}">${iconSvg}</div>
        <div class="hub-item-meta">
          <span class="hub-kind-badge ${kindBg}">${kindLabel}</span>
          <span class="hub-item-source">${this._esc(item.sourceName)}</span>
        </div>
      </div>
      <div class="hub-item-title">${this._esc(item.title)}</div>
      ${item.summary ? `<div class="hub-item-summary">${this._esc(item.summary)}</div>` : ''}
      <div class="hub-item-tags">
        <span class="hub-tag">${timeLabel()}</span>
        ${item.status === 'done' ? '<span class="hub-tag hub-tag-done">已完成</span>' : ''}
      </div>
      ${actionsHtml ? `<div class="hub-item-actions">${actionsHtml}</div>` : ''}
    </div>`;
  },

  _bindEvents() {
    // Segment 切换
    this.container.querySelectorAll('.hub-seg-btn').forEach(btn => {
      btn.onclick = () => {
        this.activeSegment = btn.dataset.seg;
        this.render();
      };
    });

    // Header 按钮
    this.container.querySelectorAll('.hub-header-btn').forEach(btn => {
      btn.onclick = () => Toast.featureUnavailable();
    });

    // 任务操作
    this.container.querySelectorAll('[data-action]').forEach(btn => {
      btn.onclick = () => {
        const key = btn.dataset.key;
        const action = btn.dataset.action;

        if (action === 'complete') this._completeTask(key);
        else if (action === 'cancel') this._cancelTask(key);
        else if (action === 'postpone') this._postponeTask(key);
        else if (action === 'source') this._openSource(key);
        else if (action === 'openGroup') this._openGroup(key);
      };
    });
  },

  _findPersonalItem(key) {
    const tasks = ChatStore.getPersonalTasksForCurrentUser();
    const t = tasks.find(t => `personal:${t.id}` === key);
    if (t) return { kind: 'personal', task: t };
    const conversations = ChatStore.getConversations();
    for (const c of conversations) {
      const post = ChatStore.getPosts(c.id).find(p => `group:${p.id}` === key);
      if (post) return { kind: 'group', post, conversationId: c.id };
    }
    return null;
  },

  _completeTask(key) {
    const item = this._findPersonalItem(key);
    if (item?.kind === 'personal') {
      if (ChatStore.completePersonalTask(item.task.id)) Toast.success('任务已完成');
    }
    this.render();
  },

  _cancelTask(key) {
    const item = this._findPersonalItem(key);
    if (item?.kind === 'personal') {
      if (ChatStore.cancelPersonalTask(item.task.id)) Toast.success('任务已取消');
    }
    this.render();
  },

  _postponeTask(key) {
    const item = this._findPersonalItem(key);
    if (item?.kind === 'personal') {
      const base = item.task.dueAt && item.task.dueAt > Date.now() ? item.task.dueAt : Date.now();
      if (ChatStore.reschedulePersonalTask(item.task.id, base + 3600000)) Toast.success('已延期 1 小时');
    }
    this.render();
  },

  _openSource(key) {
    const item = this._findPersonalItem(key);
    if (item?.kind === 'personal') {
      const t = item.task;
      const convId = (t.deliveryTarget.type === 'conversation' || t.deliveryTarget.type === 'both')
        ? t.deliveryTarget.conversationId
        : t.sourceConversationId;
      if (!convId) { Toast.warn('这个任务只在工作空间内，没有绑定群聊'); return; }
      // 对齐 Vue 版 openPersonalSource：切换会话 + 重置聊天视图
      ChatStore.setActiveConversation(convId);
      UiStore.setActiveConversation(convId, true);
      UiStore.resetToChatView();
      window.location.hash = '#/chat';
    }
  },

  _openGroup(key) {
    const item = this._findPersonalItem(key);
    if (item?.kind === 'group') {
      // 对齐 Vue 版 openGroupItem：切换群聊 + 打开任务详情
      ChatStore.setActiveConversation(item.conversationId);
      UiStore.setActiveConversation(item.conversationId, true);
      UiStore.resetToChatView();
      UiStore.openTask({
        type: 'native_post',
        postId: item.post.id,
        title: item.post.title || '任务详情',
        summary: item.post.summary,
      });
      window.location.hash = '#/chat';
    }
  },

  _esc(s) { return String(s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); },
  destroy() {},
};

export default TasksPage;
