#!/usr/bin/env python3
"""
h5-cards/scripts/render.py
接收 JSON → 匹配 card_type 到模板 → 生成自包含单文件 HTML

选项:
  --save-dir DIR   同时保存 data.json / ai-card.css / ai-card.js 到指定目录
"""
import argparse
import sys
import json
import base64
import re
from pathlib import Path

CARD_TYPE_MAP = {
    'h5_entry': 'text-card',
    'assistant_welcome': 'text-card',
    'recommendation': 'text-card',
    'task': 'text-card',
    'health_advice': 'text-card',
    'homework_reminder': 'homework-card',
    'media_preview': 'media-card',
    'english_word': 'english-word-card',
    'english_sentence': 'english-sentence-card',
    'english_sentence_input': 'english-input-card',
    'comic_strip': 'comic-card',
    'qa_answer': 'answer-card',
}

TEMPLATE_ROOT = Path(__file__).resolve().parent.parent / 'templates'


def find_template(card_type):
    name = CARD_TYPE_MAP.get(card_type)
    if not name:
        print(f"错误：未知 card_type '{card_type}'", file=sys.stderr)
        sys.exit(1)
    d = TEMPLATE_ROOT / name
    if not d.is_dir():
        print(f"错误：模板目录不存在 {d}", file=sys.stderr)
        sys.exit(1)
    return name, d


def read_template(template_dir):
    """Read template files: CSS, JS, and optional fonts."""
    result = {}
    for key, filename in [('css', 'ai-card.css'), ('js', 'ai-card.js')]:
        p = template_dir / filename
        if p.is_file():
            result[key] = p.read_text(encoding='utf-8')
    if 'css' not in result:
        p = template_dir / 'style.css'
        if p.is_file():
            result['css'] = p.read_text(encoding='utf-8')
    if 'js' not in result:
        p = template_dir / 'app.js'
        if p.is_file():
            result['js'] = p.read_text(encoding='utf-8')
    # Fonts
    fonts_dir = template_dir / 'fonts'
    result['fonts_css'] = ''
    if fonts_dir.is_dir():
        fc = fonts_dir / 'fonts.css'
        if fc.is_file():
            result['fonts_css'] = fc.read_text(encoding='utf-8')
        result['fonts_b64'] = {}
        for woff2 in fonts_dir.glob('*.woff2'):
            b64 = base64.b64encode(woff2.read_bytes()).decode('ascii')
            result['fonts_b64'][woff2.name] = b64
    return result


def inline_fonts(fonts_css, fonts_b64):
    """Replace url('name.woff2') with base64 data URIs."""
    for name, b64 in fonts_b64.items():
        fonts_css = re.sub(
            r"url\(\s*['\"]?" + re.escape(name) + r"['\"]?\s*\)",
            f"url(data:font/woff2;base64,{b64})",
            fonts_css
        )
    return fonts_css


def build_web_component(template, card_data):
    """Build self-contained HTML for Web Component cards."""
    card_json = json.dumps(card_data, ensure_ascii=False)

    parts = ['<!DOCTYPE html>', '<html lang="zh-CN">', '<head>']
    parts.append('<meta charset="UTF-8">')
    parts.append('<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">')
    title = card_data.get('title', '卡片')
    parts.append(f'<title>{title}</title>')
    parts.append(f'<style>\n{template["css"]}\n</style>')
    if template.get('fonts_css'):
        parts.append(f'<style>\n{template["fonts_css"]}\n</style>')
    parts.append('</head>')
    parts.append('<body>')
    parts.append('<div id="root"></div>')
    parts.append(f'<script>\n{template["js"]}\n</script>')
    # Inject user data (runs after ai-card.js, overrides any test data)
    parts.append(f'''<script>
(function() {{
  var CARD = {card_json};
  var root = document.getElementById('root');
  if (!root) return;
  root.innerHTML = '';
  var el = document.createElement('ai-card');
  el.setAttribute('data', JSON.stringify(CARD).replace(/'/g, '&#39;'));
  root.appendChild(el);
}})();
</script>''')
    parts.append('</body></html>')
    return '\n'.join(parts)


def build_standalone(template, card_data):
    """Build self-contained HTML for standalone cards (answer-card)."""
    card_json = json.dumps(card_data, ensure_ascii=False)
    title = card_data.get('title', '炎图 AI 知识问答')

    parts = ['<!DOCTYPE html>', '<html lang="zh-CN">', '<head>']
    parts.append('<meta charset="UTF-8">')
    parts.append('<meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">')
    parts.append(f'<title>{title}</title>')
    parts.append(f'<style>\n{template["css"]}\n</style>')
    parts.append('</head>')
    parts.append('<body>')
    parts.append('<div class="qa-card" id="qaCard">')
    parts.append('  <div class="qa-card-body" id="qaBody">')
    parts.append('    <div class="qa-state" id="idleState">')
    parts.append('      <span class="state-icon">💡</span>')
    parts.append('      <p class="state-text">知识库问答结果将在这里展示</p>')
    parts.append('    </div>')
    parts.append('  </div>')
    parts.append('</div>')
    parts.append(f'<script>\n{template["js"]}\n</script>')
    parts.append(f'''<script>
(function() {{
  if (typeof renderAnswer === 'function') {{
    renderAnswer({card_json}, '');
  }}
}})();
</script>''')
    parts.append('</body></html>')
    return '\n'.join(parts)


def save_assets(save_dir, card, tpl, html):
    """Save data.json, ai-card.css, ai-card.js alongside the HTML."""
    d = Path(save_dir)
    d.mkdir(parents=True, exist_ok=True)
    (d / 'data.json').write_text(
        json.dumps(card, ensure_ascii=False, indent=2), encoding='utf-8')
    if tpl.get('css'):
        (d / 'ai-card.css').write_text(tpl['css'], encoding='utf-8')
    if tpl.get('fonts_css'):
        (d / 'fonts.css').write_text(tpl['fonts_css'], encoding='utf-8')
    if tpl.get('js'):
        (d / 'ai-card.js').write_text(tpl['js'], encoding='utf-8')
    (d / 'index.html').write_text(html, encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(prog='render')
    parser.add_argument('output', nargs='?', help='输出 HTML 文件路径')
    parser.add_argument('--save-dir', default=None, help='同时保存 data.json/css/js 到指定目录')
    args = parser.parse_args()

    raw = sys.stdin.read()
    if not raw.strip():
        print("错误：stdin 无输入", file=sys.stderr)
        sys.exit(1)
    try:
        card = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"JSON 解析错误：{e}", file=sys.stderr)
        sys.exit(1)

    ct = card.get('card_type', '')
    if not ct:
        print("错误：缺少 card_type 字段", file=sys.stderr)
        sys.exit(1)

    name, tpl_dir = find_template(ct)
    tpl = read_template(tpl_dir)

    # 保存原始 CSS/JS（内联字体前），供 --save-dir 使用
    raw_css = tpl.get('css', '')
    raw_fonts_css = tpl.get('fonts_css', '')
    if tpl.get('fonts_css'):
        tpl['fonts_css'] = inline_fonts(tpl['fonts_css'], tpl.get('fonts_b64', {}))

    if name == 'answer-card':
        html = build_standalone(tpl, card)
    else:
        html = build_web_component(tpl, card)

    out = args.output
    if out:
        Path(out).write_text(html, encoding='utf-8')
        print(out)
    else:
        print(html)

    if args.save_dir:
        # 用原始 CSS（未内联字体）保存，更干净可复用
        save_tpl = {**tpl, 'css': raw_css, 'fonts_css': raw_fonts_css}
        save_assets(args.save_dir, card, save_tpl, html)


if __name__ == '__main__':
    main()
