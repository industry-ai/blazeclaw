---
card_type: qa_answer
folder: Question and Answer Card-Answer
version: "1.0"
---

# 问答卡片 · 精简版 (Question and Answer Card-Answer)

精简 AI 知识问答结果卡片模板，专注回答展示。无头部、无快捷提问、无 API 健康检测、无输入栏，配置硬编码在 JS 中，由外部调用渲染函数。适用场景：嵌入已有页面、仅展示问答结果。

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
Question and Answer Card-Answer/
├── index.html       # 页面结构（卡片骨架 + 加载 CSS/JS）
├── style.css        # 卡片样式（渐变装饰条 + 文件类型色标 + 动画）
├── app.js           # 渲染逻辑（DOM 渲染 + 状态切换，配置硬编码，由外部调用）
└── metadata.md      # 本文件
```

> 与 `Question and Answer Card` 的区别：无 `data.json`，所有配置（API 地址、top_k、超时、文件图标映射）均硬编码在 `app.js` 中。

---

## JSON Schema

无外部配置文件。`app.js` 中硬编码以下常量：

```js
const API_BASE   = 'http://127.0.0.1:8899';
const QA_URL     = API_BASE + '/qa';
const TOP_K      = 5;
const TIMEOUT_MS = 30000;
```

API 请求体格式：

```json
{
  "question": "用户输入的问题",
  "top_k": 5
}
```

API 响应体预期格式：

```json
{
  "description": "基于知识库的回答文本…",
  "sources": ["[architecture] 系统架构设计.md", "[hr] 员工手册.docx"],
  "error": ""
}
```

---

## 字段说明

### 请求字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `question` | string | 是 | 用户输入的自然语言问题 |
| `top_k` | number | 是 | 知识库检索返回数量，固定 5 |

### 响应字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `description` | string | 否 | AI 回答正文。为空时回退到 `answer` 字段 |
| `answer` | string | 否 | AI 回答正文的备用字段 |
| `sources` | array | 否 | 来源文件名数组，格式 `[category] name.ext` |
| `error` | string | 否 | 服务端错误信息。存在时渲染错误块 |

---

## HTML 渲染结构

`app.js` 动态生成以下 DOM 树：

```
body
  └── .qa-card#qaCard                          ← 卡片根容器
  │     ├── ::before                           ← 顶部 3px 渐变装饰条（紫→紫罗兰→淡紫，shimmer 动画）
  │     └── .qa-card-body#qaBody               ← 卡片主体（可滚动）
  │           ├── [qa-state]                   ← 空闲状态
  │           │     ├── .state-icon 💡          ← 状态图标（80×80 渐变圆角方块）
  │           │     └── .state-text             ← 引导文案
  │           ├── [typing-dots]                ← 加载状态（三个跳动圆点）
  │           ├── [qa-answer]                  ← 回答结果
  │           │     ├── .answer-header          ← 回答头部
  │           │     │     ├── .answer-avatar "AI" ← AI 头像（紫蓝渐变方块）
  │           │     │     └── .answer-meta
  │           │     │           ├── .answer-label "AI 回答"
  │           │     │           └── .answer-badge "● 基于知识库"
  │           │     ├── .answer-text            ← 回答正文（渐变背景 + 左侧 3px 紫蓝渐变竖线）
  │           │     ├── .sources-block          ← 来源文件区
  │           │     │     ├── .sources-header
  │           │     │     │     ├── .sources-label 📂 "参考来源"
  │           │     │     │     └── .sources-count "N 个文件"
  │           │     │     └── .sources-files → .source-file × N
  │           │     │           ├── .file-icon-wrap.type-{ext}  ← 文件图标（按类型着色）
  │           │     │           ├── .file-info
  │           │     │           │     ├── .file-name
  │           │     │           │     └── .file-category  ← 分类标签（可选）
  │           │     │           └── .file-type-tag.tag-{ext}  ← 文件类型徽章（按类型着色）
  │           │     └── [qa-error]              ← 服务端错误块（可选）
  │           └── [qa-state]                   ← 空结果状态
  └── (无外部输入栏，渲染函数由外部调用)
```

### 与完整版的 DOM 差异

| 元素 | 完整版 | 精简版 |
|------|--------|--------|
| 卡片头部（.qa-card-header） | ✅ 有（logo + 标题 + 副标题） | ❌ 无 |
| 顶部渐变条（::before shimmer） | ❌ 无 | ✅ 有（3px 五色渐变 + 4s 流光动画） |
| 快捷提问（.quick-prompts） | ✅ 有 | ❌ 无 |
| API 状态栏（.api-status） | ✅ 有 | ❌ 无 |
| 回答 Avatar + Badge | ❌ 无（纯文本） | ✅ 有（AI 头像 + "基于知识库" 标签） |
| 回答正文渐变背景 | ❌ 无 | ✅ 有（淡紫渐变 + 左侧渐变竖线） |
| 来源分类标签 | ✅ 有（kc-tags） | ❌ 无（合并到 file-category） |
| 文件类型色标 | ❌ 无（单一灰色背景） | ✅ 有（6 种文件类型对应 6 种颜色） |
| 配置方式 | data.json 外部配置 | JS 内部硬编码 |
| 输入行 | ✅ 卡片内部 footer | ❌ 无（纯展示，由外部驱动） |

### 状态切换

| 状态 | 触发条件 | DOM |
|------|---------|-----|
| 空闲 | 页面加载后 | `.qa-state`（💡 + 引导文案） |
| 加载中 | 调用 `showLoading()` | `.typing-dots`（三个 10px 跳动圆点） |
| 回答 | 调用 `renderAnswer(data)` | `.qa-answer`（AI 头像 + 正文 + 来源文件） |
| 空结果 | 数据无有效内容 | `.qa-state`（🤔 + "未找到相关内容"） |
| 错误 | 调用 `showError(msg)` 或 API 返回 error | `.qa-error`（⚠️ 粉红背景 + 错误详情） |

### 自适应规则

| 条件 | 行为 |
|------|------|
| `data.description` 或 `data.answer` 为空 | `.answer-header` 和 `.answer-text` 不渲染 |
| `data.sources` 为空或无 | `.sources-block` 不渲染 |
| 文件名为 `[category] name.ext` 格式 | 自动解析分类名，显示在 `.file-category` 中 |
| 文件扩展名不在 `FILE_META` 中 | 使用 `FILE_OTHER`（📎 / cls: other）作为默认图标 |
| 网络错误 | 渲染红色错误块，显示错误消息 |
| API 返回 `error` 字段 | 渲染服务端错误块 |

---

## 数据变体

精简版只有一个变体，配置硬编码在 `app.js` 中：

| 属性 | 值 |
|------|-----|
| API 地址 | `http://127.0.0.1:8899` |
| QA 端点 | `/qa` |
| top_k | 5 |
| 超时 | 30s |

> 可扩展：修改 `app.js` 中的 `API_BASE`、`TOP_K`、`TIMEOUT_MS` 常量；修改 `FILE_META` 对象添加更多文件类型图标。

---

## 主题配色

| 元素 | 颜色 | 说明 |
|------|------|------|
| 主色 `--primary` | #5b5fe3 | 按钮、输入框聚焦、文件类型背景 |
| 主色浅 `--primary-soft` | #eeeffd | 来源 block 中 label 图标背景 |
| 辅助色 `--accent` | #8b5cf6 | 渐变中的紫色 |
| 成功 `--success` | #3ecf8e | "基于知识库" badge 颜色 |
| 危险 `--danger` | #e85d75 | 错误块背景和文字 |
| 卡片阴影 | `--shadow-card` | 多层叠加阴影，柔和立体感 |
| 输入阴影 | `--shadow-input` | 独立阴影 + 聚焦时紫色光晕 |
| 顶部渐变条 | #5b5fe3 → #8b5cf6 → #c084fc → #8b5cf6 → #5b5fe3 | shimmer 流光动画 |
| 回答正文背景 | linear-gradient(135deg, #fafbfe, #f8f7ff) | 淡紫渐变 + 边框 |
| 回答正文左侧线 | linear-gradient(180deg, #5b5fe3, #8b5cf6) | 3px 紫蓝渐变竖线 |

### 文件类型色标

| 扩展名 | 图标 | 图标背景色 | 徽章文字色 |
|--------|------|-----------|-----------|
| `md` | 📘 | #eef2ff（蓝） | #4f46e5 |
| `docx` / `doc` | 📄 | #e0f2fe（天蓝） | #0369a1 |
| `pptx` / `ppt` | 📊 | #fef3c7（黄） | #a16207 |
| `pdf` | 📕 | #fee2e2（红） | #b91c1c |
| `xlsx` / `xls` | 📈 | #dcfce7（绿） | #15803d |
| `txt` | 📝 | #f3f4f6（灰） | #4b5563 |
| 其他 | 📎 | #f3f4f6（灰） | #6b7280 |

---

## 响应式适配

3 端适配断点（`style.css` 中管理）：

| 端 | 断点 | 卡片 max-width | 主要变化 |
|----|------|---------------|---------|
| 手机端 | < 480px | 600px | 减小 padding 和 body 间距，状态图标缩小至 64px |
| 平板端 | ≥ 480px | 600px | 标准布局，卡片居中 |
| 大屏 / 电视 | ≥ 768px | 600px | 卡片居中，舒适间距 |

### 缩放策略

- **卡片宽度**：`max-width: 600px`，比完整版（520px）略宽，适配无 header 的简洁布局
- **移动端**：body padding 从 24px → 12px，body 的 padding-top 从 48px → 24px
- **状态图标**：80px → 64px（手机端），圆角从 24px → 20px
- **回答正文**：padding 从 18px/20px → 14px/16px，字体从 0.9rem → 0.85rem
- **来源文件区**：padding 从 18px/20px → 14px

---

## 渲染说明

1. **硬编码配置**：API 地址、top_k、超时、文件类型图标均在 `app.js` 中定义，无需外部配置文件，可直接嵌入其他页面
2. **渐变装饰条**：卡片顶部 `::before` 伪元素生成 3px 五色渐变条，`shimmer` 动画 4s 循环流光，视觉上更有 AI 科技感
3. **AI 回答 Avatar**：回答左侧显示 32×32 紫蓝渐变的 "AI" 文字头像 + "AI 回答" 标签 + 绿色 ● "基于知识库" badge，明确标识来源
4. **回答正文样式**：淡紫渐变背景 + 浅灰边框 + 左侧 3px 紫蓝渐变竖线，视觉上区分问题和回答
5. **文件类型色标**：`FILE_META` 定义了 6 种文件类型的图标、CSS class 和颜色，`.file-icon-wrap.type-{ext}` 和 `.file-type-tag.tag-{ext}` 为每种类型着不同背景色
6. **来源解析**：文件名格式 `[category] filename.ext`，自动解析出分类名和文件扩展名，分类名显示为文件信息下方的灰色标签
7. **外部驱动**：卡片不包含输入组件，`renderAnswer()`、`showLoading()`、`showError()` 三个公开函数供外部调用
8. **纯前端**：无框架依赖，零构建步骤，可直接在浏览器中打开
