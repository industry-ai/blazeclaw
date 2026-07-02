---
name: format-result
version: 1.0.0
author: 炎图科技
license: MIT
source: internal
tags: [format, json, result, standard, openclaw]
compatibility: python>=3.10
allowed-tools: Bash(python:*)
description: Format skill processing results into standardized OpenClaw JSON responses with support for text, webview URL, and error outputs. Use when any skill (tts, stt, scene-manager, knowledge-qa, etc.) completes processing and needs to return a unified response format — must always be called as the final step of every yantu skill pipeline. Do not use when the caller needs custom JSON structures, streaming responses, or output formats that diverge from the OpenClaw standard schema.
---

# Format Result — 标准结果格式化

将处理结果格式化为 OpenClaw 标准 JSON 响应。

所有 yantu skill 完成处理后的最后一步，必须调用本 skill 生成统一的返回格式。

## 成功场景

```bash
# 仅 URL（TTS、文件上传类）
python format-result/scripts/format.py \
  --skill-id tts \
  --skill-name "tts" \
  --url "https://oss.example.com/audio.wav" \
  --url-title "卡片模板"

# 文本 + URL（STT 类）
python format-result/scripts/format.py \
  --skill-id stt \
  --skill-name "stt" \
  --text "识别出的文本" \
  --text-title "识别结果" \
  --url "https://oss.example.com/text.txt" \
  --url-title "文本文件"

# 纯文本（问答类，无需 OSS）
python format-result/scripts/format.py \
  --skill-id knowledge-qa \
  --skill-name "knowledge-qa" \
  --text "回答内容" \
  --text-title "回答"
```

## 失败场景

```bash
python format-result/scripts/format.py \
  --skill-id tts \
  --skill-name "tts" \
  --error "TTS 服务连接失败"
```

## 参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--skill-id` | skill 标识 | 必填 |
| `--skill-name` | skill 显示名称 | 必填 |
| `--url` | OSS 文件 URL | 无 |
| `--text` | 纯文本结果 | 无 |
| `--text-title` | 文本结果标题 | `"结果"` |
| `--url-title` | URL 结果标题 | `"文件预览"` |
| `--error` | 错误描述（设置 status=error） | 无 |

## 返回格式

`outputs` 中的条目类型由各 skill 传入的参数决定：

| 参数组合 | output type | 适用技能 |
|----------|-------------|----------|
| 仅 `--url` | `webview` | tts、upload-to-oss |
| 仅 `--text` | `text` | knowledge-qa、scene-manager |
| `--text` + `--url` | `text` + `webview` | stt |

### 仅 URL（tts、upload-to-oss）

```json
{
  "providerId": "openclaw",
  "skillId": "tts",
  "skillName": "tts",
  "status": "done",
  "summary": "处理完成",
  "outputs": [
    {"type": "webview", "title": "卡片模板", "url": "https://oss.example.com/audio.wav"}
  ]
}
```

### 仅文本（knowledge-qa、scene-manager）

```json
{
  "providerId": "openclaw",
  "skillId": "knowledge-qa",
  "skillName": "knowledge-qa",
  "status": "done",
  "summary": "处理完成",
  "outputs": [
    {"type": "text", "title": "回答", "text": "根据公司规定，餐补标准为...", "url": null}
  ]
}
```

### 文本 + URL（stt）

```json
{
  "providerId": "openclaw",
  "skillId": "stt",
  "skillName": "stt",
  "status": "done",
  "summary": "处理完成",
  "outputs": [
    {"type": "text", "title": "识别结果", "text": "识别出的文本内容", "url": null},
    {"type": "webview", "title": "文本文件", "url": "https://oss.example.com/text.txt"}
  ]
}
```

### 失败

```json
{
  "providerId": "openclaw",
  "skillId": "tts",
  "skillName": "tts",
  "status": "error",
  "summary": "TTS 服务连接失败",
  "outputs": []
}
```
