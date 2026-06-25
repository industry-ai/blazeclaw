/* ================================================================
   AgentChat HTML 版 - 通知中心页面 (匹配 NotificationCenterPanel)
   ================================================================ */

import ChatStore from '../stores/chatStore.js';
import UiStore from '../stores/uiStore.js';
import Toast from '../utils/toast.js';
import TimeUtils from '../utils/time.js';

const NotificationsPage = {
  container: null,

  init() {
    this.container = document.getElementById('page-notifications');
    // 进入通知页后自动清零角标
    ChatStore.setLastSeenNotificationsAt();
    this.render();
  },

  render() {
    if (!this.container) return;

    const now = Date.now();
    const lastSeen = ChatStore.getLastSeenNotificationsAt();
    const tasks = ChatStore.getPersonalTasksForCurrentUser();
    const conversations = ChatStore.getConversations();
    ChatStore.setLastSeenNotificationsAt(now);

    const sourceNames = {};
    conversations.forEach(c => { sourceNames[c.id] = c.name; });

    const items = [];

    // 群聊邀请通知（对齐 Vue 版 groupInvitationItems）
    ChatStore.getGroupInvitationNotifications().forEach(item => {
      const inviter = item.inviterPhone || item.inviter || '成员';
      items.push({
        key: item.id,
        kind: 'group_invited',
        title: `已加入 ${item.roomName || sourceNames[item.conversationId] || '群聊'}`,
        summary: `您被 ${inviter} 邀请加入群聊`,
        sourceName: item.roomName || sourceNames[item.conversationId] || '群聊',
        conversationId: item.conversationId,
        createdAt: item.createdAt,
      });
    });

    // 个人提醒
    tasks.filter(t => {
      if (t.status === 'done' || t.status === 'canceled') return false;
      if (!t.dueAt) return false;
      return t.dueAt <= now;
    }).forEach(t => {
      items.push({
        key: `personal:${t.id}`,
        kind: 'personal_reminder',
        title: t.title,
        summary: t.summary,
        sourceName: t.sourceConversationId ? (sourceNames[t.sourceConversationId] || '来源群聊') : '工作空间',
        conversationId: t.deliveryTarget.type === 'conversation' || t.deliveryTarget.type === 'both'
          ? t.deliveryTarget.conversationId
          : t.sourceConversationId || '',
        taskId: t.id,
        createdAt: t.createdAt,
        dueAt: t.dueAt,
      });
    });

    // 群任务截止
    conversations.filter(c => c.type === 'group').forEach(c => {
      ChatStore.getPosts(c.id)
        .filter(p => p.deadlineAt && p.deadlineAt <= now && p.deadlineAt > now - 86400000 && p.status === 'published')
        .forEach(p => {
          items.push({
            key: `deadline:${p.id}`,
            kind: 'group_task_deadline',
            title: p.title,
            summary: p.summary,
            sourceName: c.name,
            conversationId: c.id,
            postId: p.id,
            createdAt: p.createdAt,
            dueAt: p.deadlineAt,
          });
        });
    });

    items.sort((a, b) => b.createdAt - a.createdAt);

    this.container.innerHTML = `
    <div class="hub-page">
      <div class="hub-page-bg">
        <div class="hub-glow hub-glow-1"></div>
        <div class="hub-glow hub-glow-2"></div>
      </div>
      <div class="hub-page-inner">
        <div class="hub-header">
          <span class="hub-header-title">通知</span>
   
        </div>
        <div class="hub-body">
          <div class="hub-content">
            ${this._renderItems(items)}
          </div>
        </div>
      </div>
    </div>`;
    this._bindEvents();
  },

  _renderItems(items) {
    if (!items.length) {
      return `
      <div class="hub-empty">
        <div class="hub-empty-icon">
          <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M18 8A6 6 0 006 8c0 7-3 9-3 9h18s-3-2-3-9M13.73 21a2 2 0 01-3.46 0"/></svg>
        </div>
        <div class="hub-empty-title">暂无新通知</div>
        <div class="hub-empty-desc">新的个人提醒和群任务截止会出现在这里。<br>进入通知页后角标自动清零。</div>
      </div>`;
    }

    return `
    <div class="hub-notif-hint">个人提醒和群任务截止将按时间倒序出现在这里。管理任务请前往任务中心。</div>
    <div class="hub-item-list">
      ${items.map(item => this._renderItem(item)).join('')}
    </div>`;
  },

  _renderItem(item) {
    const kindColor = {
      personal_reminder: 'hub-kind-personal',
      group_task_deadline: 'hub-kind-amber',
      group_invited: 'hub-kind-green',
      device_status: 'hub-kind-green',
      system: 'hub-kind-slate',
    };
    const kindLabel = {
      personal_reminder: '个人提醒',
      group_task_deadline: '群任务截止',
      group_invited: '群聊邀请',
      device_status: '设备状态',
      system: '系统消息',
    };
    const iconSvg = {
      personal_reminder: '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 8A6 6 0 006 8c0 7-3 9-3 9h18s-3-2-3-9M13.73 21a2 2 0 01-3.46 0"/></svg>',
      group_task_deadline: '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/></svg>',
      group_invited: '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17 21v-2a4 4 0 00-4-4H5a4 4 0 00-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 00-3-3.87"/><path d="M16 3.13a4 4 0 010 7.75"/></svg>',
      device_status: '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>',
      system: '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15a2 2 0 01-2 2H7l-4 4V5a2 2 0 012-2h14a2 2 0 012 2z"/></svg>',
    };
    const kc = kindColor[item.kind] || 'hub-kind-personal';
    const parts = [`创建于 ${TimeUtils.formatBeijingMdHm(item.createdAt)}`];
    if (item.dueAt) parts.push(` · 提醒：${TimeUtils.formatBeijingMdHm(item.dueAt)}`);

    let actions = '';
    if (item.kind === 'personal_reminder') {
      actions = `
        <div class="hub-item-actions">
          <button class="hub-task-btn hub-task-btn-primary" data-action="complete" data-key="${item.key}">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg>完成
          </button>
          <button class="hub-task-btn hub-task-btn-secondary" data-action="goTasks" data-key="${item.key}">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 11l3 3L22 4"/><path d="M21 12v7a2 2 0 01-2 2H5a2 2 0 01-2-2V5a2 2 0 012-2h11"/></svg>去任务
          </button>
        </div>`;
    }

    return `
    <div class="hub-item-card hub-item-clickable" data-key="${item.key}" data-action="navigate">
      <div class="hub-item-head">
        <div class="hub-item-icon ${kc}">${iconSvg[item.kind] || iconSvg.personal_reminder}</div>
        <div class="hub-item-meta">
          <span class="hub-kind-badge ${kc}">${kindLabel[item.kind] || '通知'}</span>
          <span class="hub-item-source">${this._esc(item.sourceName)}</span>
        </div>
      </div>
      <div class="hub-item-title">${this._esc(item.title)}</div>
      ${item.summary ? `<div class="hub-item-summary">${this._esc(item.summary)}</div>` : ''}
      <div class="hub-item-tags">
        <span class="hub-tag">${parts.join('')}</span>
      </div>
      ${actions}
    </div>`;
  },

  _bindEvents() {
    this.container.querySelectorAll('.hub-header-btn').forEach(btn => {
      btn.onclick = () => Toast.featureUnavailable();
    });

    this.container.querySelectorAll('[data-action="navigate"]').forEach(el => {
      el.onclick = () => {
        const key = el.dataset.key;
        let found;

        // 群聊邀请通知
        if (key && key.startsWith('group-invited:')) {
          const invitation = ChatStore.getGroupInvitationNotifications().find(i => i.id === key);
          if (invitation) found = { conversationId: invitation.conversationId };
        }

        // 个人提醒
        if (!found) {
          const tasks = ChatStore.getPersonalTasksForCurrentUser();
          for (const t of tasks) {
            if (`personal:${t.id}` === key) { found = t; break; }
          }
        }
        // 群任务截止
        if (!found) {
          for (const c of ChatStore.getConversations()) {
            const post = ChatStore.getPosts(c.id).find(p => `deadline:${p.id}` === key);
            if (post) { found = { conversationId: c.id, postId: post.id }; break; }
          }
        }
        if (found) {
          const convId = found.conversationId || found.sourceConversationId;
          if (convId) {
            ChatStore.setActiveConversation(convId);
            UiStore.setActiveConversation(convId);
            window.location.hash = '#/chat';
          } else {
            Toast.warn('这条通知没有绑定群聊');
          }
        }
      };
    });

    this.container.querySelectorAll('[data-action="complete"]').forEach(el => {
      el.onclick = (e) => {
        e.stopPropagation();
        const key = el.dataset.key;
        const taskId = key.replace('personal:', '');
        if (ChatStore.completePersonalTask(taskId)) Toast.success('任务已完成');
        this.render();
      };
    });

    this.container.querySelectorAll('[data-action="goTasks"]').forEach(el => {
      el.onclick = (e) => {
        e.stopPropagation();
        window.location.hash = '#/tasks';
      };
    });
  },

  _esc(s) { return String(s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); },
  destroy() {},
};

export default NotificationsPage;
