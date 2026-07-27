/* ================================================================
   聊天记录本地化存储（localStorage 持久化层）
   ----------------------------------------------------------------
   职责：将聊天消息和输入框草稿持久化到 localStorage，
   使页面刷新后仍可恢复聊天记录。
   - 按 userId 命名空间隔离，不同用户数据互不干扰
   - 每会话独立存储，防抖写入避免频繁 IO
   - 只持久化已完成的消息（跳过 streaming/sending 临时态）
   ================================================================ */

const STORAGE_PREFIX = 'agentchat:history';
const DRAFTS_PREFIX = 'agentchat:drafts';
const KICKED_PREFIX = 'agentchat:kicked';
const MAX_MESSAGES_PER_CONV = 200;
const SAVE_DEBOUNCE_MS = 500;

let _currentUserId = '';
const _pendingSaveTimers = new Map(); // convId -> timer

function _msgKey(userId, convId) {
  return `${STORAGE_PREFIX}:${userId}:${convId}`;
}

function _draftsKey(userId) {
  return `${DRAFTS_PREFIX}:${userId}`;
}

function _kickedKey(userId) {
  return `${KICKED_PREFIX}:${userId}`;
}

function setUserId(userId) {
  _currentUserId = String(userId || '').trim() || 'default';
}

// ── 消息持久化 ──

/**
 * 防抖保存会话消息到 localStorage。
 * 跳过 streaming/sending 状态的临时消息，只持久化已完成消息。
 */
function saveMessages(convId, messages) {
  if (!_currentUserId || !convId || !Array.isArray(messages)) return;

  if (_pendingSaveTimers.has(convId)) {
    clearTimeout(_pendingSaveTimers.get(convId));
  }

  const timer = setTimeout(() => {
    _pendingSaveTimers.delete(convId);
    const toSave = messages
      .filter((m) => m.status !== 'streaming' && m.delivery !== 'sending')
      .slice(-MAX_MESSAGES_PER_CONV);
    if (!toSave.length) return;
    try {
      localStorage.setItem(_msgKey(_currentUserId, convId), JSON.stringify(toSave));
    } catch (e) {
      // localStorage 满或不可用时静默降级
      console.warn('[chatHistoryStore] save messages failed', e);
    }
  }, SAVE_DEBOUNCE_MS);
  _pendingSaveTimers.set(convId, timer);
}

/**
 * 从 localStorage 加载会话消息。
 * @returns {Array} 消息数组，无缓存时返回空数组
 */
function loadMessages(convId) {
  if (!_currentUserId || !convId) return [];
  try {
    const raw = localStorage.getItem(_msgKey(_currentUserId, convId));
    if (!raw) return [];
    const arr = JSON.parse(raw);
    return Array.isArray(arr) ? arr : [];
  } catch (e) {
    console.warn('[chatHistoryStore] load messages failed', e);
    return [];
  }
}

/**
 * 批量加载多个会话的消息。
 * @param {string[]} convIds
 * @returns {Map<string, Array>} convId -> messages
 */
function loadAllMessages(convIds) {
  const result = new Map();
  if (!Array.isArray(convIds)) return result;
  for (const id of convIds) {
    const msgs = loadMessages(id);
    if (msgs.length) result.set(id, msgs);
  }
  return result;
}

/**
 * 清除单个会话的本地消息缓存
 */
function clearMessages(convId) {
  if (!_currentUserId || !convId) return;
  try {
    localStorage.removeItem(_msgKey(_currentUserId, convId));
  } catch (e) {}
}

/**
 * 清除当前用户所有会话的本地消息缓存
 */
function clearAllMessages() {
  if (!_currentUserId) return;
  try {
    const prefix = `${STORAGE_PREFIX}:${_currentUserId}:`;
    const toRemove = [];
    for (let i = 0; i < localStorage.length; i++) {
      const key = localStorage.key(i);
      if (key && key.startsWith(prefix)) toRemove.push(key);
    }
    toRemove.forEach((k) => localStorage.removeItem(k));
  } catch (e) {
    console.warn('[chatHistoryStore] clear all messages failed', e);
  }
}

// ── 被踢出群聊持久化 ──
// 本地保存被踢出的群聊信息，使会话列表仍能展示这些群聊并查看本地缓存的历史记录。
// 存储结构：{ convId: { convId, name, title, scope, kickedAt, reason, operator } }

function _loadKickedMap() {
  if (!_currentUserId) return {};
  try {
    const raw = localStorage.getItem(_kickedKey(_currentUserId));
    if (!raw) return {};
    const obj = JSON.parse(raw);
    return (obj && typeof obj === 'object') ? obj : {};
  } catch (e) {
    console.warn('[chatHistoryStore] load kicked rooms failed', e);
    return {};
  }
}

function _saveKickedMap(map) {
  if (!_currentUserId) return;
  try {
    localStorage.setItem(_kickedKey(_currentUserId), JSON.stringify(map || {}));
  } catch (e) {
    console.warn('[chatHistoryStore] save kicked rooms failed', e);
  }
}

/**
 * 保存/更新一条被踢出群聊记录
 * @param {Object} room - { convId, name, title, scope, kickedAt, reason, operator }
 */
function saveKickedRoom(room) {
  if (!_currentUserId || !room || !room.convId) return;
  const map = _loadKickedMap();
  map[room.convId] = {
    convId: room.convId,
    name: room.name || '',
    title: room.title || '',
    scope: room.scope || 'group',
    kickedAt: room.kickedAt || Date.now(),
    reason: room.reason || '',
    operator: room.operator || '',
  };
  _saveKickedMap(map);
}

/**
 * 获取所有被踢出群聊记录
 * @returns {Array} 被踢出群聊数组
 */
function getKickedRooms() {
  const map = _loadKickedMap();
  return Object.values(map);
}

/**
 * 判断某个会话是否已被踢出
 */
function isKicked(convId) {
  if (!_currentUserId || !convId) return false;
  const map = _loadKickedMap();
  return !!map[convId];
}

/**
 * 移除单条被踢出群聊记录（如用户被重新邀请回群）
 */
function removeKickedRoom(convId) {
  if (!_currentUserId || !convId) return;
  const map = _loadKickedMap();
  if (map[convId]) {
    delete map[convId];
    _saveKickedMap(map);
  }
}

/**
 * 清除当前用户所有被踢出群聊记录
 */
function clearKickedRooms() {
  if (!_currentUserId) return;
  try {
    localStorage.removeItem(_kickedKey(_currentUserId));
  } catch (e) {}
}

// ── 草稿持久化 ──

/**
 * 保存草稿对象到 localStorage
 * @param {Object} drafts - { convId: draftText }
 */
function saveDrafts(drafts) {
  if (!_currentUserId) return;
  try {
    const obj = drafts || {};
    localStorage.setItem(_draftsKey(_currentUserId), JSON.stringify(obj));
  } catch (e) {
    console.warn('[chatHistoryStore] save drafts failed', e);
  }
}

/**
 * 从 localStorage 加载草稿
 * @returns {Object} { convId: draftText }
 */
function loadDrafts() {
  if (!_currentUserId) return {};
  try {
    const raw = localStorage.getItem(_draftsKey(_currentUserId));
    if (!raw) return {};
    const obj = JSON.parse(raw);
    return (obj && typeof obj === 'object') ? obj : {};
  } catch (e) {
    console.warn('[chatHistoryStore] load drafts failed', e);
    return {};
  }
}

/**
 * 清除草稿缓存
 */
function clearDrafts() {
  if (!_currentUserId) return;
  try {
    localStorage.removeItem(_draftsKey(_currentUserId));
  } catch (e) {}
}

export default {
  setUserId,
  saveMessages,
  loadMessages,
  loadAllMessages,
  clearMessages,
  clearAllMessages,
  saveDrafts,
  loadDrafts,
  clearDrafts,
  saveKickedRoom,
  getKickedRooms,
  isKicked,
  removeKickedRoom,
  clearKickedRooms,
};
