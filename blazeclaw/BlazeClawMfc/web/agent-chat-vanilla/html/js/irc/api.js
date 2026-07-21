/* ================================================================
   IRC 聊天室 - API 门面（Layer 4 API 层）
   ----------------------------------------------------------------
   对齐 IRC_CHATROOM_ARCHITECTURE.md §4.7 / §7：
   - 页面只调用 IrcApi 的方法，不直接访问 core / postMessage
   - 所有方法通过 core.request(kind, payload) 发起 postMessage
   - 响应回填到 store，页面通过 subscribe 重渲染

   消息类型决策（§6）：
   - sendPrivmsg  -> kind "send_message"  -> CMessage (TLS 加密)  普通聊天
   - sendPrompt   -> kind "send_prompt"   -> CPrompt  (TCP 明文)  AI指令/通知
   ================================================================ */

import core from './core.js';
import store from './store.js';

let _initialized = false;

function init() {
  if (_initialized) return;
  _initialized = true;
  core.init();
  core.onPush(_handlePush);
}

// ── 推送事件处理（C++ -> WebView push 通道）──
function _handlePush(msg) {
  const { kind, payload } = msg;
  switch (kind) {
    case 'privmsg': {
      // 其他成员/Agent 的实时消息
      const ch = payload.channel;
      const m = {
        id: payload.id || `push-${Date.now()}`, channel: ch,
        from: payload.from || payload.author,
        text: payload.text, ts: payload.ts || Date.now(),
        type: 'message', isAgent: !!payload.isAgent,
      };
      store._setters.appendMessage(ch, m);
      store._setters.notify();
      break;
    }
    case 'member_event': {
      // JOIN / PART / QUIT 通知
      const { channel, nick, event } = payload;
      const ch = store.getChannel(channel);
      if (ch) {
        if (event === 'JOIN') {
          if (!ch.members.find((m) => m.nick === nick)) {
            ch.members.push({ nick, username: '', hostname: '', realname: nick, mode: { operator: false, voice: false, away: false, invisible: false }, joinOrder: ch.members.length + 1 });
            store._setters.setMembers(channel, ch.members);
          }
          store._setters.appendMessage(channel, { id: `sys-${Date.now()}`, channel, from: 'system', text: `${nick} 已加入频道`, ts: Date.now(), type: 'system', isSystem: true });
        } else if (event === 'PART' || event === 'QUIT' || event === 'KICK' || event === 'BAN') {
          ch.members = ch.members.filter((m) => m.nick !== nick);
          ch.operators = ch.operators.filter((n) => n !== nick);
          store._setters.setMembers(channel, ch.members);
          store._setters.setOperators(channel, ch.operators);
          const actionText = event === 'QUIT' ? '退出' : event === 'KICK' ? '被踢出' : event === 'BAN' ? '被封禁' : '离开';
          store._setters.appendMessage(channel, { id: `sys-${Date.now()}`, channel, from: 'system', text: `${nick} ${actionText}频道`, ts: Date.now(), type: 'system', isSystem: true });
        }
      }
      store._setters.notify();
      break;
    }
    case 'op_switch': {
      // §5.2 Operator 切换推送
      const { channel, newOp, reason } = payload;
      const ch = store.getChannel(channel);
      if (ch && newOp && !ch.operators.includes(newOp)) {
        ch.operators.push(newOp);
        const m = ch.members.find((x) => x.nick === newOp);
        if (m) m.mode.operator = true;
        store._setters.setOperators(channel, ch.operators);
        store._setters.setMembers(channel, ch.members);
        store._setters.appendMessage(channel, { id: `sys-${Date.now()}`, channel, from: 'system', text: `[OP_SWITCH] ${reason || 'Operator 已切换为 ' + newOp}`, ts: Date.now(), type: 'system', isSystem: true });
        store._setters.notify();
      }
      break;
    }
    case 'topic_reply': {
      // 话题新回复推送（AI 自动回复等）
      const { channel, topicId, reply } = payload;
      store._setters.appendReply(channel, topicId, reply);
      store._setters.notify();
      break;
    }
    default:
      break;
  }
}

// ================================================================
// 频道管理（CMgrChannels）
// ================================================================

/** JOIN 频道 -> kind: join_channel -> CMgrChannels::Join */
async function joinChannel(channel, key = '') {
  const r = await core.request('join_channel', { channel, key });
  if (r && r.ok !== false) {
    const chName = String(channel).startsWith('#') ? channel : `#${channel}`;
    // 合并 C++ 返回的频道数据到 store
    const ch = r.data || r;
    if (ch && typeof ch === 'object' && (ch.name || ch.channel)) {
      store._setters.mergeChannel({ ...ch, name: ch.name || ch.channel });
    }
    store._setters.activeChannel(chName);
    store._setters.notify();
    // 拉取该频道的历史消息
    // getHistory(chName).catch(() => {});
  }
  return r;
}

/** PART 离开 -> kind: part_channel -> CMgrChannels::Part */
async function partChannel(channel, reason = '') {
  const r = await core.request('part_channel', { channel, reason });
  if (r && r.ok !== false) {
    const chName = String(channel).startsWith('#') ? channel : `#${channel}`;
    store._setters.removeChannel(chName);
    store._setters.notify();
  }
  return r;
}

// ================================================================
// 消息收发（CMessage / CPrompt）- §6 / §7.1
// ================================================================

/**
 * 发送普通群聊消息（PRIVMSG）
 * -> kind: send_message -> 构造 CMessage -> TLS 加密 -> :9443
 * 对齐 §7.1：chatApi.sendPrivmsg("#room1", "大家好", sessionId)
 */
async function sendPrivmsg(channel, text, sessionId) {
  return core.request('send_message', { channel, text, sessionId, ts: Date.now() });
}

/**
 * 发送 Prompt（AI 指令 / 通知 / Operator 命令）
 * -> kind: send_prompt -> 构造 CPrompt -> TCP 明文 -> :8765
 */
async function sendPrompt(channel, text) {
  return core.request('send_prompt', { channel, text, ts: Date.now() });
}

/** 历史消息 -> kind: get_history */
async function getHistory(channel) {
  const r = await core.request('get_history', { channel });
  if (r && r.ok !== false) {
    // 兼容 C++ 返回格式：{ ok, data: [...] } 或直接返回数组
    const msgs = Array.isArray(r.data) ? r.data : (Array.isArray(r) ? r : []);
    if (msgs.length) {
      const chName = String(channel).startsWith('#') ? channel : `#${channel}`;
      store._setters.messages(chName, msgs);
      store._setters.notify();
    }
  }
  return r;
}

// ================================================================
// Operator 管理（§5）- 需 Operator 权限
// ================================================================

/** KICK -> kind: kick_member -> CMgrChannels::Kick */
async function kickMember(channel, target, reason = '') {
  return core.request('kick_member', { channel, target, reason });
}

/** BAN -> kind: ban_member -> CMgrChannels::Ban */
async function banMember(channel, mask) {
  return core.request('ban_member', { channel, mask });
}

/** 设置频道 TOPIC -> kind: set_topic -> CMgrChannels::SetTopic */
async function setTopic(channel, topic) {
  return core.request('set_topic', { channel, topic });
}

/** 设置频道模式 -> kind: set_mode -> CMgrChannels::SetChannelMode */
async function setMode(channel, mode, enable) {
  return core.request('set_mode', { channel, mode, enable });
}

/** 提升 Operator -> kind: promote_operator -> CMgrChannels::PromoteToOperator */
async function promoteOperator(channel, target) {
  return core.request('promote_operator', { channel, target });
}

/** 降级 Operator -> kind: demote_operator -> CMgrChannels::DemoteOperator */
async function demoteOperator(channel, target) {
  return core.request('demote_operator', { channel, target });
}

/** 修改话题群聊标题和描述 -> kind: set_channel_info */
async function setChannelInfo(channel, title, topic) {
  const r = await core.request('set_channel_info', { channel, title, topic });
  if (r && r.ok !== false) {
    const chName = String(channel).startsWith('#') ? channel : `#${channel}`;
    store._setters.mergeChannel({ name: chName, title, topic });
    store._setters.notify();
  }
  return r;
}

/** 解散话题群聊 -> kind: dissolve_channel */
async function dissolveChannel(channel) {
  const r = await core.request('dissolve_channel', { channel });
  if (r && r.ok !== false) {
    const chName = String(channel).startsWith('#') ? channel : `#${channel}`;
    store._setters.removeChannel(chName);
    store._setters.notify();
  }
  return r;
}

// ================================================================
// 查询命令
// ================================================================

/** WHOIS -> kind: whois -> CMgrChannels::Whois */
async function whois(nick) {
  return core.request('whois', { nick });
}

/** NAMES -> kind: names -> CMgrChannels::GetNamesList */
async function getNames(channel) {
  return core.request('names', { channel });
}

// ================================================================
// Newsgroup 话题（CTopic）- §4.5
// ================================================================

/** 创建话题 -> kind: create_topic -> CChannel::CreateTopic */
async function createTopic(channel, title, content, opts = {}) {
  return core.request('create_topic', { channel, title, content, aiParticipation: opts.aiParticipation, aiContext: opts.aiContext });
}

/** 回复话题 -> kind: reply_topic -> CTopic::AddReply */
async function replyTopic(channel, topicId, content, parentId = '') {
  return core.request('reply_topic', { channel, topicId, content, parentId });
}

/** 话题列表 -> kind: list_topics -> CChannel::ListTopics */
async function listTopics(channel) {
  const r = await core.request('list_topics', { channel });
  if (r && r.ok !== false) {
    const topics = Array.isArray(r.data) ? r.data : (Array.isArray(r) ? r : []);
    if (topics.length) {
      store._setters.topics(String(channel).startsWith('#') ? channel : `#${channel}`, topics);
      store._setters.notify();
    }
  }
  return r;
}

/** 关闭话题 -> kind: close_topic -> CChannel::CloseTopic */
async function closeTopic(channel, topicId) {
  return core.request('close_topic', { channel, topicId });
}

// ── 同步 getter（页面渲染用）──
function getChannels() { return store.getChannels(); }
function getActiveChannel() { return store.getActiveChannel(); }
function getChannel(name) { return store.getChannel(name); }
function getMembers(channel) { return store.getMembers(channel); }
function getOperators(channel) { return store.getOperators(channel); }
function isOperator(channel, nick) { return store.isOperator(channel, nick); }
function getChannelModes(channel) { return store.getChannelModes(channel); }
function getMessages(channel) { return store.getMessages(channel); }
function getTopics(channel) { return store.getTopics(channel); }
function getTopic(channel, id) { return store.getTopic(channel, id); }
function getWhois(nick) { return store.getWhois(nick); }
function getStatus() { return store.getStatus(); }
function getCurrentNick() { return store.getCurrentNick(); }
function isGlobalOperator() { return store.isGlobalOperator(); }

function setActiveChannel(name) {
  store.setActiveChannel(name);
  store._setters.notify();
}

function subscribe(fn) { return store.subscribe(fn); }

/**
 * 将群聊面板传来的话题同步到聊天室话题列表（使两处标题一致）
 * @param {string} channel 频道名
 * @param {object} topic { id, title, content, creator }
 */
function _syncPendingTopic(channel, topic) {
  if (!channel || !topic) return;
  const existing = store.getTopics(channel);
  // 避免重复添加
  if (!existing.find((t) => t.id === topic.id)) {
    const ircTopic = {
      id: topic.id,
      channel,
      title: topic.title,
      creator: topic.creator || '匿名',
      status: 'open',
      content: topic.content || '',
      replies: [],
      aiParticipation: false,
      aiContext: '',
    };
    store._setters.addTopic(channel, ircTopic);
  }
  // 同步频道显示标题和描述，使频道列表与群聊面板话题一致
  store._setters.mergeChannel({ name: channel, title: topic.title, topic: topic.content || '' });
  store._setters.notify();
}

export default {
  init,
  // channels
  joinChannel, partChannel,
  // messages
  sendPrivmsg, sendPrompt, getHistory,
  // operator
  kickMember, banMember, setTopic, setMode, promoteOperator, demoteOperator,
  setChannelInfo, dissolveChannel,
  // queries
  whois, getNames,
  // topics
  createTopic, replyTopic, listTopics, closeTopic,
  // getters
  getChannels, getActiveChannel, getChannel, getMembers, getOperators,
  isOperator, getChannelModes, getMessages, getTopics, getTopic, getWhois,
  getStatus, getCurrentNick, isGlobalOperator, setActiveChannel,
  // pub/sub
  subscribe,
  // internal
  _syncPendingTopic,
};
