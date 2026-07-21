/**
 * comic-card/ai-card.js
 * Template: Video player + paginated comic strip (one frame per page)
 * Features: unit tabs, touch swipe, keyboard nav, page dots, fade transitions, tap-to-read (点读)
 */

var PALETTE = {
  amber: { brand:'#f59e0b', light:'#fffbeb', soft:'#fef3c7', gradStart:'#f59e0b', gradEnd:'#fbbf24' },
  blue:  { brand:'#3b82f6', light:'#eff6ff', soft:'#dbeafe', gradStart:'#3b82f6', gradEnd:'#60a5fa' },
  green: { brand:'#10b981', light:'#ecfdf5', soft:'#d1fae5', gradStart:'#10b981', gradEnd:'#34d399' },
  pink:  { brand:'#ec4899', light:'#fdf2f8', soft:'#fce7f3', gradStart:'#ec4899', gradEnd:'#f472b6' },
  purple:{ brand:'#8b5cf6', light:'#f5f3ff', soft:'#ede9fe', gradStart:'#8b5cf6', gradEnd:'#a78bfa' },
  orange:{ brand:'#f97316', light:'#fff7ed', soft:'#ffedd5', gradStart:'#f97316', gradEnd:'#fb923c' }
};

var THEME_MAP = { comic: 'amber' };

// Unit color assignments (cycle through palette)
var UNIT_COLORS = ['amber', 'blue', 'green', 'pink', 'purple', 'orange'];

function themeColor(t) {
  var c = THEME_MAP[t || 'comic'] || 'amber';
  return PALETTE[c] || PALETTE.amber;
}

function unitColor(index) {
  var c = UNIT_COLORS[index % UNIT_COLORS.length] || 'amber';
  return PALETTE[c] || PALETTE.amber;
}

var BUBBLE_CLASSES = ['left', 'right', 'center'];

function esc(s) {
  if (s == null) return '';
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function cardHTML(units) {
  var html = '<div class="comic-card">';

  // Unit tabs
  if (units.length > 1) {
    html += '<div class="unit-tabs">';
    for (var u = 0; u < units.length; u++) {
      var uc = unitColor(u);
      html += '<button class="unit-tab" data-unit="' + u + '" style="--tab-color:' + uc.brand + ';--tab-light:' + uc.light + '">' +
        '<span class="unit-tab-label">Unit ' + (u + 1) + '</span>' +
        '<span class="unit-tab-title">' + esc(units[u].title.split('· ')[1] || units[u].title) + '</span>' +
      '</button>';
    }
    html += '</div>';
  }

  // Video area (will be populated on unit switch)
  html += '<div class="video-area"></div>';

  html += '<div class="card-body">' +
      '<div class="hdr">' +
        '<div class="title"></div>' +
        '<div class="subtitle"></div>' +
      '</div>' +
      '<div class="desc"></div>' +
      '<div class="comic-viewport"></div>' +
      '<div class="page-dots"></div>' +
      '<div class="nav">' +
        '<button class="nav-btn" data-nav="prev" disabled>← 上一页</button>' +
        '<button class="nav-btn primary" data-nav="next">下一页 →</button>' +
      '</div>' +
    '</div>' +
  '</div>';
  return html;
}

// ── Tap-to-Read (点读) using Web Speech API ──
var _speechSupported = typeof window !== 'undefined' && !!window.speechSynthesis;
var speechSynth = _speechSupported ? window.speechSynthesis : null;
var _speakingBubble = null;        // currently highlighted bubble element
var _englishVoice = null;          // cached English voice

function getEnglishVoice() {
  if (_englishVoice) return _englishVoice;
  if (!speechSynth) return null;
  var voices;
  try {
    voices = speechSynth.getVoices();
  } catch (e) {
    return null;
  }
  if (!voices || voices.length === 0) return null;
  // Prefer an English voice (en-US > en-GB > any en-* > first available)
  for (var i = 0; i < voices.length; i++) {
    if (voices[i].lang === 'en-US') { _englishVoice = voices[i]; break; }
  }
  if (!_englishVoice) {
    for (var j = 0; j < voices.length; j++) {
      if (voices[j].lang.indexOf('en') === 0) { _englishVoice = voices[j]; break; }
    }
  }
  // Fallback: use the first available voice (better than nothing)
  if (!_englishVoice && voices.length > 0) {
    _englishVoice = voices[0];
  }
  return _englishVoice || null;
}

// Preload voices (they load asynchronously in most browsers)
if (_speechSupported && speechSynth) {
  try {
    speechSynth.getVoices();
    speechSynth.addEventListener('voiceschanged', function () {
      _englishVoice = null; // reset cache so we pick up fresh voices
    });
  } catch (e) {
    // Some Android/Huawei browsers throw on getVoices()
    _speechSupported = false;
    speechSynth = null;
  }
}

function speakText(text, bubbleEl) {
  if (!_speechSupported || !speechSynth) return;
  try {
    // Cancel any current speech
    speechSynth.cancel();
  } catch (e) { /* ignore cancel errors on Android */ }

  // Remove highlight from previous bubble
  if (_speakingBubble) {
    _speakingBubble.classList.remove('reading');
    _speakingBubble = null;
  }

  var utterance;
  try {
    utterance = new SpeechSynthesisUtterance(text);
  } catch (e) {
    // SpeechSynthesisUtterance constructor may throw on some Huawei browsers
    return;
  }

  var voice = getEnglishVoice();
  if (voice) {
    utterance.voice = voice;
  }
  utterance.rate = 0.82;     // slightly slower for learners
  utterance.pitch = 1.05;    // slightly higher, more child-friendly
  utterance.volume = 1;

  // Highlight the bubble being read
  if (bubbleEl) {
    bubbleEl.classList.add('reading');
    _speakingBubble = bubbleEl;
  }

  utterance.addEventListener('end', function () {
    if (_speakingBubble) {
      _speakingBubble.classList.remove('reading');
      _speakingBubble = null;
    }
  });

  utterance.addEventListener('error', function () {
    if (_speakingBubble) {
      _speakingBubble.classList.remove('reading');
      _speakingBubble = null;
    }
  });

  try {
    speechSynth.speak(utterance);
  } catch (e) {
    // speak() may fail on some Android browsers
    if (_speakingBubble) {
      _speakingBubble.classList.remove('reading');
      _speakingBubble = null;
    }
  }
}

function renderFrame(viewport, frame, index, total) {
  var texts = frame.texts || [];
  var pointReadClass = _speechSupported ? ' point-read' : '';
  var pointReadTitle = _speechSupported ? ' title="点击朗读 / Tap to read"' : '';
  var html = '<div class="page-indicator">' + (index + 1) + ' / ' + total + '</div>' +
    '<div class="frame-container"><img class="frame-img" src="' + esc(frame.image) + '" alt="panel ' + (index + 1) + '">' +
    '<div class="bubbles">';
  for (var j = 0; j < texts.length; j++) {
    var cls = BUBBLE_CLASSES[j] || 'center';
    if (j === 1 && texts.length === 2) cls += ' bottom';
    // Point-read hint icon (🔊) only when speech is supported
    html += '<div class="bubble ' + cls + pointReadClass + '" data-read="' + j + '"' + pointReadTitle + '>' +
      '<span class="bubble-text">' + esc(texts[j]) + '</span>' +
      (_speechSupported ? '<span class="bubble-speaker">🔊</span>' : '') +
    '</div>';
  }
  html += '</div></div>';
  viewport.innerHTML = html;

  // Attach click handlers for point-read (only when speech is supported)
  if (_speechSupported) {
    var bubbles = viewport.querySelectorAll('.bubble.point-read');
    for (var b = 0; b < bubbles.length; b++) {
      (function (bubbleEl, text) {
        bubbleEl.addEventListener('click', function (e) {
          e.stopPropagation();
          if (bubbleEl.classList.contains('reading')) {
            // Clicking the active bubble stops speech
            try { speechSynth && speechSynth.cancel(); } catch (ex) {}
            bubbleEl.classList.remove('reading');
            _speakingBubble = null;
            return;
          }
          speakText(text, bubbleEl);
        });
      })(bubbles[b], texts[b]);
    }
  }
}

function renderDots(dotsContainer, current, total) {
  var html = '';
  for (var i = 0; i < total; i++) {
    html += '<button class="page-dot' + (i === current ? ' active' : '') + '" data-dot="' + i + '" aria-label="Page ' + (i + 1) + '"></button>';
  }
  dotsContainer.innerHTML = html;
}

var AICard = (function () {
  var el = customElements.get('ai-card');
  if (el) return el;

  var Klass = function () {
    var self = Reflect.construct(HTMLElement, [], new.target);
    self.attachShadow({ mode: 'open' });
    self._connected = false;
    self._current = 0;
    self._unitIndex = 0;
    self._units = [];
    self._frames = [];
    self._touchStartX = 0;
    self._touchStartY = 0;
    return self;
  };
  Klass.prototype = Object.create(HTMLElement.prototype);
  Klass.prototype.constructor = Klass;

  Object.defineProperty(Klass, 'observedAttributes', {
    get: function () { return ['data']; }
  });

  Klass.prototype.attributeChangedCallback = function () {
    if (this._connected) this._render();
  };

  Klass.prototype.connectedCallback = function () {
    this._connected = true;
    this._render();
  };

  Klass.prototype.disconnectedCallback = function () {
    this._cleanup();
  };

  Klass.prototype._cleanup = function () {
    if (this._keyHandler) {
      document.removeEventListener('keydown', this._keyHandler);
      this._keyHandler = null;
    }
  };

  Klass.prototype._render = function () {
    var raw = this.getAttribute('data');
    if (!raw) { this.shadowRoot.innerHTML = ''; return; }
    var d;
    try { d = JSON.parse(raw); } catch (e) { this.shadowRoot.innerHTML = ''; return; }

    // Support both single card object and array of card objects
    var units;
    if (Array.isArray(d)) {
      // If first element has 'frames' it's an array of card objects
      // Otherwise treat the whole thing as one card
      if (d.length > 0 && typeof d[0] === 'object' && d[0].frames) {
        units = d;
      } else {
        units = [d];
      }
    } else {
      units = [d];
    }
    this._units = units;
    this._unitIndex = 0;
    this._current = 0;

    var p = themeColor('comic');
    this.shadowRoot.innerHTML = '';

    var link = document.createElement('link');
    link.rel = 'stylesheet';
    link.href = 'ai-card.css';
    this.shadowRoot.appendChild(link);

    var vars = document.createElement('style');
    vars.textContent = ':host{--c-brand:' + p.brand + ';--c-light:' + p.light + ';--c-soft:' + p.soft + ';--c-grad-start:' + p.gradStart + ';--c-grad-end:' + p.gradEnd + '}';
    this.shadowRoot.appendChild(vars);

    var wrapper = document.createElement('div');
    wrapper.innerHTML = cardHTML(units);
    this.shadowRoot.appendChild(wrapper.firstElementChild);

    var self = this;

    // Cache elements
    this._videoArea = this.shadowRoot.querySelector('.video-area');
    this._titleEl = this.shadowRoot.querySelector('.title');
    this._subtitleEl = this.shadowRoot.querySelector('.subtitle');
    this._descEl = this.shadowRoot.querySelector('.desc');
    this._viewport = this.shadowRoot.querySelector('.comic-viewport');
    this._dotsContainer = this.shadowRoot.querySelector('.page-dots');
    this._prevBtn = this.shadowRoot.querySelector('[data-nav="prev"]');
    this._nextBtn = this.shadowRoot.querySelector('[data-nav="next"]');
    this._unitTabs = this.shadowRoot.querySelectorAll('.unit-tab');

    // Load first unit
    this._loadUnit(0);

    // Unit tab clicks
    this._unitTabs.forEach(function (tab) {
      tab.addEventListener('click', function () {
        var idx = parseInt(this.getAttribute('data-unit'));
        if (idx !== self._unitIndex) {
          self._loadUnit(idx);
        }
      });
    });

    // Navigation buttons
    this._prevBtn.addEventListener('click', function () {
      self._navigate(-1);
    });

    this._nextBtn.addEventListener('click', function () {
      self._navigate(1);
    });

    // Dot clicks
    this._dotsContainer.addEventListener('click', function (e) {
      var dot = e.target.closest('.page-dot');
      if (!dot) return;
      var idx = parseInt(dot.getAttribute('data-dot'));
      if (idx !== self._current) {
        self._goToFrame(idx);
      }
    });

    // Touch swipe support
    var viewport = this._viewport;
    viewport.addEventListener('touchstart', function (e) {
      self._touchStartX = e.touches[0].clientX;
      self._touchStartY = e.touches[0].clientY;
    }, { passive: true });

    viewport.addEventListener('touchend', function (e) {
      var dx = e.changedTouches[0].clientX - self._touchStartX;
      var dy = e.changedTouches[0].clientY - self._touchStartY;
      // Only swipe if horizontal movement > vertical and > 30px
      if (Math.abs(dx) > Math.abs(dy) && Math.abs(dx) > 30) {
        self._navigate(dx < 0 ? 1 : -1);
      }
    }, { passive: true });

    // Keyboard navigation
    this._keyHandler = function (e) {
      // Only handle if this card is in viewport or focused
      if (e.key === 'ArrowLeft') {
        self._navigate(-1);
      } else if (e.key === 'ArrowRight') {
        self._navigate(1);
      }
    };
    document.addEventListener('keydown', this._keyHandler);
  };

  Klass.prototype._loadUnit = function (index) {
    if (index < 0 || index >= this._units.length) return;

    // Stop any ongoing speech when switching units
    if (speechSynth) { try { speechSynth.cancel(); } catch (e) {} }
    if (_speakingBubble) {
      _speakingBubble.classList.remove('reading');
      _speakingBubble = null;
    }

    this._unitIndex = index;
    this._current = 0;
    this._frames = this._units[index].frames || [];

    var d = this._units[index];
    var uc = unitColor(index);

    // Update CSS vars for current unit color
    var vars = this.shadowRoot.querySelector('style:last-child');
    if (!vars) {
      vars = document.createElement('style');
      this.shadowRoot.appendChild(vars);
    }
    vars.textContent = ':host{--c-brand:' + uc.brand + ';--c-light:' + uc.light + ';--c-soft:' + uc.soft + ';--c-grad-start:' + uc.gradStart + ';--c-grad-end:' + uc.gradEnd + '}';

    // Update header
    this._titleEl.textContent = d.title || '';
    this._subtitleEl.textContent = d.subtitle || '';
    this._descEl.textContent = d.description || '';

    // Update video
    if (d.video_url) {
      this._videoArea.innerHTML = '<video controls src="' + esc(d.video_url) + '" preload="metadata"' +
        ' playsinline webkit-playsinline' +
        ' x5-video-player-type="h5" x5-video-player-fullscreen="true" x5-video-orientation="portraint"' +
        '></video>';
      this._videoArea.style.display = '';
    } else {
      this._videoArea.innerHTML = '';
      this._videoArea.style.display = 'none';
    }

    // Update unit tabs
    var tabs = this._unitTabs;
    for (var t = 0; t < tabs.length; t++) {
      var tabIdx = parseInt(tabs[t].getAttribute('data-unit'));
      tabs[t].classList.toggle('active', tabIdx === index);
    }

    // Render first frame
    var total = this._frames.length;
    if (total > 0) {
      renderFrame(this._viewport, this._frames[0], 0, total);
      renderDots(this._dotsContainer, 0, total);
    } else {
      this._viewport.innerHTML = '<div class="no-frames">No panels available</div>';
      this._dotsContainer.innerHTML = '';
    }

    this._updateNav();
    this._animateIn();
  };

  Klass.prototype._navigate = function (dir) {
    var newIdx = this._current + dir;
    if (newIdx < 0 || newIdx >= this._frames.length) return;
    this._goToFrame(newIdx, dir);
  };

  Klass.prototype._goToFrame = function (index, dir) {
    this._current = index;
    var total = this._frames.length;

    if (total > 0) {
      var dirClass = dir ? (dir > 0 ? 'slide-left' : 'slide-right') : 'fade';
      this._viewport.classList.add('transition-' + dirClass);
      renderFrame(this._viewport, this._frames[index], index, total);
      renderDots(this._dotsContainer, index, total);
      // Remove transition class after animation
      var self = this;
      setTimeout(function () {
        self._viewport.classList.remove('transition-slide-left', 'transition-slide-right', 'transition-fade');
      }, 300);
    }

    this._updateNav();
  };

  Klass.prototype._updateNav = function () {
    var total = this._frames.length;
    this._prevBtn.disabled = this._current === 0;
    this._nextBtn.disabled = this._current === total - 1;
  };

  Klass.prototype._animateIn = function () {
    var cardBody = this.shadowRoot.querySelector('.card-body');
    if (cardBody) {
      cardBody.style.opacity = '0';
      cardBody.style.transform = 'translateY(8px)';
      cardBody.style.transition = 'opacity 0.3s ease, transform 0.3s ease';
      requestAnimationFrame(function () {
        cardBody.style.opacity = '1';
        cardBody.style.transform = 'translateY(0)';
      });
    }
  };

  if (typeof customElements !== 'undefined' && customElements.define) {
    customElements.define('ai-card', Klass);
  } else {
    console.warn('[comic-card] Custom Elements v1 not supported. Please use a modern browser.');
  }
  return Klass;
})();

window.renderCards = function (containerId, cards) {
  var root = document.getElementById(containerId);
  if (!root) return;
  // If single card entry with array data, use it directly (array of units)
  // Otherwise pass cards as-is
  var data;
  if (cards.length === 1 && Array.isArray(cards[0].data)) {
    data = cards[0].data;
  } else {
    data = cards.map(function (c) { return c.data; });
  }
  root.innerHTML = '<ai-card data=\'' + JSON.stringify(data).replace(/'/g, '&#39;') + '\'></ai-card>';
};

(function () {
  var root = document.getElementById('root');
  if (!root) return;

  // Built-in data — works without HTTP server
  var BUILTIN = [{
    "schema_version": "1.0",
    "card_type": "comic_strip",
    "title": "PEP外研版 · Unit 1 Pets",
    "subtitle": "Meet My Little Friends",
    "description": "Watch the video and learn how to introduce your pets. Say hello to Yaya, Maomao, Dora, Snowball and Orange!",
    "button_text": "查看完整内容",
    "video_url": "../assets/video/unit1_activity1.mp4",
    "theme": "comic",
    "frames": [
      {"image": "../assets/image/unit1_panel1.png", "texts": ["It's Yaya. It's my rabbit."]},
      {"image": "../assets/image/unit1_panel2.png", "texts": ["It's Maomao. It's my cat."]},
      {"image": "../assets/image/unit1_panel3.png", "texts": ["It's Dora. It's my bird."]},
      {"image": "../assets/image/unit1_panel4.png", "texts": ["It's Snowball. It's my dog."]},
      {"image": "../assets/image/unit1_panel5.png", "texts": ["And it's Orange. It's my fish!"]}
    ],
    "layout": { "variant": "comic_strip", "icon": "comic" }
  }, {
    "schema_version": "1.0",
    "card_type": "comic_strip",
    "title": "PEP外研版 · Unit 2 Animals",
    "subtitle": "A Day at the Zoo",
    "description": "Watch the video and explore the zoo. Learn to name wild animals and describe them with simple sentences!",
    "button_text": "查看完整内容",
    "video_url": "../assets/video/unit2_activity1.mp4",
    "theme": "comic",
    "frames": [
      {"image": "../assets/image/unit2_panel1.png", "texts": ["It's the zoo!"]},
      {"image": "../assets/image/unit2_panel2.png", "texts": ["It's a tiger."]},
      {"image": "../assets/image/unit2_panel3.png", "texts": ["They're lions."]},
      {"image": "../assets/image/unit2_panel4.png", "texts": ["Look at the bears. They're big."]},
      {"image": "../assets/image/unit2_panel5.png", "texts": ["They're monkeys."]},
      {"image": "../assets/image/unit2_panel6.png", "texts": ["They're cute."]}
    ],
    "layout": { "variant": "comic_strip", "icon": "comic" }
  }, {
    "schema_version": "1.0",
    "card_type": "comic_strip",
    "title": "PEP外研版 · Unit 3 Face",
    "subtitle": "We Are Twins!",
    "description": "Watch the video first, then flip through the comic story. Practice describing facial features with Meimei and Feifei!",
    "button_text": "查看完整内容",
    "video_url": "../assets/video/unit3_activity1.mp4",
    "theme": "comic",
    "frames": [
      {"image": "../assets/image/unit3_panel1.png", "texts": ["Hi, I'm Meimei.", "I'm Feifei.", "We are twins."]},
      {"image": "../assets/image/unit3_panel2.png", "texts": ["I have big eyes.", "Me too."]},
      {"image": "../assets/image/unit3_panel3.png", "texts": ["I have a small nose.", "Me too."]},
      {"image": "../assets/image/unit3_panel4.png", "texts": ["I have a small mouth.", "Me too."]},
      {"image": "../assets/image/unit3_panel5.png", "texts": ["We look the same.", "I know!"]},
      {"image": "../assets/image/unit3_panel6.png", "texts": ["Now I have long hair.", "I have short hair.", "We are different now."]}
    ],
    "layout": { "variant": "comic_strip", "icon": "comic" }
  }, {
    "schema_version": "1.0",
    "card_type": "comic_strip",
    "title": "PEP外研版 · Unit 4 Body",
    "subtitle": "My Robot Monster",
    "description": "Watch the video and learn body parts with a funny robot story. Can you spot the monster?",
    "button_text": "查看完整内容",
    "video_url": "../assets/video/unit4_activity1.mp4",
    "theme": "comic",
    "frames": [
      {"image": "../assets/image/unit4_panel1.png", "texts": ["Look at my robot! It has a big head and two big feet!", "Cool! And it has long arms and long legs!"]},
      {"image": "../assets/image/unit4_panel2.png", "texts": ["Oh! No! Look!"]},
      {"image": "../assets/image/unit4_panel3.png", "texts": ["It has two heads and three arms!", "It's a monster!"]},
      {"image": "../assets/image/unit4_panel4.png", "texts": ["Ha ha! It's Aunt Lucy and her cat!"]}
    ],
    "layout": { "variant": "comic_strip", "icon": "comic" }
  }, {
    "schema_version": "1.0",
    "card_type": "comic_strip",
    "title": "PEP外研版 · Unit 5 My Home",
    "subtitle": "Tidy Up Time",
    "description": "Watch the video and learn words for rooms and furniture. Help Tiantian and her friend clean up the home!",
    "button_text": "查看完整内容",
    "video_url": "../assets/video/unit5_activity1.mp4",
    "theme": "comic",
    "frames": [
      {"image": "../assets/image/unit5_panel1.png", "texts": ["Tiantian, come to my home. Let's play!"]},
      {"image": "../assets/image/unit5_panel2.png", "texts": ["Oh, so many things!", "Here's the sofa. Please sit down."]},
      {"image": "../assets/image/unit5_panel3.png", "texts": ["Come to my room. It's my bed.", "Oh, so many things!"]},
      {"image": "../assets/image/unit5_panel4.png", "texts": ["It's my table.", "Oh, so many things!"]},
      {"image": "../assets/image/unit5_panel5.png", "texts": []},
      {"image": "../assets/image/unit5_panel6.png", "texts": ["OK!", "It's clean and tidy now. Let's play."]}
    ],
    "layout": { "variant": "comic_strip", "icon": "comic" }
  }, {
    "schema_version": "1.0",
    "card_type": "comic_strip",
    "title": "PEP外研版 · Unit 6 Time",
    "subtitle": "Happy Birthday, Dad!",
    "description": "Watch the video and learn to talk about daily routines and time. Join Toby as he prepares a birthday surprise!",
    "button_text": "查看完整内容",
    "video_url": "../assets/video/unit6_activity1.mp4",
    "theme": "comic",
    "frames": [
      {"image": "../assets/image/unit6_panel1.png", "texts": ["Happy birthday, Dad!", "Thank you, Toby!"]},
      {"image": "../assets/image/unit6_panel2.png", "texts": ["Let's have lunch together!", "OK!"]},
      {"image": "../assets/image/unit6_panel3.png", "texts": ["Dad is not home yet."]},
      {"image": "../assets/image/unit6_panel4.png", "texts": ["Dad is busy.", "I know!"]},
      {"image": "../assets/image/unit6_panel5.png", "texts": ["Thank you!"]}
    ],
    "layout": { "variant": "comic_strip", "icon": "comic" }
  }];

  // ══════════════════════════════════════════════════════════════
  // 本地测试数据（直接内嵌，无需 fetch / 无需起服务）
  // 后续部署时将 DATA_FILES 替换为 fetch 远程 JSON 即可
  // ══════════════════════════════════════════════════════════════

  renderCards('root', [{ data: BUILTIN }]);
})();
