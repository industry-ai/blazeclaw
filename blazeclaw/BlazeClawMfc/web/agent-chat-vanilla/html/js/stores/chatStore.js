/* ================================================================
   AgentChat HTML 版 - 聊天状态管理
   对应原版 sessionStore.ts，接入真实 Transport 层
   新增：Outbox 管理、echo stitching、重试机制
   ================================================================ */

import TimeUtils from '../utils/time.js';
import { createDefaultTransport } from '../transport/transportFactory.js';
import ChatApi from '../api/chatApi.js';
import AuthStore from './authStore.js';

const ChatStore = (() => {
  const listeners = new Set();
  const AGENT_SKILLS_STORAGE_KEY = 'agent_skills_disabled';
  const DEVICE_PENDING_SESSIONS_STORAGE_KEY = 'devices.persist.pendingSessions';
  const DEFAULT_DEVICE_NAME = 'Living Room TV';
  const AGENT_SKILL_CONFIGS = [
    {
      id: 'publish_homework_skill',
      title: '作业发布',
      description: '根据日期、科目和资源生成群任务草稿。',
      permission: '需要群任务发布权限',
      intent: 'create_task',
      keywords: ['作业', '练习', '习题', '试卷', '测验', '听写', '默写', '朗读', '背诵', 'homework'],
    },
    {
      id: 'publish_news_skill',
      title: '公告发布',
      description: '整理通知和资讯内容，生成群公告草稿。',
      permission: '需要群公告发布权限',
      intent: 'publish_news',
      keywords: ['公告', '通知', '资讯', 'news', '放假', '安排', '家长会', '提醒大家', '所有家长', '所有同学'],
    },
    {
      id: 'publish_event_skill',
      title: '活动发起',
      description: '生成活动、签到或回执类草稿。',
      permission: '需要群活动发起权限',
      intent: 'create_event',
      keywords: ['活动', '签到', '接龙', '报名', '统计', '回执', '聚会', '祭扫', '清明', '运动会', '出游'],
    },
    {
      id: 'knowledge_query_skill',
      title: '知识问答',
      description: '基于当前对话和资料回答问题。',
      permission: '需要读取当前对话上下文',
      intent: 'query',
      keywords: ['知识', '问答', '是什么', '为什么', '怎么做', '解释', '含义', '定义'],
    },
    {
      id: 'file_summary_skill',
      title: '文件总结',
      description: '提取文件重点，整理摘要和后续行动。',
      permission: '需要读取上传文件',
      intent: 'query',
      keywords: ['总结', '摘要', '概括', '归纳', '梳理', '整理文件', '总结文件', '文件内容'],
    },
    {
      id: 'device_delivery_skill',
      title: '设备投递',
      description: '把内容投递到电视、笔记本等设备。',
      permission: '需要访问已绑定设备',
      intent: 'query',
      keywords: ['投递', '投屏', '投到', '发送到电视', '展示到电视', '在电视上'],
    },
  ];
  const AGENT_PUBLISH_SKILLS = [
    {
      id: 'hw_1125',
      owner: 'publish_homework_skill',
      intent: 'create_task',
      intentKeywords: ['11.25', '十一月二十五', '跟读', '英语作业', '今天作业', '今日作业', '今天英语作业', '今日英语作业', 'today homework', 'english homework today'],
      draft: {
        template: 'homework',
        title: '英语跟读作业 (11.25)',
        summary: '请完成今日的课文跟读与选择题练习，系统将自动评分。',
        resourceUrl: 'https://works.blazegraph.site/works/8/homework-11.25/index.html',
      },
    },
    {
      id: 'hw_oral_homework',
      owner: 'publish_homework_skill',
      intent: 'create_task',
      intentKeywords: ['口语作业', '交口语作业', '口语练习'],
      draft: {
        template: 'homework',
        title: '口语作业提交提醒',
        summary: '请大家在规定时间前完成并提交口语作业。',
      },
    },
    {
      id: 'news_holiday',
      owner: 'publish_news_skill',
      intent: 'publish_news',
      intentKeywords: ['资讯', 'news', '假期安排', '放假通知'],
      draft: {
        template: 'news',
        title: '五一假期群资讯',
        summary: '请查看假期值班、出行提醒与返校时间安排。',
        resourceUrl: 'https://example.com/news/may-holiday',
      },
    },
    {
      id: 'act_qingming',
      owner: 'publish_event_skill',
      intent: 'create_event',
      intentKeywords: ['祭扫', '清明', '家族活动'],
      draft: {
        template: 'event',
        title: '2026 家族清明祭祖签到',
        summary: '今年清明节计划于 4 月 4 日上午组织回乡祭祖并统一订餐，请确认是否参加。',
      },
    },
    {
      id: 'act_sports_meeting',
      owner: 'publish_event_skill',
      intent: 'create_event',
      intentKeywords: ['运动会', '参加运动会'],
      draft: {
        template: 'event',
        title: '运动会参加确认',
        summary: '请大家明天早上九点准时参加运动会，并提前做好准备。',
      },
    },
  ];

  // ─ 状态 ──
  let _conversations = [];
  let _activeConversationId = '';
  let _messagesByConv = {};
  let _postsByConv = {};
  let _roomMembersByConv = {};
  let _memberIdentityMapByConversationId = {};
  let _personalTasks = [];
  let _agentSessions = {};
  let _boundDevices = [];
  let _pendingDeviceSessions = [];
  let _lastSeenNotificationsAt = 0;
  // 对齐 Vue 版 sessionStore.groupInvitationNotifications：群聊邀请通知列表
  let _groupInvitationNotifications = [];
  let _agentSkills = [];
  let _skillStates = {};
  let _executionHistory = [];
  let _status = 'disconnected';
  let _serverPersonalWorkspaceId = '';
  // 对齐 Vue 版 sessionStore.locallyDeletedConversationIds：本地已删除会话 ID 集合
  // 标记后该会话不会被新消息自动恢复（除非收到带可展示内容的消息）
  let _locallyDeletedConversationIds = {};

  // ── Transport 相关 ──
  let _transport = null;
  let _transportUnsub = null;
  let _agentStreamingMsgs = {}; // conversationId -> { messageId, content }

  // ── Outbox 管理 ──
  let _outbox = []; // { id, conversationId, text, createdAt, retryCount, status }
  const MAX_RETRIES = 3;
  const RETRY_DELAY_MS = 2000;
  // 发送超时：超过此时间仍未收到 echo/ack，判定为发送失败（对齐微信"发送中→失败"体验）
  const SEND_TIMEOUT_MS = 15000;
  // 发送超时计时器：outboxId -> timer
  let _sendTimers = {};

  // ── Echo stitching 映射 ──
  let _pendingEchoes = {}; // clientMsgId -> { conversationId, text, createdAt }

  function notify() { for (const fn of listeners) fn(); }

  function _isExpectedOfflineError(e) {
    const msg = e?.message || '';
    return msg.includes('timeout') || msg.includes('超时') || msg.includes('abort')
      || msg.includes('fetch') || msg.includes('NetworkError')
      || e?.name === 'RequestTimeoutError' || e?.name === 'AbortError';
  }

  // ── 工具函数 ──
  function _mkId(prefix) {
    return `${prefix}_${Math.random().toString(16).slice(2)}_${Date.now().toString(16)}`;
  }

  function _getUserId() {
    return AuthStore.getUserId() || AuthStore.getPhone() || 'local-user';
  }

  function _getUserName() {
    return AuthStore.getPhone() || '我';
  }

  // ── Outbox 管理 ──
  function _addToOutbox(conversationId, text) {
    const entry = {
      id: _mkId('outbox'),
      conversationId,
      text,
      createdAt: Date.now(),
      retryCount: 0,
      status: 'pending',
    };
    _outbox.push(entry);
    return entry;
  }

  function _removeFromOutbox(entryId) {
    _outbox = _outbox.filter(e => e.id !== entryId);
    _clearSendTimer(entryId);
  }

  // ── 发送超时计时 ──
  // 发送时启动，收到 echo/ack 后清除；超时则标记 delivery='failed'
  function _startSendTimer(outboxId, conversationId) {
    _clearSendTimer(outboxId);
    _sendTimers[outboxId] = setTimeout(() => {
      delete _sendTimers[outboxId];
      const entry = _outbox.find(e => e.id === outboxId);
      // 仍在 outbox 且未完成 → 判定超时失败
      if (entry && entry.status !== 'failed') {
        entry.status = 'failed';
        _updateMessageDeliveryStatus(conversationId, outboxId, 'failed');
      }
    }, SEND_TIMEOUT_MS);
  }

  function _clearSendTimer(outboxId) {
    if (_sendTimers[outboxId]) {
      clearTimeout(_sendTimers[outboxId]);
      delete _sendTimers[outboxId];
    }
  }

  function _retryOutboxEntry(entryId) {
    const entry = _outbox.find(e => e.id === entryId);
    if (!entry) return;

    entry.retryCount++;
    entry.status = 'retrying';

    if (entry.retryCount > MAX_RETRIES) {
      entry.status = 'failed';
      console.warn('[ChatStore] Outbox entry failed after max retries:', entryId);
      _updateMessageDeliveryStatus(entry.conversationId, entry.id, 'failed');
      return;
    }

    // 延迟重试
    setTimeout(() => {
      if (_transport && _transport.sendMessage) {
        entry.status = 'sending';
        _transport.sendMessage({
          conversationId: entry.conversationId,
          text: entry.text,
        });
      }
    }, RETRY_DELAY_MS * entry.retryCount);
  }

  function _updateMessageDeliveryStatus(convId, outboxId, status) {
    const msgs = _messagesByConv[convId];
    if (!msgs) return;

    for (const msg of msgs) {
      if (msg.outboxId === outboxId) {
        msg.delivery = status;
        break;
      }
    }
    notify();
  }

  // ── Transport 事件处理 ──
  // 对齐 Vue 版 sessionStore._startRoomMembershipSync：每次 transport connected 都同步群聊列表
  let _syncInProgress = false;

  function _loadServerData() {
    if (_syncInProgress) return;
    _syncInProgress = true;
    Promise.allSettled([
      _resolveServerPersonalWorkspaceId(),
      _syncGroupConversationsFromServer(),
      loadPersonalTasksFromServer(),
      refreshBoundDevices(),
    ]).finally(() => {
      _syncInProgress = false;
      if (_activeConversationId && _transport && _transport.joinConversation) {
        _transport.joinConversation(_activeConversationId);
      }
      notify();
    });
  }

  function _handleTransportEvent(event) {
    switch (event.type) {
      case 'transport_status':
        _status = event.status;
        // 对齐 Vue 版：transport 连接成功后才加载服务端数据
        if (event.status === 'connected') _loadServerData();
        notify();
        break;

      case 'message_echo':
        _handleMessageEcho(event);
        break;

      case 'message_received':
        _handleMessageReceived(event);
        break;

      case 'send_ack':
        _handleSendAck(event);
        break;

      case 'agent_typing':
        _handleAgentTyping(event);
        break;

      case 'conversation_signal':
        _handleConversationSignal(event);
        break;

      case 'group_invited':
        _handleGroupInvited(event);
        break;

      case 'transport_error':
        _handleTransportError(event);
        break;
    }
  }

  function _handleMessageEcho(event) {
    const { conversationId, messageId, text } = event;
    const convId = _ensureConversationId(conversationId);
    if (!convId) return;
    const normalizedText = String(text || '').trim();

    // 对齐 Vue 版 chatEventProcessor：会话被本地删除时，只有带可展示内容才恢复
    if (isConversationLocallyDeleted(convId)) {
      if (!normalizedText) return;
      _restoreLocallyDeletedConversation(convId);
    }

    if (!normalizedText) return;

    // 确保会话存在
    _ensureConversation(convId);

    // 查找 pending echo 进行匹配
    const echoKey = `${convId}:${normalizedText}`;
    const pendingEcho = _pendingEchoes[echoKey];

    let outboxId = null;
    if (pendingEcho) {
      outboxId = pendingEcho.outboxId;
      delete _pendingEchoes[echoKey];
      _removeFromOutbox(outboxId);
    }

    if (!_messagesByConv[convId]) _messagesByConv[convId] = [];

    // 对齐 Vue 版 echo stitching：更新已有乐观消息，避免重复并保留附件
    const existing = outboxId
      ? _messagesByConv[convId].find(m => m.outboxId === outboxId)
      : _messagesByConv[convId].find(m =>
          m.author === 'user' && m.content === normalizedText && m.delivery === 'sending',
        );

    if (existing) {
      existing.id = messageId || existing.id;
      existing.delivery = 'sent';
      existing.status = 'complete';
      // 对齐 Vue 版 chatEventProcessor：echo 也提取帖子信封（跨设备同步）
      const echoParsed = _parseSharedPostMessageContent(normalizedText, convId);
      if (echoParsed.post) _upsertSharedPost(echoParsed.post, convId);
      notify();
      return;
    }

    // 对齐 Vue 版 chatEventProcessor：echo 新增前也按 messageId 去重，避免重连重复
    const echoExistingIdx = _findExistingMessageIndex(convId, messageId);
    if (echoExistingIdx !== -1) {
      const echoExisting = _messagesByConv[convId][echoExistingIdx];
      echoExisting.delivery = 'sent';
      echoExisting.status = 'complete';
      const echoParsedDup = _parseSharedPostMessageContent(normalizedText, convId);
      if (echoParsedDup.post) _upsertSharedPost(echoParsedDup.post, convId);
      notify();
      return;
    }

    // 没有匹配到乐观消息时才新增（例如其他设备发送的消息回显）
    const msg = {
      id: messageId || _mkId('u'),
      conversationId: convId,
      author: 'user',
      authorName: _getUserName(),
      content: normalizedText,
      attachments: pendingEcho?.attachments || [],
      status: 'complete',
      delivery: 'sent',
      outboxId,
      createdAt: pendingEcho?.createdAt || Date.now(),
      kind: 'default',
    };
    _messagesByConv[convId].push(msg);
    // 对齐 Vue 版 chatEventProcessor：跨设备 echo 新增消息时也提取帖子信封
    const echoParsed2 = _parseSharedPostMessageContent(normalizedText, convId);
    if (echoParsed2.post) _upsertSharedPost(echoParsed2.post, convId);
    notify();
  }

  // 对齐 Vue 版 findExistingMessageIndex：按 messageId 查找已有消息，避免重连时插入重复
  function _findExistingMessageIndex(convId, messageId) {
    if (!messageId) return -1;
    const list = _messagesByConv[convId];
    if (!list) return -1;
    return list.findIndex(m => m.id === messageId);
  }

  function _handleMessageReceived(event) {
    const { conversationId, messageId, author, authorName, text, createdAt, attachments } = event;
    const convId = _ensureConversationId(conversationId);
    if (!convId) return;
    let normalizedText = String(text || '').trim();
    let msgAttachments = Array.isArray(attachments) ? attachments : [];

    // 对齐 Vue 版 chatEventProcessor：解析 agent relay envelope，提取 deliveryId + 真实消息
    let agentDeliveryId = null;
    if (author === 'agent' && normalizedText.startsWith(_GROUP_AGENT_REPLY_RELAY_PREFIX)) {
      const relay = _decodeGroupAgentReplyRelayEnvelope(normalizedText);
      if (relay) {
        agentDeliveryId = relay.deliveryId;
        normalizedText = relay.message.trim();
        if (relay.attachments && relay.attachments.length) {
          msgAttachments = relay.attachments;
        }
      }
    }

    // 对齐 Vue 版 chatEventProcessor：会话被本地删除时，只有带可展示内容才恢复
    const hasDisplayPayload = Boolean(normalizedText);
    if (isConversationLocallyDeleted(convId)) {
      if (!hasDisplayPayload) return;
      _restoreLocallyDeletedConversation(convId);
    }

    if (!normalizedText) return;

    // 确保会话存在
    _ensureConversation(convId);

    // 对齐 Vue 版 chatEventProcessor：用 agentDeliveryId 去重，
    // 发送者本地已展示带相同 deliveryId 的 agent 消息时，丢弃服务端广播的重复消息
    if (agentDeliveryId) {
      const list = _messagesByConv[convId];
      if (list && list.some(m => m.agentDeliveryId === agentDeliveryId)) {
        console.log('[ChatStore] duplicate agent delivery ignored', { convId, agentDeliveryId, textLen: normalizedText.length });
        return;
      }
    }

    // 对齐 Vue 版 chatEventProcessor：按 messageId 去重，避免重连产生重复消息
    const existingIdx = _findExistingMessageIndex(convId, messageId);
    if (existingIdx !== -1) {
      const existingMsg = _messagesByConv[convId][existingIdx];
      // agent 流式消息：更新内容
      if (author === 'agent') {
        existingMsg.content = text;
        existingMsg.status = event.status === 'complete' ? 'complete' : 'streaming';
        if (event.status === 'complete') {
          delete _agentStreamingMsgs[convId];
        }
      }
      // 对齐 Vue 版：已有消息也提取帖子信封
      const parsedExisting = _parseSharedPostMessageContent(normalizedText, convId);
      if (parsedExisting.post) _upsertSharedPost(parsedExisting.post, convId);
      notify();
      return;
    }

    // 如果是 agent 流式消息，更新现有消息或创建新消息
    if (author === 'agent') {
      const existingMsg = _findAgentStreamingMessage(convId, messageId);
      if (existingMsg) {
        // 更新流式消息内容
        existingMsg.content = text;
        existingMsg.status = event.status === 'complete' ? 'complete' : 'streaming';
        // 对齐附件透传：服务端广播的 agent 消息可能携带 H5 卡片附件
        if (msgAttachments.length) {
          existingMsg.attachments = msgAttachments;
        }
        if (event.status === 'complete') {
          delete _agentStreamingMsgs[convId];
        }
      } else {
        // 创建新的 agent 消息
        const msg = {
          id: messageId || _mkId('a'),
          conversationId: convId,
          author: 'agent',
          authorName: authorName || '炎图AI助手',
          content: normalizedText,
          attachments: msgAttachments,
          status: event.status === 'complete' ? 'complete' : 'streaming',
          createdAt: createdAt || Date.now(),
          kind: 'default',
        };
        // 对齐 Vue 版：携带 agentDeliveryId 供后续去重
        if (agentDeliveryId) msg.agentDeliveryId = agentDeliveryId;

        if (!_messagesByConv[convId]) _messagesByConv[convId] = [];
        _messagesByConv[convId].push(msg);

        if (msg.status === 'streaming') {
          _agentStreamingMsgs[convId] = { messageId: msg.id, content: text };
        }
      }
    } else {
      // 其他用户的消息
      const msg = {
        id: messageId || _mkId('m'),
        conversationId: convId,
        author: author || 'other_user',
        authorName: authorName || '未知用户',
        content: normalizedText,
        attachments: msgAttachments,
        status: 'complete',
        createdAt: createdAt || Date.now(),
        kind: 'default',
      };

      if (!_messagesByConv[convId]) _messagesByConv[convId] = [];
      _messagesByConv[convId].push(msg);
    }

    // 对齐 Vue 版 chatEventProcessor：接收消息时提取帖子分享信封并写入本地
    const parsed = _parseSharedPostMessageContent(normalizedText, convId);
    if (parsed.post) _upsertSharedPost(parsed.post, convId);

    // 对齐 Vue 版 _ensureConversationReadBaseline + 未读计数：
    // 非 user/system 的新消息，若不是当前活跃会话，则增加未读计数
    if (author !== 'user' && author !== 'system') {
      const conv = _conversations.find(c => c.id === convId);
      if (conv && convId !== _activeConversationId) {
        conv.unread = (conv.unread || 0) + 1;
      }
    }

    notify();
  }

  function _handleSendAck(event) {
    const { conversationId, status } = event;
    const convId = _ensureConversationId(conversationId);
    if (!convId) return;

    // 更新消息发送状态
    const msgs = _messagesByConv[convId];
    if (msgs) {
      for (let i = msgs.length - 1; i >= 0; i--) {
        const msg = msgs[i];
        if (msg.author === 'user' && msg.delivery === 'sending') {
          msg.delivery = status === 'sent' ? 'sent' : 'failed';
          // 清除发送超时计时器并移出 outbox，防止超时后覆盖为 failed
          if (msg.outboxId) {
            _clearSendTimer(msg.outboxId);
            _removeFromOutbox(msg.outboxId);
          }
          break;
        }
      }
    }
    notify();
  }

  function _handleAgentTyping(event) {
    const { conversationId, isTyping } = event;
    const convId = _ensureConversationId(conversationId);
    if (!convId) return;

    // 对齐 Vue 版 chatEventProcessor：会话被本地删除时忽略
    if (isConversationLocallyDeleted(convId)) return;

    // 可以在 UI 中显示 typing 指示器
    console.log('[ChatStore] Agent typing:', convId, isTyping);
  }

  function _handleConversationSignal(event) {
    const { conversationId, reason } = event;
    const convId = _ensureConversationId(conversationId);
    if (!convId) return;

    console.log('[ChatStore] Conversation signal:', convId, reason);

    // 对齐 Vue 版 chatEventProcessor：会话被本地删除时忽略 signal
    if (isConversationLocallyDeleted(convId)) return;

    // 确保会话存在于本地（参考 Vue 版 chatEventProcessor）
    _ensureConversation(convId);

    // 对齐 Vue 版 handleGroupInvited：生成群聊邀请通知
    // 跳过个人工作空间（非群聊邀请）
    if (convId && !convId.includes('workspace') && !convId.includes('personal')) {
      const conv = _conversations.find(c => c.id === convId);
      const createdAt = Date.now();
      const roomName = conv?.name || convId.replace(/^#/, '');
      const notification = {
        id: `group-invited:${convId}:${createdAt}`,
        conversationId: convId,
        roomName,
        inviter: event.inviter,
        inviterPhone: event.inviterPhone,
        createdAt,
      };
      // 去重：同一会话只保留最新一条
      _groupInvitationNotifications = [
        notification,
        ..._groupInvitationNotifications.filter(item => item.conversationId !== convId),
      ].slice(0, 50);
      _saveGroupInvitationNotifications();
      notify();
    }

    // 加入频道
    if (_transport && _transport.joinConversation) {
      _transport.joinConversation(convId);
    }
  }

  // 对齐 Vue 版 sessionStore.handleGroupInvited：
  // 收到 GROUP_INVITED 事件时创建邀请通知、自动加入频道、获取房间信息
  function _handleGroupInvited(event) {
    const { conversationId, roomName, inviter, inviterPhone, createdAt } = event;
    const convId = _ensureConversationId(conversationId);
    if (!convId) return;

    // 跳过个人工作空间（非群聊邀请）
    if (convId.includes('workspace') || convId.includes('personal')) return;

    // 对齐 Vue 版 chatEventProcessor：会话被本地删除时忽略 group_invited
    if (isConversationLocallyDeleted(convId)) return;

    console.log('[ChatStore] Group invited:', convId, roomName, inviterPhone);

    // 确保会话存在
    _ensureConversation(convId);

    // 如果服务端提供了房间名，更新会话名称
    const conv = _conversations.find(c => c.id === convId);
    if (conv && roomName && roomName !== convId && roomName !== convId.replace(/^#/, '') && conv.name !== roomName) {
      conv.name = roomName;
    }

    // 创建群聊邀请通知（对齐 Vue 版 groupInvitationNotifications）
    const ts = createdAt || Date.now();
    const notification = {
      id: `group-invited:${convId}:${ts}`,
      conversationId: convId,
      roomName: roomName || conv?.name || convId.replace(/^#/, ''),
      inviter,
      inviterPhone,
      createdAt: ts,
    };
    // 去重：同一会话只保留最新一条
    _groupInvitationNotifications = [
      notification,
      ..._groupInvitationNotifications.filter(item => item.conversationId !== convId),
    ].slice(0, 50);
    _saveGroupInvitationNotifications();

    // 自动加入频道（对齐 Vue 版：status === 'connected' 时 joinConversation）
    if (_status === 'connected' && _transport && _transport.joinConversation) {
      _transport.joinConversation(convId);
    }

    // 获取房间信息（成员列表、房间名等）
    void _fetchRoomInfo(convId);

    notify();
  }

  function _handleTransportError(event) {
    const { conversationId, message } = event;
    console.error('[ChatStore] Transport error:', message);

    // 查找对应的 Outbox 条目并重试
    const convId = _ensureConversationId(conversationId);
    if (convId) {
      const pendingEntry = _outbox.find(e => e.conversationId === convId && e.status === 'sending');
      if (pendingEntry) {
        _retryOutboxEntry(pendingEntry.id);
      }
    }
  }

  function _findAgentStreamingMessage(convId, messageId) {
    const msgs = _messagesByConv[convId];
    if (!msgs) return null;

    // 先按 messageId 查找
    if (messageId) {
      const found = msgs.find(m => m.id === messageId && m.author === 'agent' && m.status === 'streaming');
      if (found) return found;
    }

    // 再按 conversationId 查找最新的流式消息
    const streaming = _agentStreamingMsgs[convId];
    if (streaming) {
      return msgs.find(m => m.id === streaming.messageId && m.status === 'streaming');
    }

    return null;
  }

  function _ensureConversationId(raw) {
    if (!raw) return '';
    // 兼容可能传入对象的情况（如 transport 事件）
    if (typeof raw === 'object') {
      return String(raw.id || raw.conversationId || raw.conversation_id || raw.channel || raw.room_id || '').trim();
    }
    return String(raw).trim();
  }

  function _isPersonalWorkspaceConversationId(raw) {
    const id = _ensureConversationId(raw).toLowerCase();
    return id.includes('workspace') || id.includes('personal');
  }

  function _normalizeConversationName(rawName, id) {
    const name = String(rawName || '').trim();
    const fallbackName = id.replace(/^#/, '');
    const isRawId = !name || name === id || name === fallbackName;
    return isRawId ? fallbackName : name;
  }

  function _parseDeliveryTarget(raw) {
    if (!raw) return { type: 'workspace' };
    if (typeof raw === 'string') {
      try {
        return JSON.parse(raw);
      } catch {
        return { type: 'workspace' };
      }
    }
    return raw;
  }

  function _readStoredSessionId() {
    try {
      return localStorage.getItem('auth.session_id')?.trim() || '';
    } catch {
      return '';
    }
  }

  function _loadDisabledSkillIds() {
    try {
      const raw = localStorage.getItem(AGENT_SKILLS_STORAGE_KEY);
      if (!raw) return new Set();
      const parsed = JSON.parse(raw);
      return new Set(Array.isArray(parsed) ? parsed.filter((item) => typeof item === 'string') : []);
    } catch {
      return new Set();
    }
  }

  function _saveDisabledSkillIds() {
    try {
      const disabledIds = _agentSkills.filter((item) => !_skillStates[item.id]).map((item) => item.id);
      localStorage.setItem(AGENT_SKILLS_STORAGE_KEY, JSON.stringify(disabledIds));
    } catch {
      // ignore localStorage write failure
    }
  }

  function _loadPendingDeviceSessions() {
    try {
      const raw = localStorage.getItem(DEVICE_PENDING_SESSIONS_STORAGE_KEY);
      const parsed = raw ? JSON.parse(raw) : [];
      return Array.isArray(parsed) ? parsed : [];
    } catch {
      return [];
    }
  }

  function _savePendingDeviceSessions() {
    try {
      localStorage.setItem(DEVICE_PENDING_SESSIONS_STORAGE_KEY, JSON.stringify(_pendingDeviceSessions));
    } catch {
      // ignore localStorage write failure
    }
  }

  function _normalizeTimestamp(value, fallback = Date.now()) {
    const raw = Number(value);
    if (Number.isFinite(raw) && raw > 0) {
      return raw < 10_000_000_000 ? raw * 1000 : raw;
    }
    const parsed = Date.parse(String(value || '').trim());
    return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
  }

  function _copyDraft(draft) {
    return {
      template: draft.template,
      title: draft.title,
      summary: draft.summary,
      ...(draft.deadlineAt ? { deadlineAt: draft.deadlineAt } : {}),
      ...(draft.resourceUrl ? { resourceUrl: draft.resourceUrl } : {}),
    };
  }

  function _parseDeviceBindPayload(raw) {
    const text = String(raw || '').trim();
    if (!text) return null;
    if (/^bt_/i.test(text)) return { bindToken: text };
    if (/^agentchat:\/\//i.test(text)) {
      try {
        const url = new URL(text);
        const bindToken = url.searchParams.get('bindToken') || url.searchParams.get('bind_token');
        return bindToken ? { bindToken: String(bindToken).trim() } : null;
      } catch {
        return null;
      }
    }
    if (text.startsWith('{') || text.startsWith('[')) {
      try {
        const parsed = JSON.parse(text);
        const bindToken = parsed?.bindToken || parsed?.bind_token;
        return bindToken ? { bindToken: String(bindToken).trim() } : null;
      } catch {
        return null;
      }
    }
    return null;
  }

  function _normalizeDeviceBindSession(raw, fallbackToken = '') {
    if (!raw || typeof raw !== 'object') return null;
    const bindToken = String(raw.bind_token || raw.bindToken || fallbackToken || '').trim();
    if (!bindToken) return null;
    return {
      bindToken,
      deviceId: String(raw.device_id || raw.deviceId || raw.node_id || raw.nodeId || '').trim() || undefined,
      nodeId: String(raw.node_id || raw.nodeId || raw.device_id || raw.deviceId || '').trim() || undefined,
      deviceName: String(raw.device_name || raw.deviceName || DEFAULT_DEVICE_NAME).trim() || DEFAULT_DEVICE_NAME,
      deviceType: String(raw.device_type || raw.deviceType || 'tv').trim().toLowerCase() || 'tv',
      deviceFingerprint: String(raw.device_fingerprint || raw.deviceFingerprint || '').trim() || undefined,
      status: String(raw.status || 'pending').trim().toLowerCase() || 'pending',
      createdAt: _normalizeTimestamp(raw.created_at || raw.createdAt, Date.now()),
      expireAt: _normalizeTimestamp(raw.expires_at || raw.expire_at || raw.expireAt, Date.now() + 10 * 60 * 1000),
      conversationId: String(raw.conversation_id || raw.conversationId || '').trim() || undefined,
      chatSessionId: String(raw.chat_session_id || raw.chatSessionId || '').trim() || undefined,
      sessionExpiresAt: raw.session_expires_at || raw.sessionExpiresAt
        ? _normalizeTimestamp(raw.session_expires_at || raw.sessionExpiresAt, Date.now())
        : undefined,
    };
  }

  function _normalizeBoundDevice(raw, fallback = {}) {
    if (!raw || typeof raw !== 'object') return null;
    const deviceId = String(raw.device_id || raw.deviceId || raw.node_id || raw.nodeId || fallback.deviceId || '').trim();
    if (!deviceId) return null;
    return {
      deviceId,
      nodeId: String(raw.node_id || raw.nodeId || fallback.nodeId || deviceId).trim() || deviceId,
      deviceName: String(raw.device_name || raw.deviceName || fallback.deviceName || DEFAULT_DEVICE_NAME).trim() || DEFAULT_DEVICE_NAME,
      deviceType: String(raw.device_type || raw.deviceType || fallback.deviceType || 'tv').trim().toLowerCase() || 'tv',
      deviceFingerprint: String(raw.device_fingerprint || raw.deviceFingerprint || fallback.deviceFingerprint || '').trim() || undefined,
      ownerPhone: String(raw.owner_phone || raw.ownerPhone || fallback.ownerPhone || _getUserName()).trim() || _getUserName(),
      boundConversationId: String(raw.conversation_id || raw.conversationId || raw.boundConversationId || fallback.boundConversationId || '').trim(),
      boundConversationName: String(raw.conversation_name || raw.conversationName || raw.boundConversationName || fallback.boundConversationName || '当前会话').trim() || '当前会话',
      boundAt: _normalizeTimestamp(raw.bound_at || raw.boundAt || raw.updated_at || raw.updatedAt || fallback.boundAt, Date.now()),
      status: String(raw.status || fallback.status || 'online').trim().toLowerCase() || 'online',
      chatSessionId: String(raw.chat_session_id || raw.chatSessionId || fallback.chatSessionId || '').trim() || undefined,
      sessionExpiresAt: raw.session_expires_at || raw.sessionExpiresAt || fallback.sessionExpiresAt
        ? _normalizeTimestamp(raw.session_expires_at || raw.sessionExpiresAt || fallback.sessionExpiresAt, Date.now())
        : undefined,
    };
  }

  function _getConversationName(conversationId) {
    return _conversations.find((item) => item.id === conversationId)?.name || '当前会话';
  }

  function _syncExecutionHistory() {
    _executionHistory = Object.values(_agentSessions)
      .flatMap((session) => Array.isArray(session.history) ? session.history : [])
      .sort((a, b) => b.timestamp - a.timestamp);
  }

  function _ensureAgentSession(conversationId) {
    if (!_agentSessions[conversationId]) {
      _agentSessions[conversationId] = { lastResult: null, lastError: null, history: [] };
    }
    return _agentSessions[conversationId];
  }

  function _recordAgentResult(conversationId, result) {
    const session = _ensureAgentSession(conversationId);
    session.lastResult = {
      ...result,
      draftActions: Array.isArray(result.draftActions) ? result.draftActions.map((action) => ({
        skillId: action.skillId,
        draft: _copyDraft(action.draft),
      })) : [],
    };
    session.lastError = null;
    session.history.unshift({
      id: _mkId('exec'),
      title: result.summaryMessage || 'AI 执行',
      timestamp: Date.now(),
      status: session.lastResult.draftActions.length > 0 ? 'awaiting_confirmation' : 'success',
      sourceConversationId: conversationId,
      intent: result.intent || 'unknown',
      skillIds: Array.isArray(result.selectedSkillIds) ? [...result.selectedSkillIds] : [],
    });
    if (session.history.length > 50) {
      session.history = session.history.slice(0, 50);
    }
    _syncExecutionHistory();
  }

  function _recordAgentError(conversationId, error) {
    const session = _ensureAgentSession(conversationId);
    session.lastError = error;
    session.history.unshift({
      id: _mkId('exec'),
      title: error || 'AI 执行失败',
      timestamp: Date.now(),
      status: 'failed',
      sourceConversationId: conversationId,
      intent: 'unknown',
      skillIds: [],
    });
    if (session.history.length > 50) {
      session.history = session.history.slice(0, 50);
    }
    _syncExecutionHistory();
  }

  function _inferResourceType(resourceUrl, template) {
    if (!resourceUrl) return 'none';
    if (template === 'homework' && /\.html?(?:[?#].*)?$/i.test(resourceUrl)) return 'html';
    if (/\.(avif|bmp|gif|jpe?g|png|svg|webp)(?:[?#].*)?$/i.test(resourceUrl)) return 'image';
    return 'link';
  }

  function _draftMetaForTemplate(template) {
    if (template === 'news') return { actionType: 'read', resourceType: 'link' };
    if (template === 'event') return { actionType: 'confirm', resourceType: 'link' };
    return { actionType: 'upload', resourceType: 'html' };
  }

  function _buildGenericDraft(userText, finalText, template) {
    const trimmed = String(userText || '').replace(/\s+/g, ' ').trim();
    const brief = trimmed.length > 30 ? `${trimmed.slice(0, 30)}...` : trimmed;
    if (template === 'news') {
      return {
        template: 'news',
        title: brief ? `群公告草稿：${brief}` : '群公告草稿',
        summary: String(finalText || '请确认公告内容后发布到目标群。').trim() || '请确认公告内容后发布到目标群。',
      };
    }
    if (template === 'event') {
      return {
        template: 'event',
        title: brief ? `活动草稿：${brief}` : '活动草稿',
        summary: String(finalText || '请确认活动内容后发布到目标群。').trim() || '请确认活动内容后发布到目标群。',
      };
    }
    return {
      template: 'homework',
      title: brief ? `任务草稿：${brief}` : '任务草稿',
      summary: String(finalText || '请确认任务内容后发布到目标群。').trim() || '请确认任务内容后发布到目标群。',
    };
  }

  function _buildStructuredAgentResult(userText, finalText) {
    const query = String(userText || '').toLowerCase();
    const matchedPublishSkills = AGENT_PUBLISH_SKILLS.filter((skill) =>
      skill.intentKeywords.some((keyword) => query.includes(keyword.toLowerCase()))
    );
    const matchedConfigs = AGENT_SKILL_CONFIGS.filter((skill) =>
      skill.keywords.some((keyword) => query.includes(keyword.toLowerCase()))
    );
    let draftActions = matchedPublishSkills.map((skill) => ({
      skillId: skill.id,
      draft: _copyDraft(skill.draft),
    }));
    let selectedSkillIds = matchedPublishSkills.map((skill) => skill.id);
    let intent = matchedPublishSkills[0]?.intent || matchedConfigs[0]?.intent || 'unknown';

    if (draftActions.length === 0) {
      if (/(公告|通知|群公告|发布公告)/u.test(userText)) {
        draftActions = [{ skillId: 'publish_news_skill', draft: _buildGenericDraft(userText, finalText, 'news') }];
        selectedSkillIds = ['publish_news_skill'];
        intent = 'publish_news';
      } else if (/(活动|报名|签到|接龙|回执|运动会)/u.test(userText)) {
        draftActions = [{ skillId: 'publish_event_skill', draft: _buildGenericDraft(userText, finalText, 'event') }];
        selectedSkillIds = ['publish_event_skill'];
        intent = 'create_event';
      } else if (/(作业|任务|练习|课后)/u.test(userText)) {
        draftActions = [{ skillId: 'publish_homework_skill', draft: _buildGenericDraft(userText, finalText, 'homework') }];
        selectedSkillIds = ['publish_homework_skill'];
        intent = 'create_task';
      } else if (/(总结|摘要|概括|梳理)/u.test(userText)) {
        selectedSkillIds = ['file_summary_skill'];
        intent = 'query';
      } else if (/(投递|投屏|设备|电视)/u.test(userText)) {
        selectedSkillIds = ['device_delivery_skill'];
        intent = 'query';
      } else if (matchedConfigs.length > 0) {
        selectedSkillIds = matchedConfigs.map((skill) => skill.id);
      }
    }

    return {
      intent,
      selectedSkillIds: [...new Set(selectedSkillIds)],
      draftActions,
      summaryMessage: String(finalText || '').trim() || 'AI 执行',
      directReply: String(finalText || '').trim() || undefined,
      source: 'openclaw',
    };
  }

  function _getFallbackPersonalWorkspaceId() {
    const userId = _getUserId();
    return userId ? `#workspace_${encodeURIComponent(userId)}` : '#workspace_local';
  }

  function _getPreferredPersonalWorkspaceId() {
    return _serverPersonalWorkspaceId || _getFallbackPersonalWorkspaceId();
  }

  function _shouldHydrateConversation(conversationOrId) {
    const id = typeof conversationOrId === 'string'
      ? _ensureConversationId(conversationOrId)
      : _ensureConversationId(conversationOrId?.id);
    if (_isPersonalWorkspaceConversationId(id)) return false;
    const conversation = typeof conversationOrId === 'string'
      ? _conversations.find(c => c.id === id)
      : conversationOrId;
    return conversation?.scope !== 'personal_workspace';
  }

  function _normalizeReminderSourceText(rawText) {
    return String(rawText || '')
      .replace(/@?炎图AI助手/gu, '')
      .replace(/\s+/gu, ' ')
      .trim();
  }

  // ── Agent mention 辅助函数（对齐 Vue 版 sendMessageService.ts）──
  const ZERO_WIDTH_SPACE = '\u200B';
  const _AGENT_MENTION_PATTERNS = ['@炎图AI助手', '炎图AI助手'];

  function _normalizeAgentMentionText(text) {
    return String(text || '').replaceAll(ZERO_WIDTH_SPACE, '');
  }

  function _isAgentMention(text) {
    const normalized = _normalizeAgentMentionText(text);
    return _AGENT_MENTION_PATTERNS.some(p => normalized.includes(p));
  }

  function _sanitizeAgentMentionForServer(text) {
    return _normalizeAgentMentionText(text)
      .replaceAll('@炎图AI助手', `@炎图AI${ZERO_WIDTH_SPACE}助手`)
      .replaceAll('炎图AI助手', `炎图AI${ZERO_WIDTH_SPACE}助手`);
  }

  function _stripAgentMention(text) {
    return _AGENT_MENTION_PATTERNS.reduce(
      (next, pattern) => next.replaceAll(pattern, ''),
      _normalizeAgentMentionText(text),
    ).trim();
  }

  function _looksLikeGroupReminder(text) {
    return /(提醒大家|通知大家|告诉大家|叫大家|提醒所有人|通知所有人|告诉所有人|群里|群内|同学们|老师们)/u.test(text);
  }

  // ── Agent 响应解析（对齐 Vue 版 openclawAgentProvider.normalizeStructuredReply）──
  // OpenClaw 返回的文本可能包含结构化 JSON 或纯文本 URL，
  // 从中提取 H5 卡片 URL 附件，供前端渲染可点击的卡片。

  function _isHttpUrl(value) {
    const url = String(value || '').trim();
    return /^https?:\/\//i.test(url);
  }

  function _looksLikeH5CardUrl(url) {
    const lower = String(url || '').toLowerCase();
    // 对齐 Vue 版 isGeneratedH5CardResource：card*.html / index.html
    return /\/[^/?#]*card[^/?#]*\.html?(?:[?#].*)?$/i.test(lower)
      || /\/index\.html?(?:[?#].*)?$/i.test(lower);
  }

  function _titleFromUrl(url) {
    try {
      const u = new URL(url);
      const last = u.pathname.split('/').filter(Boolean).pop() || u.hostname;
      return decodeURIComponent(last).replace(/\.html?$/i, '');
    } catch {
      return '';
    }
  }

  /**
   * 从 agent 响应文本中提取 H5 卡片附件（对齐 Vue 版 parseOpenClawReplyText）。
   * 支持两种形态：
   *   1. 结构化 JSON（含 outputs 数组，每项 type=artifact/webview + url）
   *   2. 纯文本中的 http(s) URL（特别是 card*.html / index.html）
   * @returns {{ text: string, attachments: Array }}
   */
  function _parseAgentReplyText(rawText) {
    const text = String(rawText || '').trim();
    if (!text) return { text: '', attachments: [] };

    const attachments = [];

    // 1. 尝试提取结构化 JSON（对齐 Vue 版 extractJsonObject + normalizeStructuredReply）
    const jsonStart = text.indexOf('{');
    if (jsonStart !== -1) {
      const jsonText = _extractJsonObject(text, jsonStart);
      if (jsonText) {
        try {
          const parsed = JSON.parse(jsonText);
          if (parsed && typeof parsed === 'object') {
            // outputs 数组（对齐 Vue 版 outputs → attachments 映射）
            if (Array.isArray(parsed.outputs)) {
              for (const output of parsed.outputs) {
                if (!output || typeof output !== 'object') continue;
                const type = String(output.type || '').trim();
                const url = String(output.url || '').trim();
                const title = String(output.title || '').trim() || _titleFromUrl(url);
                if (!_isHttpUrl(url) || !title) continue;
                if (type === 'text') continue;
                // artifact / webview / audio / image / file
                const isH5 = _looksLikeH5CardUrl(url)
                  || String(output.objectKind || '').toLowerCase() === 'h5_card'
                  || String(output.artifactType || '').toLowerCase() === 'html'
                  || String(output.sourceSkillId || '').toLowerCase() === 'h5-cards';
                attachments.push({
                  type: 'webview',
                  url,
                  title,
                  summary: String(output.summary || '').trim() || undefined,
                  objectKind: isH5 ? 'h5_card' : undefined,
                  artifactType: isH5 ? 'html' : undefined,
                });
              }
            }
            // 顶层 attachments 数组
            if (Array.isArray(parsed.attachments)) {
              for (const att of parsed.attachments) {
                if (!att || typeof att !== 'object') continue;
                if (att.type === 'webview' && _isHttpUrl(att.url)) {
                  attachments.push({
                    type: 'webview',
                    url: String(att.url).trim(),
                    title: String(att.title || '').trim() || _titleFromUrl(att.url),
                    summary: String(att.summary || '').trim() || undefined,
                  });
                }
              }
            }
            // 顶层 url 字段
            const topUrl = String(parsed.url || parsed.audioUrl || parsed.imageUrl || parsed.fileUrl || '').trim();
            if (_isHttpUrl(topUrl)) {
              attachments.push({
                type: 'webview',
                url: topUrl,
                title: String(parsed.title || '').trim() || _titleFromUrl(topUrl),
                summary: String(parsed.summary || '').trim() || undefined,
              });
            }
          }
        } catch {
          // 非 JSON，继续走纯文本提取
        }
      }
    }

    // 2. 纯文本 URL 提取（对齐 Vue 版 extractHttpUrlAttachments）
    const urlRegex = /https?:\/\/[^\s"'<>\]\\]+/gi;
    const found = text.match(urlRegex) || [];
    const seen = new Set(attachments.map(a => a.url));
    for (const rawUrl of found) {
      const url = rawUrl.replace(/[)\].,;!?]+$/, '');
      if (seen.has(url)) continue;
      seen.add(url);
      attachments.push({
        type: 'webview',
        url,
        title: _titleFromUrl(url),
      });
    }

    return { text, attachments };
  }

  function _extractJsonObject(text, startIndex) {
    let depth = 0;
    let inString = false;
    let escaped = false;
    for (let i = startIndex; i < text.length; i++) {
      const ch = text[i];
      if (escaped) { escaped = false; continue; }
      if (ch === '\\') { escaped = true; continue; }
      if (ch === '"') { inString = !inString; continue; }
      if (inString) continue;
      if (ch === '{') depth++;
      if (ch === '}') {
        depth--;
        if (depth === 0) return text.slice(startIndex, i + 1);
      }
    }
    return null;
  }

  // 对齐 Vue 版 stripAttachedInlineLinks：从回复文本中剥离已作为附件的 URL
  const _INLINE_LINK_RE = /\[([^\]\n]{1,80})\]\((https?:\/\/[^\s)]+)\)|https?:\/\/[^\s<>)，。！？、\u4e00-\u9fff]+/gi;
  function _stripAttachedInlineLinks(content, attachments) {
    const urls = new Set((attachments || []).filter(a => a.type === 'webview').map(a => a.url));
    if (!content || urls.size === 0) return content;
    const stripped = content.replace(_INLINE_LINK_RE, (match, label, markdownUrl) => {
      const url = (markdownUrl || match || '').replace(/[)\].,;!?]+$/, '');
      if (!urls.has(url)) return match;
      const normalizedLabel = String(label || '').trim();
      return normalizedLabel && normalizedLabel !== url ? normalizedLabel : '';
    });
    const cleanedLines = stripped.split(/\r?\n/).map(l => l.trimEnd()).filter(line => {
      return !/^(?:\u{1F517}\s*)?(?:链接|地址|入口|游戏入口在这里|资源地址|访问地址)[:：]?\s*$/u.test(line.trim());
    });
    const cleaned = cleanedLines.join('\n').replace(/\n{3,}/g, '\n\n').trim();
    return cleaned || '已处理完成，请查看下方资源。';
  }

  // 对齐 Vue 版 sanitizeAgentVisibleReply：有 H5 卡片附件时简化回复文本
  const _LOW_INFO_REPLY_RE = /^(已处理完成，请查看当前结果。?|已处理完成。?|处理完成。?|已完成。?|完成。?)$/u;
  function _sanitizeAgentReplyText(content, attachments) {
    const text = String(content || '').trim();
    if (!text) return text;
    const hasH5Card = (attachments || []).some(a => a.type === 'webview' && (a.objectKind === 'h5_card' || a.sourceSkillId === 'h5-cards' || a.artifactType === 'html'));
    if (hasH5Card) return 'H5 卡片已生成，请查看下方卡片。';
    if (_LOW_INFO_REPLY_RE.test(text) && hasH5Card) return 'H5 卡片已生成，请查看下方卡片。';
    return text;
  }

  // 对齐 Vue 版 buildConversationContext：提取最近对话上下文传给 OpenClaw
  const _OPENCLAW_CONTEXT_FOLLOW_UP_RE = /^(再|继续|换|改|这个|那个|它|上一个|上一张|刚才|同样|也|重新|加上|去掉|不要|帮我把|把这|把它|再来|换成|改成|follow up|again|same|change|make it)/iu;
  function _buildConversationContextForAgent(convId, currentText) {
    const normalizedCurrentText = String(currentText || '').trim();
    if (!_OPENCLAW_CONTEXT_FOLLOW_UP_RE.test(normalizedCurrentText)) return [];
    const msgs = _messagesByConv[convId];
    if (!msgs || !msgs.length) return [];
    return msgs
      .filter(m => (m.author === 'user' || m.author === 'agent') && m.status !== 'streaming')
      .slice(-6)
      .map(m => {
        const label = m.author === 'user' ? 'User' : 'Agent';
        const text = String(m.content || '').trim();
        return text ? `${label}: ${text}` : '';
      })
      .filter(entry => entry && entry !== `User: ${normalizedCurrentText}`);
  }

  // ── Agent deliveryId 去重（对齐 Vue 版 groupAgentDeliveryMarker + groupAgentReplyRelayEnvelope）──
  const _GROUP_AGENT_REPLY_RELAY_PREFIX = '[::GROUP_AGENT_REPLY::]';
  const _GROUP_AGENT_DELIVERY_MARKER_RE = /\u2063agent-delivery:([A-Za-z0-9%_.~:-]+)\u2063/g;

  function _createGroupAgentDeliveryId(convId) {
    const safe = String(convId || '').replace(/[^A-Za-z0-9_.~-]/g, '_');
    return `ga:${safe}:${Date.now()}:${Math.random().toString(36).slice(2)}`;
  }

  function _encodeGroupAgentReplyRelayEnvelope(deliveryId, message, attachments) {
    const id = String(deliveryId || '').trim();
    const msg = String(message || '').trim();
    if (!id || !msg) return '';
    const payload = { v: 1, deliveryId: id, message: msg };
    if (Array.isArray(attachments) && attachments.length) payload.attachments = attachments;
    return `${_GROUP_AGENT_REPLY_RELAY_PREFIX}${encodeURIComponent(JSON.stringify(payload))}`;
  }

  function _decodeGroupAgentReplyRelayEnvelope(content) {
    const text = String(content || '');
    if (!text.startsWith(_GROUP_AGENT_REPLY_RELAY_PREFIX)) return null;
    try {
      const payload = JSON.parse(decodeURIComponent(text.slice(_GROUP_AGENT_REPLY_RELAY_PREFIX.length)));
      const deliveryId = String(payload.deliveryId || '').trim();
      const message = String(payload.message || '').trim();
      if (!deliveryId || !message) return null;
      return {
        deliveryId,
        message,
        attachments: Array.isArray(payload.attachments) ? payload.attachments : undefined,
      };
    } catch {
      return null;
    }
  }

  function _parseReminderAmount(raw) {
    const token = String(raw || '').trim();
    if (!token) return 0;
    if (token === '半') return 0.5;
    if (/^\d+(?:\.\d+)?$/u.test(token)) return Number(token);
    const map = { 一: 1, 二: 2, 两: 2, 三: 3, 四: 4, 五: 5, 六: 6, 七: 7, 八: 8, 九: 9, 十: 10 };
    if (token === '十') return 10;
    if (token.length === 2 && token.startsWith('十')) return 10 + (map[token[1]] || 0);
    if (token.length === 2 && token.endsWith('十')) return (map[token[0]] || 0) * 10;
    if (token.length === 3 && token[1] === '十') return (map[token[0]] || 0) * 10 + (map[token[2]] || 0);
    return map[token] || 0;
  }

  function _delayMsForReminderUnit(amount, unit) {
    if (!Number.isFinite(amount) || amount <= 0) return 0;
    if (/(秒钟|秒)/u.test(unit)) return amount * 1000;
    if (/(分钟|分)/u.test(unit)) return amount * 60 * 1000;
    if (/(小时|时)/u.test(unit)) return amount * 60 * 60 * 1000;
    if (/天/u.test(unit)) return amount * 24 * 60 * 60 * 1000;
    return 0;
  }

  function _cleanReminderTitle(text, matchedText) {
    return String(text || '')
      .replace(String(matchedText || ''), '')
      .replace(/^(请)?(提醒我|叫我|通知我|告诉我|帮我记得|帮我提醒|remind me to|timer)/iu, '')
      .replace(/^(去|要|记得)/u, '')
      .replace(/[，。,.!！\s]+$/u, '')
      .trim() || '这件事';
  }

  function _tryParsePersonalReminderDraft(rawText, nowMs = Date.now()) {
    const text = _normalizeReminderSourceText(rawText);
    if (!text) return null;
    if (_looksLikeGroupReminder(text)) return null;
    if (!/(提醒我|叫我|通知我|告诉我|帮我记得|帮我提醒|remind me|timer|闹钟)/iu.test(text)) return null;
    const match = /(\d+(?:\.\d+)?|半|[一二两三四五六七八九十]{1,3})\s*(秒钟|秒|分钟|分|小时|时|天)\s*(?:之后|以后|后)/u.exec(text);
    if (!match) return null;
    const amount = _parseReminderAmount(match[1]);
    const delayMs = _delayMsForReminderUnit(amount, match[2] || '');
    if (!delayMs || delayMs > 30 * 24 * 60 * 60 * 1000) return null;
    return {
      title: _cleanReminderTitle(text, match[0]),
      dueAt: nowMs + delayMs,
      delayLabel: match[0] || '',
    };
  }

  async function _createPersonalReminderTaskFromMessage(input) {
    const draft = _tryParsePersonalReminderDraft(input?.text, input?.createdAt || Date.now());
    if (!draft) return null;
    const ownerUserId = _getUserId();
    const existing = _personalTasks.find(task =>
      task.ownerUserId === ownerUserId &&
      task.createdFromMessageId &&
      task.createdFromMessageId === input.messageId,
    );
    if (existing) return existing;
    return await createPersonalTask({
      ownerUserId,
      creatorUserId: ownerUserId,
      title: draft.title,
      summary: `提醒时间：${draft.delayLabel}`,
      sourceConversationId: input.conversationId,
      createdFromMessageId: input.messageId,
      dueAt: draft.dueAt,
      deliveryTarget: { type: 'conversation', conversationId: input.conversationId },
    });
  }

  async function _resolveServerPersonalWorkspaceId() {
    if (_status === 'error' || _status === 'disconnected') return;
    try {
      const result = await ChatApi.getPersonalWorkspaceId();
      const rawId =
        result?.conversationId ||
        result?.conversation_id ||
        result?.workspaceId ||
        result?.workspace_id ||
        result?.room_id ||
        result?.channel ||
        result?.id;
      const normalized = _ensureConversationId(rawId);
      _serverPersonalWorkspaceId = normalized
        ? (normalized.startsWith('#') ? normalized : `#${normalized}`)
        : '';
    } catch {
      _serverPersonalWorkspaceId = '';
    }
  }

  function _normalizeServerRoom(rawRoom) {
    if (!rawRoom || typeof rawRoom !== 'object') return null;
    const id = _ensureConversationId(
      rawRoom.id ||
      rawRoom.room_id ||
      rawRoom.roomId ||
      rawRoom.conversation_id ||
      rawRoom.conversationId,
    );
    if (!id) return null;
    return {
      id: id.startsWith('#') ? id : `#${id}`,
      title: String(rawRoom.title || rawRoom.name || rawRoom.room_name || rawRoom.roomName || '').trim(),
      kind: String(rawRoom.kind || rawRoom.type || 'group').trim() || 'group',
      ownerUserId: String(rawRoom.owner_user_id || rawRoom.ownerUserId || '').trim(),
      createdAt: String(rawRoom.created_at || rawRoom.createdAt || '').trim(),
    };
  }

  function _createLocalGroupConversation(name, serverId, options = {}) {
    const id = _ensureConversationId(serverId);
    if (!id) return null;
    const normalizedId = id.startsWith('#') ? id : `#${id}`;
    const existing = _conversations.find((item) => item.id === normalizedId);
    if (existing) {
      existing.type = 'group';
      existing.scope = 'group';
      existing.topic = existing.topic || '开始群组协作';
      if (options.ownerUserId) existing.ownerUserId = options.ownerUserId;
      if (name && existing.name === normalizedId.replace(/^#/, '')) {
        existing.name = name;
      }
      _messagesByConv[normalizedId] = _messagesByConv[normalizedId] || [];
      _postsByConv[normalizedId] = _postsByConv[normalizedId] || [];
      return existing;
    }
    const conversation = {
      id: normalizedId,
      type: 'group',
      scope: 'group',
      name: name || normalizedId.replace(/^#/, ''),
      topic: options.topic || '开始群组协作',
      unread: 0,
      createdAt: Number(options.createdAt) || Date.now(),
      ownerUserId: options.ownerUserId || _getUserId(),
    };
    _conversations.push(conversation);
    _messagesByConv[normalizedId] = _messagesByConv[normalizedId] || [];
    _postsByConv[normalizedId] = _postsByConv[normalizedId] || [];
    return conversation;
  }

  function _mergeServerConversation(rawConv, options = {}) {
    const room = _normalizeServerRoom(rawConv);
    if (!room || !room.id.startsWith('#') || _isPersonalWorkspaceConversationId(room.id)) return null;
    const name = _normalizeConversationName(room.title, room.id);
    const conversation = _createLocalGroupConversation(name, room.id, {
      topic: String(rawConv?.topic || '').trim(),
      ownerUserId: room.ownerUserId || undefined,
      createdAt: room.createdAt,
    });
    if (!conversation) return null;

    if (options.join !== false && _transport && _transport.joinConversation) {
      _transport.joinConversation(room.id);
    }
    if (options.fetchRoomInfo !== false) {
      void _fetchRoomInfo(room.id);
    }
    return conversation;
  }

  /**
   * 确保会话存在（对齐 Vue 版 _ensureConversation）
   * 仅做最小创建，不自动 join/hydrate——由调用方决定
   */
  function _ensureConversation(convId) {
    const id = _ensureConversationId(convId);
    if (!id) return;

    const existing = _conversations.find(c => c.id === id);
    if (existing) {
      // 对齐 Vue 版：名称是 fallback 时才异步获取真实名称
      if (id.startsWith('#') && existing.name === id.replace(/^#/, '')) {
        void _fetchRoomInfo(id);
      }
      return;
    }

    // 对齐 Vue 版：创建最小 Conversation
    const conv = {
      id,
      name: id.replace(/^#/, '') || '新会话',
      type: id.startsWith('#') ? 'group' : 'personal_workspace',
      topic: '',
      scope: _isPersonalWorkspaceConversationId(id) ? 'personal_workspace' : (id.startsWith('#') ? 'group' : 'personal_workspace'),
      unread: 0,
      createdAt: Date.now(),
      ownerUserId: _getUserId(),
    };

    _conversations.push(conv);
    _messagesByConv[id] = _messagesByConv[id] || [];
    _postsByConv[id] = _postsByConv[id] || [];

    _saveGroupsToStorage();

    // 对齐 Vue 版：异步获取完整房间信息（成员列表等）
    void _fetchRoomInfo(id);

    notify();
  }

  // ── 获取房间信息（参考 Vue 版 _fetchRoomInfo） ──
  async function _fetchRoomInfo(conversationId) {
    const id = _ensureConversationId(conversationId);
    if (!id) return;

    const conv = _conversations.find(c => c.id === id);
    if (!conv) return;

    // 避免重复请求，存储 Promise 供 getRoomMembers 复用
    if (conv._fetchingRoomInfoPromise) return conv._fetchingRoomInfoPromise;
    conv._fetchingRoomInfoPromise = (async () => {
    try {
      const info = await ChatApi.getRoomInfo(id);

      // 更新会话名称（对齐 Vue 版：从多个字段中获取服务端真实名称）
      const serverName = String(info.name || '').trim();
      const fallbackName = id.replace(/^#/, '');
      const isRawId = serverName === id || serverName === fallbackName;

      if (serverName && !isRawId && serverName !== conv.name) {
        conv.name = serverName;
        _saveGroupsToStorage();
        notify();
      }

      // 更新 topic
      if (info.topic && info.topic !== conv.topic) {
        conv.topic = info.topic;
      }

      // 更新成员信息
      if (Array.isArray(info.members)) {
        conv.memberCount = info.members.length;
        // 存储规范化成员，供 getRoomMembers 复用，避免重复请求
        const normalizedMembers = _normalizeRoomMembers(info.members);
        if (normalizedMembers.length) {
          _roomMembersByConv[id] = normalizedMembers;
        }
      }

      // 更新 ownerUserId：从 members 中 role=owner 的成员提取
      const ownerFromMembers = (info.members || []).find(m => m.role === 'owner');
      const ownerUserId = String(info.owner_user_id || info.ownerUserId || (ownerFromMembers?.user_id || '')).trim();
      if (ownerUserId && ownerUserId !== String(conv.ownerUserId || '').trim()) {
        conv.ownerUserId = ownerUserId;
      }

      const identityMap = {};
      _normalizeRoomMembers(info.members || []).forEach((member) => {
        if (member.memberKind !== 'user' || !member.phone) return;
        if (member.userId) identityMap[member.userId] = member.phone;
        identityMap[member.phone] = member.phone;
      });
      if (Object.keys(identityMap).length) {
        _memberIdentityMapByConversationId[id] = identityMap;
      }

      _saveGroupsToStorage();
      notify();
    } catch (e) {
      console.warn('[ChatStore] Failed to fetch room info:', id, e.message);
    } finally {
      conv._fetchingRoomInfoPromise = null;
    }
    })();
  }

  // ─ localStorage 持久化（参考 Vue 版 ChatPersistenceRepository） ──
  // 对齐 Vue 版 chatPersistenceRepository：按账号隔离的持久化 key
  function _accountScopeToken() {
    const phone = AuthStore.getPhone() || AuthStore.getUserId() || 'anonymous';
    const digits = String(phone).replace(/\D/g, '');
    const accountId = digits || String(phone).trim().toLowerCase().replace(/[^a-z0-9]+/g, '_') || 'anonymous';
    return accountId;
  }

  function _persistKey() {
    return `chat.persist.account.${_accountScopeToken()}.v1`;
  }
  function _postsPersistKey() {
    return `chat.persist.account.${_accountScopeToken()}.posts.v1`;
  }
  // 对齐 Vue 版 _schedulePersist：个人任务持久化 key
  function _personalTasksPersistKey() {
    return `chat.persist.account.${_accountScopeToken()}.personalTasks.v1`;
  }
  function _savePersonalTasksToStorage() {
    try {
      localStorage.setItem(_personalTasksPersistKey(), JSON.stringify(_personalTasks));
    } catch (e) {
      console.warn('[ChatStore] Failed to persist personal tasks:', e.message);
    }
  }
  function _loadPersonalTasksFromStorage() {
    try {
      const raw = localStorage.getItem(_personalTasksPersistKey());
      if (!raw) return [];
      const tasks = JSON.parse(raw);
      return Array.isArray(tasks) ? tasks : [];
    } catch {
      return [];
    }
  }

  // ── 群聊邀请通知持久化（对齐 Vue 版 sessionStore._schedulePersist） ──
  function _groupInvitationsPersistKey() {
    return `agentchat:group_invitations:${_getUserId()}`;
  }
  function _saveGroupInvitationNotifications() {
    try {
      localStorage.setItem(_groupInvitationsPersistKey(), JSON.stringify(_groupInvitationNotifications));
    } catch (e) {
      console.warn('[ChatStore] Failed to persist group invitations:', e.message);
    }
  }
  function _loadGroupInvitationNotifications() {
    try {
      const raw = localStorage.getItem(_groupInvitationsPersistKey());
      if (!raw) return [];
      const items = JSON.parse(raw);
      return Array.isArray(items) ? items : [];
    } catch {
      return [];
    }
  }

  // ── 本地已删除会话持久化（对齐 Vue 版 locallyDeletedConversationIds） ──
  function _locallyDeletedPersistKey() {
    return `chat.persist.account.${_accountScopeToken()}.locallyDeleted.v1`;
  }
  function _saveLocallyDeletedConversationIds() {
    try {
      localStorage.setItem(_locallyDeletedPersistKey(), JSON.stringify(_locallyDeletedConversationIds));
    } catch (e) {
      console.warn('[ChatStore] Failed to persist locally deleted ids:', e.message);
    }
  }
  function _loadLocallyDeletedConversationIds() {
    try {
      const raw = localStorage.getItem(_locallyDeletedPersistKey());
      if (!raw) return {};
      const parsed = JSON.parse(raw);
      return (parsed && typeof parsed === 'object') ? parsed : {};
    } catch {
      return {};
    }
  }

  // 对齐 Vue 版 isConversationLocallyDeleted
  function isConversationLocallyDeleted(convId) {
    const id = _ensureConversationId(convId);
    if (!id) return false;
    return Boolean(_locallyDeletedConversationIds[id]);
  }

  // 对齐 Vue 版 _restoreLocallyDeletedConversation
  function _restoreLocallyDeletedConversation(convId) {
    const id = _ensureConversationId(convId);
    if (!id || !isConversationLocallyDeleted(id)) return;
    delete _locallyDeletedConversationIds[id];
    _saveLocallyDeletedConversationIds();
    _ensureConversation(id);
  }

  // 对齐 Vue 版 deleteConversationFromList：纯本地删除，不调用服务端接口
  function deleteConversationFromList(conversationId) {
    const id = _ensureConversationId(conversationId);
    if (!id) return false;
    const index = _conversations.findIndex(c => c.id === id);
    const existing = index >= 0 ? _conversations[index] : null;
    if (existing && (existing.scope === 'personal_workspace' || existing.type === 'agent')) return false;

    _locallyDeletedConversationIds[id] = true;
    if (index >= 0) _conversations.splice(index, 1);
    delete _messagesByConv[id];
    delete _postsByConv[id];
    delete _memberIdentityMapByConversationId[id];
    delete _roomMembersByConv[id];
    if (_activeConversationId === id) {
      _activeConversationId = '';
    }
    _outbox = _outbox.filter(item => item.conversationId !== id);
    _saveGroupsToStorage();
    _saveLocallyDeletedConversationIds();
    notify();
    return true;
  }

  function _saveGroupsToStorage() {
    try {
      const groups = _conversations.filter(c => c.scope === 'group' && c.type === 'group');
      localStorage.setItem(_persistKey(), JSON.stringify(groups));
      // 对齐 Vue 版：同时持久化群聊帖子（合并所有群聊的帖子）
      const postsSnapshot = {};
      for (const c of groups) {
        const posts = _postsByConv[c.id];
        if (Array.isArray(posts) && posts.length) postsSnapshot[c.id] = posts;
      }
      localStorage.setItem(_postsPersistKey(), JSON.stringify(postsSnapshot));
    } catch (e) {
      console.warn('[ChatStore] Failed to persist groups:', e.message);
    }
  }

  function _loadGroupsFromStorage() {
    try {
      const raw = localStorage.getItem(_persistKey());
      if (!raw) return [];
      const groups = JSON.parse(raw);
      return Array.isArray(groups) ? groups : [];
    } catch {
      return [];
    }
  }

  // 对齐 Vue 版 ChatHydrationService：从本地恢复帖子
  function _loadPostsFromStorage() {
    try {
      const raw = localStorage.getItem(_postsPersistKey());
      if (!raw) return {};
      const parsed = JSON.parse(raw);
      if (!parsed || typeof parsed !== 'object') return {};
      const result = {};
      Object.entries(parsed).forEach(([convId, list]) => {
        if (!Array.isArray(list)) { result[convId] = []; return; }
        result[convId] = list.filter(item => {
          if (!item || typeof item !== 'object') return false;
          const id = String(item.id ?? '').trim();
          const conversationId = String(item.conversationId ?? '').trim();
          return Boolean(id && conversationId);
        });
      });
      return result;
    } catch {
      return {};
    }
  }

  // ── 初始化 ───
  async function init() {
    // ── 本地初始化（瞬时完成，不依赖网络）──
    _agentSkills = AGENT_SKILL_CONFIGS.map((skill) => ({ ...skill }));
    const disabledSkillIds = _loadDisabledSkillIds();
    _skillStates = {};
    _agentSkills.forEach((skill) => {
      _skillStates[skill.id] = !disabledSkillIds.has(skill.id);
    });
    _pendingDeviceSessions = _loadPendingDeviceSessions()
      .map((item) => _normalizeDeviceBindSession(item))
      .filter(Boolean);

    _ensureFixedConversations();
    _hydrateStoredGroupConversations();

    // 对齐 Vue 版 hydrateFromStorage：恢复群聊邀请通知
    _groupInvitationNotifications = _loadGroupInvitationNotifications();

    // 对齐 Vue 版：恢复本地已删除会话标记，防止已删除会话被新消息自动恢复
    _locallyDeletedConversationIds = _loadLocallyDeletedConversationIds();

    // 设置默认活跃会话（使用本地数据）
    if (_conversations.length > 0 && !_activeConversationId) {
      _activeConversationId = _conversations[0].id;
    }

    // ── 初始化 Transport（后台连接，不阻塞）──
    _transport = createDefaultTransport();
    _transportUnsub = _transport.onEvent(_handleTransportEvent);
    _transport.connect().catch((e) => {
      console.error('[ChatStore] Failed to initialize transport:', e);
      _status = 'error';
    });

    notify();
  }

  // ── 确保固定会话存在（参考 Vue 版 ensurePersonalWorkspaceConversation） ──
  function _ensureFixedConversations() {
    const userId = _getUserId();
    // 1. 确保"我的AI工作空间"存在
    const workspaceId = _getPreferredPersonalWorkspaceId();
    const existingWorkspace = _conversations.find(c =>
      c.scope === 'personal_workspace' ||
      c.id === workspaceId ||
      c.id === '#personal-workspace' ||
      c.name === '我的 AI 工作空间'
    );

    if (!existingWorkspace) {
      _conversations.unshift({
        id: workspaceId,
        name: '我的 AI 工作空间',
        type: 'group',
        scope: 'personal_workspace',
        topic: '文件、任务和设备同步',
        unread: 0,
        createdAt: Date.now(),
        ownerUserId: userId,
        agentId: 'personal-workspace-assistant',
      });
      _messagesByConv[workspaceId] = [];
      _postsByConv[workspaceId] = [];
    } else {
      // 规范化已有工作空间会话
      const previousId = existingWorkspace.id;
      existingWorkspace.id = workspaceId;
      existingWorkspace.type = 'group';
      existingWorkspace.scope = 'personal_workspace';
      existingWorkspace.name = '我的 AI 工作空间';
      existingWorkspace.topic = existingWorkspace.topic || '文件、任务和设备同步';
      existingWorkspace.agentId = existingWorkspace.agentId || 'personal-workspace-assistant';
      if (previousId && previousId !== workspaceId) {
        _messagesByConv[workspaceId] = _messagesByConv[workspaceId] || _messagesByConv[previousId] || [];
        _postsByConv[workspaceId] = _postsByConv[workspaceId] || _postsByConv[previousId] || [];
        delete _messagesByConv[previousId];
        delete _postsByConv[previousId];
        if (_activeConversationId === previousId) {
          _activeConversationId = workspaceId;
        }
      }
      _messagesByConv[workspaceId] = _messagesByConv[workspaceId] || [];
      _postsByConv[workspaceId] = _postsByConv[workspaceId] || [];
    }

    // 2. 确保"炎图AI助手"存在
    const agentId = 'conv-agent-assistant';
    const existingAgent = _conversations.find(c =>
      c.type === 'agent' ||
      c.name === '炎图AI助手'
    );

    if (!existingAgent) {
      _conversations.push({
        id: agentId,
        name: '炎图AI助手',
        type: 'agent',
        scope: 'agent',
        topic: '',
        unread: 0,
        createdAt: Date.now(),
      });
      _messagesByConv[agentId] = [];
    } else {
      // 规范化已有 agent 会话
      existingAgent.type = 'agent';
      existingAgent.scope = 'agent';
      existingAgent.name = '炎图AI助手';
      _messagesByConv[existingAgent.id] = _messagesByConv[existingAgent.id] || [];
    }
  }

  // ── 从本地存储恢复群聊列表 ──
  function _hydrateStoredGroupConversations() {
    const storedGroups = _loadGroupsFromStorage();
    for (const conv of storedGroups) {
      _mergeServerConversation(conv, { join: true, fetchRoomInfo: false });
    }
    console.log(`[ChatStore] Loaded ${storedGroups.length} group conversations from localStorage`);

    // 对齐 Vue 版 ChatHydrationService：从本地恢复帖子
    const storedPosts = _loadPostsFromStorage();
    Object.entries(storedPosts).forEach(([convId, posts]) => {
      if (Array.isArray(posts) && posts.length) {
        _postsByConv[convId] = posts;
      }
    });

    // 对齐 Vue 版 hydrateFromStorage：从本地恢复个人任务（即时 UI，不等待服务器）
    restorePersonalTasksFromStorage();
  }

  // ── 从服务端同步群聊列表（对齐 Vue 版 syncMyChatRoomsFromServer）──
  async function _syncGroupConversationsFromServer() {
    if (_status === 'error' || _status === 'disconnected') return;
    try {
      const result = await ChatApi.listConversations();
      const rooms = Array.isArray(result?.conversations) ? result.conversations : [];
      const joinedIds = [];

      for (const room of rooms) {
        const id = _ensureConversationId(room?.id);
        // 对齐 Vue 版：会话被本地删除时，服务端同步不自动恢复
        if (!id || isConversationLocallyDeleted(id)) continue;
        const merged = _mergeServerConversation(room, { join: false, fetchRoomInfo: false });
        if (!merged) continue;
        if ((merged?.name || '') === id.replace(/^#/, '')) {
          void _fetchRoomInfo(id);
        }

        // 对齐 Vue 版：加入频道
        if (_status === 'connected' && _transport && _transport.joinConversation) {
          _transport.joinConversation(id);
        }
        joinedIds.push(id);
      }

      _saveGroupsToStorage();
      console.log(`[ChatStore] Synced ${joinedIds.length} group conversations from server`);
    } catch (e) {
      if (!_isExpectedOfflineError(e)) console.warn('[ChatStore] Failed to load conversations from server:', e.message);
    }
  }

  // ── 会话 ──
  function getConversations() { return _conversations; }
  function getActiveConversationId() { return _activeConversationId; }
  function getActiveConversation() {
    return _conversations.find(c => c.id === _activeConversationId) || null;
  }
  function setActiveConversation(id) {
    if (id === _activeConversationId) return;
    _activeConversationId = id;

    if (!_activeConversationId) {
      notify();
      return;
    }

    // 加入会话频道
    if (_transport && _transport.joinConversation) {
      _transport.joinConversation(id);
    }

    const conversation = _conversations.find(c => c.id === id);
    if (conversation?.type === 'group' && _shouldHydrateConversation(conversation)) {
      if ((_messagesByConv[id]?.length ?? 0) === 0) {
        void loadConversationHistory(id, 50);
      }
      // 对齐 Vue 版 useChatShellLifecycle：每次切换到群聊都从服务端同步帖子（合并，不替换）
      void loadGroupPosts(id);
      // 始终预取房间信息（含成员列表），供群聊看板复用
      void _fetchRoomInfo(id);
    }

    notify();
  }
  function getMessages(convId) { return _messagesByConv[convId] || []; }
  function getActiveMessages() { return getMessages(_activeConversationId); }
  function getStatus() { return _status; }
  function getConversationUnreadCount(convId) {
    const conv = _conversations.find(c => c.id === convId);
    return conv?.unread || 0;
  }
  function getTotalUnreadCount() {
    return _conversations.reduce((t, c) => t + (c.unread || 0), 0);
  }
  function clearMarkRead(convId) {
    const conv = _conversations.find(c => c.id === convId);
    if (conv && conv.unread > 0) {
      conv.unread = 0;
      notify();
    }
  }

  // ── 帖子 ─
  function getPosts(convId) { return _postsByConv[convId] || []; }

  // 对齐 Vue 版 sessionStore.upsertSharedPost：从消息信封提取的帖子写入本地，保证跨模块即时可见
  function _upsertSharedPost(post, convId) {
    if (!post || !post.id) return;
    const targetConvId = _ensureConversationId(convId || post.conversationId);
    if (!targetConvId) return;
    const list = (_postsByConv[targetConvId] ??= []);
    const idx = list.findIndex(p => p.id === post.id);
    const normalized = {
      ...post,
      conversationId: targetConvId,
      responses: idx >= 0 ? (list[idx].responses || []) : (post.responses || []),
    };
    if (idx >= 0) {
      list[idx] = { ...list[idx], ...normalized };
    } else {
      list.unshift(normalized);
    }
    _postsByConv[targetConvId] = _sortPostsDescending(list);
    _saveGroupsToStorage();
  }

  function _groupPostTemplateLabel(template) {
    if (template === 'homework') return '家庭作业';
    if (template === 'event') return '群活动';
    return '群资讯';
  }

  // 对齐 Vue 版 conversationPostTemplates.ts: template ↔ taskKind 映射
  const _TEMPLATE_TO_TASK_KIND = { news: 'notice', homework: 'homework', event: 'activity' };
  const _TASK_KIND_TO_TEMPLATE = { notice: 'news', homework: 'homework', activity: 'event' };
  function _templateToTaskKind(template) {
    return _TEMPLATE_TO_TASK_KIND[template] || 'notice';
  }
  function _taskKindToTemplate(taskKind) {
    return _TASK_KIND_TO_TEMPLATE[taskKind] || 'news';
  }

  // ── 帖子分享信封（对齐 Vue 版 postShareEnvelope.ts）──
  // 将帖子编码为 base64 token 嵌入消息内容，便于聊天卡片渲染与跨模块联动
  const _POST_SHARE_START = '<!-- agent-chat:post-share ';
  const _POST_SHARE_END = ' -->';

  function _toBase64Utf8(value) {
    const bytes = new TextEncoder().encode(value);
    let binary = '';
    bytes.forEach((byte) => { binary += String.fromCharCode(byte); });
    return btoa(binary);
  }

  function _fromBase64Utf8(value) {
    const binary = atob(value);
    const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
    return new TextDecoder().decode(bytes);
  }

  function _encodeSharedPostMessageContent(content, post) {
    const baseContent = String(content ?? '').trim();
    const token = _toBase64Utf8(JSON.stringify({ version: 1, post }));
    return `${baseContent}\n${_POST_SHARE_START}${token}${_POST_SHARE_END}`;
  }

  function _parseSharedPostMessageContent(content, conversationId) {
    const source = String(content ?? '');
    const start = source.indexOf(_POST_SHARE_START);
    if (start === -1) return { content: source };
    const end = source.indexOf(_POST_SHARE_END, start);
    if (end === -1) return { content: source };
    const token = source.slice(start + _POST_SHARE_START.length, end).trim();
    const before = source.slice(0, start).trimEnd();
    const after = source.slice(end + _POST_SHARE_END.length).trimStart();
    const cleanContent = [before, after].filter(Boolean).join('\n').trim();
    if (!token) return { content: cleanContent };
    try {
      const payload = JSON.parse(_fromBase64Utf8(token));
      if (payload.version !== 1 || !payload.post) return { content: cleanContent };
      const post = { ...payload.post, conversationId: conversationId || payload.post.conversationId };
      return { content: cleanContent, post, attachments: _buildPostAttachments(post) };
    } catch {
      return { content: cleanContent };
    }
  }

  // 对齐 Vue 版 conversationPostTemplates.ts buildConversationPostAttachments
  function _buildPostAttachments(post) {
    const attachments = [{
      type: 'native_post',
      postId: post.id,
      title: post.title,
      summary: post.summary,
      template: post.template,
      conversationId: post.conversationId,
      actionType: post.actionType,
      resourceType: post.resourceType,
      resourceUrl: post.resourceUrl,
      deadlineAt: post.deadlineAt,
      status: post.status,
      visibility: post.visibility,
      createdAt: post.createdAt,
      createdById: post.createdById,
      createdByName: post.createdByName,
    }];
    if (post.resourceUrl && (post.resourceType === 'html' || post.resourceType === 'link')) {
      attachments.unshift({
        type: 'webview',
        url: post.resourceUrl,
        title: post.template === 'homework' ? '打开作业资源' : post.title,
        summary: post.summary,
        postId: post.id,
        template: post.template,
      });
    }
    return attachments;
  }

  async function createGroupPost(convId, data) {
    const convIdNorm = _ensureConversationId(convId);
    if (!convIdNorm) throw new Error('invalid conversation id');

    // 对齐 Vue 版：生成 post id + taskKind + createdBy
    const postId = `post_${Date.now()}_${Math.random().toString(16).slice(2, 8)}`;
    const taskKind = _templateToTaskKind(data.template || 'news');
    const createdById = _getUserId();
    const createdByName = _getUserName();

    const result = await import('../api/postApi.js').then(m => m.default.create({
      id: postId,
      conversationId: convIdNorm,
      title: data.title,
      summary: data.summary || '',
      taskKind,
      actionType: data.actionType || 'read',
      resourceType: data.resourceType || 'none',
      resourceUrl: data.resourceUrl || '',
      deadlineAt: data.deadlineAt ? String(data.deadlineAt) : '',
      createdById,
      createdByName,
    }));

    const serverPostId = result?.postId || result?.post?.id || postId;
    const post = result?.post || {};
    const postObj = {
      id: serverPostId,
      conversationId: convIdNorm,
      type: data.template === 'news' ? 'info' : data.template === 'event' ? 'event' : 'task',
      title: data.title,
      summary: data.summary || '',
      status: 'published',
      template: data.template || 'news',
      actionType: data.actionType || 'read',
      resourceType: data.resourceType || 'none',
      resourceUrl: data.resourceUrl || '',
      deadlineAt: data.deadlineAt || null,
      createdAt: Date.now(),
      createdById,
      createdByName,
      visibility: 'detail_only',
      responses: Array.isArray(post.responses) ? post.responses : [],
    };

    (_postsByConv[convIdNorm] ??= []).unshift(postObj);
    _postsByConv[convIdNorm] = _sortPostsDescending(_postsByConv[convIdNorm]);
    _saveGroupsToStorage();
    notify();
    if (data?.announceInChat) {
      // 对齐 Vue 版 publishConversationPostAndAnnounce：
      // 发送带帖子分享信封 + 附件卡片的用户消息，使聊天展示可点击的任务卡片
      const summaryText = `我发布了新的${_groupPostTemplateLabel(postObj.template)}：${postObj.title}`;
      const encodedText = _encodeSharedPostMessageContent(summaryText, postObj);
      const attachments = _buildPostAttachments(postObj);
      sendUserMessage(encodedText, convIdNorm, { attachments });
    }
    return postObj;
  }

  // 对齐 Vue 版 sortPostsDescending：按 createdAt 降序排列
  function _sortPostsDescending(posts) {
    return [...posts].sort((a, b) => {
      const aTime = a.createdAt ?? 0;
      const bTime = b.createdAt ?? 0;
      if (bTime !== aTime) return bTime - aTime;
      return String(b.id || '').localeCompare(String(a.id || ''));
    });
  }

  function _normalizeServerPost(p, convIdNorm) {
    // 对齐 Vue 版 fetchConversationPostsFromServer：task_kind → template
    const template = _taskKindToTemplate(String(p.task_kind ?? p.taskKind ?? p.template ?? ''));
    return {
      id: p.id || `post_${Date.now()}`,
      conversationId: convIdNorm,
      type: template === 'news' ? 'info' : template === 'event' ? 'event' : 'task',
      title: p.title || p.name || '',
      summary: p.summary || '',
      status: p.status === 'closed' ? 'closed' : (p.status || 'published'),
      template,
      actionType: p.action_type || p.actionType || 'read',
      resourceType: p.resource_type || p.resourceType || 'none',
      resourceUrl: p.resource_url || p.resourceUrl || '',
      deadlineAt: p.deadline_at || p.deadlineAt || null,
      createdAt: p.created_at ? (typeof p.created_at === 'string' ? new Date(p.created_at).getTime() : p.created_at) : Date.now(),
      createdById: String(p.created_by_id || p.createdById || '').trim(),
      createdByName: p.created_by_name || p.createdByName || '未知用户',
      responses: Array.isArray(p.responses) ? p.responses.map(r => {
        const responseType = r.response_type || r.responseType || 'read';
        return {
          id: r.id || `resp_${Date.now()}`,
          userId: r.user_id || r.userId || '',
          userName: r.user_name || r.userName || r.actor_name || r.actorName || '未知用户',
          actorName: r.actor_name || r.actorName || r.user_name || r.userName || '成员',
          responseType,
          confirmation: r.confirmation || (responseType === 'accept' ? 'going' : responseType === 'reject' ? 'not_going' : ''),
          content: r.content || r.note || '',
          createdAt: r.created_at ? (typeof r.created_at === 'string' ? new Date(r.created_at).getTime() : r.created_at) : Date.now(),
        };
      }) : [],
    };
  }

  async function loadGroupPosts(convId) {
    const convIdNorm = _ensureConversationId(convId);
    if (!convIdNorm) return [];

    try {
      const result = await import('../api/postApi.js').then(m => m.default.list(convIdNorm));
      const posts = result?.posts || result || [];
      // 对齐 Vue 版 fetchConversationPostsFromServer：合并服务端帖子到本地，不替换本地已存在的帖子
      const local = (_postsByConv[convIdNorm] ??= []);
      for (const p of posts) {
        const id = String(p.id ?? '').trim();
        if (!id || local.some(item => item.id === id)) continue;
        local.unshift(_normalizeServerPost(p, convIdNorm));
      }
      _postsByConv[convIdNorm] = _sortPostsDescending(local);
      _saveGroupsToStorage();
      notify();
      return _postsByConv[convIdNorm];
    } catch (e) {
      console.warn('[ChatStore] Failed to load posts:', e.message);
      return _postsByConv[convIdNorm] || [];
    }
  }

  // 对齐 Vue 版：任务页打开时预取所有群聊的帖子
  async function loadAllGroupPosts() {
    const groupConvs = _conversations.filter(c => c.type === 'group' && c.scope !== 'personal_workspace');
    await Promise.allSettled(groupConvs.map(c => loadGroupPosts(c.id)));
  }

  async function respondToGroupPost(postId, convId, responseData) {
    const convIdNorm = _ensureConversationId(convId);
    const responseType = String(responseData?.responseType || 'read').trim() || 'read';
    const confirmation = responseData?.confirmation
      || (responseType === 'accept' ? 'going' : responseType === 'reject' ? 'not_going' : '');
    const content = String(responseData?.content || '').trim();
    const actorUserId = _getUserId();
    const actorName = _getUserName();
    // 对齐 Vue 版 respondToConversationPost：传 actorUserId/actorName/actionType/attachmentsJson
    const actionType = responseData?.actionType || 'read';
    const result = await import('../api/postApi.js').then(m => m.default.respond({
      conversationId: convIdNorm,
      postId,
      actorUserId,
      actorName,
      actionType,
      responseType,
      confirmation,
      content,
      attachmentsJson: '{}',
    }));

    const responseId = result?.responseId || result?.response?.id || `resp_${Date.now()}`;
    const response = result?.response || {};
    // 更新本地帖子状态
    const posts = _postsByConv[convIdNorm];
    if (posts) {
      const post = posts.find(p => p.id === postId);
      if (post) {
        if (!post.responses) post.responses = [];
        const nextResponse = {
          id: responseId,
          userId: actorUserId,
          userName: actorName,
          actorName,
          responseType,
          confirmation,
          content,
          createdAt: Date.now(),
        };
        const existingIndex = post.responses.findIndex(item => (item.userId || '') === actorUserId);
        if (existingIndex >= 0) {
          post.responses.splice(existingIndex, 1, nextResponse);
        } else {
          post.responses.unshift(nextResponse);
        }
        // 对齐 Vue 版 postService.submitConversationPostResponse: 更新 latestResponseAt 并重排序
        post.latestResponseAt = nextResponse.createdAt;
        _postsByConv[convIdNorm] = _sortPostsDescending(posts);
        // 对齐 Vue 版 postService.submitConversationPostResponse: 发送用户消息到聊天
        sendUserMessage(_buildGroupPostResponseMessage(post, nextResponse), convIdNorm);
        _saveGroupsToStorage();
        notify();
      }
    }
    return response;
  }

  /**
   * 构建帖子响应消息文本（对齐 Vue 版 buildResponseMessage）
   */
  function _buildGroupPostResponseMessage(post, response) {
    if (!post) return '';
    if (post.actionType === 'confirm') {
      return response.confirmation === 'not_going'
        ? `我无法参加：${post.title}`
        : `我确认参加：${post.title}`;
    }
    if (post.actionType === 'read') {
      return `我已查看：${post.title}`;
    }
    if (post.actionType === 'upload') {
      const note = String(response.content || '').trim();
      if (note) return `我已提交作业：${post.title}\n${note}`;
      return `我已提交作业：${post.title}`;
    }
    if (post.actionType === 'checkin') {
      return `我已签到：${post.title}`;
    }
    return `我已完成：${post.title}`;
  }

  async function submitHomeworkPostResponse(postId, convId, input = {}) {
    const convIdNorm = _ensureConversationId(convId);
    const note = String(input?.note || '').trim();
    const file = input?.file || null;
    // 对齐 Vue 版：作业提交也走 CONVERSATION_POST_RESPONSE（无独立文件上传端点）
    const actorUserId = _getUserId();
    const actorName = _getUserName();
    const result = await import('../api/postApi.js').then(m => m.default.respond({
      conversationId: convIdNorm,
      postId,
      actorUserId,
      actorName,
      actionType: 'upload',
      responseType: 'upload',
      confirmation: '',
      content: note || (file ? `已提交文件：${file.name}` : ''),
      attachmentsJson: '{}',
    }));
    const responseId = result?.responseId || result?.response?.id || `resp_${Date.now()}`;
    const response = result?.response || {};

    const posts = _postsByConv[convIdNorm];
    if (posts) {
      const post = posts.find((item) => item.id === postId);
      if (post) {
        if (!post.responses) post.responses = [];
        const nextResponse = {
          id: responseId,
          userId: actorUserId,
          userName: actorName,
          actorName,
          responseType: 'upload',
          confirmation: '',
          content: note || (file ? `已提交文件：${file.name}` : ''),
          createdAt: Date.now(),
        };
        const existingIndex = post.responses.findIndex((item) => (item.userId || '') === actorUserId);
        if (existingIndex >= 0) {
          post.responses.splice(existingIndex, 1, nextResponse);
        } else {
          post.responses.unshift(nextResponse);
        }
        // 对齐 Vue 版 postService.submitConversationPostResponse: 更新 latestResponseAt 并重排序
        post.latestResponseAt = nextResponse.createdAt;
        _postsByConv[convIdNorm] = _sortPostsDescending(posts);
        sendUserMessage(_buildGroupPostResponseMessage(post, {
          ...nextResponse,
          hasAttachment: Boolean(file),
          attachmentName: file?.name || '',
        }), convIdNorm);
        _saveGroupsToStorage();
        notify();
      }
    }
    return response;
  }

  async function closeGroupPost(postId, convId) {
    const convIdNorm = _ensureConversationId(convId);
    const result = await import('../api/postApi.js').then(m => m.default.close(postId, convIdNorm));

    const post = result?.post || {};
    // 更新本地帖子状态
    const posts = _postsByConv[convIdNorm];
    let closedTitle = '';
    if (posts) {
      const postIdx = posts.findIndex(p => p.id === postId);
      if (postIdx >= 0) {
        closedTitle = posts[postIdx].title || '';
        posts[postIdx].status = 'closed';
        posts[postIdx].closedAt = Date.now();
        _saveGroupsToStorage();
        notify();
      }
    }
    // 对齐 Vue 版 TaskFullScreen.closeSelectedPost：关闭后发送完成消息到聊天
    if (closedTitle) {
      sendUserMessage(`任务已完成：${closedTitle}`, convIdNorm);
    }
    return post;
  }

  // ─ 个人任务 ──
  function getPersonalTasksForCurrentUser() { return _personalTasks; }

  // 对齐 Vue 版 sessionStore.groupInvitationNotifications
  function getGroupInvitationNotifications() { return _groupInvitationNotifications; }

  // 对齐 Vue 版 hydrateFromStorage：启动时先从 localStorage 恢复个人任务
  function restorePersonalTasksFromStorage() {
    const local = _loadPersonalTasksFromStorage();
    if (local.length) {
      _personalTasks = local;
      notify();
    }
  }

  async function loadPersonalTasksFromServer() {
    if (_status === 'error' || _status === 'disconnected') return;
    try {
      const result = await ChatApi.listPersonalTasks();
      const rawTasks = result?.tasks || [];
      if (!Array.isArray(rawTasks)) return;

      const serverTasks = rawTasks.map(t => ({
        id: String(t.id || ''),
        ownerUserId: String(t.owner_user_id || t.ownerUserId || ''),
        creatorUserId: String(t.creator_user_id || t.creatorUserId || ''),
        title: String(t.title || ''),
        summary: String(t.summary || ''),
        sourceConversationId: String(t.source_conversation_id || t.sourceConversationId || ''),
        createdFromMessageId: String(t.created_from_message_id || t.createdFromMessageId || ''),
        dueAt: t.due_at || t.dueAt || null,
        status: String(t.status || 'pending'),
        deliveryTarget: _parseDeliveryTarget(t.delivery_target_json || t.deliveryTarget),
        createdAt: t.created_at || t.createdAt || Date.now(),
        completedAt: t.completed_at || t.completedAt || null,
        canceledAt: t.canceled_at || t.canceledAt || null,
        syncStatus: 'synced',
      })).filter(t => t.id);

      // 对齐 Vue 版 fetchPersonalTasksFromServer 的合并策略：
      // 服务器数据为基础，但若本地刚做了状态变更（done/canceled）而服务器尚未同步，保留本地状态
      const localById = {};
      _personalTasks.forEach(t => { localById[t.id] = t; });

      const merged = serverTasks.map(serverTask => {
        const localTask = localById[serverTask.id];
        if (localTask && (localTask.status === 'done' || localTask.status === 'canceled') && serverTask.status === 'pending') {
          // 本地状态变更尚未同步到服务器，保留本地状态
          return { ...serverTask, status: localTask.status, completedAt: localTask.completedAt, canceledAt: localTask.canceledAt };
        }
        return serverTask;
      });

      // 保留本地有但服务器没有的任务（新建未同步）
      const serverIds = new Set(serverTasks.map(t => t.id));
      _personalTasks.forEach(t => {
        if (!serverIds.has(t.id)) merged.push(t);
      });

      _personalTasks = merged;
      _savePersonalTasksToStorage();

      console.log(`[ChatStore] Loaded ${serverTasks.length} personal tasks from server, ${merged.length} after merge`);
      notify();
    } catch (e) {
      if (!_isExpectedOfflineError(e)) console.warn('[ChatStore] Failed to load personal tasks from server:', e.message);
    }
  }

  // 对齐 Vue 版 sessionStore.completePersonalTask：乐观更新 + syncStatus 跟踪 + 异步同步到服务器
  function completePersonalTask(taskId) {
    const task = _personalTasks.find(t => t.id === taskId);
    if (!task || task.status === 'done' || task.status === 'canceled') return false;
    task.status = 'done';
    task.completedAt = Date.now();
    task.syncStatus = 'syncing';
    task.syncError = undefined;
    _savePersonalTasksToStorage();
    notify();
    // 异步同步到服务器，不阻塞 UI，完成后更新 syncStatus
    ChatApi.updatePersonalTaskStatus(taskId, task.ownerUserId, 'done')
      .then(() => {
        task.syncStatus = 'synced';
        _savePersonalTasksToStorage();
        notify();
      })
      .catch(e => {
        console.error('[ChatStore] Failed to sync task completion:', e.message);
        task.syncStatus = 'failed';
        task.syncError = e.message;
        _savePersonalTasksToStorage();
        notify();
      });
    return true;
  }

  // 对齐 Vue 版 sessionStore.cancelPersonalTask：乐观更新 + syncStatus 跟踪 + 异步同步到服务器
  function cancelPersonalTask(taskId) {
    const task = _personalTasks.find(t => t.id === taskId);
    if (!task || task.status === 'done' || task.status === 'canceled') return false;
    task.status = 'canceled';
    task.canceledAt = Date.now();
    task.syncStatus = 'syncing';
    task.syncError = undefined;
    _savePersonalTasksToStorage();
    notify();
    // 异步同步到服务器，不阻塞 UI，完成后更新 syncStatus
    ChatApi.updatePersonalTaskStatus(taskId, task.ownerUserId, 'canceled')
      .then(() => {
        task.syncStatus = 'synced';
        _savePersonalTasksToStorage();
        notify();
      })
      .catch(e => {
        console.error('[ChatStore] Failed to sync task cancellation:', e.message);
        task.syncStatus = 'failed';
        task.syncError = e.message;
        _savePersonalTasksToStorage();
        notify();
      });
    return true;
  }

  // 对齐 Vue 版 sessionStore.reschedulePersonalTask：乐观更新 + syncStatus 跟踪 + 异步同步到服务器
  function reschedulePersonalTask(taskId, newDueAt) {
    const task = _personalTasks.find(t => t.id === taskId);
    if (!task) return false;
    task.dueAt = newDueAt;
    task.syncStatus = 'syncing';
    task.syncError = undefined;
    // 对齐 Vue 版：已取消的任务延期后恢复为 pending
    if (task.status === 'canceled') {
      task.status = 'pending';
      task.canceledAt = undefined;
    }
    _savePersonalTasksToStorage();
    notify();
    // 异步同步到服务器，不阻塞 UI，完成后更新 syncStatus
    ChatApi.reschedulePersonalTask(taskId, task.ownerUserId, newDueAt)
      .then(() => {
        task.syncStatus = 'synced';
        _savePersonalTasksToStorage();
        notify();
      })
      .catch(e => {
        console.error('[ChatStore] Failed to sync task reschedule:', e.message);
        task.syncStatus = 'failed';
        task.syncError = e.message;
        _savePersonalTasksToStorage();
        notify();
      });
    return true;
  }

  async function createPersonalTask(opts) {
    const ownerUserId = opts.ownerUserId || _getUserId();
    const creatorUserId = opts.creatorUserId || ownerUserId;
    const taskId = `pt-${Date.now()}`;
    const deliveryTarget = opts.deliveryTarget || { type: 'workspace' };
    const deliveryTargetJson = typeof deliveryTarget === 'string' ? deliveryTarget : JSON.stringify(deliveryTarget);

    try {
      const result = await ChatApi.createPersonalTask({
        id: taskId,
        owner_user_id: ownerUserId,
        creator_user_id: creatorUserId,
        title: opts.title || '新任务',
        summary: opts.summary || '',
        source_conversation_id: opts.sourceConversationId || '',
        created_from_message_id: opts.createdFromMessageId || '',
        due_at: opts.dueAt || '',
        status: 'pending',
        delivery_target_json: deliveryTargetJson,
      });

      const serverTaskId = result?.taskId || taskId;
      const task = {
        id: serverTaskId,
        ownerUserId,
        creatorUserId,
        title: opts.title || '新任务',
        summary: opts.summary || '',
        sourceConversationId: opts.sourceConversationId || '',
        createdFromMessageId: opts.createdFromMessageId || '',
        dueAt: opts.dueAt || null,
        status: 'pending',
        deliveryTarget: deliveryTarget,
        createdAt: Date.now(),
        syncStatus: 'synced',
      };
      _personalTasks.push(task);
      _savePersonalTasksToStorage();
      notify();
      return task;
    } catch (e) {
      console.error('[ChatStore] Failed to create personal task:', e.message);
      // 乐观更新：即使服务器失败也保留本地
      const task = {
        id: taskId,
        ownerUserId,
        creatorUserId,
        title: opts.title || '新任务',
        summary: opts.summary || '',
        sourceConversationId: opts.sourceConversationId || '',
        createdFromMessageId: opts.createdFromMessageId || '',
        dueAt: opts.dueAt || null,
        status: 'pending',
        deliveryTarget: deliveryTarget,
        createdAt: Date.now(),
        syncStatus: 'pending',
      };
      _personalTasks.push(task);
      _savePersonalTasksToStorage();
      notify();
      return task;
    }
  }

  function _normalizePendingDeviceSessions() {
    const now = Date.now();
    _pendingDeviceSessions = _pendingDeviceSessions
      .map((item) => _normalizeDeviceBindSession(item))
      .filter(Boolean)
      .map((item) => (
        item.status === 'pending' && item.expireAt <= now
          ? { ...item, status: 'expired' }
          : item
      ))
      .filter((item) => item.status !== 'bound')
      .sort((a, b) => b.createdAt - a.createdAt);
    _savePendingDeviceSessions();
  }

  async function refreshBoundDevices() {
    if (_status === 'error' || _status === 'disconnected') return;
    try {
      const result = await ChatApi.listBoundDevices();
      const devices = Array.isArray(result?.devices) ? result.devices : [];
      _boundDevices = devices
        .map((item) => _normalizeBoundDevice(item))
        .filter(Boolean)
        .sort((a, b) => b.boundAt - a.boundAt);
    } catch (e) {
      if (!_isExpectedOfflineError(e)) console.warn('[ChatStore] Failed to load bound devices:', e.message);
    } finally {
      _normalizePendingDeviceSessions();
      notify();
    }
    return _boundDevices;
  }

  async function createDeviceBindSession(deviceName) {
    const result = await ChatApi.createDeviceBindSession({
      device_name: String(deviceName || DEFAULT_DEVICE_NAME).trim() || DEFAULT_DEVICE_NAME,
      device_type: 'TV',
    });
    const session = _normalizeDeviceBindSession(result);
    if (!session) throw new Error('设备绑定服务未返回会话信息');
    _pendingDeviceSessions = [
      session,
      ..._pendingDeviceSessions.filter((item) => item.bindToken !== session.bindToken),
    ];
    _normalizePendingDeviceSessions();
    notify();
    return session;
  }

  async function getDeviceBindSession(bindToken) {
    const token = String(bindToken || '').trim();
    if (!token) return null;
    const result = await ChatApi.getDeviceBindSession(token);
    const session = _normalizeDeviceBindSession(result, token);
    if (!session) return null;
    _pendingDeviceSessions = [
      session,
      ..._pendingDeviceSessions.filter((item) => item.bindToken !== session.bindToken),
    ];
    _normalizePendingDeviceSessions();
    notify();
    return session;
  }

  function appendLocalSystemMessage(conversationId, content) {
    const convId = _ensureConversationId(conversationId);
    if (!convId || !String(content || '').trim()) return;
    _ensureConversation(convId);
    const list = (_messagesByConv[convId] ??= []);
    list.push({
      id: _mkId('sys'),
      conversationId: convId,
      author: 'system',
      authorName: '系统',
      content: String(content).trim(),
      createdAt: Date.now(),
      status: 'complete',
      kind: 'default',
    });
    notify();
  }

  function announceDeviceBound(input) {
    const deviceName = String(input?.deviceName || '').trim() || '电视设备';
    const ownerPhone = String(input?.ownerPhone || '').trim() || _getUserName();
    appendLocalSystemMessage(
      input?.conversationId,
      `已将设备「${deviceName}」绑定到当前群聊，绑定账号：${ownerPhone}`,
    );
  }

  async function confirmDeviceBind(input) {
    const bindToken = String(input?.bindToken || '').trim();
    const conversationId = _ensureConversationId(input?.conversationId);
    const conversationName = String(input?.conversationName || _getConversationName(conversationId) || '当前会话').trim();
    if (!bindToken) throw new Error('请输入 bind_token');
    if (!conversationId) throw new Error('请选择目标会话');
    const result = await ChatApi.confirmDeviceBind(bindToken, {
      bind_token: bindToken,
      conversation_id: conversationId,
      conversation_name: conversationName,
      device_name: input?.deviceName || undefined,
      owner_phone: _getUserName(),
    });
    const session = _normalizeDeviceBindSession(result, bindToken) || {
      bindToken,
      deviceName: String(input?.deviceName || DEFAULT_DEVICE_NAME).trim() || DEFAULT_DEVICE_NAME,
      deviceType: 'tv',
      status: 'bound',
      createdAt: Date.now(),
      expireAt: Date.now() + 10 * 60 * 1000,
      conversationId,
    };
    const device = _normalizeBoundDevice(result, {
      deviceId: session.deviceId,
      nodeId: session.nodeId,
      deviceName: session.deviceName,
      boundConversationId: conversationId,
      boundConversationName: conversationName,
      ownerPhone: _getUserName(),
      status: 'online',
      boundAt: Date.now(),
    });
    if (!device) throw new Error('设备绑定服务未返回设备信息');
    _pendingDeviceSessions = _pendingDeviceSessions
      .map((item) => item.bindToken === bindToken ? { ...item, ...session, status: 'bound', conversationId } : item)
      .filter((item) => item.bindToken !== bindToken);
    _boundDevices = [
      device,
      ..._boundDevices.filter((item) => item.deviceId !== device.deviceId),
    ].sort((a, b) => b.boundAt - a.boundAt);
    _normalizePendingDeviceSessions();
    announceDeviceBound({
      conversationId,
      deviceName: device.deviceName,
      ownerPhone: device.ownerPhone,
    });
    notify();
    return { session, device };
  }

  async function unbindDevice(deviceId) {
    const id = String(deviceId || '').trim();
    if (!id) return false;
    await ChatApi.unbindDevice(id);
    _boundDevices = _boundDevices.filter((item) => item.deviceId !== id);
    notify();
    return true;
  }

  async function publishWorkspaceDraft(skillId, targetConversationId, sourceConversationId) {
    const sourceId = _ensureConversationId(sourceConversationId) || _getPreferredPersonalWorkspaceId();
    const targetId = _ensureConversationId(targetConversationId);
    if (!targetId) throw new Error('请选择目标群聊');
    const session = _ensureAgentSession(sourceId);
    const lastResult = session.lastResult;
    const action = lastResult?.draftActions?.find((item) => item.skillId === skillId);
    if (!action) throw new Error('当前没有可发布的草稿');

    const draft = action.draft;
    const meta = _draftMetaForTemplate(draft.template);
    const created = await createGroupPost(targetId, {
      title: draft.title || '待确认草稿',
      summary: draft.summary || '',
      template: draft.template,
      actionType: meta.actionType,
      resourceType: draft.resourceUrl ? _inferResourceType(draft.resourceUrl, draft.template) : meta.resourceType,
      resourceUrl: draft.resourceUrl || '',
      deadlineAt: draft.deadlineAt || null,
      announceInChat: true,
    });
    session.lastResult = {
      ...lastResult,
      draftActions: (lastResult?.draftActions || []).filter((item) => item.skillId !== skillId),
    };
    _syncExecutionHistory();
    notify();
    return created;
  }

  // ── 消息操作 ──
  function sendUserMessage(text, convId, options = {}) {
    if (!text.trim()) return;

    const convIdNorm = _ensureConversationId(convId);
    if (!convIdNorm) return;
    const conv = _conversations.find(c => c.id === convIdNorm);
    // 确保会话存在
    _ensureConversation(convIdNorm);
    const conversation = _conversations.find(c => c.id === convIdNorm);

    const isAgentChat = conversation && conversation.type === 'agent';
    const isPersonalWorkspace = conversation && conversation.scope === 'personal_workspace';
    const hasAgentMention = _isAgentMention(text);

    // 对齐 Vue 版：群聊中 @炎图AI助手 时，发送给 IRC 服务器的文本需要加零宽空格，
    // 防止服务端识别并剥离 @炎图AI助手 触发词
    const isGroupWithAgentMention = !isAgentChat && !isPersonalWorkspace && hasAgentMention;
    const trimmedText = text.trim();
    const outboundText = isGroupWithAgentMention
      ? _sanitizeAgentMentionForServer(trimmedText)
      : trimmedText;

    // 添加到 Outbox
    const outboxEntry = _addToOutbox(convIdNorm, outboundText);

    // 创建本地乐观更新消息（内容与 outboundText 一致，否则 echo 回来文本比对失败）
    const localMsgId = _mkId('u');
    const localMsg = {
      id: localMsgId,
      conversationId: convIdNorm,
      author: 'user',
      authorName: _getUserName(),
      content: outboundText,
      attachments: Array.isArray(options.attachments) ? options.attachments : [],
      status: 'complete',
      delivery: 'sending',
      outboxId: outboxEntry.id,
      createdAt: Date.now(),
      kind: 'default',
    };

    if (!_messagesByConv[convIdNorm]) _messagesByConv[convIdNorm] = [];
    _messagesByConv[convIdNorm].push(localMsg);
    notify();

    // 记录 pending echo 用于后续匹配（key 必须与 outboundText 一致）
    const echoKey = `${convIdNorm}:${outboundText}`;
    _pendingEchoes[echoKey] = {
      outboxId: outboxEntry.id,
      createdAt: Date.now(),
      attachments: Array.isArray(options.attachments) ? options.attachments : [],
    };

    if (isPersonalWorkspace || hasAgentMention) {
      void _createPersonalReminderTaskFromMessage({
        text: trimmedText,
        conversationId: convIdNorm,
        messageId: localMsgId,
        createdAt: localMsg.createdAt,
      });
    }

    // 通过 Transport 发送消息（使用 outboundText，群聊 @炎图 含零宽空格）
    if (_transport && _transport.sendMessage) {
      outboxEntry.status = 'sending';
      _transport.sendMessage({
        conversationId: convIdNorm,
        text: outboundText,
        attachments: Array.isArray(options.attachments) ? options.attachments : [],
      });
      // 启动发送超时计时：超时未收到 echo/ack 则标记失败
      _startSendTimer(outboxEntry.id, convIdNorm);
    }

    // ── 调用 Agent API 获取 AI 回复 ──
    // 只有 agent 会话、个人工作空间、或群聊中 @炎图AI助手 时才调用 AI
    if (isAgentChat || isPersonalWorkspace || hasAgentMention) {
      // 对齐 Vue 版 stripAgentMention：群聊 @炎图 时剥离触发词后再传给 Agent
      const agentText = isGroupWithAgentMention
        ? (_stripAgentMention(trimmedText) || trimmedText)
        : trimmedText;
      _requestAgentReply(convIdNorm, agentText, { shouldBroadcast: isGroupWithAgentMention });
    }
  }

  /**
   * 手动重发失败消息（对齐 Vue 版 sessionStore.retrySend）
   * 根据 localMessageId 找到对应消息和 outbox 条目，重置状态后重新发送
   */
  function retrySend(localMessageId, conversationId) {
    const convId = _ensureConversationId(conversationId);
    if (!convId) return;
    const msgs = _messagesByConv[convId];
    if (!msgs) return;
    const msg = msgs.find(m => m.id === localMessageId && m.author === 'user');
    if (!msg) return;

    // 仅允许重发失败消息
    if (msg.delivery !== 'failed') return;

    // 重置 outbox 条目状态
    if (msg.outboxId) {
      const entry = _outbox.find(e => e.id === msg.outboxId);
      if (entry) {
        entry.status = 'sending';
        entry.retryCount = 0;
      }
    }

    // 重置消息状态
    msg.delivery = 'sending';
    notify();

    // 重新通过 Transport 发送
    if (_transport && _transport.sendMessage) {
      _transport.sendMessage({
        conversationId: convId,
        text: msg.content,
        attachments: msg.attachments || [],
      });
      if (msg.outboxId) _startSendTimer(msg.outboxId, convId);
    }
  }

  /**
   * 调用 Agent API 获取 AI 回复（流式）
   * 对齐 Vue 版 localAgentRequestService：
   *   1. 流式展示 agent 回复
   *   2. 完成后解析响应文本提取 H5 卡片附件，挂到 agent 消息上
   *   3. 群聊 @炎图 时调用 AGENT_BROADCAST 广播给其他成员（发送者本地已展示，不依赖 222）
   */
  async function _requestAgentReply(convId, userText, options = {}) {
    const { shouldBroadcast = false } = options;
    // 先创建占位的 streaming agent 消息
    const agentMsgId = _mkId('a');
    const agentMsg = {
      id: agentMsgId,
      conversationId: convId,
      author: 'agent',
      authorName: '炎图AI助手',
      content: '',
      attachments: [],
      status: 'streaming',
      createdAt: Date.now(),
      kind: 'default',
    };

    if (!_messagesByConv[convId]) _messagesByConv[convId] = [];
    _messagesByConv[convId].push(agentMsg);
    _agentStreamingMsgs[convId] = { messageId: agentMsgId, content: '' };
    notify();

    try {
      const sessionId = await AuthStore.getSessionId() || _readStoredSessionId() || '0';

      // 对齐 Vue 版 buildConversationContext：提取最近对话上下文传给 OpenClaw
      const conversationContext = _buildConversationContextForAgent(convId, userText);

      // 对齐 Vue 版 buildStreamReplyFromSources：收集 tool_result 文本，
      // OpenClaw 生成 H5 卡片后上传工具返回的 URL 在 tool_result 中
      const toolResultTexts = [];

      await ChatApi.callAgent(userText, convId, sessionId, {
        onDelta(delta, fullText) {
          // 更新 agent 消息内容
          agentMsg.content = fullText;
          _agentStreamingMsgs[convId] = { messageId: agentMsgId, content: fullText };
          notify();
        },
        onToolResult(content) {
          // 对齐 Vue 版 onToolResult：收集 tool_result 用于后续附件提取
          toolResultTexts.push(content);
        },
        async onDone(finalText) {
          const replyText = finalText || agentMsg.content;
          // 对齐 Vue 版 buildStreamReplyFromSources：
          // 优先从 tool_result 中提取结构化附件（H5 卡片 URL），
          // 再从 final text 和 accumulated text 中补充提取
          const toolResultParsed = toolResultTexts.flatMap(t => _parseAgentReplyText(t).attachments);
          const replyParsed = _parseAgentReplyText(replyText);
          // 合并去重附件（tool_result 附件优先）
          const seenUrls = new Set();
          const allAttachments = [];
          for (const att of [...toolResultParsed, ...replyParsed.attachments]) {
            if (seenUrls.has(att.url)) continue;
            seenUrls.add(att.url);
            allAttachments.push(att);
          }
          agentMsg.content = replyParsed.text;
          agentMsg.attachments = allAttachments;

          // 对齐 Vue 版 stripAttachedInlineLinks：从回复文本中剥离已作为附件的 URL
          agentMsg.content = _stripAttachedInlineLinks(agentMsg.content, allAttachments);

          // 对齐 Vue 版 sanitizeAgentVisibleReply：有 H5 卡片附件时简化回复文本
          agentMsg.content = _sanitizeAgentReplyText(agentMsg.content, allAttachments);

          // 对齐 Vue 版 prefixRequesterMention：群聊 @炎图 时 agent 回复前加 @发送者
          if (shouldBroadcast) {
            const requesterPhone = AuthStore.getPhone() || '';
            const mention = requesterPhone ? `@${requesterPhone} ` : '';
            if (mention && agentMsg.content && !agentMsg.content.startsWith(mention)) {
              agentMsg.content = `${mention}${agentMsg.content}`;
            }
          }

          agentMsg.status = 'complete';
          delete _agentStreamingMsgs[convId];
          _recordAgentResult(convId, _buildStructuredAgentResult(userText, agentMsg.content));
          notify();

          // 对齐 Vue 版 publishGroupAgentReply：群聊 @炎图 时广播给其他成员
          // 生成 deliveryId 标记本地 agent 消息，广播时用 relay envelope 包装，
          // 接收方通过 deliveryId 去重，避免发送者重复展示。
          if (shouldBroadcast && agentMsg.content) {
            const deliveryId = _createGroupAgentDeliveryId(convId);
            agentMsg.agentDeliveryId = deliveryId;
            const relayMessage = _encodeGroupAgentReplyRelayEnvelope(deliveryId, agentMsg.content, allAttachments);
            try {
              await ChatApi.broadcastAgentReply(convId, relayMessage, sessionId, allAttachments);
              console.log('[ChatStore] AGENT_BROADCAST ok', { convId, deliveryId, msgLen: agentMsg.content.length, attachments: allAttachments.length });
            } catch (err) {
              console.warn('[ChatStore] AGENT_BROADCAST failed:', err?.message || err);
            }
          }
        },
        onError(err) {
          agentMsg.content = agentMsg.content || `抱歉，AI 回复失败：${err.message}`;
          agentMsg.status = 'complete';
          delete _agentStreamingMsgs[convId];
          _recordAgentError(convId, agentMsg.content);
          notify();
        },
      }, { conversationContext });
    } catch (err) {
      agentMsg.content = agentMsg.content || '抱歉，AI 服务暂时不可用';
      agentMsg.status = 'complete';
      delete _agentStreamingMsgs[convId];
      _recordAgentError(convId, agentMsg.content);
      notify();
    }
  }

  function _sendMessageLocal(convId, text) {
    const msg = {
      id: _mkId('u'),
      conversationId: convId,
      author: 'user',
      authorName: _getUserName(),
      content: text,
      status: 'complete',
      delivery: 'sent',
      createdAt: Date.now(),
      kind: 'default',
    };

    if (!_messagesByConv[convId]) _messagesByConv[convId] = [];
    _messagesByConv[convId].push(msg);
    notify();
  }

  // ── 历史消息加载 ──
  async function loadConversationHistory(convId, limit = 50) {
    const convIdNorm = _ensureConversationId(convId);
    if (!convIdNorm) return;

    try {
      const msgs = await ChatApi.getConversationHistory(convIdNorm, limit);
      if (Array.isArray(msgs)) {
        const currentMessages = _messagesByConv[convIdNorm] || [];
        const conversation = _conversations.find(c => c.id === convIdNorm);
        const shouldPreserveLocalMessages =
          !_shouldHydrateConversation(conversation) ||
          (msgs.length === 0 && currentMessages.some(msg => msg.delivery === 'sending' || msg.status === 'streaming'));
        if (shouldPreserveLocalMessages) {
          return currentMessages;
        }
        _messagesByConv[convIdNorm] = msgs.map(msg => ({
          id: msg.id || _mkId('m'),
          conversationId: convIdNorm,
          author: msg.author || msg.sender || 'user',
          authorName: msg.authorName || msg.senderName || '用户',
          content: msg.content || msg.text || '',
          status: 'complete',
          createdAt: msg.createdAt || msg.created_at || Date.now(),
          kind: 'default',
        }));
        notify();
      }
    } catch (e) {
      // ignore
    }
  }

  // ── 群聊管理 ─
  /**
   * 创建群聊（对齐 Vue 版 onCreateGroupConfirm: API → createGroupConversation → activate）
   */
  async function createGroupConversation(name) {
    if (!name || !name.trim()) {
      throw new Error('群聊名称不能为空');
    }

    const trimmedName = name.trim();

    // 1. 服务端创建（对齐 Vue 版 createConversationApi）
    const result = await ChatApi.createConversation(trimmedName, 'group');
    const serverId = _ensureConversationId(result?.conversation?.id);
    if (!serverId) {
      throw new Error('创建群聊失败');
    }
    const conv = _createLocalGroupConversation(trimmedName, serverId, {
      topic: '开始群组协作',
      ownerUserId: String(result?.conversation?.ownerUserId || '').trim() || undefined,
      createdAt: result?.conversation?.createdAt,
    });
    if (!conv) {
      throw new Error('创建群聊失败');
    }
    _saveGroupsToStorage();
    if (_status === 'connected' && _transport && _transport.joinConversation) {
      _transport.joinConversation(conv.id);
    }
    void _fetchRoomInfo(conv.id);
    notify();
    return conv.id;
  }

  // ── 成员数据规范化（服务端返回 snake_case → 前端 camelCase）──
  function _normalizeMemberPhone(raw) {
    if (!raw || typeof raw !== 'object') return undefined;
    const nestedUser = raw.user && typeof raw.user === 'object' ? raw.user : null;
    return (
      String(raw.phone || raw.mobile || raw.mobile_phone || raw.mobilePhone || raw.user_phone || raw.userPhone || raw.member_phone || raw.memberPhone || raw.phone_number || raw.phoneNumber || '').trim() ||
      (nestedUser ? _normalizeMemberPhone(nestedUser) : '') ||
      undefined
    );
  }

  function _normalizeNode(raw) {
    if (!raw || typeof raw !== 'object') return undefined;
    const nodeId = String(raw.node_id || raw.nodeId || raw.id || '').trim();
    if (!nodeId) return undefined;
    return {
      nodeId,
      nodeType: String(raw.node_type || raw.nodeType || '').trim() || undefined,
      name: String(raw.name || raw.display_name || raw.displayName || '').trim() || undefined,
      deviceType: String(raw.device_type || raw.deviceType || '').trim() || undefined,
    };
  }

  function _normalizeMember(raw) {
    if (!raw || typeof raw !== 'object') return null;
    const memberKind = String(raw.member_kind || raw.memberKind || raw.kind || '').trim().toLowerCase();
    const role = String(raw.role || '').trim() || 'member';
    const status = String(raw.status || '').trim() || undefined;
    const joinedAt = _normalizeTimestamp(raw.joined_at || raw.joinedAt || raw.created_at || raw.createdAt, 0) || undefined;

    if (memberKind === 'user') {
      const userId = String(raw.user_id || raw.userId || '');
      if (!userId) return null;
      return {
        memberKind: 'user',
        userId,
        phone: _normalizeMemberPhone(raw),
        role,
        status,
        joinedAt,
      };
    }
    if (memberKind === 'phone') {
      const phone = String(raw.phone || '').trim();
      if (!phone) return null;
      return {
        memberKind: 'phone',
        phone,
        role,
        status,
        joinedAt,
      };
    }
    if (memberKind === 'node') {
      const node = _normalizeNode(raw.node);
      const nodeId = String(raw.node_id || raw.nodeId || node?.nodeId || '').trim();
      if (!nodeId) return null;
      return {
        memberKind: 'node',
        nodeId,
        role,
        status,
        joinedAt,
        node: {
          ...(node || { nodeId }),
        },
      };
    }
    return null;
  }

  function _normalizeRoomMembers(rawArray) {
    if (!Array.isArray(rawArray)) return [];
    return rawArray.map(_normalizeMember).filter(Boolean);
  }

  /**
   * 直接从服务端 /room/info 获取成员列表（绕过缓存），含 phone 字段。
   * 邀请/移除后用此函数。包含重试：服务端传播延迟时首次可能缺 phone，等待后重试。
   */
  async function _fetchRoomMembersFromServer(roomId) {
    const roomIdNorm = roomId.startsWith('#') ? roomId : `#${roomId}`;

    async function _fetchOnce() {
      const info = await ChatApi.getRoomInfo(roomIdNorm);
      const conv = _conversations.find((item) => item.id === roomIdNorm);
      const serverName = String(info.name || '').trim();
      const fallbackName = roomIdNorm.replace(/^#/, '');
      if (conv && serverName && serverName !== roomIdNorm && serverName !== fallbackName && conv.name !== serverName) {
        conv.name = serverName;
      }
      const ownerFromMembers = (info.members || []).find(m => m.role === 'owner');
      const ownerUserId = String(info.ownerUserId || (ownerFromMembers?.user_id || '')).trim();
      if (conv && ownerUserId && ownerUserId !== String(conv.ownerUserId || '').trim()) {
        conv.ownerUserId = ownerUserId;
      }
      const members = _normalizeRoomMembers(info.members || []);
      if (members.length) {
        _roomMembersByConv[roomIdNorm] = members;
      }
      const identityMap = {};
      members.forEach((member) => {
        if (member.memberKind !== 'user' || !member.phone) return;
        if (member.userId) identityMap[member.userId] = member.phone;
        identityMap[member.phone] = member.phone;
      });
      if (Object.keys(identityMap).length) {
        _memberIdentityMapByConversationId[roomIdNorm] = identityMap;
      }
      _saveGroupsToStorage();
      return members;
    }

    let members = await _fetchOnce();
    // 检查是否有 user 类型成员缺少 phone（服务端传播延迟）
    const hasPhonelessUser = members.some(m => m.memberKind === 'user' && !m.phone);
    if (hasPhonelessUser) {
      // 等待 600ms 后重试一次，让服务端完成 phone 数据传播
      await new Promise(resolve => setTimeout(resolve, 600));
      const retried = await _fetchOnce();
      if (retried.length) members = retried;
    }
    return members;
  }

  async function getRoomMembers(roomId) {
    const roomIdNorm = roomId.startsWith('#') ? roomId : `#${roomId}`;

    // 优先复用已缓存的成员数据
    if (_roomMembersByConv[roomIdNorm] && _roomMembersByConv[roomIdNorm].length) {
      return _roomMembersByConv[roomIdNorm];
    }

    // 如果 _fetchRoomInfo 正在进行，等待它完成后再读缓存
    const conv = _conversations.find((item) => item.id === roomIdNorm);
    if (conv && conv._fetchingRoomInfoPromise) {
      try { await conv._fetchingRoomInfoPromise; } catch {}
      if (_roomMembersByConv[roomIdNorm] && _roomMembersByConv[roomIdNorm].length) {
        return _roomMembersByConv[roomIdNorm];
      }
    }

    return _fetchRoomMembersFromServer(roomIdNorm);
  }

  async function inviteRoomMember(roomIdOrPayload, memberKind, targetId, role = 'member') {
    const payload = typeof roomIdOrPayload === 'object' && roomIdOrPayload ? roomIdOrPayload : null;
    const roomIdNorm = String(payload?.roomId || roomIdOrPayload || '').trim().replace(/^([^#])/, '#$1');
    const memberKindNorm = String(payload?.memberKind || memberKind || 'phone').trim() || 'phone';
    const targetIdNorm = String(
      payload?.phone ||
      payload?.userId ||
      payload?.nodeId ||
      targetId ||
      '',
    ).trim();
    try {
      await ChatApi.inviteMember(roomIdNorm, memberKindNorm, targetIdNorm, String(payload?.role || role || 'member').trim() || 'member');
      // 邀请接口返回的 members 缺 phone 字段，必须调 /room/info 获取含 phone 的完整成员数据
      delete _roomMembersByConv[roomIdNorm];
      return await _fetchRoomMembersFromServer(roomIdNorm);
    } catch (e) {
      console.error('[ChatStore] Failed to invite room member:', e);
      throw e;
    }
  }

  async function removeRoomMember(roomIdOrPayload, memberKind, targetId) {
    const payload = typeof roomIdOrPayload === 'object' && roomIdOrPayload ? roomIdOrPayload : null;
    const roomIdNorm = String(payload?.roomId || roomIdOrPayload || '').trim().replace(/^([^#])/, '#$1');
    const memberKindNorm = String(payload?.memberKind || memberKind || 'user').trim() || 'user';
    const targetIdNorm = String(
      payload?.userId ||
      payload?.nodeId ||
      targetId ||
      '',
    ).trim();
    try {
      await ChatApi.removeMember(roomIdNorm, memberKindNorm, targetIdNorm);
      // 移除接口返回的 members 缺 phone 字段，必须调 /room/info 获取含 phone 的完整成员数据
      delete _roomMembersByConv[roomIdNorm];
      return await _fetchRoomMembersFromServer(roomIdNorm);
    } catch (e) {
      console.error('[ChatStore] Failed to remove room member:', e);
      throw e;
    }
  }

  // ── Agent 技能 ──
  function getAgentSkills() { return _agentSkills; }
  function getSkillState(skillId) { return !!_skillStates[skillId]; }
  function isSkillEnabled(skillId) { return !!_skillStates[skillId]; }
  function toggleSkill(skillId) {
    _skillStates[skillId] = !_skillStates[skillId];
    _saveDisabledSkillIds();
    notify();
  }
  function enableAllSkills() {
    _agentSkills.forEach(s => { _skillStates[s.id] = true; });
    _saveDisabledSkillIds();
    notify();
  }
  function disableAllSkills() {
    _agentSkills.forEach(s => { _skillStates[s.id] = false; });
    _saveDisabledSkillIds();
    notify();
  }
  function getAgentSession(conversationId) {
    return _agentSessions[_ensureConversationId(conversationId)] || null;
  }
  function getWorkspaceDraftActions(conversationId) {
    return getAgentSession(conversationId)?.lastResult?.draftActions || [];
  }
  function getBoundDevices() { return _boundDevices; }
  function getPendingDeviceSessions() { return _pendingDeviceSessions; }
  function parseDeviceBindPayload(raw) { return _parseDeviceBindPayload(raw); }
  function getBindableConversations() {
    return _conversations.filter((item) => item.type === 'group');
  }
  function getExecutionHistory() { return _executionHistory; }
  function getLastSeenNotificationsAt() { return _lastSeenNotificationsAt; }
  function setLastSeenNotificationsAt(ts) { _lastSeenNotificationsAt = ts; notify(); }

  // ── 订阅 ──
  function subscribe(fn) {
    listeners.add(fn);
    return () => { listeners.delete(fn); };
  }

  // ── 清理 ──
  function destroy() {
    if (_transportUnsub) {
      _transportUnsub();
      _transportUnsub = null;
    }
    if (_transport) {
      _transport.disconnect?.();
      _transport = null;
    }
    _outbox = [];
    _pendingEchoes = {};
    _agentStreamingMsgs = {};
    // 清理所有发送超时计时器
    for (const id of Object.keys(_sendTimers)) {
      clearTimeout(_sendTimers[id]);
    }
    _sendTimers = {};
  }

  async function resetForAuthChange() {
    // 对齐 Vue 版：清理当前账号的本地缓存（按账号隔离的 key）
    const oldPersistKey = _persistKey();
    const oldPostsPersistKey = _postsPersistKey();
    const oldPersonalTasksPersistKey = _personalTasksPersistKey();
    const oldLocallyDeletedPersistKey = _locallyDeletedPersistKey();
    destroy();

    _conversations = [];
    _activeConversationId = '';
    _messagesByConv = {};
    _postsByConv = {};
    _personalTasks = [];
    _agentSessions = {};
    _boundDevices = [];
    _pendingDeviceSessions = [];
    _lastSeenNotificationsAt = 0;
    _groupInvitationNotifications = [];
    _executionHistory = [];
    _status = 'disconnected';
    _serverPersonalWorkspaceId = '';
    _syncInProgress = false;
    _locallyDeletedConversationIds = {};

    try {
      localStorage.removeItem(oldPersistKey);
      localStorage.removeItem(oldPostsPersistKey);
      localStorage.removeItem(oldPersonalTasksPersistKey);
      localStorage.removeItem(oldLocallyDeletedPersistKey);
      localStorage.removeItem(DEVICE_PENDING_SESSIONS_STORAGE_KEY);
    } catch {
      // ignore localStorage cleanup failure
    }

    notify();
  }

  return {
    init,
    subscribe,
    destroy,
    resetForAuthChange,
    getConversations,
    getActiveConversationId,
    getActiveConversation,
    setActiveConversation,
    getMessages,
    getActiveMessages,
    getStatus,
    getConversationUnreadCount,
    getTotalUnreadCount,
    clearMarkRead,
    getPosts,
    createGroupPost,
    loadGroupPosts,
    loadAllGroupPosts,
    respondToGroupPost,
    submitHomeworkPostResponse,
    closeGroupPost,
    parseSharedPostMessageContent: _parseSharedPostMessageContent,
    getPersonalTasksForCurrentUser,
    getGroupInvitationNotifications,
    loadPersonalTasksFromServer,
    completePersonalTask,
    cancelPersonalTask,
    reschedulePersonalTask,
    createPersonalTask,
    refreshBoundDevices,
    createDeviceBindSession,
    getDeviceBindSession,
    confirmDeviceBind,
    unbindDevice,
    getBoundDevices,
    getPendingDeviceSessions,
    parseDeviceBindPayload,
    getBindableConversations,
    announceDeviceBound,
    sendUserMessage,
    retrySend,
    loadConversationHistory,
    createGroupConversation,
    getRoomMembers,
    inviteRoomMember,
    removeRoomMember,
    getAgentSkills,
    getSkillState,
    isSkillEnabled,
    toggleSkill,
    enableAllSkills,
    disableAllSkills,
    getAgentSession,
    getWorkspaceDraftActions,
    publishWorkspaceDraft,
    getExecutionHistory,
    getLastSeenNotificationsAt,
    setLastSeenNotificationsAt,
    deleteConversationFromList,
    isConversationLocallyDeleted,
  };
})();

export default ChatStore;
