---
card_type: english_word
folder: english-word-card
version: "1.0"
---

# 英语单词卡片 (english-word-card)

英语单词启蒙卡片模板，笔记本横线纸背景 + 便利贴卡片（胶带 + 缎带装饰），左侧大字母 + 右侧单词与实物图片，点击发音（Web Speech API）。

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
english-word-card/
├── index.html         # 页面入口（零逻辑，只声明 #root + 加载 JS/CSS）
├── ai-card.css        # 组件样式 + 页面样式 + 响应式断点（Shadow DOM 内外共用）
├── ai-card.js         # 渲染引擎（Web Component + 语音交互 + 列表渲染）
├── fonts/             # 本地字体（Patrick Hand woff2 + fonts.css，无 CDN 依赖）
├── metadata.md        # 本文件
└── data.json          # Aa · apple 卡片数据
```

---

## JSON Schema

```json
{
  "schema_version": "1.0",
  "card_type": "english_word",
  "title": "ABC · 字母启蒙",
  "subtitle": "Aa",
  "description": "apple",
  "button_text": "开始学习",
  "target_url": "https://works.blazegraph.site/works/.../img16.png",
  "theme": "abc",
  "layout": {
    "variant": "english_word",
    "icon": "abc"
  }
}
```

---

## 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `schema_version` | string | 是 | 固定 `"1.0"` |
| `card_type` | string | 是 | 固定 `"english_word"` |
| `title` | string | 是 | 缎带徽章文字（如「ABC · 字母启蒙」） |
| `subtitle` | string | 是 | 字母组合（如 "Aa"），首字符为大写字母，其余为小写。渲染为左侧大号字母 |
| `description` | string | 否 | 英文单词（如 "apple"），渲染为右侧单词标签。缺失则隐藏 |
| `button_text` | string | 否 | 按钮文案，默认"开始学习" |
| `target_url` | string | 是 | 实物图片地址 |
| `theme` | string | 否 | 默认 `"abc"`，THEME_MAP 映射到 purple |
| `layout.variant` | string | 否 | 固定 `"english_word"` |
| `layout.icon` | string | 否 | 固定 `"abc"` |

---

## HTML 渲染结构

`ai-card.js` 中 `cardHTML(d)` 生成的 DOM 树：

```
<div class="abc-card">                         ← 便利贴卡片（半透明白底 + 圆角 + 阴影）
  <div class="tape-decor"></div>                ← 胶带装饰（顶部居中，半透明白色 + 虚线边）
  <div class="abc-ribbon">ABC · 字母启蒙</div>   ← 紫色缎带徽章（左上角，微微左倾）
  <div class="abc-body">                        ← 卡片内容区
    <div class="abc-row">                       ← 左右布局行
      <div class="abc-left">                    ← 左侧：大字母
        <div class="big-letter" data-speak="Aa">
          <span class="letter-upper">A</span>   ← 大写字母（60px + 文字阴影）
          <span class="letter-lower">a</span>   ← 小写字母（40px + 文字阴影）
        </div>
      </div>
      <div class="abc-right">                   ← 右侧：单词 + 图片
        <div class="word-label" data-speak="apple">apple</div>   ← 单词标签（点击发音）
        <img class="abc-img" src="..." alt="apple" data-speak="apple">   ← 实物图（点击发音）
      </div>
    </div>
    <div class="actions">                       ← 按钮区
      <button class="btn primary">开始学习</button>   ← 紫色圆角按钮
    </div>
  </div>
</div>
```

### 页面级包装（仅 standalone index.html）

```
body
  └── #root
        └── .page-container          ← 笔记本横线纸（三层背景：光晕 + 横线 + 米黄底）
              └── .main-scroll-wrapper
                    └── .game-area
                          └── .abc-area
                                └── <ai-card>  ← Web Component 渲染的便利贴卡片
```

### 交互行为

| 元素 | 触发 | 行为 |
|------|------|------|
| `.big-letter` | click | 朗读字母（如 "Aa"） |
| `.word-label` | click | 朗读单词（如 "apple"） |
| `.abc-img` | click | 朗读单词（如 "apple"） |

朗读使用 `Web Speech API`（`speechSynthesis`），语速 0.85，音调 1.1，美式英语。浏览器不支持时静默跳过。

### 自适应规则

| 字段状态 | 行为 |
|---------|------|
| `subtitle` 缺失 | 回退显示 "Aa" |
| `description` 缺失 | `.word-label` 不渲染 |
| `target_url` 缺失 | `.abc-img` 不渲染 |
| `button_text` 缺失 | 按钮显示默认文案"开始学习" |
| `speechSynthesis` 不可用 | 点击无反应，无报错 |

---

## 数据变体

### data.json — Aa · apple
| 属性 | 值 |
|------|-----|
| 缎带标题 | ABC · 字母启蒙 |
| 字母 | Aa |
| 单词 | apple |
| theme | `abc` |
| icon | `abc` |

> 可扩展：添加 `b-for-ball.json`（Bb · ball）、`c-for-cat.json`（Cc · cat）等，只需新建 JSON 文件并加入 `FILES` 数组。

---

## 主题配色

| theme | 品牌色 | 浅色背景 | 适用场景 |
|-------|--------|---------|---------|
| `abc` | #8e44ad | #f5f3ff | 字母启蒙 |

> 缎带和按钮使用品牌色（紫色 #8e44ad）。`theme` 使用 `abc` 作为语义标识。后续可扩展不同主题色（如动物卡片、水果卡片等）。

---

## 响应式适配

5 端适配断点（`ai-card.css` 统一管理）：

| 端 | 断点 | 卡片变化 | 页面变化 |
|----|------|---------|---------|
| 手机端 | < 480px | 大写 60px / 小写 40px / 图片 80px / 圆角 10px | 单列居中，800px 宽内容区 |
| 平板端 | ≥ 480px | 大写 68px / 小写 46px / 图片 90px / 缎带 20px | 内容区全宽 |
| 大屏端 | ≥ 768px | 大写 80px / 小写 54px / 图片 100px / 圆角 12px / 间距 24px | padding 增大 |
| 电脑端 | ≥ 1024px | 大写 96px / 小写 64px / 图片 110px / 圆角 14px / 间距 32px | padding 继续增大 |
| 电视端 | ≥ 1440px | 大写 112px / 小写 74px / 图片 130px / 圆角 16px / 间距 40px | 48px padding，适配大屏视距 |

### 缩放策略

- **字母**：从 60px/40px → 112px/74px（~+87%），保持大小写比例约 3:2
- **胶带**：宽度从 100px → 180px，高度从 30px → 46px
- **缎带**：字体从 18px → 28px，padding 同步增大
- **图片**：从 80px → 130px（+62.5%）
- **页面布局**：始终单列居中，适合儿童学习场景

---

## Android & 华为适配

面向中国市场，CSS 已内置以下移动端适配：

| 适配项 | 属性 | 说明 |
|--------|------|------|
| 消除点击高亮 | `-webkit-tap-highlight-color: transparent` | 安卓 WebView 默认蓝/灰闪烁 |
| 消除点击延迟 | `touch-action: manipulation` | .btn / .btn-play 上消除 300ms 延迟 |
| 粘滞悬停修复 | `@media (hover: hover)` | .abc-img:hover / .btn-play:hover 只在真悬停设备生效 |
| 华为字体回退 | `"HarmonyOS Sans SC"` → `"PingFang SC"` → `"Microsoft YaHei"` → `"Noto Sans SC"` | Patrick Hand 采用本地 woff2 加载，回退链为保险措施 |
| 字体平滑 | `-webkit-font-smoothing: antialiased` | EMUI/HarmonyOS 渲染优化 |
| 防字体缩放 | `-webkit-text-size-adjust: 100%` | :host / body / html 均设置 |
| 防下拉刷新干扰 | `overscroll-behavior: none` | body 层级，华为浏览器下拉刷新不影响卡片操作 |
| 语音识别 | 特性检测 `SpeechRecognition` 可用性 | 华为 EMUI WebView 可能不支持；`file://` 协议下不可用 |

---

## 渲染说明

1. **笔记本横线纸背景**：`.page-container` 使用三层 CSS 背景（径向光晕 + repeating 横线 + 米黄底色），仅在 standalone `index.html` 中生效，demo 页面中不渲染（Shadow DOM 隔离）
2. **subtitle 特殊用途**：`subtitle` 字段承载字母组合（如 "Aa"），渲染为左侧大号字母。首字符为大写、其余为小写，分别使用不同的字号和阴影
3. **description 特殊用途**：`description` 字段承载英文单词（如 "apple"），渲染为右侧单词标签，同时作为图片的 alt 文本
4. **语音交互**：通过 `data-speak` 属性传递朗读文本，在 `connectedCallback` 中统一绑定 click 事件
5. **CSS 精简**：`ai-card.css` 包含组件样式（`.abc-card`、`.tape-decor`、`.abc-ribbon` 等）和页面样式（`.page-container`、`.main-scroll-wrapper` 等），不含 `.strip`、`.bar`、`.media-area`
6. **PALETTE + THEME_MAP**：JS 中只定义了 purple 一种颜色，`THEME_MAP` 将 abc 映射到 purple
7. **本地字体**：使用 Patrick Hand 手写字体（单字重，所有文字元素 `font-weight: 400`，无 faux-bold），woff2 格式存储在卡片目录的 `fonts/` 子目录中，通过 `<link rel="stylesheet" href="fonts/fonts.css">` 加载，无 CDN 依赖
8. **列表渲染**：`ai-card.js` 末尾 IIFE 自动 fetch `FILES` 数组中的 JSON，渲染到页面
