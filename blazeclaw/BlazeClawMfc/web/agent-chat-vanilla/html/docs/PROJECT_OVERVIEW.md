# AgentChat Vanilla 项目文档

## 1. 项目概述

AgentChat Vanilla 是一个基于原生 ES Modules 的 SPA 聊天应用，无构建工具依赖，直接以 `<script type="module">` 加载。

**核心架构原则**：页面逻辑与业务逻辑彻底分离。
- **页面逻辑**：使用原生 HTML/JS 实现，负责 UI 渲染和事件绑定
- **业务逻辑**：由 C++ 原生宿主实现，前端通过 `postMessage` 通信

## 2. 通信架构

前端通过 `window.chrome.webview.postMessage` 与 C++ 原生宿主通信，C++ 通过 CustomEvent 派发响应和推送。

两套独立桥接协议：
- **会话层** (`agentchat.bridge.*`)：鉴权、聊天、帖子、成员、任务、设备、Agent
- **话题群聊层** (`chatroom.bridge.*`)：IRC 频道操作（加入/退出/发消息/管理成员）

会话列表（频道列表）由会话层通过 `chatroomBridgeRequest('list_conversations')` 拉取，频道=会话下的群聊。

## 3. 项目结构

```
agent-chat-vanilla/
├── index.html                  # 应用入口 HTML
├── css/
│   ├── variables.css           # CSS 变量定义
│   └── main.css                # 全局样式
├── docs/
│   ├── PROJECT_OVERVIEW.md     # 本文档
│   └── IRC_CHATROOM_ARCHITECTURE.md  # 话题群聊 C++ 架构设计文档
└── js/
    ├── app.js                  # SPA 路由器
    ├── config.js               # 全局配置
    ├── bridge/                 # 会话层 postMessage 桥接
    │   ├── core.js             #   通信核心（agentchat.bridge.* 通道）
    │   ├── index.js            #   业务门面（页面唯一调用入口）
    │   └── state.js            #   视图状态缓存
    ├── irc/                    # 话题群聊层 postMessage 桥接
    │   ├── core.js             #   通信核心（委托 chatroomTransport.js）
    │   ├── api.js              #   API 门面（频道操作）
    │   └── store.js            #   视图状态缓存
    ├── transport/
    │   └── chatroomTransport.js #  C++ 通信封装（chatroom.bridge.* 协议）
    ├── pages/                  # UI 页面（纯渲染 + 事件绑定）
    │   ├── chat.js             #   会话页面（聊天/群管理/话题/任务）
    │   ├── chatroom.js         #   话题群聊页面
    │   ├── ai.js               #   AI 工作台页面
    │   ├── tasks.js            #   任务中心页面
    │   ├── me.js               #   个人中心页面
    │   ├── notifications.js    #   通知中心页面
    │   └── devices.js          #   设备管理页面
    ├── panels/                 # 右侧抽屉面板
    │   ├── devicesPanel.js     #   设备面板
    │   └── postsPanel.js       #   群聊看板/任务/成员面板
    ├── stores/
    │   └── uiStore.js          # UI 状态管理（主题/视图栈）
    └── utils/                  # UI 工具
        ├── avatar.js           #   头像颜色生成
        ├── markdown.js         #   Markdown 渲染
        ├── time.js             #   时间格式化
        ├── toast.js            #   Toast 提示
        └── authErrorMessage.js #   认证错误码映射
```

## 4. 分层架构

```
┌─────────────────────────────────────────────────────┐
│                    UI 层 (pages/ + panels/)           │
│   纯 HTML 渲染 + 事件绑定，不直接访问网络              │
├──────────────┬──────────────────────────────────────┤
│  Bridge 门面  │           IrcApi 门面                 │
│  (bridge/)    │           (irc/)                     │
│  会话/群组/   │           频道操作                    │
│  任务/Agent   │           （加入/发消息/历史/管理）    │
├──────────────┼──────────────────────────────────────┤
│  bridge/core  │           irc/core                   │
│  直接 post    │           委托 chatroomTransport      │
│  Message      │                                      │
├──────────────┼──────────────────────────────────────┤
│  bridge/state │           irc/store                   │
│  视图缓存 +    │           视图缓存 +                  │
│  subscribe    │           subscribe                  │
├──────────────┼──────────────────────────────────────┤
│               │   chatroomTransport.js               │
│               │   C++ 通信封装层                      │
│               │   (chatroom.bridge.* 协议)            │
└──────────────┴──────────────────────────────────────┘
         ↑
    config.js
    全局配置
```

### 4.1 通信流程

**会话层 (bridge/)**：
```
页面调用 Bridge.xxx()
        ↓
bridge/core.js request(kind, payload)
        ↓
window.chrome.webview.postMessage({ channel: 'agentchat.bridge.request', ... })
        ↓
C++ 原生宿主处理
        ↓
CustomEvent 'agentchat.bridge.message' -> _onMessage -> resolve
```

**话题群聊层 (irc/)**：
```
页面调用 IrcApi.xxx()
        ↓
irc/core.js request(kind, payload)
        ↓
chatroomTransport.js chatroomBridgeRequest(kind, payload)
        ↓
window.chrome.webview.postMessage({ channel: 'chatroom.bridge.request', ... })
        ↓
C++ 原生宿主处理
        ↓
CustomEvent 'chatroom.bridge.response' -> resolve
```

### 4.2 两套桥接体系

| | bridge/（会话层） | irc/（话题群聊层） |
|---|---|---|
| **协议** | `agentchat.bridge.*` | `chatroom.bridge.*` |
| **通信核心** | `bridge/core.js` 直接 postMessage | `irc/core.js` 委托 `chatroomTransport.js` |
| **请求通道** | `agentchat.bridge.request` | `chatroom.bridge.request` |
| **响应监听** | `agentchat.bridge.message` CustomEvent | `chatroom.bridge.response` CustomEvent |
| **推送监听** | `agentchat.bridge.push` | `chatroom.bridge.push` CustomEvent |
| **页面入口** | `Bridge` (bridge/index.js) | `IrcApi` (irc/api.js) |
| **会话列表** | `loadConversations()` 通过 `chatroomBridgeRequest` | 不负责（由 Bridge 层管理） |
| **架构文档** | - | IRC_CHATROOM_ARCHITECTURE.md |

## 5. 核心模块说明

### 5.1 config.js - 全局配置

```javascript
AppConfig.getCurrentAi()           // 当前 AI 模型名称
AppConfig.isWebSmsLoginEnabled()   // 是否启用 Web 短信登录
```

### 5.2 bridge/ - 会话层桥接

**bridge/core.js** - postMessage 通信核心

信封结构：
```json
{
  "channel": "agentchat.bridge.request",
  "requestId": "pm-1-1234567890",
  "kind": "chat.send",
  "payload": {}
}
```

响应/推送通过 `agentchat.bridge.message` CustomEvent 派发，`detail.channel` 区分响应 (`agentchat.bridge.response`) 和推送 (`agentchat.bridge.push`)。

**bridge/index.js** - 业务门面（80+ 方法），主要分类：

| 分类 | 方法示例 | kind |
|------|---------|------|
| 鉴权 | `sendSmsCode, login, logout` | `auth.send_code, auth.login, auth.logout` |
| 聊天 | `connect, loadConversations, sendUserMessage` | `chat.connect, chat.conversations.list, chat.send` |
| 帖子 | `loadGroupPosts, createGroupPost` | `posts.list, posts.create` |
| 成员 | `getRoomMembers, inviteRoomMember` | `room.members.list, room.invite` |
| 话题 | `loadTopics, createTopic, joinTopic` | `topics.list, topics.create, topics.join` |
| 任务 | `getPersonalTasksForCurrentUser` | `tasks.personal.list` |
| 设备 | `getBoundDevices, refreshBoundDevices` | `devices.list` |
| Agent | `getAgentSkills, onAgentStream` | `agent.skills.list, agent.turn` |

`loadConversations()` 通过 `chatroomBridgeRequest('list_conversations')` 拉取频道列表，C++ 返回格式：
```json
{
  "channels": ["#group_xxx", ...],
  "conversations": [{ "id": "#group_xxx", "name": "www", "title": "www" }]
}
```
映射为 `{ id, title, kind: 'group' }` 存入 bridge/state。

**bridge/state.js** - 视图状态缓存，采用 subscribe/notify 发布订阅模式

### 5.3 irc/ - 话题群聊层

**irc/core.js** - 委托 chatroomTransport.js 通信
- `request(kind, payload)` -> `chatroomBridgeRequest(kind, payload, '0', timeoutMs)`
- `init()` -> `onChatroomPush(handler)` 监听 C++ 推送并归一化

**irc/api.js** - 频道操作 API（不包含会话列表拉取，会话列表由 Bridge 层管理）

| 方法 | kind | 说明 |
|------|------|------|
| `joinChannel(channel)` | `join_channel` | 加入频道 |
| `partChannel(channel)` | `part_channel` | 退出频道 |
| `sendPrivmsg(channel, text)` | `send_message` | 发送消息 |
| `sendPrompt(channel, text)` | `send_prompt` | 发送 Prompt |
| `getHistory(channel)` | `get_history` | 获取历史消息 |
| `kickMember(channel, target)` | `kick_member` | 移除成员 |
| `banMember(channel, mask)` | `ban_member` | 封禁成员 |
| `setTopic(channel, topic)` | `set_topic` | 设置主题 |
| `setChannelInfo(channel, title, topic)` | `set_channel_info` | 修改标题/描述 |
| `dissolveChannel(channel)` | `dissolve_channel` | 解散群聊 |
| `promoteOperator(channel, target)` | `promote_operator` | 设为管理员 |
| `createTopic(channel, title, content)` | `create_topic` | 创建话题 |
| `listTopics(channel)` | `list_topics` | 列出话题 |
| `closeTopic(channel, topicId)` | `close_topic` | 关闭话题 |

**irc/store.js** - 数据模型：
- **CChannel**: `{ name, title, topic, founder, modes, members[], operators[] }`
- **CPeer**: `{ nick, username, hostname, realname, mode: { operator, voice, away } }`
- **CTopic**: `{ id, channel, title, creator, status, content, replies[] }`

### 5.4 chatroomTransport.js - C++ 通信封装

封装了 `chatroom.bridge.*` 协议的底层通信：
- `chatroomBridgeRequest(kind, payload, sessionId)` - 请求-响应
- `onChatroomPush(handler)` - 推送监听
- 高级 API：`chatroomListConversations`, `chatroomJoinChannel`, `chatroomSendMessage` 等

### 5.5 pages/ - UI 页面

| 页面 | 路由 | 说明 |
|------|------|------|
| chat.js | `#/chat` | 会话主页面：聊天、群管理、话题列表、任务面板 |
| chatroom.js | `#/chatroom` | 话题群聊：频道列表（从 Bridge 获取）、成员管理 |
| tasks.js | `#/tasks` | 任务中心 |
| ai.js | `#/ai` | AI 工作台 |
| notifications.js | `#/notifications` | 通知中心 |
| me.js | `#/me` | 个人中心 |
| devices.js | `#/devices` | 设备管理 |

chatroom.js 同时订阅 `IrcApi` 和 `Bridge` 状态变化：
- 频道列表来自 `Bridge.getConversations()`
- 频道操作通过 `IrcApi`（join/send/history/manage）

## 6. 话题群聊与会话的关系

```
会话页 (#/chat)
├── Bridge.loadConversations() 拉取频道列表
│   └── chatroomBridgeRequest('list_conversations')
│       └── C++ 返回 { channels, conversations }
├── 群聊 "产品研发组"
│   └── 话题 "本周代码评审" -> [点击进入]
│       ↓ 存储 irc_channel_parents 映射
话题群聊页 (#/chatroom)
└── 频道列表来自 Bridge.getConversations()
    └── #topic-code-review
        ├── IrcApi.joinChannel() 加入频道
        ├── IrcApi.getHistory() 获取历史消息
        ├── IrcApi.sendPrivmsg() 发送消息
        └── IrcApi.kickMember() 等管理操作
```

## 7. C++ 接入说明

### 7.1 会话层 (agentchat.bridge.*)

C++ 监听 `window.chrome.webview.postMessage`，按 `channel` 字段路由：
- `agentchat.bridge.request` -> 处理请求
- 响应通过 `agentchat.bridge.message` CustomEvent 派发，`detail.channel = 'agentchat.bridge.response'`
- 推送通过 `agentchat.bridge.message` CustomEvent 派发，`detail.channel = 'agentchat.bridge.push'`

### 7.2 话题群聊层 (chatroom.bridge.*)

C++ 监听 `chatroom.bridge.request` 通道：
- 响应通过 `chatroom.bridge.response` CustomEvent 派发
- 推送通过 `chatroom.bridge.push` CustomEvent 派发

详细架构设计见 [IRC_CHATROOM_ARCHITECTURE.md](./IRC_CHATROOM_ARCHITECTURE.md)。

### 7.3 信封格式

**请求（WebView -> C++）**：
```json
{
  "channel": "agentchat.bridge.request",
  "requestId": "pm-1-1234567890",
  "kind": "chat.send",
  "payload": {}
}
```

**响应（C++ -> WebView）**：
```json
{
  "channel": "agentchat.bridge.response",
  "requestId": "pm-1-1234567890",
  "payload": {},
  "ok": true
}
```

**推送（C++ -> WebView）**：
```json
{
  "channel": "agentchat.bridge.push",
  "kind": "chat.push",
  "payload": {}
}
```

## 8. 本地运行

项目为纯静态文件，无需构建。使用任意 HTTP 服务器即可：

```bash
python -m http.server 8080
# 或
npx serve -p 8080
```

浏览器访问 `http://localhost:8080` 即可。需在 C++ WebView2 宿主中运行才能与 C++ 通信。
