/* ================================================================
   AgentChat HTML 版 - 应用入口 & 路由器
   基于 hash 的 SPA 路由，精确匹配原版 Vue Router
   ================================================================ */

import AuthStore from './stores/authStore.js';
import ChatStore from './stores/chatStore.js';
import UiStore from './stores/uiStore.js';
import AppHttp from './api/httpClient.js';
import AppConfig from './config.js';

// ── 全局：确保 API 请求始终路由到桥接服务（对齐 Vue 版 Vite proxy /api/chat → 127.0.0.1:8787）──
AppHttp.setBaseUrl(AppConfig.getChatConfig().httpBaseUrl);

// ── 页面模块映射 ──
const pageModules = {
  login: () => import('./pages/auth.js'),
  chat: () => import('./pages/chat.js'),
  tasks: () => import('./pages/tasks.js'),
  ai: () => import('./pages/ai.js'),
  notifications: () => import('./pages/notifications.js'),
  me: () => import('./pages/me.js'),
  devices: () => import('./pages/devices.js'),
};

// ── 状态 ──
let currentRoute = '';
let currentPage = null;

// ── 初始化 ──
document.addEventListener('DOMContentLoaded', async () => {
  UiStore.initTheme();
  await AuthStore.init();

  // 已登录才初始化 ChatStore（本地瞬时完成，网络在后台并行）
  if (AuthStore.isLoggedIn()) {
    ChatStore.init();
    // 订阅 ChatStore 变化，实时更新底部导航未读 badge（对齐 Vue 版响应式更新）
    ChatStore.subscribe(() => _updateBadges());
  }

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

  // 绑定导航点击
  _bindNavEvents();
});

// ── 路由解析 ──
function _resolveRoute() {
  const hash = window.location.hash.replace(/^#/, '') || '/';
  const parts = hash.split('?');
  const path = parts[0];

  if (path === '/login' || path === 'login') return '/login';
  if (path === '/chat' || path === 'chat' || path === '/' || path === '') return '/chat';
  if (path === '/tasks' || path === 'tasks') return '/tasks';
  if (path === '/ai' || path === 'ai') return '/ai';
  if (path === '/notifications' || path === 'notifications') return '/notifications';
  if (path === '/me' || path === 'me') return '/me';
  if (path === '/devices' || path === 'devices') return '/devices';
  return '/chat';
}

// ── 导航逻辑 ──
async function _navigateTo(route) {
  // 登录守卫
  const needsAuth = route !== '/login';
  if (needsAuth && !AuthStore.isLoggedIn()) {
    window.location.hash = '#/login';
    return;
  }

  if (route === '/login' && AuthStore.isLoggedIn()) {
    window.location.hash = '#/chat';
    return;
  }

  // 销毁旧页面
  if (currentPage && currentPage.destroy) currentPage.destroy();

  // 隐藏所有页面
  _hideAllPages();

  // 加载新页面
  const pageConfig = _getPageConfig(route);
  if (!pageConfig) return;

  const { pageId, moduleKey, placeholderText } = pageConfig;
  const pageEl = document.getElementById(pageId);
  if (pageEl) pageEl.classList.add('active');

  if (currentRoute === route && currentPage) return;

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
  document.querySelectorAll('.page').forEach(el => el.classList.remove('active'));
}

function _getPageConfig(route) {
  const map = {
    '/login': { pageId: 'page-auth', moduleKey: 'login', placeholderText: '' },
    '/chat': { pageId: 'page-chat', moduleKey: 'chat', placeholderText: '' },
    '/tasks': { pageId: 'page-tasks', moduleKey: 'tasks', placeholderText: '任务中心' },
    '/ai': { pageId: 'page-ai', moduleKey: 'ai', placeholderText: '工作台' },
    '/notifications': { pageId: 'page-notifications', moduleKey: 'notifications', placeholderText: '通知中心' },
    '/me': { pageId: 'page-me', moduleKey: 'me', placeholderText: '个人中心' },
    '/devices': { pageId: 'page-devices', moduleKey: 'devices', placeholderText: '设备管理' },
  };
  return map[route] || null;
}

// ── 底部导航 ──
function _updateNav(route) {
  const nav = document.getElementById('bottom-nav');
  if (!nav) return;

  const showNav = ['/chat', '/tasks', '/ai', '/notifications', '/me'].includes(route);
  nav.style.display = showNav ? 'flex' : 'none';

  nav.querySelectorAll('.nav-item').forEach(item => {
    const target = item.dataset.nav;
    const isActive = _navMatches(route, target);
    item.classList.toggle('active', isActive);
  });
}

function _navMatches(route, target) {
  if (target === 'chat') return route === '/chat' || route === '/';
  if (target === 'tasks') return route === '/tasks';
  if (target === 'ai') return route === '/ai';
  if (target === 'notifications') return route === '/notifications';
  if (target === 'me') return route === '/me' || route === '/devices';
  return false;
}

function _bindNavEvents() {
  document.querySelectorAll('.nav-item').forEach(item => {
    item.addEventListener('click', (e) => {
      e.preventDefault();
      const target = item.dataset.nav;
      const routeMap = {
        chat: '#/chat',
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
    const unread = ChatStore.getTotalUnreadCount();
    chatBadge.textContent = unread > 99 ? '99+' : unread;
    chatBadge.style.display = unread > 0 ? 'flex' : 'none';
  }

  const tasksBadge = document.getElementById('tasks-badge');
  if (tasksBadge) {
    const tasks = ChatStore.getPersonalTasksForCurrentUser();
    const pending = tasks.filter(t => t.status !== 'done' && t.status !== 'canceled').length;
    tasksBadge.textContent = pending > 99 ? '99+' : pending;
    tasksBadge.style.display = pending > 0 ? 'flex' : 'none';
  }

  const notifBadge = document.getElementById('notifications-badge');
  if (notifBadge) {
    const lastSeen = ChatStore.getLastSeenNotificationsAt();
    const now = Date.now();
    const tasks = ChatStore.getPersonalTasksForCurrentUser();
    const conversations = ChatStore.getConversations();

    let notificationCount = 0;

    // 群聊邀请通知（对齐 Vue 版 groupInvitationNotifications）
    ChatStore.getGroupInvitationNotifications()
      .filter(i => i.createdAt > lastSeen)
      .forEach(() => notificationCount++);

    tasks.filter(t => {
      if (t.status === 'done' || t.status === 'canceled') return false;
      if (!t.dueAt) return false;
      return t.dueAt <= now && t.dueAt > lastSeen;
    }).forEach(() => notificationCount++);

    conversations.filter(c => c.type === 'group').forEach(c => {
      ChatStore.getPosts(c.id)
        .filter(p => p.deadlineAt && p.deadlineAt <= now && p.deadlineAt > lastSeen && p.status === 'published')
        .forEach(() => notificationCount++);
    });

    notifBadge.textContent = notificationCount > 99 ? '99+' : notificationCount;
    notifBadge.style.display = notificationCount > 0 ? 'flex' : 'none';
  }
}
