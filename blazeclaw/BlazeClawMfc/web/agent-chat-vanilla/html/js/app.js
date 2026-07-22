/* ================================================================
   AgentChat 重构版 - 应用入口 & 路由器
   ----------------------------------------------------------------
   - 基于 hash 的 SPA 路由
   - 左侧 Tab 导航（替代原底部 Tab）
   - 页面显示逻辑与业务逻辑分离：页面只做 UI，业务逻辑统一走 Bridge
   ================================================================ */

import UiStore from './stores/uiStore.js';
import Bridge from './bridge/index.js';

// ── 页面模块映射（懒加载）──
const pageModules = {
  chat: () => import('./pages/chat.js'),
  chatroom: () => import('./pages/chatroom.js'),
  tasks: () => import('./pages/tasks.js'),
  ai: () => import('./pages/ai.js'),
  notifications: () => import('./pages/notifications.js'),
  me: () => import('./pages/me.js'),
  devices: () => import('./pages/devices.js'),
};

let currentRoute = '';
let currentPage = null;

document.addEventListener('DOMContentLoaded', async () => {
  // 初始化 UI 主题（纯前端）
  UiStore.initTheme();
  // 初始化业务桥接（postMessage 与 C++ 通信）
  Bridge.init();

  // 监听 C++ 宿主注入鉴权事件（先注册监听，避免错过事件）
  // 对齐原项目：事件仅作信号，从 window.__INJECTED_AUTH__ 读取鉴权数据
  window.addEventListener('__auth_injected__', async (event) => {
    const injected = window.__INJECTED_AUTH__ || (event && event.detail) || {};
    if (injected && typeof injected === 'object' && (injected.token || injected.jwt)) {
      Bridge.applyInjectedAuth(injected);
    }
    _connectChat();
    Bridge.subscribe(() => _updateBadges());
    const route = _resolveRoute();
    await _navigateTo(route);
    _updateBadges();
  });

  // 直接连接：C++ 端管理登录状态，前端不需要前置判断
  _connectChat();
  Bridge.subscribe(() => _updateBadges());

  // 监听 hash 变化
  window.addEventListener('hashchange', () => {
    const route = _resolveRoute();
    _navigateTo(route);
    _updateBadges();
  });

  // 初始路由
  const route = _resolveRoute();
  await _navigateTo(route);
  _updateBadges();

  // 绑定左侧导航点击
  _bindNavEvents();
});

async function _connectChat() {
  try {
    await Bridge.connect([]);
  } catch (e) {
    console.warn('[app] Bridge.connect 失败:', e.message);
  }
}

// ── 路由解析 ──
function _resolveRoute() {
  const hash = window.location.hash.replace(/^#/, '') || '/';
  const path = hash.split('?')[0];
  if (path === '/chat' || path === 'chat' || path === '/' || path === '') return '/chat';
  if (path === '/chatroom' || path === 'chatroom') return '/chatroom';
  if (path === '/tasks' || path === 'tasks') return '/tasks';
  if (path === '/ai' || path === 'ai') return '/ai';
  if (path === '/notifications' || path === 'notifications') return '/notifications';
  if (path === '/me' || path === 'me') return '/me';
  if (path === '/devices' || path === 'devices') return '/devices';
  return '/chat';
}

// ── 导航逻辑 ──
async function _navigateTo(route) {
  // 同一路由且页面已加载时，不重复销毁/重建，仅确保可见
  if (currentRoute === route && currentPage) {
    const pageConfig = _getPageConfig(route);
    if (pageConfig) {
      const pageEl = document.getElementById(pageConfig.pageId);
      if (pageEl) pageEl.classList.add('active');
    }
    return;
  }

  // 销毁旧页面
  if (currentPage && currentPage.destroy) {
    try { currentPage.destroy(); } catch (e) { console.warn('[app] page destroy error', e); }
  }

  // 隐藏所有页面
  _hideAllPages();

  const pageConfig = _getPageConfig(route);
  if (!pageConfig) return;

  const { pageId, moduleKey, placeholderText } = pageConfig;
  const pageEl = document.getElementById(pageId);
  if (pageEl) pageEl.classList.add('active');

  try {
    const mod = await pageModules[moduleKey]();
    const page = mod.default || mod;
    currentPage = page;
    if (page.init) page.init();
  } catch (e) {
    console.error('页面加载失败:', route, e);
    const placeholder = document.getElementById('placeholder-page');
    if (placeholder) {
      document.getElementById('placeholder-text').textContent = placeholderText || '页面加载失败';
      placeholder.classList.add('active');
    }
  }

  currentRoute = route;
  _updateNav(route);
}

function _hideAllPages() {
  document.querySelectorAll('.page').forEach((el) => el.classList.remove('active'));
}

function _getPageConfig(route) {
  const map = {
    '/chat': { pageId: 'page-chat', moduleKey: 'chat', placeholderText: '' },
    '/chatroom': { pageId: 'page-chatroom', moduleKey: 'chatroom', placeholderText: 'IRC 聊天室' },
    '/tasks': { pageId: 'page-tasks', moduleKey: 'tasks', placeholderText: '任务中心' },
    '/ai': { pageId: 'page-ai', moduleKey: 'ai', placeholderText: '工作台' },
    '/notifications': { pageId: 'page-notifications', moduleKey: 'notifications', placeholderText: '通知中心' },
    '/me': { pageId: 'page-me', moduleKey: 'me', placeholderText: '个人中心' },
    '/devices': { pageId: 'page-devices', moduleKey: 'devices', placeholderText: '设备管理' },
  };
  return map[route] || null;
}

// ── 左侧导航显隐与高亮 ──
function _updateNav(route) {
  const nav = document.getElementById('side-nav');
  if (!nav) return;

  nav.style.display = 'flex';

  nav.querySelectorAll('.nav-item').forEach((item) => {
    const target = item.dataset.nav;
    item.classList.toggle('active', _navMatches(route, target));
  });

  // 状态指示
  const statusEl = document.getElementById('side-nav-status');
  if (statusEl) {
    statusEl.textContent = '已连接';
  }
}

function _navMatches(route, target) {
  if (target === 'chat') return route === '/chat' || route === '/';
  if (target === 'chatroom') return route === '/chatroom';
  if (target === 'tasks') return route === '/tasks';
  if (target === 'ai') return route === '/ai';
  if (target === 'notifications') return route === '/notifications';
  if (target === 'me') return route === '/me' || route === '/devices';
  return false;
}

function _bindNavEvents() {
  document.querySelectorAll('.nav-item').forEach((item) => {
    item.addEventListener('click', (e) => {
      e.preventDefault();
      const target = item.dataset.nav;
      const routeMap = {
        chat: '#/chat',
        chatroom: '#/chatroom',
        tasks: '#/tasks',
        ai: '#/ai',
        notifications: '#/notifications',
        me: '#/me',
      };
      if (routeMap[target]) window.location.hash = routeMap[target];
    });
  });
}

// ── 角标更新 ──
function _updateBadges() {
  const chatBadge = document.getElementById('chat-badge');
  if (chatBadge) {
    const unread = Bridge.getTotalUnreadCount();
    chatBadge.textContent = unread > 99 ? '99+' : unread;
    chatBadge.style.display = unread > 0 ? 'flex' : 'none';
  }

  const tasksBadge = document.getElementById('tasks-badge');
  if (tasksBadge) {
    const tasks = Bridge.getPersonalTasksForCurrentUser();
    const pending = tasks.filter((t) => t.status !== 'done' && t.status !== 'canceled').length;
    tasksBadge.textContent = pending > 99 ? '99+' : pending;
    tasksBadge.style.display = pending > 0 ? 'flex' : 'none';
  }

  const notifBadge = document.getElementById('notifications-badge');
  if (notifBadge) {
    const lastSeen = Bridge.getLastSeenNotificationsAt();
    const now = Date.now();
    const tasks = Bridge.getPersonalTasksForCurrentUser();
    const conversations = Bridge.getConversations();
    let count = 0;
    Bridge.getGroupInvitationNotifications()
      .filter((i) => i.createdAt > lastSeen)
      .forEach(() => count++);
    tasks.filter((t) => {
      if (t.status === 'done' || t.status === 'canceled') return false;
      if (!t.dueAt) return false;
      return t.dueAt <= now && t.dueAt > lastSeen;
    }).forEach(() => count++);
    conversations.filter((c) => c.type === 'group').forEach((c) => {
      Bridge.getPosts(c.id)
        .filter((p) => p.deadlineAt && p.deadlineAt <= now && p.deadlineAt > lastSeen && p.status === 'published')
        .forEach(() => count++);
    });
    // C++ 推送的通知（系统消息等）
    Bridge.getPushNotifications()
      .filter((i) => i.createdAt > lastSeen)
      .forEach(() => count++);
    const _invites = Bridge.getGroupInvitationNotifications();
    const _pushNotifs = Bridge.getPushNotifications();
    console.log('[角标] 通知未读数:', count, 'lastSeen:', lastSeen,
      '| 群邀请:', _invites.length, _invites.map(i => `createdAt=${i.createdAt}(>${lastSeen}?${i.createdAt > lastSeen})`).join(', '),
      '| 推送通知:', _pushNotifs.length);
    notifBadge.textContent = count > 99 ? '99+' : count;
    notifBadge.style.display = count > 0 ? 'flex' : 'none';
  }
}
