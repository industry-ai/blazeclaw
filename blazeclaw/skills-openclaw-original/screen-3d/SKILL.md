---
name: screen-3d
version: 2.0.0
author: 炎图科技
license: MIT
source: internal
tags: [3d, game, scene, screen, camera]
compatibility: python>=3.10
allowed-tools: Bash(python:*)
description: 当用户提到 3D 游戏、3D 场景、3D 画面、游戏场景、立体游戏等字眼时触发。从 AI_TASK_REQUEST 读取 input.text 和 input.requester 拼接到 URL query 参数。仅返回 URL，不做跳转。
---

# Screen 3D — 3D 游戏场景入口

## 输入格式

Skill 接收 OpenClaw 传入的 `AI_TASK_REQUEST` JSON：

```json
{
  "taskNo": "AI20260608-B2E9M4",
  "input": {
    "text": "用户原始输入",
    "requester": {
      "userId": "user-001",
      "phone": "13800138000",
      "displayName": "张三"
    },
    "state": {}
  },
  "resourceNaming": {
    "objectKeyPrefix": "ai/openclaw/AI20260608-B2E9M4"
  }
}
```

提取以下字段拼接到 URL query 参数中（需 URL 编码）：

| 来源 | 参数名 | 说明 |
|------|--------|------|
| `input.text` | `text` | 用户完整输入 |
| `input.requester.userId` | `userId` | 用户 ID |
| `input.requester.displayName` | `displayName` | 用户显示名称 |

---

## 触发场景

- "3D 游戏"
- "3D 场景"
- "3D 画面"
- "立体游戏"
- "游戏场景"
- "进入 3D"
- "打开 3D"
- "3D 视角"

## 输出格式

```json
{
  "providerId": "openclaw",
  "skillId": "screen-3d",
  "skillName": "screen-3d",
  "status": "done",
  "summary": "处理完成",
  "outputs": [
    {"type": "webview", "title": "3D 游戏场景", "url": "https://static.blazegraph.site/screen_3d/camera/index.html#/?text=进入3D游戏&userId=user-001&displayName=张三","postmessage":true}
  ]
}
```
