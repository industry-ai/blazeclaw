/* ================================================================
   AgentChat HTML 版 - Toast/HUD 提示
   ================================================================ */

const Toast = (() => {
  let container = null;

  function ensureContainer() {
    if (!container) {
      container = document.createElement('div');
      container.id = 'toast-container';
      document.body.appendChild(container);
    }
    return container;
  }

  function show(text, tone = '') {
    const el = document.createElement('div');
    el.className = `toast ${tone}`;
    el.textContent = text;
    ensureContainer().appendChild(el);

    setTimeout(() => {
      if (el.parentNode) el.parentNode.removeChild(el);
    }, 2200);
  }

  function success(text) { show(text, 'success'); }
  function warn(text) { show(text, 'warn'); }
  function error(text) { show(text, 'error'); }
  function info(text) { show(text); }
  function featureUnavailable() { warn('该功能暂未开放'); }

  return { show, success, warn, error, info, featureUnavailable };
})();

export default Toast;
