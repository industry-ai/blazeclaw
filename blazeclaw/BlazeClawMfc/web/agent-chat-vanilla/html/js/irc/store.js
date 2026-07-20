/* ================================================================
   IRC 聊天室 - 视图状态缓存（View State）
   ----------------------------------------------------------------
   职责：仅保存 IRC 业务逻辑回传的视图数据，供页面同步读取渲染。
   - 不包含任何业务逻辑（无网络请求、无协议处理）
   - 仅由 irc/api.js 在收到 postMessage 响应/推送时更新
   - 页面通过 subscribe 订阅变化以触发重渲染

   数据模型对齐 IRC_CHATROOM_ARCHITECTURE.md §4：
   - CChannel  : 频道（名称 / 主题 / 模式 / 成员 / operators / ban / topics）
   - CPeer     : 成员（nick / mode: Operator|Voice|Away|Invisible / join_order）
   - COperator : 操作者（继承 CPeer，OperType: Local|Global）
   - CTopic    : Newsgroup 风格话题（title / status / 层级回复 / AI 参与）
   - CMessage  : TLS 加密消息（普通聊天 / 认证）
   - CPrompt   : TCP 明文消息（JOIN/PART / AI 指令 / Operator 命令）
   ================================================================ */

const _listeners = new Set();

// ── 频道列表 ──
// channel: { name, topic, founder, modes, members:[Peer], operators:[nick], banList:[mask], userLimit }
let _channels = [];
let _activeChannel = null;

// ── 消息（按频道分组）── channel -> msg[]
const _messagesByChannel = new Map();
// ── 话题（按频道分组）── channel -> topic[]
const _topicsByChannel = new Map();
// ── WHOIS 缓存 ── nick -> info
const _whoisCache = new Map();

// ── 连接状态 ── disconnected | connecting | connected | error
let _status = 'disconnected';

// ── 当前用户（由鉴权通道回填，供判断 isMine / 是否 operator）──
let _currentUser = { nick: '', isGlobalOperator: false };

function notify() {
  for (const fn of _listeners) {
    try { fn(); } catch (e) { console.warn('[irc/store] listener error', e); }
  }
}

function subscribe(fn) {
  _listeners.add(fn);
  return () => _listeners.delete(fn);
}

// ── 频道 ──
function getChannels() { return _channels; }
function getActiveChannel() { return _activeChannel; }
function setActiveChannel(name) { _activeChannel = name || null; }
function getChannel(name) {
  return _channels.find((c) => c.name === name) || null;
}
function getMembers(channel) {
  const c = getChannel(channel);
  return c ? (c.members || []) : [];
}
function getOperators(channel) {
  const c = getChannel(channel);
  return c ? (c.operators || []) : [];
}
function isOperator(channel, nick) {
  const c = getChannel(channel);
  return !!(c && c.operators && c.operators.includes(nick));
}
function getChannelModes(channel) {
  const c = getChannel(channel);
  return c ? (c.modes || {}) : {};
}

// ── 消息 ──
function getMessages(channel) {
  return _messagesByChannel.get(channel) || [];
}
function getActiveMessages() {
  return _activeChannel ? getMessages(_activeChannel) : [];
}

// ── 话题 ──
function getTopics(channel) {
  return _topicsByChannel.get(channel) || [];
}
function getTopic(channel, topicId) {
  const list = getTopics(channel);
  return list.find((t) => t.id === topicId) || null;
}

// ── WHOIS ──
function getWhois(nick) { return _whoisCache.get(nick) || null; }

// ── 连接状态 ──
function getStatus() { return _status; }

// ── 当前用户 ──
function getCurrentUser() { return _currentUser; }
function getCurrentNick() { return _currentUser.nick; }
function isGlobalOperator() { return !!_currentUser.isGlobalOperator; }

// ── 内部更新方法（仅供 irc/api.js 调用）──
const _setters = {
  channels(list) { _channels = list || []; },
  mergeChannel(ch) {
    const idx = _channels.findIndex((c) => c.name === ch.name);
    if (idx >= 0) _channels[idx] = { ..._channels[idx], ...ch };
    else _channels = [..._channels, ch];
  },
  removeChannel(name) {
    _channels = _channels.filter((c) => c.name !== name);
    _messagesByChannel.delete(name);
    _topicsByChannel.delete(name);
    if (_activeChannel === name) _activeChannel = null;
  },
  activeChannel(name) { _activeChannel = name; },

  // 成员更新
  setMembers(channel, members) {
    const c = getChannel(channel);
    if (c) { c.members = members || []; }
  },
  setOperators(channel, ops) {
    const c = getChannel(channel);
    if (c) { c.operators = ops || []; }
  },
  setModes(channel, modes) {
    const c = getChannel(channel);
    if (c) { c.modes = { ...(c.modes || {}), ...modes }; }
  },
  setTopic(channel, topic) {
    const c = getChannel(channel);
    if (c) { c.topic = topic; }
  },
  addBan(channel, mask) {
    const c = getChannel(channel);
    if (c) { c.banList = [...(c.banList || []), mask]; }
  },

  // 消息
  messages(channel, list) { _messagesByChannel.set(channel, list || []); },
  appendMessage(channel, msg) {
    const list = _messagesByChannel.get(channel) || [];
    list.push(msg);
    _messagesByChannel.set(channel, list);
  },

  // 话题
  topics(channel, list) { _topicsByChannel.set(channel, list || []); },
  addTopic(channel, topic) {
    const list = _topicsByChannel.get(channel) || [];
    list.push(topic);
    _topicsByChannel.set(channel, list);
  },
  updateTopic(channel, topic) {
    const list = _topicsByChannel.get(channel) || [];
    const idx = list.findIndex((t) => t.id === topic.id);
    if (idx >= 0) list[idx] = { ...list[idx], ...topic };
    else list.push(topic);
    _topicsByChannel.set(channel, list);
  },
  appendReply(channel, topicId, reply) {
    const list = _topicsByChannel.get(channel) || [];
    const t = list.find((x) => x.id === topicId);
    if (t) {
      t.replies = [...(t.replies || []), reply];
    }
  },

  // WHOIS
  whois(nick, info) { _whoisCache.set(nick, info); },

  status(s) { _status = s; },
  currentUser(u) { _currentUser = { ..._currentUser, ...u }; },

  resetAll() {
    _channels = [];
    _activeChannel = null;
    _messagesByChannel.clear();
    _topicsByChannel.clear();
    _whoisCache.clear();
    _status = 'disconnected';
    _currentUser = { nick: '', isGlobalOperator: false };
  },
  notify,
};

export default {
  subscribe,
  // getters
  getChannels, getActiveChannel, setActiveChannel, getChannel,
  getMembers, getOperators, isOperator, getChannelModes,
  getMessages, getActiveMessages,
  getTopics, getTopic,
  getWhois,
  getStatus,
  getCurrentUser, getCurrentNick, isGlobalOperator,
  // internal setters
  _setters,
};
