---
card_type: h5_entry / assistant_welcome / recommendation / task / health_advice
folder: text-card
version: "1.0"
---

# 文本卡片 (text-card)

通用文本类卡片模板，覆盖 5 种 card_type。顶部渐变装饰条 + 左侧品牌色条 + 图标 + 标题 + 描述 + 按钮。

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
text-card/
├── index.html              # 页面结构（定义数据 + 加载 JS）
├── ai-card.css             # 卡片样式（Shadow DOM 内生效）
├── ai-card.js              # 渲染引擎（Web Component + 列表渲染）
├── metadata.md             # 本文件
├── h5-ai-assistant.json    # H5 入口 · 炎图 AI 助手
├── h5-essay.json           # H5 入口 · 作文范文
├── edu-ai.json             # AI 助手 · 教育场景
├── medical-ai.json         # AI 助手 · 医疗场景
├── oral-practice.json      # 内容推荐 · 口语练习
├── weekly-report.json      # 任务协作 · 教学周报
└── medication-reminder.json # 健康建议 · 服药提醒
```

---

## JSON Schema

```json
{
  "schema_version": "1.0",
  "card_type": "h5_entry",
  "title": "炎图 AI 助手",
  "subtitle": "我已加入当前聊天室",
  "description": "可以为你提供智能协同服务，点击查看推荐内容。",
  "button_text": "打开页面",
  "target_url": "https://www.baidu.com",
  "theme": "general",
  "layout": {
    "variant": "h5_entry",
    "icon": "ai"
  }
}
```

---

## 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `schema_version` | string | 是 | 固定 `"1.0"` |
| `card_type` | string | 是 | `h5_entry` / `assistant_welcome` / `recommendation` / `task` / `health_advice` |
| `title` | string | 是 | 卡片标题 |
| `subtitle` | string | 否 | 副标题，灰色小字。缺失则隐藏 |
| `description` | string | 否 | 卡片正文描述。缺失则隐藏 |
| `button_text` | string | 否 | 按钮文案，默认"打开页面" |
| `target_url` | string | 是 | 点击按钮跳转地址 |
| `theme` | string | 否 | 语义名（`general`/`ai`/`recommendation`/`task`/`health`），THEME_MAP 映射到颜色 |
| `layout.variant` | string | 否 | 与 `card_type` 一致 |
| `layout.icon` | string | 否 | `ai` / `link` / `audio` / `sparkle` / `task` / `health` |

---

## HTML 渲染结构

`ai-card.js` 中 `defaultHTML(d)` 生成的 DOM 树：

```
<div class="card">                          ← 卡片根容器
  <div class="strip"></div>                 ← 顶部 3px 渐变装饰条
  <div class="card-body">                   ← 卡片主体
    <div class="hdr">                       ← 头部：图标 + 标题行
      <div class="icon">🤖</div>             ← 图标（42×42，浅色背景 + 品牌色文字）
      <div class="hinfo">                   ← 标题信息区
        <div class="title">炎图 AI 助手</div>          ← 标题（16px 加粗）
        <div class="subtitle">我已加入当前聊天室</div>    ← 副标题（可选，灰色 11px）
        <span class="badge">AI 助手</span>             ← 类型标签（可选，card_type 驱动）
      </div>
    </div>
    <div class="desc">可以为你提供智能协同...</div>  ← 描述（可选）
    <div class="actions">                   ← 按钮区
      <button class="btn primary">打开页面</button>    ← 操作按钮
    </div>
  </div>
</div>
```

### card_type 与 badge 映射

| card_type | badge 文案 |
|-----------|-----------|
| `h5_entry` | H5 入口 |
| `assistant_welcome` | AI 助手 |
| `recommendation` | AI 推荐 |
| `task` | （无） |
| `health_advice` | （无） |

### 左侧色条

`card-body::after` 伪元素生成 4px 宽品牌色竖条，位于卡片左侧。

### 自适应规则

| 字段状态 | 行为 |
|---------|------|
| `subtitle` 缺失 | `div.subtitle` 不渲染 |
| `description` 缺失 | `div.desc` 不渲染 |
| `button_text` 缺失 | 按钮显示默认文案"打开页面" |
| badge 为空字符串 | `span.badge` 不渲染 |

---

## 数据变体

### h5-ai-assistant.json — 炎图 AI 助手
| 属性 | 值 |
|------|-----|
| card_type | `h5_entry` |
| theme | `general` |
| icon | `ai` |
| 场景 | 聊天室中 AI 助手入口卡片 |

### h5-essay.json — 作文范文
| 属性 | 值 |
|------|-----|
| card_type | `h5_entry` |
| theme | `general` |
| icon | `link` |
| 场景 | 作文范文链接入口 |

### edu-ai.json — 教育 AI 助手
| 属性 | 值 |
|------|-----|
| card_type | `assistant_welcome` |
| theme | `ai` |
| icon | `ai` |
| 场景 | 教育场景 AI 助手欢迎页 |

### medical-ai.json — 医疗 AI 助手
| 属性 | 值 |
|------|-----|
| card_type | `assistant_welcome` |
| theme | `health` |
| icon | `ai` |
| 场景 | 医疗场景 AI 助手欢迎页 |

### oral-practice.json — 英语口语推荐
| 属性 | 值 |
|------|-----|
| card_type | `recommendation` |
| theme | `recommendation` |
| icon | `audio` |
| 场景 | AI 推荐口语练习内容 |

### weekly-report.json — 教学周报
| 属性 | 值 |
|------|-----|
| card_type | `task` |
| theme | `task` |
| icon | `task` |
| 场景 | 多人协作任务，各科老师提交周报 |

### medication-reminder.json — 服药提醒
| 属性 | 值 |
|------|-----|
| card_type | `health_advice` |
| theme | `emerald` |
| icon | `health` |
| 场景 | 用药提醒，含剂量、禁忌、疗程信息 |

---

## 主题配色

| theme | 品牌色 | 浅色背景 | 适用场景 |
|-------|--------|---------|---------|
| `general` | #3b82f6 | #eff6ff | 通用、入口 |
| `ai` | #8b5cf6 | #f5f3ff | AI 助手 |
| `recommendation` | #0891b2 | #ecfeff | 内容推荐 |
| `task` | #d97706 | #fffbeb | 任务协作 |
| `health` | #059669 | #ecfdf5 | 健康、医疗 |

---

## 响应式适配

5 端适配断点（`ai-card.css` 和 `index.html` 同步）：

| 端 | 断点 | 卡片 max-width | 页面布局 | 主要变化 |
|----|------|---------------|---------|---------|
| 手机端 | < 480px | 380px | 单列居中 | 基准尺寸 |
| 平板端 | ≥ 480px | 420px | 单列居中 | 图标 46px、标题 17px、内边距 18px |
| 大屏端 | ≥ 768px | 460px | 双列网格 | 图标 50px、标题 18px、装饰条 4px |
| 电脑端 | ≥ 1024px | 500px | 双列网格（更宽） | 图标 54px、标题 19px、内边距 24px |
| 电视端 | ≥ 1440px | 560px | 三列网格 | 图标 60px、标题 21px、内边距 28px |

### 缩放策略

- **渐进式**：所有尺寸（字体、图标、间距、圆角）随视口宽度线性递增
- **卡片宽度**：380 → 420 → 460 → 500 → 560px（每档约 +10%）
- **页面布局**：手机/平板单列 → 大屏/电脑双列 → 电视三列
- **按钮触摸区域**：手机端 10px padding → 电视端 14px padding（确保远距离可点击）

---

## 渲染说明

1. **语义化主题**：`theme` 使用语义名（如 `general`、`ai`），通过 `THEME_MAP` 映射到 PALETTE 颜色。视觉差异由 `theme`、`icon`、`badge` 字段控制
2. **CSS 精简**：`ai-card.css` 包含 `.strip`、`.card-body::after`、`.hdr`、`.icon`、`.badge`、`.desc`、`.actions`、`.btn`，不含 `.bar`（作业）、`.media-area`（媒体）
3. **列表渲染**：`ai-card.js` 末尾 IIFE 读取全局 `CARDS` 变量，自动渲染所有卡片变体到 `#root` 容器
4. **PALETTE + THEME_MAP**：JS 中定义了 5 种颜色（blue/purple/cyan/amber/emerald），通过 THEME_MAP 将语义主题名映射到颜色
