/* ================================================================
   AgentChat 重构版 - 群聊看板面板（右侧抽屉）
   ----------------------------------------------------------------
   职责：群聊看板、任务发布/详情、群成员管理、话题管理的渲染与事件绑定。
   - 以右侧抽屉形式展示（posts-panel CSS 类）
   - 通过 mixin 合并到 ChatPage，this 指向 ChatPage
   - 业务逻辑通过 Bridge -> postMessage 交由 C++ 处理
   ================================================================ */

import Bridge from '../bridge/index.js';
import UiStore from '../stores/uiStore.js';
import Toast from '../utils/toast.js';
import TimeUtils from '../utils/time.js';
import { chatroomInviteMember } from '../transport/chatroomTransport.js';

const PostsPanelMixin = {
  // ── 第1段：面板状态捕获/恢复 ──
  _capturePostsPanelState() {
    const panelBody = this.container?.querySelector('#posts-panel .pp-body');
    const conversationId = Bridge.getActiveConversationId();
    return {
      open: this.postsPanelOpen,
      conversationId,
      view: this.postsPanelView || 'board',
      scrollTop: panelBody ? panelBody.scrollTop : 0,
    };
  },

  _restorePostsPanelState(state) {
    if (!state?.open) return;
    const conversationId = Bridge.getActiveConversationId();
    if (state.conversationId !== conversationId) return;
    if ((this.postsPanelView || 'board') !== state.view) return;
    const panelBody = this.container?.querySelector('#posts-panel .pp-body');
    if (!panelBody) return;
    requestAnimationFrame(() => {
      const maxTop = Math.max(0, panelBody.scrollHeight - panelBody.clientHeight);
      panelBody.scrollTop = Math.min(state.scrollTop, maxTop);
    });
  },

  // ── 第2段：工作空间任务辅助方法 ──
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
    const source = Bridge.getConversations().find(c => c.id === task.sourceConversationId);
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
    const msgs = Bridge.getMessages(conversationId) || [];
    for (let i = msgs.length - 1; i >= 0; i -= 1) {
      const content = String(msgs[i]?.text || msgs[i]?.content || '').trim();
      if (content) return content;
    }
    return '';
  },

  // ── 第3段：群成员辅助方法 ──
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

  // 判断当前用户是否是群主（拥有删除其他成员的权限）
  _isCurrentUserOwner(members) {
    const userId = Bridge.getUserId() || '';
    const phone = Bridge.getPhone() || '';
    if (!userId && !phone) return false;
    return (members || []).some((m) => {
      if (m.role !== 'owner') return false;
      if (m.memberKind === 'user') {
        return (userId && m.userId === userId) || (phone && (m.phone === phone || m.userId === phone));
      }
      if (m.memberKind === 'phone') {
        return phone && m.phone === phone;
      }
      return false;
    });
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
    const userId = Bridge.getUserId() || '';
    const phone = Bridge.getPhone() || '';
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

  // ── 第4段：Posts Panel 渲染 ──
  // ── Posts Panel ──
  _renderPostsPanel() {
    const conv = Bridge.getActiveConversation();
    const isWorkspace = conv && conv.scope === 'personal_workspace';
    const isGroup = conv && conv.type === 'group';
    const posts = isGroup ? Bridge.getPosts(conv.id) : [];
    const topics = isGroup ? Bridge.getTopics(conv.id) : [];
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
      const allTasks = Bridge.getPersonalTasksForCurrentUser();
      const workspaceTasks = allTasks.filter(task => this._isWorkspaceTask(task, conv.id));
      const outsideWorkspaceTaskCount = allTasks.filter(task => !this._isWorkspaceTask(task, conv.id)).length;
      const pendingTasks = workspaceTasks.filter(task => task.status !== 'done' && task.status !== 'canceled');
      const completedTasks = workspaceTasks.filter(task => task.status === 'done');
      const canceledTasks = workspaceTasks.filter(task => task.status === 'canceled');
      const targetGroups = Bridge.getConversations().filter(c => c.type === 'group' && c.scope !== 'personal_workspace');
      const draftActions = Bridge.getWorkspaceDraftActions(conv.id);
      const boundDevices = Bridge.getBoundDevices();
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
            <div class="pp-title">${this._esc(conv?.name ? `${conv.name}` : '群组看板')}</div>
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
                <button class="pp-topic-create-btn" id="pp-banner-create-btn" title="发布新任务">
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
                  <span>发布新任务</span>
                </button>
              </div>
              ${loading ? `<div class="pp-loading">正在加载任务...</div>` : error ? `<div class="pp-error">${this._esc(error)}</div>` : orderedPosts.length === 0 ? `
                <div class="pp-empty">当前群组还没有任务，点击上方"发布新任务"开始创建。</div>
              ` : orderedPosts.map(p => this._renderPostCard(p)).join('')}
            </div>

            <!-- Topics Section (群聊话题) -->
            <div class="pp-section">
              <div class="pp-section-header">
                <h2 class="pp-section-title">话题</h2>
                <button class="pp-topic-create-btn" id="pp-topic-create-btn" title="创建话题">
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
                  <span>创建话题</span>
                </button>
              </div>
              ${this.topicsLoading ? `<div class="pp-loading">正在加载话题...</div>` : this.topicsError ? `<div class="pp-error">${this._esc(this.topicsError)}</div>` : topics.length === 0 ? `
                <div class="pp-empty">当前群组还没有话题，点击上方"创建话题"开始发起。</div>
              ` : topics.map(t => this._renderTopicCard(t)).join('')}
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
    // 只有群主才能移除其他成员，且不能移除自己
    const isOwner = this._isCurrentUserOwner(this.groupMembers);
    const canRemove = isOwner && !isSelf && member.role !== 'owner';
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
      ${canRemove ? `
        <button class="pp-member-remove-btn" data-member-kind="${member.memberKind}" data-member-id="${this._esc(member.memberKind === 'user' ? member.userId : member.nodeId)}" data-member-name="${this._esc(this._memberTitle(member))}" title="移除">
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

  // ── 群聊话题卡片 ──
  _renderTopicCard(topic) {
    const joined = !!topic.joined;
    const channelName = topic.channelName || `#topic-${topic.id}`;
    return `
    <div class="pp-topic-card" data-topic-id="${this._esc(topic.id)}" data-topic-channel="${this._esc(channelName)}">
      <div class="pp-topic-card-icon">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15a2 2 0 01-2 2H7l-4 4V5a2 2 0 012-2h14a2 2 0 012 2z"/></svg>
      </div>
      <div class="pp-topic-card-body">
        <div class="pp-topic-card-title">${this._esc(topic.title)}</div>
        ${topic.summary ? `<div class="pp-topic-card-summary">${this._esc(topic.summary)}</div>` : ''}
        <div class="pp-topic-card-meta">
          <span>by ${this._esc(topic.creator || '匿名')}</span>
          <span>${topic.memberCount || 0} 人参与</span>
        </div>
      </div>
      <div class="pp-topic-card-action">
        ${joined
          ? `<button class="pp-topic-enter-btn" data-topic-id="${this._esc(topic.id)}" data-topic-channel="${this._esc(channelName)}">点击进入</button>
             <button class="pp-topic-leave-btn" data-topic-id="${this._esc(topic.id)}" title="退出话题">退出</button>`
          : `<button class="pp-topic-join-btn" data-topic-id="${this._esc(topic.id)}" data-topic-title="${this._esc(topic.title)}">申请加入</button>`
        }
      </div>
    </div>`;
  },

  // ── 第5段：帖子状态/图标/进度辅助方法 ──
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

  // ── 第6段：面板打开/关闭/刷新/切换 ──
  async _openPostsPanel() {
    const conv = Bridge.getActiveConversation();
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
      this.topicsLoading = false;
      this.topicsError = '';
      try {
        // Parallel load: posts + members（话题暂无接口，仅展示静态空态）
        const [postsResult, membersResult] = await Promise.allSettled([
          Bridge.loadGroupPosts(conv.id),
          Bridge.getRoomMembers(conv.id).catch(() => []),
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
        this.topicsLoading = false;
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

  // ── 第7段：面板事件绑定与成员管理 ──
  _bindPostsPanelEvents() {
    const conv = Bridge.getActiveConversation();
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
            const task = await Bridge.createPersonalTask({
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
            Bridge.setActiveConversation(conv.id);
            UiStore.setActiveConversation(conv.id);
            Bridge.sendUserMessage('请总结当前会话的主要内容', conv.id);
            return;
          }

          if (action === 'open-devices') {
            this._closePostsPanel();
            this._openDevicesPanel();
            return;
          }

          if (action === 'publish-draft-quick' || action === 'publish-draft') {
            const targetId = this.workspacePublishTargetId || Bridge.getConversations().find(item => item.type === 'group' && item.scope !== 'personal_workspace')?.id;
            const draftActions = Bridge.getWorkspaceDraftActions(conv.id);
            const publishSkillId = skillId || draftActions[0]?.skillId;
            if (!publishSkillId || !targetId) {
              Toast.warn(targetId ? '暂无可发布的草稿' : '请先选择目标群聊');
              return;
            }
            try {
              await Bridge.publishWorkspaceDraft(publishSkillId, targetId, conv.id);
              const targetName = Bridge.getConversations().find(item => item.id === targetId)?.name || '目标群聊';
              Toast.success(`已发布到 ${targetName}`);
            } catch (e) {
              Toast.warn(e.message || '发布失败，请稍后再试');
            }
            return;
          }

          if (!taskId) return;

          if (action === 'complete-task') {
            const ok = await Bridge.completePersonalTask(taskId);
            if (!ok) { Toast.warn('任务完成失败，请稍后再试'); return; }
            Toast.success('任务已完成');
            this._refreshPostsPanel();
            return;
          }

          if (action === 'cancel-task') {
            const ok = await Bridge.cancelPersonalTask(taskId);
            if (!ok) { Toast.warn('任务取消失败，请稍后再试'); return; }
            Toast.success('任务已取消');
            this._refreshPostsPanel();
            return;
          }

          if (action === 'reschedule-task') {
            const task = Bridge.getPersonalTasksForCurrentUser().find(item => item.id === taskId);
            const base = task?.dueAt && task.dueAt > Date.now() ? task.dueAt : Date.now();
            const ok = await Bridge.reschedulePersonalTask(taskId, base + 60 * 60 * 1000);
            if (!ok) { Toast.warn('任务延期失败，请稍后再试'); return; }
            Toast.success('任务已延后1小时');
            this._refreshPostsPanel();
            return;
          }

          if (action === 'open-task-source') {
            const task = Bridge.getPersonalTasksForCurrentUser().find(item => item.id === taskId);
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
            Bridge.setActiveConversation(conversationId);
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

      // Member preview section -> open members view
      const memberPreviewBtn = document.getElementById('pp-member-preview-btn');
      if (memberPreviewBtn) memberPreviewBtn.onclick = (e) => {
        // Don't trigger if clicking invite button
        if (e.target.closest('#pp-member-invite-btn')) return;
        this._switchPostsPanelToMembers();
      };

      // Invite button in member preview -> open members view with invite open
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

      // 创建话题按钮
      const topicCreateBtn = document.getElementById('pp-topic-create-btn');
      if (topicCreateBtn) topicCreateBtn.onclick = () => this._openCreateTopic();

      // 申请加入话题按钮
      this.container.querySelectorAll('.pp-topic-join-btn').forEach(btn => {
        btn.onclick = (e) => {
          e.stopPropagation();
          const topicId = btn.dataset.topicId;
          const topic = Bridge.getTopics(conv?.id).find(t => t.id === topicId);
          if (topic) this._openJoinTopicConfirm(topic);
        };
      });

      // 已加入话题 - 点击进入按钮 -> 跳转聊天室对应话题群聊
      this.container.querySelectorAll('.pp-topic-enter-btn').forEach(btn => {
        btn.onclick = (e) => {
          e.stopPropagation();
          const channelName = btn.dataset.topicChannel;
          if (!channelName) return;
          const topicId = btn.dataset.topicId;
          const topic = Bridge.getTopics(conv?.id).find(t => t.id === topicId);
          this._navigateToChatroom(channelName, topic);
        };
      });

      // 已加入话题 - 退出按钮
      this.container.querySelectorAll('.pp-topic-leave-btn').forEach(btn => {
        btn.onclick = async (e) => {
          e.stopPropagation();
          const topicId = btn.dataset.topicId;
          if (!topicId || !conv) return;
          try {
            const r = await Bridge.leaveTopic(conv.id, topicId);
            if (r && r.ok !== false) {
              Toast.success('已退出话题');
              this._refreshPostsPanel();
            } else {
              Toast.warn((r && r.error && r.error.message) || '退出失败');
            }
          } catch (err) {
            Toast.error(err.message || '退出失败');
          }
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
          const memberName = btn.dataset.memberName || '';
          if (memberId) this._doRemoveMemberFromPostPanel(memberId, memberKind, memberName);
        };
      });
    }
  },

  async _refreshPostPanelMembers() {
    const conv = Bridge.getActiveConversation();
    if (!conv) return;

    this.groupMembersLoading = true;
    this.render();
    try {
      const members = await Bridge.getRoomMembers(conv.id);
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
    const conv = Bridge.getActiveConversation();
    if (!conv) return;

    const phone = this.invitePhone.trim();
    if (!phone) {
      Toast.warn('请输入手机号');
      return;
    }

    try {
      // 调用 chatroom transport 层邀请 API（channel / member_kind / target / role）
      await chatroomInviteMember(conv.id, 'phone', phone, 'member');
      // 邀请成功后刷新成员列表
      const members = await Bridge.getRoomMembers(conv.id);
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

  async _doRemoveMemberFromPostPanel(memberId, memberKind = 'user', memberName = '') {
    const conv = Bridge.getActiveConversation();
    if (!conv) return;

    // 使用自定义弹窗替代原生 confirm（对齐"不显示该聊天"弹窗样式）
    const confirmed = await this._showRemoveMemberDialog(memberName);
    if (!confirmed) return;

    try {
      const members = await Bridge.removeRoomMember(conv.id, memberKind, memberId);
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

  // 移除成员确认弹窗（对齐"不显示该聊天"dc-modal 样式）
  _showRemoveMemberDialog(memberName) {
    return new Promise((resolve) => {
      let dialog = document.getElementById('remove-member-dialog');
      if (!dialog) {
        dialog = document.createElement('div');
        dialog.id = 'remove-member-dialog';
        dialog.className = 'modal-overlay';
        document.body.appendChild(dialog);
      }
      const nameHtml = memberName
        ? `「<span class="dc-name">${this._esc(memberName)}</span>」`
        : '该成员';
      dialog.innerHTML =
        '<div class="modal-card dc-modal">' +
          '<div class="dc-icon-wrap">' +
            '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M16 21v-2a4 4 0 00-4-4H6a4 4 0 00-4 4v2"/><circle cx="9" cy="7" r="4"/><line x1="17" y1="8" x2="23" y2="14"/><line x1="23" y1="8" x2="17" y2="14"/></svg>' +
          '</div>' +
          '<div class="dc-content">' +
            '<h3 class="dc-title">确定要移除' + nameHtml + '吗？</h3>' +
            '<p class="dc-hint">移除后，该成员将不再属于此群聊。</p>' +
          '</div>' +
          '<div class="modal-footer dc-footer">' +
            '<button id="rm-cancel-btn" class="btn btn-secondary">取消</button>' +
            '<button id="rm-confirm-btn" class="btn btn-danger">移除</button>' +
          '</div>' +
        '</div>';
      dialog.style.display = 'flex';
      const cancelBtn = document.getElementById('rm-cancel-btn');
      if (cancelBtn) cancelBtn.onclick = () => { dialog.style.display = 'none'; resolve(false); };
      const confirmBtn = document.getElementById('rm-confirm-btn');
      if (confirmBtn) confirmBtn.onclick = () => { dialog.style.display = 'none'; resolve(true); };
      dialog.onclick = (e) => {
        if (e.target === dialog) { dialog.style.display = 'none'; resolve(false); }
      };
    });
  },

  // ── 第8段：Post Creator 与 Task Detail 渲染 ──
  // ── Post Creator Modal (对齐 Vue PostCreatorFullScreen 内容) ──
  _renderPostCreator() {
    const conv = Bridge.getActiveConversation();
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

  // ── Task Detail Drawer (对齐 Vue TaskFullScreen 内容) ──
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

  // ── 第9段：Post Creator 事件 ──
  _openCreatePost() {
    const conv = Bridge.getActiveConversation();
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
    const conv = Bridge.getActiveConversation();
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
      await Bridge.createGroupPost(conv.id, {
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
      await Bridge.loadGroupPosts(conv.id);
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

  // ── 第10段：Post Detail 事件 ──
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
};

export default PostsPanelMixin;
