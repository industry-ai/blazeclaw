/**
 * media-card/ai-card.js — 媒体预览卡片
 * 模板：暗色媒体预览区（类型标签+播放按钮+时长）+ 标题 + 描述 + 按钮
 */
const PALETTE = {
  indigo:  { brand:'#4f46e5', light:'#eef2ff', soft:'#e0e7ff', gradStart:'#4f46e5', gradEnd:'#818cf8' },
};

var THEME_MAP = { video:'indigo', audio:'indigo', image:'indigo', file:'indigo' };
function themeColor(t) { var c = THEME_MAP[t||'video'] || 'indigo'; return PALETTE[c] || PALETTE.indigo; }

const ICONS = { video:'🎬', audio:'🎧', image:'🖼', file:'📄' };
function iconEmoji(d) { const i=(d.layout&&d.layout.icon)||'video'; return ICONS[i]||'🎬'; }

const TYPE_LABEL = { video:'视频', audio:'音频', image:'图片', file:'文件' };

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
    wrapper.innerHTML = mediaHTML(d);
    this.shadowRoot.appendChild(wrapper.firstElementChild);
  }
}

function mediaHTML(d) {
  var btn = d.button_text || '播放视频';
  var mtype = (d.layout&&d.layout.icon)||'video';
  var typeLabel = TYPE_LABEL[mtype]||'文件';
  var mediaArea;
  var mediaSrc = d.video_url || d.target_url;
  if (mediaSrc) {
    mediaArea = '<div class="media-area"><video controls src="' + esc(mediaSrc) + '"></video></div>';
  } else {
    mediaArea = '<div class="media-area"><div class="media-preview">' +
      '<span class="media-badge">' + typeLabel + '</span>' +
      '<div class="media-play">▶</div>' +
      (d.subtitle ? '<span class="media-dur">' + esc(d.subtitle) + '</span>' : '') +
    '</div></div>';
  }
  return '<div class="card">' + mediaArea +
    '<div class="card-body">' +
      '<div class="hdr">' +
        '<div class="icon">' + iconEmoji(d) + '</div>' +
        '<div class="hinfo"><div class="title">' + esc(d.title) + '</div></div>' +
      '</div>' +
      (d.description ? '<div class="desc">' + esc(d.description) + '</div>' : '') +
      '<div class="actions"><button class="btn primary">' + esc(btn) + '</button></div>' +
    '</div>' +
  '</div>';
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
  var FILES = ['class-video.json'];
  Promise.all(FILES.map(function(f) { return fetch(f).then(function(r) { return r.json(); }); }))
    .then(function(data) { renderCards('root', data.map(function(d) { return { data:d }; })); });
})();
