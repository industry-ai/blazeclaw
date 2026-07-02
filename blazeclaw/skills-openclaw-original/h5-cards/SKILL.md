---
name: h5-cards
version: 1.0.0
author: 炎图科技
license: MIT
source: internal
tags: [h5, cards, render, html, static, oss]
compatibility: python>=3.10
allowed-tools: Bash(python:*)
description: 当用户想要「看」或「生成」某种卡片时触发——英语单词卡、英语句子卡、英语漫画卡、作业提醒卡、媒体预览卡、问答结果卡、推荐卡、任务卡、健康建议卡、AI助手欢迎卡、H5入口卡、英语输入卡。典型触发词：查单词/学单词/单词卡片 → english-word-card；每日一句/英语句子 → english-sentence-card；英语漫画 → comic-card；作业提醒/今天作业 → homework-reminder；播放视频/查看图片 → media-preview；AI助手/欢迎页 → assistant-welcome；推荐/猜你喜欢 → recommendation；任务/待办 → task；H5入口/打开页面 → h5-entry；问答结果/答案展示 → qa-answer；翻译输入/英语输入 → english-sentence-input。该 skill 负责 JSON→HTML→OSS 的渲染部署管线，不处理业务逻辑（英语评分用 english-scoring，作业制作用 homework-maker，医学/用药/健康提醒用 medical，知识检索用 knowledge-qa）。
---

# H5 Cards — H5 卡片渲染引擎

根据用户意图匹配 card_type，构造 JSON，渲染为自包含 HTML，上传 OSS 返回链接。支持 12 种卡片类型。

## 工作流程

**以下步骤必须严格按顺序执行：**

### 0. 前置：生成图片（仅 english_word）

`english_word` 卡片需要单词插图。在构造 JSON 之前，先调用 image-generator：

```bash
RESULT=$(python image-generator/scripts/generate.py --prompt "<中文释义>")
LOCAL_PATH=$(echo "$RESULT" | python -c "import sys,json; d=json.load(sys.stdin); print(d.get('local_path',''))")
IMG_URL=$(bash upload-to-oss/run.sh upload "$LOCAL_PATH" "image-generator/$(basename $LOCAL_PATH)")
```

将 `$IMG_URL` 填入 JSON 的 `target_url`。若 image-generator 返回 `"success": false`，`target_url` 置空。

### 1. 匹配 card_type

```bash
MATCH=$(python h5-cards/scripts/match.py "<用户意图>")
```

输出 JSON，包含 `card_type`、`theme`、`icon`、`button_text` 等字段，用作卡片 JSON 的基础。

### 2. 构造卡片 JSON

调用 enrich.py，传入用户原始意图和 match 结果，自动调用 DeepSeek API 根据 [references/card-schemas.md](references/card-schemas.md) 中的 schema 规则生成完整 JSON：

```bash
CARD_JSON=$(echo "$MATCH" | python h5-cards/scripts/enrich.py "<用户意图>")
```

enrich.py 自动读取对应 card_type 的 JSON 模板和构造规则，通过 LLM 填充 title、subtitle、description、target_url 等字段。所有卡片自动包含 `schema_version: "1.0"` 和 `card_type` 字段。

支持 `--no-llm` 跳过 LLM 调用直接输出 match 结果（调试用），`--model` 切换模型。

卡片类型速览：

| card_type | 模板目录 | 渲染方式 | schema 参考 |
|-----------|---------|---------|------------|
| `h5_entry` | text-card | Web Component | card-schemas.md § text-card 系列 |
| `assistant_welcome` | text-card | Web Component | card-schemas.md § text-card 系列 |
| `recommendation` | text-card | Web Component | card-schemas.md § text-card 系列 |
| `task` | text-card | Web Component | card-schemas.md § text-card 系列 |
| `health_advice` | text-card | Web Component | card-schemas.md § text-card 系列 |
| `homework_reminder` | homework-card | Web Component | card-schemas.md § homework-card |
| `media_preview` | media-card | Web Component | card-schemas.md § media-card |
| `english_word` | english-word-card | Web Component | card-schemas.md § english-word-card |
| `english_sentence` | english-sentence-card | Web Component | card-schemas.md § english-sentence-card |
| `english_sentence_input` | english-input-card | Web Component | card-schemas.md § english-input-card |
| `comic_strip` | comic-card | Web Component | card-schemas.md § comic-card |
| `qa_answer` | answer-card | Standalone (`renderAnswer()`) | card-schemas.md § answer-card |

### 3. 渲染 + 部署（一步完成）

```bash
OSS_URL=$(echo "$CARD_JSON" | bash h5-cards/scripts/deploy.sh)
```

deploy.sh 内部流程：render.py 生成 HTML + 保存 data.json/css/js → 上传所有文件到 OSS 同一目录 → 输出 index.html 的公开 URL。

### 4. 调用 format-result 生成标准响应

```bash
python format-result/scripts/format.py \
  --skill-id h5-cards \
  --skill-name "h5-cards" \
  --url "$OSS_URL" \
  --url-title "查看卡片"
```

失败时：

```bash
python format-result/scripts/format.py \
  --skill-id h5-cards \
  --skill-name "h5-cards" \
  --error "错误描述"
```

将输出作为本技能最终返回值。

## 自包含 HTML 生成规则

1. **CSS 内联**：`ai-card.css`（或 `style.css`）原样插入 `<style>` 标签
2. **字体内联**：`fonts/*.woff2` 以 base64 data URI 内联到 `@font-face` 规则中
3. **JS 内联**：`ai-card.js`（或 `app.js`）插入 `<script>` 标签，移除测试数据加载，替换为用户数据注入
4. **Web Component 卡片**：渲染为 `<ai-card data='...'></ai-card>` 挂载到 `#root`
5. **Standalone 卡片**（answer-card）：调用 `renderAnswer(data)` 渲染

## 文件结构

```
h5-cards/
├── SKILL.md
├── references/
│   └── card-schemas.md   # JSON 模板与构造规则
├── scripts/
│   ├── match.py          # 意图 → card_type 匹配
│   ├── enrich.py         # match 结果 → 完整卡片 JSON（LLM）
│   ├── render.py         # JSON → 自包含 HTML
│   ├── deploy.sh         # 渲染 + 上传 OSS 编排
│   └── test_all_cards.py # 全量回归测试
├── assets/
│   ├── image/
│   └── video/
└── templates/
    ├── text-card/
    ├── homework-card/
    ├── media-card/
    ├── english-word-card/
    ├── english-sentence-card/
    ├── english-input-card/
    ├── comic-card/
    └── answer-card/
```
