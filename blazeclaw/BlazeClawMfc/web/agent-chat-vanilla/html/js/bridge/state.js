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

// ── 工具：解析消息中的帖子分享信封（纯字符串解析，无业务逻辑）──
function parseSharedPostMessageContent(rawContent, convId) {
  if (!rawContent || typeof rawContent !== 'string') return null;
  const marker = 'c:agentchat.collaboration';
  const idx = rawContent.indexOf(marker);
  if (idx < 0) return null;
  try {
    const json = rawContent.slice(idx + marker.length).trim();
    const obj = JSON.parse(json);
    return { ...obj, _convId: convId };
  } catch (e) { return null; }
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
    _locallyDeleted.clear();
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
