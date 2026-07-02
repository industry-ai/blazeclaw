---
card_type: media_preview
folder: media-card
version: "1.0"
---

# 媒体卡片 (media-card)

媒体预览卡片模板，150px 暗色预览区 + 播放按钮 + 类型/时长标签，适用于视频、音频、图片、文件等媒体资源分享。

---

## 目录

1. [文件结构](#文件结构)
2. [JSON Schema](#json-schema)
3. [字段说明](#字段说明)
4. [HTML 渲染结构](#html-渲染结构)
5. [数据变体](#数据变体)
6. [主题配色](#主题配色)
7. [响应式适配](#响应式适配)
8. [渲染说明](#渲染说明)

---

## 文件结构

```
media-card/
├── index.html         # 页面结构（定义数据 + 加载 JS）
├── ai-card.css        # 卡片样式（Shadow DOM 内生效）
├── ai-card.js         # 渲染引擎（Web Component + 列表渲染）
├── metadata.md        # 本文件
└── class-video.json   # 课堂录像
```

---

## JSON Schema

```json
{
  "schema_version": "1.0",
  "card_type": "media_preview",
  "title": "课堂录像 · 分数的加减法",
  "subtitle": "42:18",
  "description": "张老师 · 2026/05/24 上传 · MP4 · 1080p · 1.2 GB...",
  "button_text": "播放视频",
  "target_url": "https://media.example.com/video/math-fraction",
  "theme": "video",
  "layout": {
    "variant": "media_preview",
    "icon": "video"
  }
}
```

---

## 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `schema_version` | string | 是 | 固定 `"1.0"` |
| `card_type` | string | 是 | 固定 `"media_preview"` |
| `title` | string | 是 | 媒体标题，显示在预览区下方 |
| `subtitle` | string | 否 | **作为时长标签**显示在预览区右下角（如"42:18"）。缺失则隐藏 |
| `description` | string | 否 | 上传者、格式、大小、简介等元信息。缺失则隐藏 |
| `button_text` | string | 否 | 按钮文案，默认"播放视频" |
| `target_url` | string | 是 | 媒体资源地址 |
| `theme` | string | 否 | 默认 `"video"`，可选 `video`/`audio`/`image`/`file`，THEME_MAP 映射到 indigo |
| `layout.variant` | string | 否 | 固定 `"media_preview"` |
| `layout.icon` | string | 否 | `video` / `audio` / `image` / `file`。控制媒体类型标签和图标 |

---

## HTML 渲染结构

`ai-card.js` 中 `mediaHTML(d)` 生成的 DOM 树：

```
<div class="card">                            ← 卡片根容器
  <div class="media-area">                    ← 暗色媒体预览区（150px，#1e1b4b 背景）
    <span class="media-badge">视频</span>       ← 类型标签（左上角，半透明黑底）
    <div class="media-play">▶</div>            ← 播放按钮（52×52 白色圆形，居中）
    <span class="media-dur">42:18</span>       ← 时长标签（右下角，可选，subtitle 字段）
  </div>
  <div class="card-body">                     ← 卡片主体
    <div class="hdr">                         ← 头部行
      <div class="icon">🎬</div>               ← 图标（42×42，浅色背景 + 品牌色）
      <div class="hinfo">
        <div class="title">课堂录像 · 分数的加减法</div>   ← 标题
      </div>
    </div>
    <div class="desc">张老师 · 2026/05/24...</div>  ← 描述（可选）
    <div class="actions">                     ← 按钮区
      <button class="btn primary">播放视频</button>     ← 操作按钮
    </div>
  </div>
</div>
```

### 媒体类型标签

| layout.icon | 类型标签 | emoji 图标 |
|-------------|---------|-----------|
| `video` | 视频 | 🎬 |
| `audio` | 音频 | 🎧 |
| `image` | 图片 | 🖼 |
| `file` | 文件 | 📄 |

### 自适应规则

| 字段状态 | 行为 |
|---------|------|
| `subtitle` 缺失 | `span.media-dur` 不渲染（无时长标签） |
| `description` 缺失 | `div.desc` 不渲染 |
| `button_text` 缺失 | 按钮显示默认文案"播放视频" |
| `layout.icon` = `video` | 类型标签显示"视频"，图标 🎬 |
| `layout.icon` = `audio` | 类型标签显示"音频"，图标 🎧 |

---

## 数据变体

### class-video.json — 课堂录像
| 属性 | 值 |
|------|-----|
| 标题 | 课堂录像 · 分数的加减法 |
| 时长 | 42:18 |
| 类型 | 视频 |
| theme | `video` |
| icon | `video` |

> 可扩展：添加 `audio-preview.json`（音频）、`image-gallery.json`（图片）、`file-download.json`（文件）。

---

## 主题配色

| theme | 品牌色 | 浅色背景 | 适用场景 |
|-------|--------|---------|---------|
| `video` / `audio` / `image` / `file` | #4f46e5 | #eef2ff | 媒体预览 |

> 媒体卡片所有类型统一用靛蓝色（indigo），与暗色预览区（#1e1b4b）搭配。`theme` 使用媒体类型名作为语义标识。

---

## 响应式适配

5 端适配断点（`ai-card.css` 和 `index.html` 同步）：

| 端 | 断点 | 卡片 max-width | 页面布局 | 主要变化 |
|----|------|---------------|---------|---------|
| 手机端 | < 480px | 380px | 单列居中 | 预览区 150px、播放按钮 52px |
| 平板端 | ≥ 480px | 420px | 单列居中 | 预览区 170px、播放按钮 56px、图标 46px |
| 大屏端 | ≥ 768px | 460px | 双列网格 | 预览区 190px、播放按钮 60px、图标 50px |
| 电脑端 | ≥ 1024px | 500px | 双列网格（更宽） | 预览区 210px、播放按钮 66px、图标 54px |
| 电视端 | ≥ 1440px | 560px | 三列网格 | 预览区 240px、播放按钮 72px、图标 60px |

### 缩放策略

- **暗色预览区**：高度从 150px → 240px（+60%），保持 16:9 的视觉比例感
- **播放按钮**：直径从 52px → 72px，确保各端清晰可见
- **叠加标签**：`.media-badge` 和 `.media-dur` 字体/间距同步缩放
- **页面布局**：手机/平板单列 → 大屏/电脑双列 → 电视三列

---

## 渲染说明

1. **暗色预览区**：`.media-area` 固定 150px 高度，深紫色背景（#1e1b4b），与白色卡片主体形成对比
2. **subtitle 特殊用途**：与其他模板不同，此处的 `subtitle` 不作为副标题显示，而是作为**时长标签**渲染在预览区右下角
3. **CSS 精简**：`ai-card.css` 只包含 `.media-area`、`.media-play`、`.media-badge`、`.media-dur`、`.card-body`、`.hdr`、`.icon`、`.title`、`.desc`、`.actions`、`.btn`，不含 `.strip`、`.card-body::after`、`.bar`
4. **PALETTE + THEME_MAP**：JS 中只定义了 indigo 一种颜色，`THEME_MAP` 将 video/audio/image/file 统一映射到 indigo
5. **列表渲染**：`ai-card.js` 末尾 IIFE 读取全局 `CARDS` 变量，自动渲染所有卡片变体到 `#root` 容器
