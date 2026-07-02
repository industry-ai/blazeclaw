/**
 * homework-card/ai-card.js — 作业提醒卡片
 * 模板：学科色条横幅（标题+教师+学科标签）+ 描述 + 按钮
 */
const PALETTE = {
  red:     { brand:'#dc2626', light:'#fef2f2', soft:'#fee2e2', gradStart:'#dc2626', gradEnd:'#f87171' },
  blue:    { brand:'#3b82f6', light:'#eff6ff', soft:'#dbeafe', gradStart:'#3b82f6', gradEnd:'#60a5fa' },
  green:   { brand:'#16a34a', light:'#f0fdf4', soft:'#dcfce7', gradStart:'#16a34a', gradEnd:'#4ade80' },
};

var THEME_MAP = { chinese:'red', math:'blue', english:'green' };
function themeColor(t) { var c = THEME_MAP[t||'math'] || 'blue'; return PALETTE[c] || PALETTE.blue; }

const ICONS = { chinese:'📖', math:'📐', english:'🌐', homework:'📖' };
function iconEmoji(d) { const i=(d.layout&&d.layout.icon)||'homework'; return ICONS[i]||'📖'; }

var SUBJECT_LABEL = { chinese:'语文', math:'数学', english:'英语' };
function subjectLabel(t) { return SUBJECT_LABEL[t] || ''; }

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
    wrapper.innerHTML = homeworkHTML(d);
    this.shadowRoot.appendChild(wrapper.firstElementChild);
  }
}

function homeworkHTML(d) {
  const icon = iconEmoji(d);
  const label = subjectLabel(d.theme);
  const btn = d.button_text || '查看作业';
  return `
<div class="card">
  <div class="bar">
    <div class="bicon">${icon}</div>
    <div class="binfo">
      <div class="btitle">${esc(d.title)}</div>
      ${d.subtitle?`<div class="bsub">${esc(d.subtitle)}</div>`:''}
      ${label?`<span class="bbadge">${label}</span>`:''}
    </div>
  </div>
  <div class="card-body">
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
  var FILES = ['chinese.json','math.json','english.json'];
  Promise.all(FILES.map(function(f) { return fetch(f).then(function(r) { return r.json(); }); }))
    .then(function(data) { renderCards('root', data.map(function(d) { return { data:d }; })); });
})();
