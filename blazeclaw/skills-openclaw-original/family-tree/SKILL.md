---
name: family-tree
version: 2.0.0
author: 炎图科技
license: MIT
source: internal
tags: [family-tree, genealogy, ancestry, clan, interpretation, culture]
compatibility: python>=3.10
allowed-tools: Bash(python:*)
description: 当用户提到家族/宗族/姓氏/谱系时触发。从 AI_TASK_REQUEST 读取 input.text 和 input.requester，拼接到 URL 的 query 参数中。族谱树入口：浏览带 text+userId+displayName，关系/代际/辈分查询额外带 ?q=问题。智能解读入口：始终带 ?q=问题+text+userId+displayName。仅返回 URL，不做跳转。
---

# Family Tree — 族谱树 & 智能解读

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

**所有 URL 都必须拼接这三个基础参数。** 其他业务参数（如 `q`）在此基础上追加。

---

## 入口一：族谱树 `#/tree`

### 浏览模式

看族谱/打开页面，不带 `q`。

> "看看族谱"、"郭家的族谱"、"张氏家谱"

→ `https://corp.blazegraph.site/family-tree/dist/index.html#/tree?text=看看郭家的族谱&userId=user-001&displayName=张三`

### 查询模式

问具体人物关系、代际、辈分，追加 `q` 参数。**`q` 和 `text` 均使用 `input.text` 完整原文。**

> "张三和李四是什么关系"

→ `https://corp.blazegraph.site/family-tree/dist/index.html#/tree?q=张三和李四是什么关系&text=张三和李四是什么关系&userId=user-001&displayName=张三`

---

## 入口二：智能解读 `#/interpretation`

始终带 `q` 参数，加上基础三参数。

**参数拼接规则：**

| 参数 | 取值 |
|------|------|
| `q` | 从 `input.text` 中**去掉姓氏/家族名前缀**后的查询意图，如"郭嘉的宗族简介"→`q=宗族简介`，"郭家的家训是什么"→`q=家训是什么` |
| `text` | `input.text` **完整原文**，不做任何改动 |

**示例：**

| 用户输入 | q | text |
|----------|----|------|
| 郭家简介 | `简介` | `郭家简介` |
| 郭家的家训是什么 | `家训是什么` | `郭家的家训是什么` |
| 郭嘉的宗族简介 | `宗族简介` | `郭嘉的宗族简介` |

完整 URL 示例：
> "郭嘉的宗族简介" → `https://corp.blazegraph.site/family-tree/dist/index.html#/interpretation?q=宗族简介&text=郭嘉的宗族简介&userId=user-001&displayName=张三`

> "郭家的家训是什么" → `https://corp.blazegraph.site/family-tree/dist/index.html#/interpretation?q=家训是什么&text=郭家的家训是什么&userId=user-001&displayName=张三`

---

## 输出示例

```json
// 看族谱
{"type": "webview", "title": "族谱树", "url": "https://corp.blazegraph.site/family-tree/dist/index.html#/tree?text=看看郭家的族谱&userId=user-001&displayName=张三"}

// 关系查询
{"type": "webview", "title": "族谱查询", "url": "https://corp.blazegraph.site/family-tree/dist/index.html#/tree?q=张三和李四是什么关系&text=张三和李四是什么关系&userId=user-001&displayName=张三"}

// 智能解读
{"type": "webview", "title": "智能解读", "url": "https://corp.blazegraph.site/family-tree/dist/index.html#/interpretation?q=宗族简介&text=郭嘉的宗族简介&userId=user-001&displayName=张三"}
```

## 决策图

```
读取 AI_TASK_REQUEST.input

拼接基础参数: ?text={text}&userId={userId}&displayName={displayName}

    ├── 只是要看族谱/打开页面
    │   └── #/tree + 基础参数
    │
    ├── 问了具体人物关系、代际、辈分
    │   └── #/tree?q=问题 + 基础参数
    │
    └── 家族简介/家训/家风/解读相关
        └── #/interpretation?q=问题 + 基础参数
```
