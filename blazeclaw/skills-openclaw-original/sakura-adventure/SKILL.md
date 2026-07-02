---
name: sakura-adventure
version: 1.0.0
author: 炎图科技
license: MIT
source: internal
tags: [sakura-adventure, game, webgame, play, entertainment]
compatibility: python>=3.10
allowed-tools: Bash(python:*)
description: 当用户表达“我要玩游戏”、“开始游戏”、“玩游戏”、“樱花大冒险”、“游戏”等意图时，返回固定的游戏入口 URL。该 skill 不直接跳转，仅返回路径。
---

# Sakura Adventure — 樱花大冒险 游戏入口

当用户想玩游戏时，触发本 skill，返回固定页面路径：

`https://static.blazegraph.site/yt_godot_game_html/index.html#/`

本 skill 仅返回该 URL，调用方可据此打开 WebView 或展示给用户。它不会自行导航或跳转。

## 触发场景

- “我要玩游戏”
- “开始游戏”
- “玩一局游戏”
- “带我去玩游戏”
- “游戏入口”
- “打开游戏”
- “樱花大冒险”
- “玩樱花大冒险”
- “樱花大冒险游戏”
- “玩樱花大冒险游戏”
- “打开樱花大冒险”
- “樱花大冒险入口”
- “带我去玩樱花大冒险”
- “我要玩樱花大冒险”
- “开始玩樱花大冒险”
- “玩一局樱花大冒险”
- “打开樱花大冒险游戏”
- “樱花大冒险游戏入口”
- “带我去玩樱花大冒险游戏”
- “我要玩樱花大冒险游戏”
- “开始玩樱花大冒险游戏”
- “游戏”
## 输出格式

```json
{
  "providerId": "openclaw",
  "skillId": "sakura-adventure",
  "skillName": "sakura-adventure",
  "status": "done",
  "summary": "处理完成",
  "outputs": [
    {
      "type": "webview", 
      "title": "游戏入口", 
      "url": "https://static.blazegraph.site/yt_godot_game_html/index.html#/",
      "postmessage":true
    }
  ]
}
```

## 说明

- 固定返回游戏页面地址。
- 不做跳转操作，只输出路径。
- 可结合现有调用框架，作为 game 相关意图的统一入口。
