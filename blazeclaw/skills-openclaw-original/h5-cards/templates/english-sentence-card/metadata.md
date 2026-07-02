---
card_type: english_sentence
folder: english-sentence-card
version: "1.0"
---

# 英语句子卡片 (english-sentence-card)

英语句子学习卡片模板，笔记本横线纸背景 + 便利贴卡片（缎带装饰），句子 + 中文翻译，点击发音（Web Speech API），支持跟读打分。

---

## 目录

1. [文件结构](#文件结构)
2. [JSON Schema](#json-schema)
3. [字段说明](#字段说明)
4. [HTML 渲染结构](#html-渲染结构)
5. [数据变体](#数据变体)
6. [主题配色](#主题配色)
7. [响应式适配](#响应式适配)
8. [Android & 华为适配](#android--华为适配)
9. [渲染说明](#渲染说明)

---

## 文件结构

```
english-sentence-card/
├── index.html         # 页面入口（零逻辑，只声明 #root + 加载 JS/CSS）
├── ai-card.css        # 组件样式 + 页面样式 + 响应式断点（Shadow DOM 内外共用）
├── ai-card.js         # 渲染引擎（Web Component + 语音交互 + 跟读打分）
├── fonts/             # 本地字体（Patrick Hand woff2 + fonts.css，无 CDN 依赖）
├── metadata.md        # 本文件
└── data.json          # 每日一句 · 卡片数据
```

---

## JSON Schema

```json
{
  "schema_version": "1.0",
  "card_type": "english_sentence",
  "title": "每日一句",
  "subtitle": "The best preparation for tomorrow is doing your best today.",
  "description": "为明天做的最好准备，就是今天做到最好。",
  "button_text": "句子发音",
  "theme": "sentence",
  "layout": {
    "variant": "english_sentence",
    "icon": "sentence"
  }
}
```

---

## 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `schema_version` | string | 是 | 固定 `"1.0"` |
| `card_type` | string | 是 | 固定 `"english_sentence"` |
| `title` | string | 是 | 缎带徽章文字（如「每日一句」） |
| `subtitle` | string | 是 | 英语句子（如 "The early bird catches the worm."），渲染为卡片主体的英文句子 |
| `description` | string | 否 | 中文翻译（如 "早起的鸟儿有虫吃。"），点击句子后短暂显示，3 秒后自动隐藏 |
| `button_text` | string | 否 | 按钮文案，默认"句子发音" |
| `theme` | string | 否 | 默认 `"sentence"`，THEME_MAP 映射到 blue |
| `layout.variant` | string | 否 | 固定 `"english_sentence"` |
| `layout.icon` | string | 否 | 固定 `"sentence"` |

---

## HTML 渲染结构

`ai-card.js` 中 `cardHTML(d)` 生成的 DOM 树：

```
<div class="sentence-card">                         ← 便利贴卡片（笔记本横线纸 + 圆角 + 阴影）
  <div class="sentence-ribbon">每日一句</div>          ← 蓝色缎带徽章（左上角，微微左倾）
  <div class="sentence-body">                        ← 卡片内容区
    <div class="sentence-text-area">               ← 句子区
      <div class="sentence-en" data-speak="...">   ← 英语句子（左对齐，支持多行换行，点击发音 + 显示翻译）
        The best preparation for tomorrow
        is doing your best today.
      </div>
      <div class="sentence-zh">                    ← 中文翻译（默认隐藏，点击后淡入，3秒后淡出）
        为明天做的最好准备，就是今天做到最好。
      </div>
    </div>
    <div class="actions">                            ← 按钮区
      <button class="btn primary">句子发音</button>    ← 蓝色圆角按钮
      <button class="btn shadow">跟读</button>         ← 跟读按钮（镂空样式）
    </div>
    <div class="shadow-feedback"></div>               ← 跟读反馈区（正确/错误/录音回放）
  </div>
</div>
```

### 页面级包装（仅 standalone index.html）

```
body
  └── #root
        └── <ai-card>  ← Web Component 渲染的便利贴卡片
```

### 交互行为

| 元素 | 触发 | 行为 |
|------|------|------|
| `.sentence-en` | click | 朗读英语句子 + 显示中文翻译（3秒） |
| `.btn.primary` | click | 朗读英语句子 |
| `.btn.shadow` | click | 先播放 TTS 范读 → 自动开启麦克风跟读 → 相似度打分反馈 |

朗读使用 `Web Speech API`（`speechSynthesis`），语速 0.8，音调 1.0，美式英语。浏览器不支持时静默跳过。

跟读使用 `Web Speech API`（`SpeechRecognition`），通过**词重叠相似度**（word overlap similarity ≥ 60%）判断是否正确，最多重试 3 次。

### 自适应规则

| 字段状态 | 行为 |
|---------|------|
| `subtitle` 缺失 | 卡片主体为空（异常状态） |
| `description` 缺失 | `.sentence-zh` 不渲染 |
| `button_text` 缺失 | 按钮显示默认文案"句子发音" |
| `speechSynthesis` 不可用 | 点击无反应，无报错 |
| `SpeechRecognition` 不可用 | 跟读按钮不渲染 |

---

## 数据变体

### data.json — 每日一句
| 属性 | 值 |
|------|-----|
| 缎带标题 | 每日一句 |
| 英语句子 | The best preparation for tomorrow is doing your best today. |
| 中文翻译 | 为明天做的最好准备，就是今天做到最好。 |
| theme | `sentence` |
| icon | `sentence` |

> 可扩展：添加更多句子数据文件（如 `daily-sentence-2.json`、`proverb-1.json` 等），只需新建 JSON 文件并加入 `FILES` 数组。

---

## 主题配色

| theme | 品牌色 | 浅色背景 | 适用场景 |
|-------|--------|---------|---------|
| `sentence` | #2563eb | #eff6ff | 英语句子（默认） |
| `abc` | #8e44ad | #f5f3ff | 字母/单词（兼容） |

> 缎带和按钮使用品牌色。`sentence` 主题默认使用蓝色（#2563eb），与单词卡片的紫色区分开。后续可扩展更多主题色。

---

## 响应式适配

5 端适配断点（`ai-card.css` 统一管理）：

| 端 | 断点 | 句子字号 | 卡片宽度 |
|----|------|---------|---------|
| 手机端 | < 480px | 28px | 420px |
| 平板端 | ≥ 480px | 30px | 460px |
| 大屏端 | ≥ 768px | 34px | 500px |
| 电脑端 | ≥ 1024px | 38px | 540px |
| 电视端 | ≥ 1440px | 42px | 600px |

### 缩放策略

- **句子字号**：从 28px → 42px（~+50%），适配各端阅读距离
- **缎带**：字体从 18px → 28px，padding 同步增大
- **句子对齐**：左对齐，多行换行时每行对齐左边缘
- **页面布局**：始终单列居中，适合学习场景

---

## Android & 华为适配

面向中国市场，CSS 已内置以下移动端适配：

| 适配项 | 属性 | 说明 |
|--------|------|------|
| 消除点击高亮 | `-webkit-tap-highlight-color: transparent` | 安卓 WebView 默认蓝/灰闪烁 |
| 消除点击延迟 | `touch-action: manipulation` | .btn / .btn-play 上消除 300ms 延迟 |
| 粘滞悬停修复 | `@media (hover: hover)` | .btn-play:hover 只在真悬停设备生效 |
| 华为字体回退 | `"HarmonyOS Sans SC"` → `"PingFang SC"` → `"Microsoft YaHei"` → `"Noto Sans SC"` | Patrick Hand 采用本地 woff2 加载，回退链为保险措施 |
| 字体平滑 | `-webkit-font-smoothing: antialiased` | EMUI/HarmonyOS 渲染优化 |
| 防字体缩放 | `-webkit-text-size-adjust: 100%` | :host / body / html 均设置 |
| 防下拉刷新干扰 | `overscroll-behavior: none` | body 层级，华为浏览器下拉刷新不影响卡片操作 |
| 语音识别 | 特性检测 `SpeechRecognition` 可用性 | 华为 EMUI WebView 可能不支持；`file://` 协议下不可用，自动隐藏跟读按钮 |

---

## 渲染说明

1. **笔记本横线纸背景**：`.sentence-card` 使用三层 CSS 背景（repeating 横线 + 米黄底色），模拟真实笔记本纸张
2. **subtitle 特殊用途**：`subtitle` 字段承载英语句子，渲染为卡片主体的英文文本，左对齐，支持自动换行
3. **description 特殊用途**：`description` 字段承载中文翻译，默认隐藏，点击句子/按钮后淡入显示，3 秒后自动淡出
4. **语音交互**：通过 `data-speak` 属性传递朗读文本，在 `connectedCallback` 中统一绑定 click 事件
5. **跟读打分**：使用词重叠相似度算法（word overlap similarity）判断发音准确性，≥60% 即认为正确
6. **录音回放**：跟读时自动录音，反馈区显示「听我的发音」按钮，点击可回放自己的录音与范读对比
7. **CSS 精简**：`ai-card.css` 包含组件样式（`.sentence-card`、`.sentence-ribbon` 等）和页面样式，不含冗余规则
8. **PALETTE + THEME_MAP**：JS 中定义 purple 和 blue 两种配色，`THEME_MAP` 将 `sentence` 映射到 blue、`abc` 映射到 purple
9. **两层字体策略**：`:host` 用 Patrick Hand + 系统无衬线回退；`.sentence-en` 英文 Patrick Hand `font-weight: 400`（单字重，无 faux-bold）；`.sentence-zh` 中文用宋体栈（`STSong / SimSun / Noto Serif SC`），`min-height: 1.5em` 预留空间，翻译淡入淡出不跳布局。本地 woff2 存储在 `fonts/` 子目录，通过 `<link rel="stylesheet" href="fonts/fonts.css">` 加载
10. **列表渲染**：`ai-card.js` 末尾 IIFE 自动 fetch `FILES` 数组中的 JSON，渲染到页面
