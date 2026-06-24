/* ================================================================
   AgentChat HTML 版 - UI 状态管理
   主题切换、页面视图、移动端导航状态
   ================================================================ */

const UiStore = (() => {
  const THEME_KEY = 'agentchat-ui-theme';

  let _theme = 'light'; // 'light' | 'dark' | 'system'
  let _activeConversationId = null;
  let _viewStack = ['list']; // list | chat | hub | task | creator
  let _pendingTask = null; // 待打开的任务详情（对齐 Vue ui.openTask）
  const listeners = new Set();

  function notify() { for (const fn of listeners) fn(); }

  function _applyTheme(t) {
    document.documentElement.classList.toggle('dark', t === 'dark');
  }

  function _resolveTheme(mode) {
    if (mode === 'system') {
      return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    }
    return mode;
  }

  function initTheme() {
    const stored = localStorage.getItem(THEME_KEY);
    if (['light', 'dark', 'system'].includes(stored)) _theme = stored;
    else _theme = 'light';
    _applyTheme(_resolveTheme(_theme));

    // 监听系统主题变化
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
      if (_theme === 'system') _applyTheme(e.matches ? 'dark' : 'light');
    });
  }

  function setTheme(mode) {
    _theme = mode;
    localStorage.setItem(THEME_KEY, mode);
    _applyTheme(_resolveTheme(mode));
    notify();
  }

  function getTheme() { return _theme; }

  function toggleTheme() {
    const resolved = _resolveTheme(_theme);
    setTheme(resolved === 'dark' ? 'light' : 'dark');
  }

  function isDark() {
    return _resolveTheme(_theme) === 'dark';
  }

  function _currentView() {
    return _viewStack[_viewStack.length - 1] || 'list';
  }

  function setView(view) {
    _viewStack = [view];
    notify();
  }

  function getView() { return _currentView(); }
  function getViewStack() { return [..._viewStack]; }
  function getActiveConversationId() { return _activeConversationId; }

  function setActiveConversation(id, preventNavigation = false) {
    _activeConversationId = id || null;
    if (!_activeConversationId) {
      _viewStack = ['list'];
      notify();
      return;
    }
    if (!preventNavigation) {
      _viewStack = ['list', 'chat'];
    }
    notify();
  }

  function resetToChatView() {
    _viewStack = ['list', 'chat'];
    notify();
  }

  function resetToListView() {
    _activeConversationId = null;
    _viewStack = ['list'];
    notify();
  }

  function openHub() {
    if (_currentView() === 'hub') return;
    _viewStack = [..._viewStack.filter(Boolean), 'hub'];
    notify();
  }

  // 对齐 Vue 版 ui.openTask：存储待打开的任务详情，由 chat 页面消费
  function openTask(task) {
    _pendingTask = task || null;
    notify();
  }

  function consumePendingTask() {
    const task = _pendingTask;
    _pendingTask = null;
    return task;
  }

  return {
    initTheme, setTheme, getTheme, toggleTheme, isDark,
    setView, getView, getViewStack, getActiveConversationId,
    setActiveConversation, resetToChatView, resetToListView, openHub,
    openTask, consumePendingTask,
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); },
  };
})();

export default UiStore;
