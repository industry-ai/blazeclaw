/* ================================================================
   AgentChat HTML 版 - 工作台页面 (匹配 PersonalWorkspaceAiPanel)
   ================================================================ */

import ChatStore from '../stores/chatStore.js';
import UiStore from '../stores/uiStore.js';
import Toast from '../utils/toast.js';
import TimeUtils from '../utils/time.js';

const AiPage = {
  activeSegment: 'overview',
  container: null,
  unsub: null,

  init() {
    this.container = document.getElementById('page-ai');
    this.activeSegment = 'overview';
    this.unsub = ChatStore.subscribe(() => this.render());
    void ChatStore.refreshBoundDevices();
    this.render();
  },

  render() {
    if (!this.container) return;
    const seg = this.activeSegment;

    this.container.innerHTML = `
    <div class="hub-page">
      <div class="hub-page-bg">
        <div class="hub-glow hub-glow-1"></div>
        <div class="hub-glow hub-glow-2"></div>
      </div>
      <div class="hub-page-inner">
        <div class="hub-header">
          <span class="hub-header-title">工作台</span>
        </div>

        <div class="hub-body">
          <div class="hub-segments">
            <button class="hub-seg-btn ${seg === 'overview' ? 'active' : ''}" data-seg="overview"><span>工作台</span></button>
            <button class="hub-seg-btn ${seg === 'skills' ? 'active' : ''}" data-seg="skills"><span>能力</span></button>
            <button class="hub-seg-btn ${seg === 'history' ? 'active' : ''}" data-seg="history"><span>执行</span></button>
          </div>
          <div class="hub-content">
            ${seg === 'overview' ? this._renderOverview() : ''}
            ${seg === 'skills' ? this._renderSkills() : ''}
            ${seg === 'history' ? this._renderHistory() : ''}
          </div>
        </div>
      </div>
    </div>`;
    this._bindEvents();
  },

  _renderOverview() {
    const tasks = ChatStore.getPersonalTasksForCurrentUser();
    const conversations = ChatStore.getConversations();
    const execHistory = ChatStore.getExecutionHistory();
    const workspace = conversations.find(c => c.scope === 'personal_workspace');
    const draftActions = ChatStore.getWorkspaceDraftActions(workspace?.id);
    const devices = ChatStore.getBoundDevices();
    const onlineCount = devices.filter(d => d.status === 'online').length;

    let pendingCount = 0;
    tasks.forEach(t => { if (t.status !== 'done' && t.status !== 'canceled') pendingCount++; });
    conversations.filter(c => c.type === 'group').forEach(c => {
      ChatStore.getPosts(c.id).forEach(p => { if (p.status !== 'closed') pendingCount++; });
    });

    return `
    <!-- 今天需要处理 -->
    <div class="hub-card">
      <div class="hub-card-head-row">
        <div class="hub-card-icon"><svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 11l3 3L22 4"/><path d="M21 12v7a2 2 0 01-2 2H5a2 2 0 01-2-2V5a2 2 0 012-2h11"/></svg></div>
        <div class="hub-card-text">
          <div class="hub-card-label">今天需要处理</div>
          <div class="hub-card-desc">聊天里产生事情，工作台调度 AI 和设备。</div>
        </div>
        <span class="hub-card-dot"></span>
      </div>
      <div class="hub-card-3col">
        <button class="hub-mini-card" data-action="goTasks">
          <div class="hub-mini-num">${pendingCount}</div>
          <div class="hub-mini-label">待处理</div>
        </button>
        <button class="hub-mini-card" data-action="switchTo" data-seg="history">
          <div class="hub-mini-num">${execHistory.length}</div>
          <div class="hub-mini-label">执行记录</div>
        </button>
        <div class="hub-mini-card">
          <div class="hub-mini-num">${onlineCount}</div>
          <div class="hub-mini-label">设备在线</div>
        </div>
      </div>
    </div>

    <!-- AI 能力 -->
    ${this._renderAbilitiesSummary()}


    <!-- 设备摘要 -->
    ${this._renderDeviceSummary()}

    <!-- 最近执行 -->
    `;
  },

  _renderAbilitiesSummary() {
    // 对齐 Vue 版 PersonalWorkspaceAiPanel：仅显示 intent === 'query' 的工作台技能
    const skills = ChatStore.getAgentSkills().filter(s => s.intent === 'query');
    const enabledCount = skills.filter(s => ChatStore.isSkillEnabled(s.id)).length;

    return `
    <div class="hub-card">
      <div class="hub-card-head-row">
        <div class="hub-card-icon"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg></div>
        <div>
          <div class="hub-card-label">AI 能力</div>
          <div class="hub-card-desc">${enabledCount} / ${skills.length} 已启用</div>
        </div>
        <button class="hub-mini-link" data-action="switchTo" data-seg="skills">管理能力</button>
      </div>
      <div class="hub-card-2col" style="margin-top:1rem;">
        ${skills.map(s => `
          <div class="hub-skill-mini">
            <div class="hub-skill-mini-title">${this._esc(s.title)}</div>
            <div class="hub-skill-mini-desc">${this._esc(s.description)}</div>
          </div>
        `).join('')}
      </div>
    </div>`;
  },

  _renderDeviceSummary() {
    const devices = ChatStore.getBoundDevices();
    const onlineCount = devices.filter(d => d.status === 'online').length;
    const tvCount = devices.filter(d => d.deviceType === 'tv' && d.status === 'online').length;
    const recent = devices.slice(0, 3);
    return `
    <div class="hub-card">
      <div class="hub-card-head-row">
        <div class="hub-card-icon"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg></div>
        <div>
          <div class="hub-card-label">设备摘要</div>
          <div class="hub-card-desc">${tvCount} 台电视在线 · ${onlineCount} 台设备在线</div>
        </div>
        <button class="hub-mini-link" data-action="goDevices">管理设备</button>
      </div>
      ${recent.length === 0
        ? `<div class="hub-device-empty">暂无绑定设备，可先在设备页生成绑定码并确认绑定。</div>`
        : `<div class="hub-exec-list">
            ${recent.map(device => `
              <div class="hub-exec-item">
                <span class="hub-mini-badge ${device.status === 'online' ? 'hub-kind-green' : 'hub-kind-slate'}">${device.deviceType === 'tv' ? '电视' : '设备'}</span>
                <span class="hub-exec-title">${this._esc(device.deviceName)} · ${this._esc(device.boundConversationName || '当前会话')}</span>
              </div>
            `).join('')}
          </div>`
      }
    </div>`;
  },

  _renderPendingDrafts(draftActions) {
    const groups = ChatStore.getConversations().filter(c => c.type === 'group' && c.scope !== 'personal_workspace');
    return `
    <div class="hub-card">
      <div class="hub-card-head-row">
        <div class="hub-card-icon"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6"/></svg></div>
        <div>
          <div class="hub-card-label">待确认</div>
          <div class="hub-card-desc">AI 生成但还没有发布的草稿</div>
        </div>
      </div>
      ${draftActions.length === 0
        ? '<div class="hub-device-empty">暂无待确认草稿。让 AI 生成任务、公告或活动后，会先出现在这里。</div>'
        : `<div class="hub-exec-list">
            ${draftActions.map(action => `
              <div style="border:1px solid var(--app-border);border-radius:1rem;background:var(--app-subtle-bg);padding:0.9rem;">
                <div style="font-size:0.9rem;font-weight:600;color:var(--app-text);">${this._esc(action.draft.title || '待确认草稿')}</div>
                <div style="margin-top:0.35rem;font-size:0.78rem;line-height:1.6;color:var(--app-muted);">${this._esc(action.draft.summary || '确认内容后，可继续发布到指定群。')}</div>
                <div style="margin-top:0.55rem;display:flex;flex-wrap:wrap;gap:0.4rem;">
                  <span class="hub-tag" style="border:1px solid var(--app-border);">${this._esc(action.skillId)}</span>
                  <span class="hub-tag" style="border:1px solid var(--app-border);">未发布</span>
                </div>
                ${groups.length > 0 ? `
                  <button class="hub-op-btn hub-op-btn-full" style="margin-top:0.8rem;" data-action="publishDraft" data-skill-id="${this._esc(action.skillId)}">
                    发布到 ${this._esc(groups[0].name)}
                  </button>
                ` : '<div class="hub-device-empty" style="margin-top:0.8rem;padding:0.75rem;">暂无可发布的群聊</div>'}
              </div>
            `).join('')}
          </div>`
      }
    </div>`;
  },

  _renderRecentExecutions(execHistory) {
    const recent = execHistory.slice(0, 3);
    const intentLabels = {
      create_task: '创建任务', publish_news: '发布公告', create_event: '活动发起', query: '意图澄清', unknown: '通用查询',
    };
    const intentColors = {
      create_task: 'hub-kind-group', publish_news: 'hub-kind-amber', create_event: 'hub-kind-green', query: 'hub-kind-slate', unknown: 'hub-kind-slate',
    };

    return `
    <div class="hub-card">
      <div class="hub-card-head-row">
        <div class="hub-card-icon"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 12h-4l-3 9L9 3l-3 9H2"/></svg></div>
        <div>
          <div class="hub-card-label">最近执行</div>
          <div class="hub-card-desc">共 ${execHistory.length} 条记录</div>
        </div>
        ${execHistory.length > 3 ? '<button class="hub-mini-link" data-action="switchTo" data-seg="history">查看全部</button>' : ''}
      </div>
      ${recent.length === 0
        ? '<div class="hub-device-empty">暂无执行记录</div>'
        : `<div class="hub-exec-list">
            ${recent.map(r => `
              <div class="hub-exec-item">
                <span class="hub-mini-badge ${intentColors[r.intent] || 'hub-kind-slate'}">${intentLabels[r.intent] || r.intent}</span>
                <span class="hub-exec-title">${this._esc(r.title)}</span>
              </div>
            `).join('')}
          </div>`
      }
    </div>`;
  },

  _renderSkills() {
    // 对齐 Vue 版 PersonalWorkspaceAiPanel：仅显示 intent === 'query' 的工作台技能
    const skills = ChatStore.getAgentSkills().filter(s => s.intent === 'query');
    const enabledCount = skills.filter(s => ChatStore.isSkillEnabled(s.id)).length;
    const disabledCount = skills.length - enabledCount;

    return `
    <div class="hub-card">
      <div class="hub-card-head-row">
        <div class="hub-card-icon"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg></div>
        <div>
          <div class="hub-card-label">AI 能力</div>
          <div class="hub-card-desc">${enabledCount} / ${skills.length} 已启用</div>
        </div>
        <button class="hub-mini-link" data-action="toggleAll">
          ${disabledCount === 0 ? '全部停用' : '全部启用'}
        </button>
      </div>

      ${skills.length === 0
        ? '<div class="hub-device-empty">暂无可用技能</div>'
        : `<div class="hub-skill-list">
            ${skills.map(s => {
              const enabled = ChatStore.isSkillEnabled(s.id);
              return `
                <div class="hub-skill-row ${enabled ? '' : 'hub-skill-disabled'}">
                  <div class="hub-skill-row-info">
                    <span class="hub-skill-row-title">${this._esc(s.title)}</span>
                    <P class="hub-skill-row-desc">${this._esc(s.description)}</P>
                    <P class="hub-skill-row-perm">${this._esc(s.permission)}</P>
                  </div>
                  <button class="hub-skill-toggle ${enabled ? 'on' : 'off'}" data-action="toggleSkill" data-skill-id="${s.id}">
                    ${enabled
                      ? '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="6" width="20" height="12" rx="6"/><circle cx="16" cy="12" r="4" fill="currentColor"/></svg>'
                      : '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="6" width="20" height="12" rx="6"/><circle cx="8" cy="12" r="4" fill="currentColor"/></svg>'}
                  </button>
                </div>`;
            }).join('')}
          </div>`
      }
    </div>`;
  },

  _renderHistory() {
    const execHistory = ChatStore.getExecutionHistory();
    const intentLabels = {
      create_task: '创建任务', publish_news: '发布公告', create_event: '活动发起', query: '意图澄清', unknown: '通用查询',
    };
    const intentColors = {
      create_task: 'hub-icon-group', publish_news: 'hub-icon-amber', create_event: 'hub-icon-green', query: 'hub-icon-slate', unknown: 'hub-icon-slate',
    };

    if (execHistory.length === 0) {
      return `
      <div class="hub-empty">
        <div class="hub-empty-icon"><svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M22 12h-4l-3 9L9 3l-3 9H2"/></svg></div>
        <div class="hub-empty-title">暂无执行记录</div>
        <div class="hub-empty-desc">在聊天中向 AI 发送指令后，<br>执行记录会出现在这里。</div>
      </div>`;
    }

    return `<div class="hub-item-list">
      ${execHistory.map(r => {
        const statusLabel = r.status === 'success' ? '已完成' : r.status === 'failed' ? '失败' : '待确认';
        const statusColor = r.status === 'failed' ? 'color:#f87171;' : '';
        const ic = intentColors[r.intent] || 'hub-icon-slate';
        const iconForIntent = r.intent === 'create_task'
          ? '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg>'
          : r.intent === 'publish_news'
            ? '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6"/></svg>'
            : '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>';

        return `
        <div class="hub-item-card">
          <div class="hub-item-head">
            <div class="hub-item-icon ${ic}">${iconForIntent}</div>
            <div class="hub-item-meta">
              <span class="hub-kind-badge ${ic}">${intentLabels[r.intent] || r.intent}</span>
              <span style="font-size:0.72rem;${statusColor}">${statusLabel}</span>
            </div>
          </div>
          <div class="hub-item-title" style="font-size:0.88rem;">${this._esc(r.title)}</div>
          ${r.skillIds.length ? `<div class="hub-item-tags">${r.skillIds.map(sid => `<span class="hub-tag" style="border:1px solid var(--app-border);">${this._esc(sid)}</span>`).join('')}</div>` : ''}
        </div>`;
      }).join('')}
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

    // Header
    this.container.querySelectorAll('.hub-header-btn').forEach(btn => {
      btn.onclick = () => Toast.featureUnavailable();
    });

    // Switch to tab
    this.container.querySelectorAll('[data-action="switchTo"]').forEach(el => {
      el.onclick = () => {
        this.activeSegment = el.dataset.seg;
        this.render();
      };
    });

    // 快捷操作
    this.container.querySelectorAll('[data-action="goTasks"]').forEach(el => {
      el.onclick = () => { window.location.hash = '#/tasks'; };
    });
    this.container.querySelectorAll('[data-action="goDevices"]').forEach(el => {
      el.onclick = () => { window.location.hash = '#/devices'; };
    });
    this.container.querySelectorAll('[data-action="createTask"]').forEach(el => {
      el.onclick = async () => {
        const ws = ChatStore.getConversations().find(c => c.scope === 'personal_workspace');
        const task = await ChatStore.createPersonalTask({
          title: '新任务', summary: '在工作台快捷创建',
          sourceConversationId: ws?.id,
          deliveryTarget: ws ? { type: 'conversation', conversationId: ws.id } : { type: 'workspace' },
        });
        if (task) {
          Toast.success('任务已创建，可在任务中心查看');
          window.location.hash = '#/tasks';
        } else {
          Toast.warn('任务创建失败，请稍后再试');
        }
      };
    });
    this.container.querySelectorAll('[data-action="summarize"]').forEach(el => {
      el.onclick = () => {
        const ws = ChatStore.getConversations().find(c => c.scope === 'personal_workspace');
        if (!ws) { Toast.featureUnavailable(); return; }
        ChatStore.setActiveConversation(ws.id);
        UiStore.setActiveConversation(ws.id);
        window.location.hash = '#/chat';
        ChatStore.sendUserMessage('请总结当前会话的主要内容', ws.id);
      };
    });
    this.container.querySelectorAll('[data-action="announcement"]').forEach(el => {
      el.onclick = () => {
        const group = ChatStore.getConversations().find(c => c.type === 'group' && c.scope !== 'personal_workspace');
        if (!group) { Toast.featureUnavailable(); return; }
        ChatStore.setActiveConversation(group.id);
        UiStore.setActiveConversation(group.id);
        window.location.hash = '#/chat';
        ChatStore.sendUserMessage('帮我生成一份群公告草稿', group.id);
      };
    });
    this.container.querySelectorAll('[data-action="tv"], [data-action="scanBind"]').forEach(el => {
      el.onclick = () => { window.location.hash = '#/devices'; };
    });
    this.container.querySelectorAll('[data-action="publishDraft"]').forEach(el => {
      el.onclick = async () => {
        const skillId = el.dataset.skillId;
        const source = ChatStore.getConversations().find(c => c.scope === 'personal_workspace');
        const target = ChatStore.getConversations().find(c => c.type === 'group' && c.scope !== 'personal_workspace');
        if (!skillId || !source || !target) {
          Toast.warn('当前没有可发布的目标群聊');
          return;
        }
        try {
          await ChatStore.publishWorkspaceDraft(skillId, target.id, source.id);
          Toast.success(`已发布到 ${target.name}`);
        } catch (e) {
          Toast.warn(e.message || '发布失败，请稍后再试');
        }
      };
    });

    // 技能管理
    this.container.querySelectorAll('[data-action="toggleAll"]').forEach(el => {
      el.onclick = () => {
        // 对齐 Vue 版 onToggleAll：仅切换 intent === 'query' 的工作台技能
        const skills = ChatStore.getAgentSkills().filter(s => s.intent === 'query');
        const anyDisabled = skills.some(s => !ChatStore.isSkillEnabled(s.id));
        if (anyDisabled) {
          skills.forEach(s => { if (!ChatStore.isSkillEnabled(s.id)) ChatStore.toggleSkill(s.id); });
        } else {
          skills.forEach(s => { if (ChatStore.isSkillEnabled(s.id)) ChatStore.toggleSkill(s.id); });
        }
        this.render();
      };
    });
    this.container.querySelectorAll('[data-action="toggleSkill"]').forEach(el => {
      el.onclick = () => {
        ChatStore.toggleSkill(el.dataset.skillId);
        this.render();
      };
    });
  },

  _esc(s) { return String(s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); },
  destroy() {
    if (this.unsub) {
      this.unsub();
      this.unsub = null;
    }
  },
};

export default AiPage;
