/**
 * text-card/ai-card.js — 文本类卡片（h5_entry / assistant_welcome / recommendation / task / health_advice）
 * 模板：顶部渐变装饰条 + 左侧品牌色条 + 图标标题 + 描述 + 按钮
 */
const PALETTE = {
  blue:    { brand:'#3b82f6', light:'#eff6ff', soft:'#dbeafe', gradStart:'#3b82f6', gradEnd:'#60a5fa' },
  purple:  { brand:'#8b5cf6', light:'#f5f3ff', soft:'#ede9fe', gradStart:'#8b5cf6', gradEnd:'#a78bfa' },
  cyan:    { brand:'#0891b2', light:'#ecfeff', soft:'#cffafe', gradStart:'#0891b2', gradEnd:'#22d3ee' },
  amber:   { brand:'#d97706', light:'#fffbeb', soft:'#fef3c7', gradStart:'#d97706', gradEnd:'#fbbf24' },
  emerald: { brand:'#059669', light:'#ecfdf5', soft:'#d1fae5', gradStart:'#059669', gradEnd:'#34d399' },
};

var THEME_MAP = { general:'blue', ai:'purple', recommendation:'cyan', task:'amber', health:'emerald' };
function themeColor(t) { var c = THEME_MAP[t||'general'] || 'blue'; return PALETTE[c] || PALETTE.blue; }

const ICONS = { ai:'🤖', link:'🔗', sparkle:'✨', task:'📋', health:'💊', audio:'🎧' };
function iconEmoji(d) { const i=(d.layout&&d.layout.icon)||'link'; return ICONS[i]||'🔗'; }

const BADGES = { h5_entry:'H5 入口', assistant_welcome:'AI 助手', recommendation:'AI 推荐', task:'', health_advice:'' };

class AICard extends HTMLElement {
  constructor() { super(); this.attachShadow({ mode:'open' }); this._connected = false; }
  static get observedAttributes() { return ['data']; }
  attributeChangedCallback() { if (this._connected) this.render(); }
  connectedCallback() { this._connected = true; this.render(); }

  render() {
    const raw = this.getAttribute('data');
    if (!raw) { this.shadowRoot.innerHTML=''; return; }
    let d; try { d=JSON.parse(raw); } catch { this.shadowRoot.innerHTML=''; return; }
    const p = themeColor(d.theme);
    this.shadowRoot.innerHTML='';

    const link = document.createElement('link');
    link.rel='stylesheet'; link.href='ai-card.css';
    this.shadowRoot.appendChild(link);

    const vars = document.createElement('style');
    vars.textContent = `:host{--c-brand:${p.brand};--c-light:${p.light};--c-soft:${p.soft};--c-grad-start:${p.gradStart};--c-grad-end:${p.gradEnd}}`;
    this.shadowRoot.appendChild(vars);

    const wrapper = document.createElement('div');
    wrapper.innerHTML = defaultHTML(d);
    this.shadowRoot.appendChild(wrapper.firstElementChild);
  }
}

function defaultHTML(d) {
  const icon = iconEmoji(d);
  const badge = BADGES[d.card_type] || '';
  const btn = d.button_text || '打开页面';
  return `
<div class="card">
  <div class="strip"></div>
  <div class="card-body">
    <div class="hdr">
      <div class="icon">${icon}</div>
      <div class="hinfo">
        <div class="title">${esc(d.title)}</div>
        ${d.subtitle?`<div class="subtitle">${esc(d.subtitle)}</div>`:''}
        ${badge?`<span class="badge">${badge}</span>`:''}
      </div>
    </div>
    ${d.description?`<div class="desc">${esc(d.description)}</div>`:''}
    <div class="actions"><button class="btn primary">${esc(btn)}</button></div>
  </div>
</div>`;
}

function esc(s) { if(s==null)return''; return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

if (!customElements.get('ai-card')) customElements.define('ai-card', AICard);

window.renderCards = function(containerId, cards) {
  var root = document.getElementById(containerId);
  if (!root) return;
  root.innerHTML = cards.map(function(c) {
    var d = c.data;
    return '<div class="wrap"><span class="label">'+(d.layout&&d.layout.variant||d.card_type)+' · '+d.theme+' · icon='+(d.layout&&d.layout.icon)+'</span><ai-card data=\''+JSON.stringify(d)+'\'></ai-card></div>';
  }).join('');
};

(function() {
  var root = document.getElementById('root');
  if (!root) return;
  var FILES = ['h5-ai-assistant.json','h5-essay.json','edu-ai.json','medical-ai.json','oral-practice.json','weekly-report.json','medication-reminder.json'];
  Promise.all(FILES.map(function(f) { return fetch(f).then(function(r) { return r.json(); }); }))
    .then(function(data) { renderCards('root', data.map(function(d) { return { data:d }; })); });
})();
