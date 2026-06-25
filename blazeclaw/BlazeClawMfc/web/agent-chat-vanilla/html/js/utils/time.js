/* ================================================================
   AgentChat HTML 版 - 时间格式化
   ================================================================ */

const TimeUtils = {
  /**
   * 时间戳 → 北京时间 "HH:mm" 或 "MM-DD HH:mm"
   */
  formatMdHm(ts) {
    if (!ts) return '';
    const d = new Date(ts);
    const now = new Date();
    const pad = (n) => String(n).padStart(2, '0');
    const time = `${pad(d.getHours())}:${pad(d.getMinutes())}`;

    if (d.toDateString() === now.toDateString()) return time;
    return `${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${time}`;
  },

  /**
   * 会话列表时间标签
   */
  formatListTime(ts) {
    if (!ts) return '';
    const d = new Date(ts);
    const now = new Date();
    const pad = (n) => String(n).padStart(2, '0');

    if (d.toDateString() === now.toDateString()) {
      return `${pad(d.getHours())}:${pad(d.getMinutes())}`;
    }

    const yesterday = new Date(now);
    yesterday.setDate(yesterday.getDate() - 1);
    if (d.toDateString() === yesterday.toDateString()) return '昨天';

    if (d.getFullYear() === now.getFullYear()) {
      return `${pad(d.getMonth() + 1)}/${pad(d.getDate())}`;
    }
    return `${d.getFullYear()}/${pad(d.getMonth() + 1)}/${pad(d.getDate())}`;
  },

  /**
   * 北京时间 "MM-DD HH:mm"
   */
  formatBeijingMdHm(ts) {
    if (!ts) return '';
    const d = new Date(ts);
    const pad = (n) => String(n).padStart(2, '0');
    return `${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
  },

  /**
   * 智能时间显示：今天显示 HH:mm，近几天显示"昨天"/"MM-DD HH:mm"
   */
  formatSmart(ts) {
    if (!ts) return '';
    const d = new Date(ts);
    if (Number.isNaN(d.getTime())) return '';

    const now = new Date();
    const pad = (n) => String(n).padStart(2, '0');
    const time = `${pad(d.getHours())}:${pad(d.getMinutes())}`;

    if (d.toDateString() === now.toDateString()) {
      return `今天 ${time}`;
    }

    const yesterday = new Date(now);
    yesterday.setDate(yesterday.getDate() - 1);
    if (d.toDateString() === yesterday.toDateString()) {
      return `昨天 ${time}`;
    }

    if (d.getFullYear() === now.getFullYear()) {
      return `${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${time}`;
    }

    return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${time}`;
  },

  /** 生成唯一 ID */
  uid() {
    return Date.now().toString(36) + Math.random().toString(36).slice(2, 8);
  },
};

export default TimeUtils;
