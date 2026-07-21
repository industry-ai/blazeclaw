/**
 * english-sentence-card/ai-card.js
 * Template: Sticky note with tape + ribbon + English sentence + image
 * Interactive: click to speak English sentence + show Chinese translation
 */

var PALETTE = {
  purple: { brand:'#8e44ad', light:'#f5f3ff', soft:'#ede9fe', gradStart:'#8e44ad', gradEnd:'#a78bfa' },
  blue:   { brand:'#2563eb', light:'#eff6ff', soft:'#dbeafe', gradStart:'#2563eb', gradEnd:'#60a5fa' },
};

var THEME_MAP = { abc: 'purple', sentence: 'blue' };

function themeColor(t) {
  var c = THEME_MAP[t || 'sentence'] || 'blue';
  return PALETTE[c] || PALETTE.blue;
}

function speak(text, lang, onEnd) {
  if (!window.speechSynthesis) return;
  window.speechSynthesis.cancel();
  var u = new SpeechSynthesisUtterance(text);
  u.lang = lang || 'en-US';
  u.rate = 0.8;
  u.pitch = 1.0;
  if (onEnd) u.onend = onEnd;
  window.speechSynthesis.speak(u);
}

function supportsSpeechRecognition() {
  return !!(window.SpeechRecognition || window.webkitSpeechRecognition);
}

function createRecognition(lang) {
  var SR = window.SpeechRecognition || window.webkitSpeechRecognition;
  var rec = new SR();
  rec.lang = lang || 'en-US';
  rec.interimResults = true;
  rec.maxAlternatives = 3;
  rec.continuous = false;
  return rec;
}

function similarity(a, b) {
  // Simple word-overlap similarity for sentence comparison
  var wordsA = a.toLowerCase().replace(/[.,?!;:'"()]+/g, '').split(/\s+/).filter(Boolean);
  var wordsB = b.toLowerCase().replace(/[.,?!;:'"()]+/g, '').split(/\s+/).filter(Boolean);
  if (wordsA.length === 0 || wordsB.length === 0) return 0;
  var match = 0;
  var setB = {};
  wordsB.forEach(function (w) { setB[w] = (setB[w] || 0) + 1; });
  wordsA.forEach(function (w) {
    if (setB[w] && setB[w] > 0) { match++; setB[w]--; }
  });
  return match / Math.max(wordsA.length, wordsB.length);
}

function listenAndCheck(shadowRoot, targetSentence, maxRetries) {
  if (!targetSentence) return;
  maxRetries = maxRetries || 3;

  var host = shadowRoot.host;
  var DEBUG = false; // set to true to see debug logs in Console

  function log() {
    if (DEBUG) console.log('[跟读]', Array.prototype.slice.call(arguments).join(' '));
  }

  // Abort any active recognition
  if (host._activeRecognition) {
    try { host._activeRecognition.abort(); } catch (e) {}
    host._activeRecognition = null;
  }

  // Clean up previous recording
  if (host._lastRecording) {
    URL.revokeObjectURL(host._lastRecording);
    host._lastRecording = null;
  }

  var fb = shadowRoot.querySelector('.shadow-feedback');
  var btn = shadowRoot.querySelector('.btn.shadow');
  var playBtn = null;

  function buildRatingHTML(result) {
    var stars = '';
    var s = Math.round((result.overall || 0) / 20);
    for (var i = 1; i <= 5; i++) { stars += i <= s ? '⭐' : '☆'; }
    var dimsHtml = (result.dimensions || []).map(function (d) {
      return '<div class="rating-dim">' +
        '<div class="dim-head"><span class="dim-label">' + esc(d.label) + '</span><span class="dim-score">' + (d.score || 0) + '</span></div>' +
        '<div class="dim-bar"><div class="dim-bar-fill" style="width:' + (d.score || 0) + '%"></div></div>' +
        (d.comment ? '<span class="dim-comment">' + esc(d.comment) + '</span>' : '') +
      '</div>';
    }).join('');
    return '<div class="ai-rating">' +
      '<div class="rating-overall">' +
        '<span class="overall-score">' + (result.overall || 0) + '</span>' +
        '<div class="overall-meta"><span class="overall-label">综合评分</span><span class="overall-stars">' + stars + '</span></div>' +
      '</div>' +
      '<div class="rating-dims">' + dimsHtml + '</div>' +
      '<p class="feedback-text">' + esc(result.feedback || '') + '</p>' +
      (result.suggestions ? '<div class="rating-suggestions"><span class="suggestion-label">💡 练习建议</span><p class="suggestion-text">' + esc(result.suggestions) + '</p></div>' : '') +
      '<div class="rating-actions"></div>' +
    '</div>';
  }

  function showFeedback(text, stateClass, duration) {
    if (fb) {
      fb.textContent = text;
      fb.className = 'shadow-feedback ' + (stateClass || '') + ' show';
      if (playBtn) { playBtn.remove(); playBtn = null; }
      if (host._lastRecording) {
        playBtn = document.createElement('button');
        playBtn.className = 'btn-play';
        playBtn.textContent = '🔊 听我的发音';
        playBtn.addEventListener('click', function (e) {
          e.stopPropagation();
          e.preventDefault();
          var audio = new Audio(host._lastRecording);
          audio.play().catch(function (err) {
            log('播放录音失败:', err);
            playBtn.textContent = '⚠️ 播放失败';
            setTimeout(function () {
              if (playBtn) playBtn.textContent = '🔊 听我的发音';
            }, 2000);
          });
        });
        fb.appendChild(playBtn);
      }
      if (duration) {
        clearTimeout(host._fbTimer);
        host._fbTimer = setTimeout(function () {
          fb.classList.remove('show');
        }, duration);
      }
    }
  }

  function stopRecording(onBlobReady) {
    if (host._mediaRecorder && host._mediaRecorder.state !== 'inactive') {
      var origOnStop = host._mediaRecorder.onstop;
      host._mediaRecorder.onstop = function (e) {
        if (origOnStop) origOnStop.call(this, e);
        if (onBlobReady) onBlobReady();
      };
      host._mediaRecorder.stop();
    } else {
      if (onBlobReady) onBlobReady();
    }
    if (host._mediaStream) {
      host._mediaStream.getTracks().forEach(function (t) { t.stop(); });
      host._mediaStream = null;
    }
  }

  // ── Diagnostic info ──
  log('=== 开始跟读 ===');
  log('目标句子:', targetSentence);
  log('URL协议:', location.protocol);
  log('SpeechRecognition支持:', supportsSpeechRecognition());
  log('mediaDevices可用:', !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia));
  log('speechSynthesis可用:', !!window.speechSynthesis);

  // Show listening state
  showFeedback('🎤 跟读中…', 'listening', 0);
  if (btn) btn.disabled = true;

  // ── Create recognition ──
  var rec;
  try {
    rec = createRecognition('en-US');
  } catch (e) {
    log('ERROR: 创建SpeechRecognition失败:', e);
    showFeedback('❌ 浏览器不支持语音识别，请使用Chrome或Edge', 'incorrect', 3000);
    if (btn) btn.disabled = false;
    return;
  }
  host._activeRecognition = rec;

  var audioChunks = [];
  var recorderStarted = false;

  function startMediaRecorder() {
    if (recorderStarted) return;
    recorderStarted = true;
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      log('mediaDevices不可用，跳过录音（可能需要HTTPS或localhost）');
      return;
    }
    log('请求麦克风录音...');
    navigator.mediaDevices.getUserMedia({ audio: true }).then(function (stream) {
      log('麦克风录音已就绪');
      host._mediaStream = stream;
      var mr;
      try {
        mr = new MediaRecorder(stream, { mimeType: MediaRecorder.isTypeSupported('audio/webm;codecs=opus') ? 'audio/webm;codecs=opus' : 'audio/webm' });
      } catch (e) {
        mr = new MediaRecorder(stream);
      }
      host._mediaRecorder = mr;
      mr.ondataavailable = function (e) {
        if (e.data.size > 0) audioChunks.push(e.data);
      };
      mr.onstop = function () {
        if (audioChunks.length > 0) {
          var blob = new Blob(audioChunks, { type: mr.mimeType || 'audio/webm' });
          host._lastRecording = URL.createObjectURL(blob);
          log('录音完成，大小:', blob.size, 'bytes');
        }
        audioChunks = [];
      };
      mr.start();
    }).catch(function (err) {
      log('录音麦克风失败:', err.name || err);
    });
  }

  // ── Set up event handlers BEFORE starting recognition ──
  rec.onaudiostart = function () {
    log('onaudiostart: 识别开始捕获音频');
    startMediaRecorder();
  };

  rec.onstart = function () {
    log('onstart: 识别会话已启动');
    setTimeout(startMediaRecorder, 200);
  };

  var speechEnded = false;
  var resultReceived = false;
  var speechTimer = null;      // max speech duration timer
  var noSpeechTimer = null;    // initial "no speech" timeout

  // Start the initial "no speech" timeout (10s for sentences, longer than words)
  noSpeechTimer = setTimeout(function () {
    log('10秒内未检测到任何语音');
    host._activeRecognition = null;
    try { rec.abort(); } catch (e) {}
    stopRecording();
    showFeedback('⏰ 未检测到声音，请大声读出句子', 'timeout', 2500);
    if (btn) btn.disabled = false;
  }, 10000);

  function forceStop() {
    clearTimeout(speechTimer);
    clearTimeout(noSpeechTimer);
    if (host._activeRecognition === rec) {
      try { rec.stop(); } catch (e) {}
    }
  }

  rec.onspeechstart = function () {
    log('onspeechstart: 检测到语音');
    clearTimeout(noSpeechTimer);
    // Max speech duration: 8 seconds for sentences
    speechTimer = setTimeout(function () {
      log('说话超过8秒，强制结束收音');
      speechEnded = true;
      forceStop();
      setTimeout(function () {
        if (!resultReceived && host._activeRecognition === rec) {
          log('强制结束后仍无结果，中止');
          host._activeRecognition = null;
          try { rec.abort(); } catch (e) {}
          stopRecording();
          showFeedback('⏰ 请简洁清晰地朗读句子（8秒内）', 'timeout', 2500);
          if (btn) btn.disabled = false;
        }
      }, 2000);
    }, 8000);
  };

  rec.onspeechend = function () {
    log('onspeechend: 语音结束');
    speechEnded = true;
    clearTimeout(speechTimer);
    setTimeout(function () {
      if (!resultReceived && host._activeRecognition === rec) {
        log('语音结束后5秒无结果，中止');
        host._activeRecognition = null;
        try { rec.abort(); } catch (e) {}
        stopRecording();
        showFeedback('🎙️ 未检测到句子，请再试一次', 'timeout', 2000);
        if (btn) btn.disabled = false;
      }
    }, 5000);
  };

  rec.onresult = function (event) {
    if (host._activeRecognition !== rec) return;
    resultReceived = true;

    var transcript = '';
    var confidence = 0;
    var isFinal = false;
    try {
      transcript = event.results[0][0].transcript.trim().toLowerCase();
      confidence = event.results[0][0].confidence;
      isFinal = event.results[0].isFinal;
    } catch (e) {
      transcript = '';
    }
    var target = targetSentence.trim().toLowerCase();
    var cleaned = transcript.replace(/[.,?!;:'"()\s]+/g, ' ').trim();

    // For sentences, use similarity instead of exact match
    var sim = similarity(cleaned, target);
    var correct = sim >= 0.6; // 60% word overlap = correct enough

    log('识别结果:', transcript, '| 清洗后:', cleaned, '| 相似度:', sim.toFixed(2), '| 置信度:', confidence, '| 最终:', isFinal, '| 正确:', correct);

    if (!isFinal) {
      if (cleaned) {
        showFeedback('🎤 识别中: ' + cleaned, 'listening', 0);
      }
      return;
    }

    // Final result
    clearTimeout(speechTimer);
    clearTimeout(noSpeechTimer);
    speechEnded = true;
    host._activeRecognition = null; // 阻止 onend 抢先显示"未识别到"

    stopRecording(function () {
      if (!cleaned) {
        showFeedback('🎙️ 未识别到句子，请再试一次', 'timeout', 2000);
        if (btn) btn.disabled = false;
        host._activeRecognition = null;
        return;
      }

      showFeedback('🤖 AI评分中…', 'listening', 0);
      fetch('http://localhost:8800/api/score', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          card_type: 'english_sentence',
          target_text: targetSentence,
          recognized_text: cleaned
        })
      })
        .then(function (r) { return r.json(); })
        .then(function (result) {
          showAIRating(result);
        })
        .catch(function () {
          showLocalResult();
        })
        .then(function () {
          host._activeRecognition = null;
          if (btn) btn.disabled = false;
        });
    });

    function showAIRating(result) {
      if (!result || result.is_fallback) { showLocalResult(); return; }
      var html = buildRatingHTML(result);
      var fb = shadowRoot.querySelector('.shadow-feedback');
      if (fb) {
        fb.innerHTML = html;
        fb.className = 'shadow-feedback show ai-rating-wrapper';
      }
      host._shadowAttempt = 0;
      if (host._lastRecording && fb) {
        var actions = fb.querySelector('.rating-actions');
        if (actions) {
          var playBtn = document.createElement('button');
          playBtn.className = 'btn-play';
          playBtn.textContent = '🔊 听我的发音';
          playBtn.addEventListener('click', function (e) {
            e.stopPropagation(); e.preventDefault();
            var audio = new Audio(host._lastRecording);
            audio.play().catch(function () {});
          });
          actions.appendChild(playBtn);
        }
      }
    }

    function showLocalResult() {
      if (correct) {
        showFeedback('✅ 跟读正确！（相似度: ' + Math.round(sim * 100) + '%）', 'correct', 3000);
        host._shadowAttempt = 0;
      } else {
        host._shadowAttempt = (host._shadowAttempt || 0) + 1;
        if (host._shadowAttempt < maxRetries) {
          showFeedback('❌ 再试一次吧！ (' + (host._shadowAttempt + 1) + '/' + maxRetries + ') 相似度: ' + Math.round(sim * 100) + '%', 'incorrect', 2500);
        } else {
          showFeedback('💪 加油！原句是：' + targetSentence, 'incorrect', 4000);
          host._shadowAttempt = 0;
        }
      }
    }
  };

  rec.onerror = function (event) {
    clearTimeout(speechTimer);
    clearTimeout(noSpeechTimer);
    stopRecording();
    log('识别错误:', event.error, event.message || '');

    var msg = '';
    var cls = 'timeout';
    switch (event.error) {
      case 'not-allowed':
      case 'service-not-allowed':
        if (location.protocol === 'file:') {
          msg = '🔒 本地文件不支持语音功能，请部署到服务器后使用';
        } else {
          msg = '🔒 请允许麦克风权限后重试（点击地址栏左侧的锁图标）';
        }
        cls = 'timeout';
        break;
      case 'no-speech':
        msg = '🎙️ 未检测到声音，请大声读出句子';
        cls = 'timeout';
        break;
      case 'audio-capture':
        msg = '🎙️ 未找到麦克风设备，请检查麦克风是否已连接';
        cls = 'timeout';
        break;
      case 'aborted':
        host._activeRecognition = null;
        if (btn) btn.disabled = false;
        return;
      case 'network':
        msg = '🌐 网络错误，语音识别需要联网';
        cls = 'incorrect';
        break;
      case 'language-not-supported':
        msg = '🌐 浏览器不支持英语语音识别';
        cls = 'incorrect';
        break;
      default:
        msg = '❌ 识别失败(' + (event.error || 'unknown') + ')，请重试';
        cls = 'incorrect';
    }
    showFeedback(msg, cls, 2800);
    if (btn) btn.disabled = false;
    host._activeRecognition = null;
  };

  rec.onend = function () {
    clearTimeout(speechTimer);
    clearTimeout(noSpeechTimer);
    log('onend: 识别会话结束');
    if (!host._activeRecognition || host._activeRecognition !== rec) return;
    stopRecording();
    showFeedback('🎙️ 未识别到句子，请再试一次', 'timeout', 2000);
    if (btn) btn.disabled = false;
    host._activeRecognition = null;
  };

  // ── Start recognition ──
  log('启动语音识别...');
  try {
    rec.start();
    log('rec.start() 调用成功');
  } catch (e) {
    log('rec.start() 异常:', e);
    showFeedback('❌ 启动语音识别失败: ' + (e.message || e), 'incorrect', 3000);
    if (btn) btn.disabled = false;
    host._activeRecognition = null;
  }

  // Safety net for MediaRecorder
  setTimeout(startMediaRecorder, 800);
}

function esc(s) {
  if (s == null) return '';
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function cardHTML(d) {
  var sentence = d.subtitle || '';
  var zh = d.description || '';
  var ribbon = d.title || '每日一句';
  var btn = d.button_text || '句子发音';

  return '<div class="sentence-card">' +
    '<div class="sentence-ribbon">' + esc(ribbon) + '</div>' +
    '<div class="sentence-body">' +
      '<div class="sentence-text-area">' +
        '<div class="sentence-en" data-speak="' + esc(sentence) + '">' + esc(sentence) + '</div>' +
        (zh ? '<div class="sentence-zh">' + esc(zh) + '</div>' : '') +
      '</div>' +
      '<div class="actions">' +
        '<button class="btn primary" data-speak="' + esc(sentence) + '">' + esc(btn) + '</button>' +
        (sentence && supportsSpeechRecognition() ? '<button class="btn shadow" data-shadow-speak="' + esc(sentence) + '">跟读</button>' : '') +
      '</div>' +
      '<div class="shadow-feedback"></div>' +
    '</div>' +
  '</div>';
}

var AICard = (function () {
  var el = customElements.get('ai-card');
  if (el) return el;

  var Klass = function () {
    var self = Reflect.construct(HTMLElement, [], new.target);
    self.attachShadow({ mode: 'open' });
    self._connected = false;
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

  Klass.prototype._render = function () {
    // Abort any active speech recognition on re-render
    if (this._activeRecognition) {
      try { this._activeRecognition.abort(); } catch (e) {}
      this._activeRecognition = null;
    }
    // Stop any active media recording
    if (this._mediaRecorder && this._mediaRecorder.state !== 'inactive') {
      try { this._mediaRecorder.stop(); } catch (e) {}
    }
    if (this._mediaStream) {
      this._mediaStream.getTracks().forEach(function (t) { t.stop(); });
      this._mediaStream = null;
    }
    if (this._lastRecording) {
      URL.revokeObjectURL(this._lastRecording);
      this._lastRecording = null;
    }
    clearTimeout(this._fbTimer);

    var raw = this.getAttribute('data');
    if (!raw) { this.shadowRoot.innerHTML = ''; return; }
    var d;
    try { d = JSON.parse(raw); } catch (e) { this.shadowRoot.innerHTML = ''; return; }

    var p = themeColor(d.theme);
    this.shadowRoot.innerHTML = '';

    var link = document.createElement('link');
    link.rel = 'stylesheet';
    link.href = 'ai-card.css';
    this.shadowRoot.appendChild(link);

    var vars = document.createElement('style');
    vars.textContent = ':host{--c-brand:' + p.brand + ';--c-light:' + p.light + ';--c-soft:' + p.soft + ';--c-grad-start:' + p.gradStart + ';--c-grad-end:' + p.gradEnd + '}';
    this.shadowRoot.appendChild(vars);

    var wrapper = document.createElement('div');
    wrapper.innerHTML = cardHTML(d);
    this.shadowRoot.appendChild(wrapper.firstElementChild);

    var self = this;
    setTimeout(function () {
      var zhEl = self.shadowRoot.querySelector('.sentence-zh');
      var timer = null;
      function showZh() {
        if (!zhEl) return;
        zhEl.classList.add('show');
        clearTimeout(timer);
        timer = setTimeout(function () { zhEl.classList.remove('show'); }, 3000);
      }
      var els = self.shadowRoot.querySelectorAll('[data-speak]');
      els.forEach(function (el) {
        el.addEventListener('click', function () {
          speak(el.getAttribute('data-speak'));
          showZh();
        });
      });

      // Bind shadowing (跟读) button
      var shadowBtns = self.shadowRoot.querySelectorAll('[data-shadow-speak]');
      shadowBtns.forEach(function (btn) {
        btn.addEventListener('click', function () {
          var targetSentence = btn.getAttribute('data-shadow-speak');
          if (!targetSentence) return;

          // Disable the shadow button during speak+listen cycle
          btn.disabled = true;

          var ttsFired = false;
          function startListening() {
            if (ttsFired) return;
            ttsFired = true;
            setTimeout(function () {
              listenAndCheck(self.shadowRoot, targetSentence, 3);
            }, 1000);
          }

          // Speak the sentence, then start listening after TTS finishes
          speak(targetSentence, 'en-US', function () {
            startListening();
          });

          // Fallback: if TTS onend doesn't fire (or speechSynthesis unavailable)
          setTimeout(function () {
            startListening();
          }, 6000);
        });
      });
    }, 0);
  };

  customElements.define('ai-card', Klass);
  return Klass;
})();

window.renderCards = function (containerId, cards) {
  var root = document.getElementById(containerId);
  if (!root) return;
  root.innerHTML = cards.map(function (c) {
    var d = c.data;
    return '<ai-card data=\'' + JSON.stringify(d).replace(/'/g, '&#39;') + '\'></ai-card>';
  }).join('');
};

(function () {
  var root = document.getElementById('root');
  if (!root) return;

  // ══════════════════════════════════════════════════════════════
  // 本地测试数据（直接内嵌 JSON，无需 fetch / 无需起服务）
  // 后续部署时将 DATA_FILES 替换为 fetch 远程 JSON 即可
  // ══════════════════════════════════════════════════════════════
  var DATA_FILES = [{"schema_version":"1.0","card_type":"english_sentence","title":"每日一句","subtitle":"The best preparation for tomorrow is doing your best today.","description":"为明天做的最好准备，就是今天做到最好。","button_text":"句子发音","theme":"sentence","layout":{"variant":"english_sentence","icon":"sentence"}}];

  renderCards('root', DATA_FILES.map(function (d) { return { data: d }; }));
})();
