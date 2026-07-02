---
card_type: homework_reminder
folder: homework-card
version: "1.0"
---

# 作业提醒卡片 (homework-card)

学科色条横幅模板，适用于布置作业、学习任务提醒等场景。

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
homework-card/
├── index.html       # 页面结构（定义数据 + 加载 JS）
├── ai-card.css      # 卡片样式（Shadow DOM 内生效）
├── ai-card.js       # 渲染引擎（Web Component + 列表渲染）
├── metadata.md      # 本文件
├── chinese.json     # 语文作业
├── math.json        # 数学作业
└── english.json     # 英语作业
```

---

## JSON Schema

```json
{
  "schema_version": "1.0",
  "card_type": "homework_reminder",
  "title": "语文 · 阅读理解训练",
  "subtitle": "张老师 · 三年级二班",
  "description": "阅读课文《荷花》第2-4自然段...",
  "button_text": "查看作业",
  "target_url": "https://homework.example.com/chinese",
  "theme": "chinese",
  "layout": {
    "variant": "homework_reminder",
    "icon": "chinese"
  }
}
```

---

## 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `schema_version` | string | 是 | 固定 `"1.0"` |
| `card_type` | string | 是 | 固定 `"homework_reminder"` |
| `title` | string | 是 | 作业标题，如"语文 · 阅读理解训练"。显示在色条横幅中 |
| `subtitle` | string | 否 | 教师 + 班级信息，如"张老师 · 三年级二班"。缺失则隐藏 |
| `description` | string | 否 | 作业详细说明。缺失则隐藏 |
| `button_text` | string | 否 | 按钮文案，默认"查看作业" |
| `target_url` | string | 是 | 点击按钮跳转地址 |
| `theme` | string | 否 | `chinese` / `math` / `english`。语义名，THEME_MAP 映射到颜色，驱动学科配色和标签 |
| `layout.variant` | string | 否 | 固定 `"homework_reminder"` |
| `layout.icon` | string | 否 | `chinese` / `math` / `english`。控制图标 emoji |

---

## HTML 渲染结构

`ai-card.js` 中 `homeworkHTML(d)` 生成的 DOM 树：

```
<div class="card">                         ← 卡片根容器
  <div class="bar">                        ← 学科色条横幅（渐变背景）
    <div class="bicon">📖</div>             ← 学科图标（42×42，半透明白底）
    <div class="binfo">                    ← 标题信息区
      <div class="btitle">语文 · 阅读理解训练</div>   ← 作业标题（白色加粗）
      <div class="bsub">张老师 · 三年级二班</div>      ← 副标题（可选，85% 透明度）
      <span class="bbadge">语文</span>                ← 学科标签（可选，theme 驱动）
    </div>
  </div>
  <div class="card-body">                  ← 卡片主体
    <div class="desc">阅读课文...</div>       ← 描述（可选）
    <div class="actions">                  ← 按钮区
      <button class="btn primary">查看作业</button>   ← 操作按钮
    </div>
  </div>
</div>
```

### 自适应规则

| 字段状态 | 行为 |
|---------|------|
| `subtitle` 缺失 | `div.bsub` 不渲染 |
| `description` 缺失 | `div.desc` 不渲染 |
| `button_text` 缺失 | 按钮显示默认文案"查看作业" |
| `theme` = `chinese` | 红色渐变横幅 + 语文标签 |
| `theme` = `math` | 蓝色渐变横幅 + 数学标签 |
| `theme` = `english` | 绿色渐变横幅 + 英语标签 |

---

## 数据变体

### chinese.json — 语文作业
| 属性 | 值 |
|------|-----|
| 标题 | 语文 · 阅读理解训练 |
| 学科 | 语文 |
| theme | `chinese` |
| icon | `chinese` |

### math.json — 数学作业
| 属性 | 值 |
|------|-----|
| 标题 | 数学 · 第三章分数练习 |
| 学科 | 数学 |
| theme | `math` |
| icon | `math` |

### english.json — 英语作业
| 属性 | 值 |
|------|-----|
| 标题 | 英语 · Unit 5 My Day |
| 学科 | 英语 |
| theme | `english` |
| icon | `english` |

---

## 主题配色

| theme | 品牌色 | 渐变 | 浅色背景 | 对应学科 |
|-------|--------|------|---------|---------|
| `chinese` | #dc2626 | #dc2626 → #f87171 | #fef2f2 | 语文 |
| `math` | #3b82f6 | #3b82f6 → #60a5fa | #eff6ff | 数学 |
| `english` | #16a34a | #16a34a → #4ade80 | #f0fdf4 | 英语 |

---

## 响应式适配

5 端适配断点（`ai-card.css` 和 `index.html` 同步）：

| 端 | 断点 | 卡片 max-width | 页面布局 | 主要变化 |
|----|------|---------------|---------|---------|
| 手机端 | < 480px | 380px | 单列居中 | 基准尺寸 |
| 平板端 | ≥ 480px | 420px | 单列居中 | 横幅图标 46px、标题 17px、内边距 18px |
| 大屏端 | ≥ 768px | 460px | 双列网格 | 横幅图标 50px、标题 18px、内边距 20px |
| 电脑端 | ≥ 1024px | 500px | 双列网格（更宽） | 横幅图标 54px、标题 19px、内边距 24px |
| 电视端 | ≥ 1440px | 560px | 三列网格 | 横幅图标 60px、标题 21px、内边距 28px |

### 缩放策略

- **横幅区域**：`.bar` 内边距和图标尺寸同步缩放，保持视觉比例
- **学科标签**：`.bbadge` 字体从 10px → 13px，padding 从 3px/9px → 4px/14px
- **页面布局**：手机/平板单列 → 大屏/电脑双列 → 电视三列

---

## 渲染说明

1. **语义化主题**：`theme` 直接使用学科名（`chinese`/`math`/`english`），`THEME_MAP` 映射到颜色（chinese→red, math→blue, english→green）
2. **标签显示**：`subjectLabel(theme)` 直接将语义名转为中文标签，显示在横幅右下角
3. **无 badge 冲突**：作业卡片不使用 BADGES 映射，学科标签由 theme 直接驱动
4. **CSS 精简**：`ai-card.css` 只包含 `.bar`、`.card-body`、`.desc`、`.actions`、`.btn` 等作业卡片用到的样式，不含 `.strip`、`.card-body::after`、`.hdr`、`.icon`、`.media-area` 等无关样式
5. **列表渲染**：`ai-card.js` 末尾 IIFE 读取全局 `CARDS` 变量，自动渲染所有卡片变体到 `#root` 容器
