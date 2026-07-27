/* ================================================================
   AgentChat 重构版 - 视图状态缓存（View State）
   ----------------------------------------------------------------
   职责：仅保存业务逻辑回传的视图数据，供页面同步读取渲染。
   - 不包含任何业务逻辑（无网络请求、无协议处理）
   - 仅由 bridge/index.js 在收到 postMessage 响应/推送时更新
   - 页面通过 subscribe 订阅变化以触发重渲染
   ================================================================ */

const _listeners = new Set();

// ── 聊天核心状态 ──
let _conversations = [];
let _messagesByConv = new Map(); // convId -> msg[]
let _activeConversationId = null;
let _postsByConv = new Map(); // convId -> post[]
let _roomMembersByConv = new Map(); // convId -> member[]
let _topicsByConv = new Map(); // convId -> topic[]（群聊话题）
let _status = 'disconnected'; // disconnected | connecting | connected | error
let _interactiveBridgeState = { connected: false };

// ── 个人任务 ──
let _personalTasks = [];

// ── 设备 ──
let _boundDevices = [];
let _pendingDeviceSessions = [];

// ── Agent / 工作台 ──
let _agentSkills = [];
let _skillStates = {}; // skillId -> boolean
let _executionHistory = [];
let _workspaceDraftActions = []; // [{ skillId, targetId, sourceConvId, ... }]

// ── 通知 ──
let _groupInvitationNotifications = [];
let _pushNotifications = []; // C++ 推送的通知（notice/system/device_status 等）
let _lastSeenNotificationsAt = Date.now();

// ── 鉴权视图（由 auth 通道回填）──
let _auth = {
  isLoggedIn: false,
  phone: '',
  userId: '',
  sessionId: '',
  jwt: '',
};

// ── 本地删除标记（纯前端视图态，用于乐观删除）──
const _locallyDeletedConv = new Set();

function notify() {
  for (const fn of _listeners) {
    try { fn(); } catch (e) { console.warn('[state] listener error', e); }
  }
}

function subscribe(fn) {
  _listeners.add(fn);
  return () => _listeners.delete(fn);
}

// ── 会话 ──
function getConversations() {
  return _conversations.filter((c) => !_locallyDeletedConv.has(c.id));
}
function getActiveConversationId() { return _activeConversationId; }
function setActiveConversationId(id) { _activeConversationId = id || null; }
function getActiveConversation() {
  if (!_activeConversationId) return null;
  return _conversations.find((c) => c.id === _activeConversationId) || null;
}
function getMessages(convId) {
  return _messagesByConv.get(convId) || [];
}
function getActiveMessages() {
  return _activeConversationId ? getMessages(_activeConversationId) : [];
}
function getConversationUnreadCount(convId) {
  const c = _conversations.find((x) => x.id === convId);
  return c ? (c.unreadCount || 0) : 0;
}
function getTotalUnreadCount() {
  return _conversations.reduce((sum, c) => sum + (c.unreadCount || 0), 0);
}
function clearMarkRead(convId) {
  const c = _conversations.find((x) => x.id === convId);
  if (c) c.unreadCount = 0;
}
function getStatus() { return _status; }
function markLocallyDeleted(convId) { _locallyDeletedConv.add(convId); }

// ── 帖子/任务 ──
function getPosts(convId) { return _postsByConv.get(convId) || []; }
function getRoomMembers(convId) { return _roomMembersByConv.get(convId) || []; }

// ── 群聊话题 ──
function getTopics(convId) { return _topicsByConv.get(convId) || []; }

// ── 个人任务 ──
function getPersonalTasksForCurrentUser() { return _personalTasks; }

// ── 设备 ──
function getBoundDevices() { return _boundDevices; }
function getPendingDeviceSessions() { return _pendingDeviceSessions; }
function getBindableConversations() {
  return _conversations.filter((c) => c.type === 'group');
}

// ── Agent / 工作台 ──
function getAgentSkills() { return _agentSkills; }
function isSkillEnabled(id) { return !!_skillStates[id]; }
function getExecutionHistory() { return _executionHistory; }
function getWorkspaceDraftActions() { return _workspaceDraftActions; }
function getInteractiveBridgeState() { return _interactiveBridgeState; }

// ── 通知 ──
function getGroupInvitationNotifications() { return _groupInvitationNotifications; }
function getPushNotifications() { return _pushNotifications; }
function getLastSeenNotificationsAt() { return _lastSeenNotificationsAt; }
function setLastSeenNotificationsAt(ts) { _lastSeenNotificationsAt = ts || Date.now(); }

// ── 鉴权 ──
function getAuth() { return _auth; }
function getUserId() { return _auth.userId; }
function getPhone() { return _auth.phone; }
function getSessionId() { return _auth.sessionId; }
function isLoggedIn() { return _auth.isLoggedIn; }

// ── 工具：解析消息中的帖子分享信封（对齐 agent 项目 postShareEnvelope.ts）──
// 支持两种信封格式（均为 HTML 注释 + base64 编码）：
// - <!-- agent-chat:forward-attachment <base64> -->：转发 ai_card/webview/image 附件
// - <!-- agent-chat:post-share <base64> -->：转发 native_post 帖子
const _POST_SHARE_START = '<!-- agent-chat:post-share ';
const _POST_SHARE_END = ' -->';
const _FWD_ATTACH_START = '<!-- agent-chat:forward-attachment ';
const _FWD_ATTACH_END = ' -->';

function _fromBase64Utf8(value) {
  try {
    const binary = atob(value);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) { bytes[i] = binary.charCodeAt(i); }
    return new TextDecoder().decode(bytes);
  } catch (e) { return ''; }
}

/**
 * 从可见文本中剥离 agent-chat HTML 注释信封，避免接收端把 base64 当作普通文本渲染。
 * 对齐 agent 项目 stripAgentChatEnvelope()。
 */
function _stripAgentChatEnvelope(content) {
  const source = String(content ?? '');
  let cursor = 0;
  let result = '';
  while (cursor < source.length) {
    const fwdStart = source.indexOf(_FWD_ATTACH_START, cursor);
    const postStart = source.indexOf(_POST_SHARE_START, cursor);
    const candidates = [];
    if (fwdStart !== -1) candidates.push({ start: fwdStart, endTag: _FWD_ATTACH_END });
    if (postStart !== -1) candidates.push({ start: postStart, endTag: _POST_SHARE_END });
    if (!candidates.length) break;
    candidates.sort((a, b) => { return a.start - b.start; });
    const pick = candidates[0];
    result += source.slice(cursor, pick.start);
    const end = source.indexOf(pick.endTag, pick.start);
    if (end === -1) { cursor = source.length; break; }
    cursor = end + pick.endTag.length;
  }
  result += source.slice(cursor);
  return result.replace(/\s+/g, ' ').trim();
}

function parseSharedPostMessageContent(rawContent, convId) {
  if (!rawContent || typeof rawContent !== 'string') return null;
  const source = String(rawContent);

  // 查找信封标记（取两者中更靠前的）
  const fwdIdx = source.indexOf(_FWD_ATTACH_START);
  const postIdx = source.indexOf(_POST_SHARE_START);
  const useFwd = fwdIdx !== -1 && (postIdx === -1 || fwdIdx < postIdx);
  const usePost = postIdx !== -1 && (fwdIdx === -1 || postIdx < fwdIdx);

  if (!useFwd && !usePost) {
    // 兼容旧格式：c:agentchat.collaboration {json}
    const marker = 'c:agentchat.collaboration';
    const idx = source.indexOf(marker);
    if (idx < 0) return null;
    try {
      const json = source.slice(idx + marker.length).trim();
      const obj = JSON.parse(json);
      return { content: source.slice(0, idx).trim(), attachments: obj.attachments, _convId: convId };
    } catch (e) { return null; }
  }

  const startIdx = useFwd ? fwdIdx : postIdx;
  const startTag = useFwd ? _FWD_ATTACH_START : _POST_SHARE_START;
  const endTag = useFwd ? _FWD_ATTACH_END : _POST_SHARE_END;

  const endIdx = source.indexOf(endTag, startIdx);
  if (endIdx === -1) {
    return { content: _stripAgentChatEnvelope(source) };
  }

  const token = source.slice(startIdx + startTag.length, endIdx).trim();
  const before = source.slice(0, startIdx).trimEnd();
  const after = source.slice(endIdx + endTag.length).trimStart();
  const displayContent = [before, after].filter(Boolean).join('\n').trim();

  try {
    const payload = JSON.parse(_fromBase64Utf8(token));

    if (useFwd && payload.version === 1 && Array.isArray(payload.attachments)) {
      return { content: displayContent, attachments: payload.attachments, _convId: convId };
    }

    if (usePost && payload.version === 1 && payload.post) {
      const post = Object.assign({}, payload.post, { conversationId: convId || payload.post.conversationId });
      return { content: displayContent, post, attachments: post.attachments || [], _convId: convId };
    }
  } catch (e) { /* fallthrough */ }

  return { content: _stripAgentChatEnvelope(source) };
}

// ── 设备绑定 payload 解析（纯本地字符串解析）──
function parseDeviceBindPayload(raw) {
  if (!raw) return null;
  const s = String(raw).trim();
  try {
    if (s.startsWith('bt_')) return { bindToken: s };
    if (s.startsWith('agentchat://')) {
      const url = new URL(s);
      const bindToken = url.searchParams.get('bind_token') || url.searchParams.get('bindToken');
      return bindToken ? { bindToken } : null;
    }
    const obj = JSON.parse(s);
    if (obj && obj.bindToken) return obj;
    return null;
  } catch (e) { return null; }
}

// ── 内部更新方法（仅供 bridge/index.js 调用）──
const _setters = {
  conversations(v) { _conversations = v || []; },
  mergeConversations(v) {
    const map = new Map(_conversations.map((c) => [c.id, c]));
    for (const c of (v || [])) map.set(c.id, { ...map.get(c.id), ...c });
    _conversations = [...map.values()];
  },
  messages(convId, list) { _messagesByConv.set(convId, list || []); },
  appendMessage(convId, msg) {
    const list = _messagesByConv.get(convId) || [];
    list.push(msg);
    _messagesByConv.set(convId, list);
  },
  updateMessage(convId, msgId, patch) {
    const list = _messagesByConv.get(convId) || [];
    const idx = list.findIndex((m) => m.id === msgId);
    if (idx >= 0) {
      list[idx] = { ...list[idx], ...patch };
      _messagesByConv.set(convId, list);
    }
  },
  removeMessage(convId, msgId) {
    const list = _messagesByConv.get(convId) || [];
    const idx = list.findIndex((m) => m.id === msgId);
    if (idx >= 0) {
      list.splice(idx, 1);
      _messagesByConv.set(convId, list);
    }
  },
  posts(convId, list) { _postsByConv.set(convId, list || []); },
  roomMembers(convId, list) { _roomMembersByConv.set(convId, list || []); },
  topics(convId, list) { _topicsByConv.set(convId, list || []); },
  addTopic(convId, topic) {
    const list = _topicsByConv.get(convId) || [];
    list.unshift(topic);
    _topicsByConv.set(convId, list);
  },
  updateTopic(convId, topicId, patch) {
    const list = _topicsByConv.get(convId) || [];
    const idx = list.findIndex((t) => t.id === topicId);
    if (idx >= 0) list[idx] = { ...list[idx], ...patch };
    _topicsByConv.set(convId, list);
  },
  status(s) { _status = s; },
  personalTasks(v) { _personalTasks = v || []; },
  appendPersonalTask(task) {
    // 去重：同一 createdFromMessageId + ownerUserId 不重复创建
    const exists = _personalTasks.some((t) => {
      return t.id === task.id ||
        (task.createdFromMessageId && t.createdFromMessageId === task.createdFromMessageId);
    });
    if (exists) return;
    _personalTasks = [_personalTasks, task].flat ? _personalTasks.concat([task]) : _personalTasks.concat([task]);
  },
  updatePersonalTask(taskId, patch) {
    const idx = _personalTasks.findIndex((t) => { return t.id === taskId; });
    if (idx >= 0) {
      _personalTasks[idx] = Object.assign({}, _personalTasks[idx], patch);
    }
  },
  boundDevices(v) { _boundDevices = v || []; },
  pendingDeviceSessions(v) { _pendingDeviceSessions = v || []; },
  agentSkills(v) { _agentSkills = v || []; },
  skillStates(v) { _skillStates = v || {}; },
  toggleSkill(id) { _skillStates[id] = !_skillStates[id]; },
  executionHistory(v) { _executionHistory = v || []; },
  workspaceDraftActions(v) { _workspaceDraftActions = v || []; },
  groupInvitationNotifications(v) { _groupInvitationNotifications = v || []; },
  pushNotifications(v) { _pushNotifications = v || []; },
  addPushNotification(n) { _pushNotifications = [n, ..._pushNotifications].slice(0, 100); },
  interactiveBridgeState(v) { _interactiveBridgeState = v || { connected: false }; },
  auth(patch) { _auth = { ..._auth, ...patch }; },
  resetAll() {
    _conversations = [];
    _messagesByConv = new Map();
    _activeConversationId = null;
    _postsByConv = new Map();
    _roomMembersByConv = new Map();
    _topicsByConv = new Map();
    _status = 'disconnected';
    _personalTasks = [];
    _boundDevices = [];
    _pendingDeviceSessions = [];
    _agentSkills = [];
    _skillStates = {};
    _executionHistory = [];
    _workspaceDraftActions = [];
    _groupInvitationNotifications = [];
    _pushNotifications = [];
    _interactiveBridgeState = { connected: false };
    _locallyDeletedConv.clear();
  },
  resetAuth() {
    _auth = { isLoggedIn: false, phone: '', userId: '', sessionId: '', jwt: '' };
  },
  notify,
};

export default {
  subscribe,
  // getters
  getConversations, getActiveConversationId, setActiveConversationId, getActiveConversation,
  getMessages, getActiveMessages, getConversationUnreadCount, getTotalUnreadCount,
  clearMarkRead, getStatus, markLocallyDeleted, getPosts, getRoomMembers, getTopics,
  getPersonalTasksForCurrentUser, getBoundDevices, getPendingDeviceSessions, getBindableConversations,
  getAgentSkills, isSkillEnabled, getExecutionHistory, getWorkspaceDraftActions, getInteractiveBridgeState,
  getGroupInvitationNotifications, getPushNotifications, getLastSeenNotificationsAt, setLastSeenNotificationsAt,
  getAuth, getUserId, getPhone, getSessionId, isLoggedIn,
  parseSharedPostMessageContent, parseDeviceBindPayload,
  // internal setters (for bridge only)
  _setters,
};
