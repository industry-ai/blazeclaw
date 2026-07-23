# AgentChat Vanilla 项目完整说明文档

> 本文档完整说明 `Debug/web/agent-chat-vanilla/html` 项目的概述、通信架构、项目结构、各板块功能与实现方式，以及数据来源（真实服务 / 本地模拟数据）。

---

## 一、项目概述

### 1.1 项目定位

AgentChat Vanilla 是一个基于原生 ES Modules 的 SPA 聊天应用，**无构建工具依赖**，直接通过 `<script type="module">` 加载。它运行在 C++ WebView2 宿主中，作为桌面客户端的 UI 层。

### 1.2 核心架构原则

**页面 UI 与业务逻辑彻底分离**：

- **页面层**：使用原生 HTML/JS 实现，只负责 UI 渲染和事件绑定，不直接访问网络
- **业务逻辑层**：由 C++ 原生宿主实现，前端通过 `chrome.webview.postMessage` 通信
- **状态管理层**：前端维护视图缓存（View State），通过发布订阅模式驱动页面重渲染

### 1.3 技术栈

| 层面 | 技术选型 |
|------|---------|
| 模块系统 | 原生 ES Modules（`import` / `export`） |
| 路由 | 基于 hash 的 SPA 路由 |
| 状态管理 | 自实现 subscribe/notify 发布订阅 |
| 样式 | 原生 CSS + CSS 变量（支持浅色/深色/跟随系统） |
| 通信 | `chrome.webview.postMessage` 双向桥接 |
| 渲染 | 原生 DOM 操作（innerHTML + 事件绑定） |

### 1.4 入口与启动流程

入口文件：[index.html](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/index.html)
启动逻辑：[js/app.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/app.js)

启动流程：
1. `DOMContentLoaded` 触发后，`UiStore.initTheme()` 初始化主题
2. `Bridge.init()` 初始化桥接，恢复鉴权状态（localStorage / C++ 注入）
3. `_connectChat()` 连接聊天（拉取会话列表 + join 频道）
4. 监听 `__auth_injected__` 事件（C++ 运行时注入鉴权）
5. 监听 `hashchange` 实现 SPA 路由
6. `_navigateTo(route)` 懒加载对应页面模块

---

## 二、通信架构

### 2.1 两套独立桥接协议

项目存在两套独立的 postMessage 桥接协议，分别承担不同职责：

| 协议 | 通道前缀 | 用途 | 通信核心文件 |
|------|---------|------|-------------|
| **会话层** | `agentchat.bridge.*` | 鉴权、聊天、帖子、成员、任务、设备、Agent | [bridge/core.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/bridge/core.js) |
| **话题群聊层** | `chatroom.bridge.*` | IRC 频道操作（加入/退出/发消息/管理成员） | [transport/chatroomTransport.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/transport/chatroomTransport.js) |

### 2.2 请求-响应流程

**会话层（agentchat.bridge.*）**：

```
页面调用 Bridge.xxx()
    ↓
bridge/core.js request(kind, payload)
    ↓
window.chrome.webview.postMessage({
  channel: 'agentchat.bridge.request',
  requestId: 'pm-1-1234567890',
  kind: 'chat.send',
  payload: {}
})
    ↓
C++ 原生宿主处理
    ↓
CustomEvent 'agentchat.bridge.message' (detail.channel = 'agentchat.bridge.response')
    ↓
_onMessage 匹配 requestId -> resolve(payload)
    ↓
state 更新 + notify() -> 页面重渲染
```

**话题群聊层（chatroom.bridge.*）**：

```
页面调用 chatroomBridgeRequest(kind, payload)
    ↓
window.chrome.webview.postMessage({
  channel: 'chatroom.bridge.request',
  requestId: 'chatroom-xxx',
  kind, payload, sessionId
})
    ↓
C++ CChatRoomBridge 处理
    ↓
CustomEvent 'chatroom.bridge.response' (匹配 requestId)
    ↓
resolve(msg.payload)
```

### 2.3 推送流程

C++ 主动推送通过两个独立的 CustomEvent 派发：

| 推送通道 | 推送内容 | 处理函数 |
|---------|---------|---------|
| `agentchat.bridge.push` | 聊天消息、Agent 流式增量、群邀请 | `_handlePush` ([bridge/index.js#L305](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/bridge/index.js)) |
| `chatroom.bridge.push` | IRC 事件（privmsg/join/part/kick/ban/notice/group_invited） | `_handleChatroomPush` ([bridge/index.js#L441](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/bridge/index.js)) |

### 2.4 信封格式

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

### 2.5 鉴权机制

鉴权信息有三种注入路径（[bridge/index.js#L168](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/bridge/index.js)）：

1. **C++ 直接注入**：通过 `window.__INJECTED_AUTH__` 全局变量
2. **事件信号触发**：`__auth_injected__` CustomEvent 触发读取
3. **localStorage 恢复**：刷新后从 `auth.jwt` / `auth.phone` / `auth.user_id` / `auth.session_id` 恢复

鉴权数据持久化到 localStorage，刷新后仍可保持登录态。

### 2.6 超时与错误处理

- 请求默认超时 30 秒（`timeoutMs = 30000`）
- 超时后从 pending Map 中删除并 reject
- C++ 返回 `ok: false` 或 `error` 字段时 reject 并携带错误码

---

## 三、项目结构

```
agent-chat-vanilla/
├── index.html                  # 应用入口 HTML（页面容器 + 左侧导航）
├── css/
│   ├── variables.css           # CSS 变量定义（主题色/间距/字号）
│   └── main.css                # 全局样式（页面/组件/弹窗）
├── docs/                       # 文档目录
│   ├── IRC_CHATROOM_ARCHITECTURE.md  # 聊天室 C++ 架构设计（RFC 1459）
│   └── PROJECT_FULL_GUIDE.md   # 本文档（完整说明）
└── js/
    ├── app.js                  # SPA 路由器 + 启动入口
    ├── config.js               # 全局配置（AI 模型/调试参数）
    │
    ├── bridge/                 # 会话层桥接（agentchat.bridge.*）
    │   ├── core.js             #   postMessage 通信核心
    │   ├── index.js            #   业务门面（80+ 方法，页面唯一入口）
    │   └── state.js            #   视图状态缓存（subscribe/notify）
    │
    ├── irc/                    # 话题群聊层（chatroom.bridge.*，已实现但未启用）
    │   ├── core.js             #   通信核心（委托 chatroomTransport）
    │   ├── api.js              #   IRC API 门面（joinChannel/sendPrivmsg 等）
    │   ├── store.js            #   视图状态缓存
    │   └── mock.js            #   静态假数据（当前 chatroom.js 实际使用）
    │
    ├── transport/
    │   └── chatroomTransport.js #  C++ 通信封装（chatroom.bridge.* 协议）
    │
    ├── pages/                  # UI 页面（纯渲染 + 事件绑定）
    │   ├── chat.js             #   会话页面（聊天/群管理/任务/Agent）
    │   ├── chatroom.js         #   话题群聊页面（静态展示版）
    │   ├── ai.js               #   AI 工作台页面
    │   ├── tasks.js            #   任务中心页面
    │   ├── me.js               #   个人中心页面
    │   ├── notifications.js    #   通知中心页面
    │   └── devices.js          #   设备管理页面
    │
    ├── panels/                 # 右侧抽屉面板（mixin 形式混入 ChatPage）
    │   ├── devicesPanel.js     #   设备面板（绑定流程）
    │   └── postsPanel.js       #   群聊看板/任务/成员面板
    │
    ├── protocol/               # 协议层（协同协议/交互资源桥接）
    │   ├── collaborationProtocol.js       # 协同指令协议
    │   ├── collaborationChatEnvelope.js   # 协同聊天信封
    │   └── interactiveResourceBridge.js  # H5 卡片交互桥接
    │
    ├── stores/
    │   └── uiStore.js          # UI 状态管理（主题/视图栈/任务详情）
    │
    └── utils/                  # UI 工具
        ├── avatar.js           #   头像颜色生成
        ├── markdown.js         #   Markdown 渲染
        ├── time.js             #   时间格式化
        ├── toast.js            #   Toast 提示
        └── authErrorMessage.js #   认证错误码映射
```

### 分层架构图

```
┌─────────────────────────────────────────────────────────┐
│              UI 层 (pages/ + panels/)                   │
│   纯 HTML 渲染 + 事件绑定，不直接访问网络                  │
├──────────────────┬──────────────────────────────────────┤
│   Bridge 门面     │           IrcApi 门面                │
│   (bridge/)      │           (irc/)                     │
│   会话/群组/     │           频道操作                     │
│   任务/Agent     │           （已实现，当前未启用）        │
├──────────────────┼──────────────────────────────────────┤
│   bridge/core    │           irc/core                   │
│   直接 post      │           委托 chatroomTransport      │
│   Message        │                                      │
├──────────────────┼──────────────────────────────────────┤
│   bridge/state   │           irc/store                  │
│   视图缓存 +      │           视图缓存 +                  │
│   subscribe      │           subscribe                  │
├──────────────────┴──────────────────────────────────────┤
│              chatroomTransport.js                       │
│              C++ 通信封装层                              │
│              (chatroom.bridge.* 协议)                   │
└─────────────────────────────────────────────────────────┘
         ↑
    config.js / protocol/ / utils/ / stores/uiStore.js
    全局配置 / 协议层 / 工具 / UI 状态
```

---

## 四、板块总览

项目共 7 个板块，通过左侧导航 Tab 切换：

| 路由 | 页面文件 | 板块名称 | 左侧导航 | 数据来源 |
|------|---------|---------|---------|---------|
| `#/chat` | [pages/chat.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/chat.js) | 会话（聊天主页面） | 会话 | **真实服务** |
| `#/chatroom` | [pages/chatroom.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/chatroom.js) | 聊天室（话题群聊） | 聊天室 | **本地模拟数据** |
| `#/tasks` | [pages/tasks.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/tasks.js) | 任务中心 | 任务 | **真实服务** |
| `#/ai` | [pages/ai.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/ai.js) | 工作台（AI 能力） | 工作台 | **真实服务** |
| `#/notifications` | [pages/notifications.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/notifications.js) | 通知中心 | 通知 | **真实服务** |
| `#/me` | [pages/me.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/me.js) | 个人中心 | 我的 | **真实服务 + 本地 UI** |
| `#/devices` | [pages/devices.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/devices.js) | 设备管理 | （从"我的"进入） | **真实服务** |

### 数据来源判定依据

1. **Bridge 层非 mock 模式**：[bridge/index.js#L2184](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/bridge/index.js) 中 `isMockMode()` 返回 `false`
2. **直连 C++ 原生宿主**：[bridge/core.js#L87](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/bridge/core.js) 使用 `window.chrome.webview.postMessage`
3. **聊天室板块明确使用 mock**：[pages/chatroom.js#L1-L11](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/chatroom.js) 文件头注释写道"静态假数据展示版"，直接 `import` 自 [irc/mock.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/irc/mock.js)

---

## 五、各板块详细说明

### 5.1 会话板块（chat）-- 聊天主页面

**文件**：[js/pages/chat.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/chat.js)
**路由**：`#/chat`（默认路由）
**数据来源**：**真实服务**（通过 `Bridge` 调用 C++）

#### 功能列表与实现

| 子功能 | 实现说明 | 关键 Bridge 方法 / C++ kind |
|------|---------|---------------------------|
| 会话列表展示 | 拉取频道列表，渲染侧边栏（桌面/移动两套 shell） | `Bridge.getConversations()` / `list_conversations` |
| 切换会话 | 点击侧栏会话项激活，自动 join 频道 | `Bridge.setActiveConversation(id)` / `join_channel` |
| 消息收发 | 乐观更新（sending->sent/failed），支持重试 | `Bridge.sendUserMessage()` / `send_message` |
| AI 流式回复 | 发送到 AI 空间或 @AI助手 时触发，先建占位气泡再流式拼接 | `Bridge._requestAgentReply()` / `agent.turn` |
| 群聊看板面板 | 右侧抽屉展示群任务/作业/活动帖子 | `Bridge.loadGroupPosts()` / `list_posts` |
| 群成员管理 | 拉取成员、邀请/移除成员 | `Bridge.getRoomMembers()` / `get_room_info` |
| 设备面板 | 右侧抽屉的设备绑定流程（三步） | `Bridge.createDeviceBindSession()` / `devices.bind.*` |
| 帖子创建器 | 弹窗创建家庭作业/群活动/群资讯 | `Bridge.createGroupPost()` / `create_post` |
| 话题创建/加入 | 弹窗创建话题或确认加入 | `Bridge.createTopic()` / `topics.create` |
| 任务详情 | 打开 native_post 类型的任务详情 | `UiStore.openTask()` |
| 创建群聊 | body 挂载模态弹窗，输入群名创建 | `Bridge.createGroupConversation()` / `create_conversation` |
| 删除群聊 | 确认弹窗 + 乐观删除（本地标记） | `Bridge.deleteConversationFromList()` / `DELETE_CONVERSATION` |
| 语音输入 | 调用 TTS 转写（占位实现） | `Bridge.transcribeAudio()` / `tts.transcribe` |
| Markdown 渲染 | AI 回复含 markdown 时本地渲染 | `utils/markdown.js` |
| H5 卡片交互 | iframe + postMessage 双向桥接 H5 卡片 | `Bridge.openInteractiveResource()` |

#### 实现要点

- **桌面/移动自适应**：`_isDesktop()` 判断后分别渲染 `_renderDesktopShell()` / `_renderMobileShell()`
- **防抖渲染**：`_scheduleRender` 对连续状态变化做 80ms 防抖，避免 innerHTML 频繁销毁 DOM
- **状态恢复**：`_captureComposerState` / `_restoreComposerState` 保留输入框内容、滚动位置、面板开关状态
- **Mixin 复用**：通过 `PostsPanelMixin` 与 `DevicesPanelMixin` 把右侧抽屉逻辑混入 ChatPage
- **Agent 流式**：发送方通过 `onAgentStream` 订阅 delta/final；接收方通过 `_remoteStreamingMsgs` 跟踪表创建/更新 streaming 占位气泡

#### 关键协议

- 会话列表：`chatroom.bridge.request` 通道 `list_conversations` ([bridge/index.js#L715](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/bridge/index.js))
- 消息发送：`chatroom.bridge.request` 通道 `send_message` ([bridge/index.js#L796](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/bridge/index.js))
- AI 回复：`agentchat.bridge.request` 通道 `agent.turn` ([bridge/index.js#L1570](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/bridge/index.js))

---

### 5.2 聊天室板块（chatroom）-- 话题群聊（静态展示版）

**文件**：[js/pages/chatroom.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/chatroom.js)
**路由**：`#/chatroom`
**数据来源**：**本地模拟数据**（[js/irc/mock.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/irc/mock.js)）

#### 功能列表与实现

| 子功能 | 实现说明 |
|------|---------|
| 频道列表展示 | 渲染 `mockChannels`（3 个内置频道：#tech-talk / #project-alpha / #random） |
| 消息流展示 | 从 `mockMessages[channel]` 读取消息并渲染气泡 |
| 发送消息 | 直接 push 到 `mockMessages[activeChannel]`，触发 `notify()` 重渲染 |
| 成员列表 | 渲染 `channel.members`，按 operator 优先 + joinOrder 排序 |
| 修改群信息 | 弹窗编辑 `title` / `topic`，直接修改 mock 对象 |
| 解散/退出群聊 | 从 `mockChannels` 数组 splice 删除，清空对应消息 |
| 移除成员 | 从 `channel.members` filter 移除 |
| 设置/取消管理员 | 修改 `member.mode.operator` 并同步 `operators` 数组 |
| 滚动锁定 | 滚动查看历史时锁定不自动回到底部 |

#### 实现要点

- **纯本地状态**：当前用户固定为"林晓"（`MOCK_NICK`），且 `MOCK_IS_GLOBAL_OP = true`，所有操作均为内存修改
- **轻量发布订阅**：`subscribe` / `notify` 自实现，无任何网络请求
- **未接入 `irc/api.js`**：尽管项目已准备好 IRC 真实 API（[js/irc/api.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/irc/api.js)），但 `chatroom.js` 完全绕过它，仅消费 mock 数据

#### Mock 数据示例

```javascript
// js/irc/mock.js
export const MOCK_NICK = "林晓";
export const MOCK_IS_GLOBAL_OP = true;
export const mockChannels = [
  { name: "#tech-talk", title: "技术交流", members: [...], operators: ["林晓"] },
  { name: "#project-alpha", title: "项目Alpha", members: [...] },
  { name: "#random", title: "闲聊灌水", members: [...] },
];
export const mockMessages = { "#tech-talk": [...], ... };
```

> **注**：`irc/api.js` 已实现完整的 IRC API 门面（`joinChannel` / `sendPrivmsg` / `kickMember` / `createTopic` 等），通过 `chatroom.bridge.*` 协议委托给 C++ 处理，但当前页面未启用，等待后续接入。

---

### 5.3 任务中心板块（tasks）

**文件**：[js/pages/tasks.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/tasks.js)
**路由**：`#/tasks`
**数据来源**：**真实服务**

#### 功能列表与实现

| 子功能 | 实现说明 | 关键 Bridge 方法 / C++ kind |
|------|---------|---------------------------|
| Segment 切换 | 待处理 / 个人 / 群任务 / 已完成 四个分类标签 | 纯前端切换 `activeSegment` |
| 任务聚合 | 把个人任务 + 各群帖（type=task）合并为统一条目 | `Bridge.getPersonalTasksForCurrentUser()` / `tasks.personal.list` |
| 完成任务 | 个人任务完成（状态置 done） | `Bridge.completePersonalTask(taskId)` / `tasks.personal.update_status` |
| 取消任务 | 个人任务取消 | `Bridge.cancelPersonalTask(taskId)` / `tasks.personal.update_status` |
| 延期 1 小时 | 重新调度 dueAt | `Bridge.reschedulePersonalTask(taskId, base+3600000)` / `tasks.personal.reschedule` |
| 查看来源 | 跳转到任务来源会话 | `Bridge.setActiveConversation()` + `UiStore.resetToChatView()` |
| 打开群任务 | 跳转到群聊并打开任务详情 | `UiStore.openTask()` |
| 统计卡片 | 待处理/个人/群任务数量统计 | 前端聚合计算 |

#### 实现要点

- **异步预取群帖**：`init()` 中 `void this._loadGroupPosts()` 调用 `Bridge.loadAllGroupPosts()` 预拉所有群的帖子
- **纯 UI 页面**：所有业务逻辑走 Bridge，文件头明确写道："业务逻辑通过 Bridge -> postMessage 交由 C++ 原生宿主处理"

---

### 5.4 工作台板块（ai）

**文件**：[js/pages/ai.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/ai.js)
**路由**：`#/ai`
**数据来源**：**真实服务**

#### 功能列表与实现

| 子功能 | 实现说明 | 关键 Bridge 方法 / C++ kind |
|------|---------|---------------------------|
| 工作台总览 | 待处理任务数、执行记录数、在线设备数统计 | `Bridge.getPersonalTasksForCurrentUser()` / `Bridge.getExecutionHistory()` / `Bridge.getBoundDevices()` |
| AI 能力管理 | 展示并切换 intent=query 的工作台技能 | `Bridge.getAgentSkills()` / `agent.skills.toggle` |
| 全部启用/停用 | 批量切换技能状态 | 循环调用 `Bridge.toggleSkill()` |
| 执行记录列表 | 展示执行历史（创建任务/发布公告/活动发起等） | `Bridge.getExecutionHistory()` |
| 设备摘要 | 在线电视数、设备总数、最近 3 个设备 | `Bridge.getBoundDevices()` |
| 快捷操作 - 创建任务 | 在 AI 工作空间创建任务并跳转任务中心 | `Bridge.createPersonalTask()` / `tasks.personal.create` |
| 快捷操作 - 总结会话 | 跳转到 AI 工作空间并发送"请总结当前会话的主要内容" | `Bridge.sendUserMessage()` |
| 快捷操作 - 生成群公告 | 跳转到第一个群聊并发送"帮我生成一份群公告草稿" | `Bridge.sendUserMessage()` |
| 发布草稿 | 将工作空间草稿发布到指定群聊 | `Bridge.publishWorkspaceDraft()` / `workspace.draft.publish` |

#### 实现要点

- **三个 Segment**：overview（工作台总览）/ skills（AI 能力）/ history（执行记录）
- **技能过滤**：仅展示 `intent === 'query'` 的工作台技能（对齐 Vue 版 `PersonalWorkspaceAiPanel`）
- **init 预取设备**：`Bridge.refreshBoundDevices()` 异步刷新设备列表

---

### 5.5 通知中心板块（notifications）

**文件**：[js/pages/notifications.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/notifications.js)
**路由**：`#/notifications`
**数据来源**：**真实服务**（含 C++ 主动推送）

#### 功能列表与实现

| 子功能 | 实现说明 | 关键 Bridge 方法 |
|------|---------|-----------------|
| 通知聚合 | 合并 4 类通知：群聊邀请、个人提醒、群任务截止、C++ 推送通知 | `Bridge.getGroupInvitationNotifications()` / `getPushNotifications()` 等 |
| 进入清角标 | `init()` 时调用 `setLastSeenNotificationsAt()` 清零未读 | `Bridge.setLastSeenNotificationsAt()` |
| 时间倒序 | 按 `createdAt` 降序排列 | 前端排序 |
| 完成任务 | 个人提醒可直接完成 | `Bridge.completePersonalTask(taskId)` |
| 跳转任务中心 | "去任务"按钮 | `window.location.hash = '#/tasks'` |
| 点击通知跳转 | 根据 key 类型跳转到对应会话/任务 | `Bridge.setActiveConversation()` |

#### 通知类型与来源

| 通知类型 | kind | 来源 |
|---------|------|------|
| 群聊邀请 | `group_invited` | C++ 推送 `chatroom.bridge.push` 的 `group_invited` 事件 |
| 个人提醒 | `personal_reminder` | 个人任务中 `dueAt <= now` 且未完成的任务 |
| 群任务截止 | `group_task_deadline` | 群帖子中 `deadlineAt <= now` 且 24h 内的帖子 |
| 系统消息 | `system` | C++ 推送 `notice` / `kick` / `ban` 事件 |
| 设备状态 | `device_status` | C++ 推送的设备状态变更 |

#### 实现要点

- **实时刷新**：`Bridge.subscribe(() => this.render())` 订阅状态变化，C++ 推送新通知时自动重渲染
- **角标逻辑**：`lastSeen` 仅在 `init()` 设置一次，避免每次 render 重置导致角标永远为 0

---

### 5.6 个人中心板块（me）

**文件**：[js/pages/me.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/me.js)
**路由**：`#/me`
**数据来源**：**真实服务 + 本地 UI**（主题切换为纯本地）

#### 功能列表与实现

| 子功能 | 实现说明 | 实现方式 |
|------|---------|---------|
| 账号信息展示 | 显示手机号、登录状态 | `Bridge.isLoggedIn()` / `Bridge.getPhone()` |
| 外观与主题 | 浅色/深色/跟随系统三选一 | **本地** `UiStore.setTheme()` 持久化到 localStorage |
| 账号与安全 | 展示手机号、登录方式（短信验证码）、安全验证状态 | 纯展示 |
| 我的设备入口 | 跳转到设备管理页 | `window.location.hash = '#/devices'` |

#### 实现要点

- **三级面板**：`home` / `appearance` / `account`，通过 `currentPanel` 切换
- **主题切换**：完全本地实现，`UiStore.initTheme()` 在应用启动时从 localStorage 恢复
- **账号头像**：取手机号后两位作为缩写

---

### 5.7 设备管理板块（devices）

**文件**：[js/pages/devices.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/pages/devices.js)
**路由**：`#/devices`
**数据来源**：**真实服务**

#### 功能列表与实现

| 子功能 | 实现说明 | 关键 Bridge 方法 / C++ kind |
|------|---------|---------------------------|
| 设备统计 | 当前在线/已绑定/可绑定数量统计 | `Bridge.getBoundDevices()` / `devices.list` |
| 当前在线设备列表 | 展示在线设备（电视/笔记本/手机） | `Bridge.getBoundDevices()` 过滤 `online === true` |
| 已绑定设备列表 | 展示所有已绑定设备 + 解绑入口 | `Bridge.getBoundDevices()` / `Bridge.unbindDevice()` / `devices.unbind` |
| 生成绑定码 | prompt 输入设备名生成绑定会话 | `Bridge.createDeviceBindSession(name)` / `devices.bind_sessions.create` |
| 复制 bind_token | 复制到剪贴板 | `navigator.clipboard.writeText()` |
| 复制 Deep Link | 复制 `agentchat://bind-device?bind_token=xxx` | 同上 |
| 手动绑定流程 | 三步：输入绑定码 -> 确认目标群聊 -> 绑定结果 | `Bridge.parseDeviceBindPayload()` / `getDeviceBindSession()` / `confirmDeviceBind()` |
| 解绑设备 | 内联确认弹窗 + 解绑 | `Bridge.unbindDevice()` / `devices.unbind` |

#### 绑定流程详解

1. **输入步骤**：用户输入 `bt_xxx` 或 `agentchat://bind-device?bind_token=xxx`，前端解析 bind_token
2. **确认步骤**：调 `Bridge.getDeviceBindSession(bindToken)` 校验会话，选择目标群聊
3. **结果步骤**：调 `Bridge.confirmDeviceBind()` 完成绑定，刷新设备列表

#### 实现要点

- **支持多种输入格式**：`bt_` 前缀、`agentchat://` 深链、JSON 对象（`Bridge.parseDeviceBindPayload`）
- **内联确认**：解绑不弹模态框，直接在设备卡片内展示"确认/取消"按钮

---

## 六、数据来源总结

### 6.1 真实服务板块（6 个）

| 板块 | 通信通道 | 关键 C++ kind |
|------|---------|-------------|
| 会话（chat） | `agentchat.bridge.*` + `chatroom.bridge.*` | `list_conversations` / `send_message` / `agent.turn` / `list_posts` 等 |
| 任务中心（tasks） | `agentchat.bridge.*` + `chatroom.bridge.*` | `tasks.personal.*` / `list_posts` |
| 工作台（ai） | `agentchat.bridge.*` | `devices.list` / `agent.skills.*` / `workspace.draft.publish` |
| 通知中心（notifications） | `agentchat.bridge.*` + 推送 | state 聚合，无主动请求 |
| 个人中心（me） | `agentchat.bridge.*` + 本地 | 主题切换为本地 localStorage |
| 设备管理（devices） | `agentchat.bridge.*` | `devices.*` 系列 |

### 6.2 本地模拟数据板块（1 个）

| 板块 | 数据来源 | 说明 |
|------|---------|------|
| 聊天室（chatroom） | [js/irc/mock.js](file:///d:/project/new-c++/Debug/web/agent-chat-vanilla/html/js/irc/mock.js) | 3 个内置频道、固定用户"林晓"、所有操作为内存修改 |

### 6.3 判定依据

1. **Bridge 层非 mock 模式**：`isMockMode()` 返回 `false`
2. **直连 C++ 原生宿主**：使用 `window.chrome.webview.postMessage`
3. **聊天室板块明确使用 mock**：文件头注释"静态假数据展示版"，直接 `import` mock 数据
4. **irc/api.js 已实现但未启用**：完整的 IRC API 门面已就绪，等待后续接入真实服务

---

## 七、本地运行与调试

项目为纯静态文件，无需构建。使用任意 HTTP 服务器即可：

```bash
python -m http.server 8080
# 或
npx serve -p 8080
```

### 重要提示

脱离 C++ WebView2 宿主独立运行时：

- **聊天室板块**（使用 mock 数据）可正常展示
- **其他板块**因依赖 `window.chrome.webview.postMessage`，所有 Bridge 请求会失败：
  - 抛出 `postMessage 请求超时`（30 秒后）
  - 或 `Native WebView Bridge not available`（无 webview 对象时）
- 需在 C++ WebView2 宿主中运行才能与 C++ 通信并获取真实数据

---

## 八、相关文档

- [IRC_CHATROOM_ARCHITECTURE.md](./IRC_CHATROOM_ARCHITECTURE.md) - 聊天室 C++ 架构设计文档（RFC 1459）
