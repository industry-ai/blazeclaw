---
name: h5-ppt
version: 1.0.0
author: 炎图科技
license: MIT
source: internal
tags: [ppt, h5-ppt]
compatibility: python>=3.10
allowed-tools: Bash(python:*)
description: 当用户表达“路演h5”、“打开路演h5”、“打开路演ppt”、“融资h5”、“打开融资h5”、“打开融资ppt”、“融资ppt”等意图时，返回固定的网页入口 URL。该 skill 不直接跳转，仅返回路径。当用户表达“下一页”、“上一页”、“翻页”、“翻到下一页”、“翻到上一页”、“next”、“prev”等意图时，返回JSON。
---

# 打开融资h5-ppt

当用户想打开融资ppt，触发本 skill，返回固定页面路径：

`https://static.blazegraph.site/h5-ppt/index.html`

本 skill 仅返回该 URL，调用方可据此打开 WebView 或展示给用户。它不会自行导航或跳转。

## 触发场景

- “路演h5”
- “路演ppt”
- “打开路演h5”
- “打开路演ppt”
- “融资h5”
- “打开融资h5”
- “打开融资ppt”
- “融资ppt”

## 输出格式

```json
{
  "providerId": "openclaw",
  "skillId": "h5-ppt",
  "skillName": "h5-ppt",
  "status": "done",
  "summary": "处理完成",
  "outputs": [
    {
      "type": "webview",
      "title": "炎图科技PPT",
      "url": "https://static.blazegraph.site/h5-ppt/index.html"
    }
  ]
}
```


## 说明

- 固定返回ppt页面地址。
- 不做跳转操作，只输出路径。
- 可结合现有调用框架，作为 融资 相关意图的统一入口。
