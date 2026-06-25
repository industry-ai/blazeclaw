/* ================================================================
   AgentChat HTML 版 - 头像颜色生成
   ================================================================ */

const AvatarSwatch = {
  PALETTE: [
    { bg: '#efe9ff', fg: '#6d3df7' },
    { bg: '#e0f2fe', fg: '#0284c7' },
    { bg: '#dcfce7', fg: '#16a34a' },
    { bg: '#fef3c7', fg: '#d97706' },
    { bg: '#fce7f3', fg: '#db2777' },
    { bg: '#e0e7ff', fg: '#4f46e5' },
    { bg: '#ccfbf1', fg: '#0d9488' },
    { bg: '#fee2e2', fg: '#dc2626' },
  ],

  /**
   * 根据 key 获取固定的彩色头像配色
   */
  getSwatch(key) {
    let hash = 0;
    const str = String(key || '');
    for (let i = 0; i < str.length; i++) {
      hash = ((hash << 5) - hash) + str.charCodeAt(i);
      hash |= 0;
    }
    const idx = Math.abs(hash) % this.PALETTE.length;
    return this.PALETTE[idx];
  },

  /**
   * 渲染一个圆形头像 HTML
   * @param {string} key - 用于落色
   * @param {string} label - 头像上显示的文字
   * @param {number} size - 直径 px
   */
  render(key, label, size = 36) {
    const swatch = this.getSwatch(key);
    const text = String(label || '').slice(-2) || '?';
    return `<div style="width:${size}px;height:${size}px;border-radius:50%;background:${swatch.bg};color:${swatch.fg};display:flex;align-items:center;justify-content:center;font-size:${size*0.38}px;font-weight:600;flex-shrink:0;">${text}</div>`;
  },
};

export default AvatarSwatch;
