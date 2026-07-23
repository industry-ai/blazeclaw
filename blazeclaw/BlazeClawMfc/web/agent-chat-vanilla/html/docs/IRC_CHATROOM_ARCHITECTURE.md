# 聊天室 RFC 1459 架构设计文档

## 1. 概述

本文档描述 BlazeClaw 聊天室系统的完整 C++ 架构。系统基于 RFC 1459 (IRC 协议) 规范，实现频道管理、Operator 权限体系、Newsgroup 风格话题讨论以及与 AI Agent 的集成。

**设计原则：**

- **业务与 UI 彻底分离** — 所有 IRC 业务逻辑（`blazeclaw::irc` 命名空间）不依赖任何 UI 框架
- **传输与消息解耦** — 消息数据结构和传输方式（TLS/TCP 明文）独立选择
- **复用已有基础设施** — 传输层直接使用现有的 `CConnection_c` / `CNetwork_c` / `CClient`
- **前端不变** — WebView2 HTML/JS 前端通过 `postMessage` 与 C++ 桥接层通信，前端改动最小化

---

## 2. 整体分层架构

```
┌──────────────────────────────────────────────────────────────────┐
│                   Layer 4: UI (WebView2 HTML/JS)                  │
│                                                                  │
│  web/agent-chat-vanilla/html/                                    │
│  ├── index.html              (页面结构)                           │
│  ├── js/api/chatApi.js       (API 层，适配 postMessage)           │
│  ├── js/stores/chatStore.js  (状态管理)                          │
│  ├── js/pages/chat.js        (UI 渲染)                           │
│  └── js/transport/           (传输层，新增 nativeWebView 模式)     │
│                                                                  │
│  通信方式: chrome.webview.postMessage ←→ CustomEvent              │
└────────────────────────────┬─────────────────────────────────────┘
                             │ channel: "chatroom.bridge.request"
                             │ channel: "chatroom.bridge.response"
                             │ channel: "chatroom.bridge.push"
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│               Layer 3: Bridge (WebView2 ↔ 业务逻辑)                │
│                                                                  │
│  CChatRoomBridge (单例)                                           │
│  ├── HandleWebMessage(json)      ← WebView2 入口                 │
│  ├── EmitToWeb(json)             → 推回 WebView2                 │
│  ├── 推送队列管理 (按 sessionId 分组)                              │
│  ├── JOIN 状态追踪                                               │
│  └── requestId 匹配 & 超时处理                                    │
│                                                                  │
│  不涉及: TCP、IRC 协议、业务逻辑                                    │
└────────────────────────────┬─────────────────────────────────────┘
                             │ 调用
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│               Layer 2: Business Logic (纯 C++, 无 UI 依赖)         │
│                                                                  │
│  namespace blazeclaw::irc {                                      │
│                                                                  │
│  CMgrChannels ─────────── 频道管理器 (RFC 1459 全集)              │
│  │  JOIN / PART / KICK / MODE / TOPIC / BAN / INVITE              │
│  │  WHOIS / WHO / NAMES / LIST                                    │
│  │  PromoteToOperator / DemoteOperator                            │
│  │  GetDiagnostics / 事件回调                                     │
│  │                                                               │
│  ├── CChannel ──────────── 单个频道                               │
│  │   ├── 成员列表 (CPeer)                                        │
│  │   ├── Operator 列表                                           │
│  │   ├── 频道模式 (InviteOnly / Secret / Moderate / TopicLock)    │
│  │   ├── Ban / Invite 列表                                       │
│  │   ├── Operator 切换机制 (OnOperatorLeft)                       │
│  │   └── CTopic 列表 (Newsgroup 话题)                             │
│  │                                                               │
│  ├── CPeer ─────────────── 频道成员                               │
│  │   ├── nickname / username / hostname / realname               │
│  │   ├── Mode: Operator / Voice / Away / Invisible               │
│  │   └── join_order (Operator 切换优先级)                         │
│  │   │                                                           │
│  │   └── COperator ──────── IRC Operator (继承 CPeer)            │
│  │       ├── OperType: LocalOperator / GlobalOperator            │
│  │       ├── OperPrivilege: KillUsers / SquitServers / ...       │
│  │       └── OperWall / 操作日志                                  │
│  │                                                               │
│  └── CTopic ─────────────── Newsgroup 风格话题                    │
│      ├── title / creator / status (Open/Closed/Pinned/Archived)  │
│      ├── 层级回复 (Reply tree)                                    │
│      └── AI 参与开关 + System Prompt                             │
│                                                                  │
│  ── 消息体系 (独立，不继承 CPeer) ──                               │
│                                                                  │
│       ├── CMessage (TLS) — AI/Operator/认证                     │
│       └── CPrompt (TCP) — 普通聊天                              │
│                                                                  │
│  } // namespace blazeclaw::irc                                   │
└────────────────────────────┬─────────────────────────────────────┘
                             │ 调用已有类
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│                Layer 1: Transport (已有，不改)                     │
│                                                                  │
│  CClient (单例)                                                   │
│  ├── 登录认证: LoginWithPassword / LoginWithSms / AutoLogin       │
│  ├── Token 管理: GetSessionToken / RefreshToken                   │
│  ├── SessionId 管理                                               │
│  └── CommandHandler 分发 (按 MsgType 注册回调)                     │
│       │                                                          │
│  CNetwork_c (单例)                                               │
│  ├── Connect(ip, port, use_tls)                                  │
│  └── SendRequest(type, payload) → 同步响应                        │
│       │                                                          │
│  CConnection_c                                                  │
│  ├── TCP/TLS Socket + OpenSSL                                    │
│  ├── 64 字节 AppProtoHeader 帧协议 (magic "HBPC")                │
│  └── ReadMessage / SendRequest                                   │
│                                                                  │
│  连接配置:                                                        │
│  ├── Chat Server TCP (明文):  101.132.254.212:8765               │
│  └── Chat Server TLS (加密):  192.168.0.211:9443                 │
└──────────────────────────────────────────────────────────────────┘
```

---

## 4. 核心类详细说明

### 4.1 CMgrChannels — 频道管理器

**命名空间:** `blazeclaw::irc`  
**文件:** `src/app/CMgrChannels.h`, `src/app/CMgrChannels.cpp`

```
职责:
  - 所有频道的创建、销毁、查询
  - 成员全局注册与跨频道路由
  - IRC 命令处理: JOIN / PART / KICK / MODE / TOPIC / BAN / INVITE
  - 查询命令: WHOIS / WHO / NAMES / LIST
  - Operator 提升/降级
  - 线程安全 (shared_mutex)
  - 事件回调 (ChannelEvent / MemberEvent)
  - 诊断统计

内部数据结构:
  channels_  : unordered_map<name, shared_ptr<CChannel>>  (shared_mutex)
  members_   : unordered_map<nick, shared_ptr<CPeer>>      (shared_mutex)
  计数器     : atomic<uint64_t>  channels_created/destroyed/joins/parts/kicks/bans

关键方法:
  CreateChannel(name, founder) → shared_ptr<CChannel>
  Join(nick, channel, key)     → JoinResult
  Part(nick, channel, reason)  → PartResult
  Kick(op, channel, target)    → KickResult
  SetTopic(nick, channel, t)   → TopicResult
  SetChannelMode(op, ch, mode) → ModeResult
  Ban(op, channel, mask)       → BanResult
  PromoteToOperator(op, ch, t) → bool
  DemoteOperator(op, ch, t)    → bool
  Whois(nick)                  → optional<WhoisInfo>
```

### 4.2 CChannel — 单个频道

**文件:** `src/app/CChannel.h`, `src/app/CChannel.cpp`

```
职责:
  - 频道成员管理 (Add / Remove / Has / GetMembers)
  - Operator 管理 (Grant / Revoke / Is / GetOperators)
  - Operator 切换: 最后一个 op 离开时自动提拔下一个合格成员
  - 频道模式 (InviteOnly / Secret / Moderate / TopicLock / KeyLock / UserLimit)
  - Ban 列表管理
  - Topic 管理
  - CTopic (Newsgroup 话题) 列表管理

Operator 切换优先级:
  1. 频道创建者 (founder_nick_)
  2. 最早加入的非 operator 成员 (按 join_order_ 升序)
  3. 无人可选 → 频道暂时无 operator

重要接口 (部分):
  bool OnOperatorLeft(nick)           // operator 离开时自动切换
  std::string SelectNextOperator()    // 选择下一个 operator
  std::vector<string> GetOperators()  // 获取所有 op 列表

  // 话题管理 (新增)
  shared_ptr<CTopic> CreateTopic(title, creator, initialContent)
  shared_ptr<CTopic> GetTopic(topicId)
  vector<shared_ptr<CTopic>> ListTopics(filter)
  bool CloseTopic(topicId, operator_nick)
```

### 4.3 CPeer — 频道成员

**文件:** `src/app/CPeer.h`, `src/app/CPeer.cpp`

```
职责:
  - 成员身份信息 (nickname / username / hostname / realname)
  - 用户模式 (Operator / Voice / Away / Invisible)
  - 加入顺序 (join_order_ — Operator 切换优先级依据)
  - 虚函数: IsOperator() — 由 COperator 子类重写

成员变量:
  nickname_    : string
  username_    : string
  hostname_    : string
  realname_    : string
  mode_        : Mode (位掩码)
  join_order_  : uint64_t

Mode 枚举:
  None      = 0
  Operator  = 1 << 0   (+o)
  Voice     = 1 << 1   (+v)
  Away      = 1 << 2   (+a)
  Invisible = 1 << 3   (+i)
```

### 4.4 COperator — IRC 操作者

**文件:** `src/app/COperator.h`, `src/app/COperator.cpp`

```
继承自: CPeer
覆写: IsOperator() → 始终返回 true

OperType:
  None           = 0
  LocalOperator  = 1 << 0   (#oper)
  GlobalOperator = 1 << 1   (*oper*)

OperPrivilege (细粒度权限,可位或组合):
  KillUsers    = 1 << 0   KILL 命令
  SquitServers = 1 << 1   SQUIT 命令
  ChannelOps   = 1 << 2   频道操作 (KICK, MODE, BAN)
  SetHost      = 1 << 3   设置 hostname
  GLine        = 1 << 4   全局禁言
  ULine        = 1 << 5   服务器链接授权
  OperWall     = 1 << 6   OPERWALL 广播

额外能力:
  - 操作日志 (环形缓冲区, 最大 1000 条)
  - OperWall 广播 (向所有 operator 发送消息)
  - 自动 OPER 命令 (连接后自动认证)
```

### 4.5 CTopic — Newsgroup 风格话题

**文件:** 待创建 `src/app/CTopic.h`, `src/app/CTopic.cpp`

```
概念:
  不同于 IRC TOPIC (频道简介字符串), CTopic 是一个完整的讨论线程,
  类似论坛帖子或 Slack 线程。一个 CChannel 可以包含多个 CTopic。

状态:
  Open     正在讨论 (所有人可回复)
  Closed   已关闭 (仅 op 可重新打开)
  Pinned   置顶
  Archived 归档 (只读)

层级回复:
  Reply {
    reply_id          UUID
    parent_reply_id   空 = 直接回复主贴
    author_nick       作者
    content           内容
    timestamp_ms      时间
    is_ai_generated   AI 自动生成标记
    attachments       附件
  }

AI 集成:
  EnableAiParticipation(bool)
    → 标记此话题需要 AI Agent 参与
    → AI 会自动生成 Reply 插入讨论
  AddAiContext(system_prompt)
    → 设置 AI 参与时的系统提示词

示例场景:
  CChannel "科技讨论"
    ├── CTopic "AI 对软件开发的影响" (Open, AI 参与)
    │   ├── Reply: 张三: "我觉得 AI 会替代初级程序员"
    │   ├── Reply: 李四: "不会，AI 只是工具" (parent=张三)
    │   ├── Reply: AI_Agent: "根据行业报告, 目前 AI 在编码辅助方面..."
    │   └── Reply: 王五: "那测试工程师呢?" (parent=AI_Agent)
    │
    └── CTopic "周末聚会" (Closed)
        └── Reply: ...
```

### 4.6 消息体系: CMessage / CPrompt

**文件:** 待创建 `src/app/ChatMessage.h`, `src/app/ChatMessage.cpp`

```
CMessage (TLS 加密传输):
  UseTls() = true
  适用场景:
    - 普通群聊文本消息
    - 认证/Token 传输
  传输路径: CTlsConnection_c(host, 9443)

CPrompt (TCP 明文传输):
  UseTls() = false
  适用场景:
    - AI Agent 指令/交互
    - JOIN / PART / QUIT 通知
    - Operator 管理命令 (MODE, KICK, BAN, OPER)
  传输路径: CConnection_c(host, 8765)

> 消息数据格式由前端定义，后端直接透传。
```

### 4.7 CChatRoomBridge — 桥接层

**文件:** 待创建 `src/app/ChatRoomBridge.h`, `src/app/ChatRoomBridge.cpp`

```
职责:
  - WebView2 ←→ 业务逻辑之间的消息翻译
  - 不涉及 TCP、不涉及 IRC 协议、不涉及消息编解码
  - 仅处理 JSON 路由和推送队列

与 CBlazeClawAgentChatView 的集成:
  OnWebMessageReceived 中新增 channel 分支:
    if (channel == "chatroom.bridge.request")
        CChatRoomBridge::Instance().HandleWebMessage(json);

WebView2 → C++ 入口 (kind 映射):
  list_conversations   → CMgrChannels::GetChannelList / 封装返回
  create_conversation  → CMgrChannels::CreateChannel
  send_message         → 构造 CMessage → TLS 加密发送
  send_prompt          → 构造 CPrompt  → TCP 明文发送
  join_channel         → CMgrChannels::Join
  part_channel         → CMgrChannels::Part
  kick_member          → CMgrChannels::Kick
  ban_member           → CMgrChannels::Ban
  set_topic            → CMgrChannels::SetTopic
  set_mode             → CMgrChannels::SetChannelMode
  promote_operator     → CMgrChannels::PromoteToOperator
  demote_operator      → CMgrChannels::DemoteOperator
  whois                → CMgrChannels::Whois
  names                → CMgrChannels::GetNamesList
  get_history          → (历史消息查询)
  create_topic         → CChannel::CreateTopic
  reply_topic          → CTopic::AddReply
  list_topics          → CChannel::ListTopics
  close_topic          → CChannel::CloseTopic

推送队列 (对齐 Node bridge 的 pushEventQueueBySession):
  m_pushQueues  : unordered_map<sessionId, deque<json>>
  m_knownSessions : 所有发起过请求的 session_id
  JOIN 状态追踪 : m_joinedChannels ("sid:channel" → joinedAtMs)
  超过 30s 未活动自动重新 JOIN (对齐 JOIN_REFRESH_MS)
```

---

## 5. RFC 1459 Operator 切换机制

### 5.1 切换触发条件


| 场景                   | 触发方式                            | 处理                               |
| -------------------- | ------------------------------- | -------------------------------- |
| **Operator 主动转让**    | `MODE #chan +o OTHER_USER`      | 授予目标 op 权限, 原 op 可不撤销自己          |
| **最后一个 Operator 离开** | `PART #chan` / `QUIT`           | `CChannel::OnOperatorLeft()` 被调用 |
| **Operator 被 Kick**  | `KICK #chan OP_NICK`            | 全局 op 操作后, 触发 `OnOperatorLeft`   |
| **Operator 被降级**     | `MODE #chan -o OP_NICK`         | 立即撤销, 如果 channel 无其他 op → 触发切换   |
| **全局 Operator 介入**   | 任意全局 op 执行 `MODE #chan +o USER` | 绕过频道 op 限制                       |


### 5.2 CChannel::OnOperatorLeft 流程

```
OnOperatorLeft(departedOpNick)
  │
  ├─ 从 operators_ 移除 departedOpNick
  │
  ├─ operators_ 是否为空?
  │   ├─ 否 → 还有 operator, 无需切换, 返回
  │   └─ 是 ↓
  │
  ├─ SelectNextOperator()
  │   │
  │   ├─ 1. founder_nick_ 仍在频道?
  │   │     └─ 是 → 授予 founder operator, 触发 "OP_SWITCH" 事件, 返回
  │   │
  │   ├─ 2. 遍历 members_, 按 join_order_ 升序
  │   │     找到第一个非 operator 成员
  │   │     └─ 找到 → 授予 operator, 触发 "OP_SWITCH" 事件, 返回
  │   │
  │   └─ 3. 无合格成员 → 频道暂时无 operator
  │        触发 "OP_VACANT" 事件, 等待全局 op 或外部 MODE 设置
```

### 5.3 权限检查 CanOperate

```
CanOperate(nickname, channel_name)
  │
  ├─ 1. 检查是否全局 operator
  │     members_ 中查找 nickname
  │     → 是 COperator 且 IsLocalOperatorType() → 返回 true
  │
  └─ 2. 检查是否频道 operator
        channels_[channel_name]->IsOperator(nickname) → 返回 true/false
```

---

## 6. 消息流: CMessage vs CPrompt

```
┌─────────────────────────────────────────────────────────────────┐
│                      发送消息决策树                             │
│                                                                 │
│  待发消息                                                       │
│    │                                                            │
│    ├── 是 AI 指令?                                              │
│        └── 否 → 构造 CMessage  → UseTls() = true                │
│        │              → CTlsConnection_c(host, 9443)            │
│        │              → TLS 1.2/1.3 加密 → Chat Server :9443    │
│        │                                                        │
│        └── 是                          │
│            → 构造 CPrompt → UseTls() = false                    │
│            → CConnection_c(host, 8765)                          │
│            → TCP 明文 → Chat Server :8765                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

消息类型与传输层对照:
┌─────────────┬────────────┬────────────┬────────────────────────┐
│ 消息类型     │ 类         │ 传输层      │ 端口                 │
├─────────────┼────────────┼────────────┼────────────────────────┤
│ 普通聊天    │ CPrompt   │ TLS 加密    │ 8765                   │
│ JOIN/PART   │ CPrompt   │ TCP 明文    │ 8765                   │
│ KICK/BAN    │ CMessage  │ TCP 加密    │ 9443                   │
│ MODE        │ CMessage  │ TCP 加密    │ 9443                   │
│ AI 指令     │ CMessage  │ TCP 明文    │ 9443                   │
│ 认证/Token  │ CMessage  │ TLS 加密    │ 9443                   │
└─────────────┴────────────┴────────────┴────────────────────────┘


---

## 7. 数据流 (完整端到端)

### 7.1 发送普通群聊消息

```

[1] JS: chatApi.sendPrivmsg("#room1", "大家好", sessionId)
       → nativeWebViewTransport.sendMessage(payload)
       → chrome.webview.postMessage({
           channel: "chatroom.bridge.request",
           requestId: "req-001",
           kind: "send_message",
           payload: { channel: "#room1", text: "大家好" }
         })

[2] C++: CChatRoomBridge::HandleWebMessage(json)
       → DispatchRequest("req-001", "send_message", payload)
       → 构造 CPrompt (payload)
       → CPrompt::UseTls() = false → 选择 TCP 明文连接
       → m_tcpClient->SendRequest(MsgType::IrcMessageReq=221, json)

[3] TCP: CConnection_c(host, 8765)
       → 构造 AppProtoHeader(type=221, seq=N, payload_len=...)
       → send_all(64 字节头 + JSON payload)
       → recv_all 等待 222 响应
       → EmitResponse("req-001", true, { ok: true, seq: N })

[4] 同时, 服务端广播 PRIVMSG 给频道其他成员
       → 后台 ReaderThread::ReadMessage() 收到 MsgType=222 推送
       → CChatRoomBridge::OnPushEvent(evt)
       → EmitToWeb({ channel: "chatroom.bridge.push", payload: {...} })

[5] JS: window.addEventListener('chatroom.bridge.message', handler)
       → chatStore.appendMessage(evt.payload)
       → UI 渲染新消息气泡

```

### 7.2 发送 Operator 命令 (KICK)

```

[1] JS: postMessage({ kind: "kick_member", payload: { channel, target } })

[2] CChatRoomBridge::HandleWebMessage
       → DispatchRequest("req-002", "kick_member", payload)
       → 构造 CMessage (敏感操作, TLS) + 调用 CMgrChannels::Kick(...)

[3] CMgrChannels::Kick
       → CanOperate(operator_nick, channel)
         ├─ 检查全局 operator 权限
         └─ 检查频道 operator 权限
       → CChannel::RemoveMember(target_nick)
       → ++kicks_total_
       → FireChannelEvent(channel, "KICK", op + " " + target, reason)

[4] Bridge → EmitResponse("req-002", true, {...})
       → WebView2 收到响应, 前端更新成员列表

```

---

## 8. 核心接口定义

### 8.1 CPeer 公共接口

```cpp
class CPeer {
public:
    // 身份信息
    const std::string& GetNickname() const;
    const std::string& GetUsername() const;
    const std::string& GetHostname() const;
    std::string GetPrefix() const;  // nick!user@host

    // 模式
    bool HasMode(Mode m) const;
    void SetMode(Mode m, bool enable);
    Mode GetModes() const;

    // 虚函数
    virtual bool IsOperator() const;  // 默认返回 false
    virtual ~CPeer() = default;
};
```

### 8.2 CChannel 公共接口

```cpp
class CChannel {
public:
    // 基本属性
    const std::string& GetName() const;
    const std::string& GetTopic() const;
    void SetTopic(const std::string& topic);

    // 成员管理
    bool AddMember(std::shared_ptr<CPeer> peer);
    bool RemoveMember(const std::string& nick);
    bool HasMember(const std::string& nick) const;
    std::vector<std::string> GetMemberNicks() const;
    std::shared_ptr<CPeer> GetMember(const std::string& nick) const;

    // Operator 管理
    bool GrantOperator(const std::string& nick);
    bool RevokeOperator(const std::string& nick);
    bool IsOperator(const std::string& nick) const;
    std::vector<std::string> GetOperators() const;

    // Operator 切换
    bool OnOperatorLeft(const std::string& nick);
    std::string SelectNextOperator();  // 返回下一个 operator nickname

    // 权限检查
    bool CanSpeak(const std::string& nick) const;
    bool CanModifyTopic(const std::string& nick) const;
    bool CanKick(const std::string& nick) const;

    // 频道模式
    void SetMode(ChannelMode mode, bool enable);
    bool HasMode(ChannelMode mode) const;

    // 广播
    void Broadcast(const std::string& message, std::shared_ptr<CPeer> exclude = nullptr);
    void BroadcastToOps(const std::string& message);
};
```

### 8.3 CMgrChannels 公共接口

```cpp
class CMgrChannels {
public:
    // 频道管理
    std::shared_ptr<CChannel> CreateChannel(const std::string& name,
                                            std::shared_ptr<CPeer> founder);
    bool DestroyChannel(const std::string& name);
    std::shared_ptr<CChannel> GetChannel(const std::string& name) const;
    std::vector<std::string> GetChannelList() const;

    // 成员管理
    void RegisterMember(std::shared_ptr<CPeer> peer);
    void UnregisterMember(const std::string& nick);
    std::shared_ptr<CPeer> GetMember(const std::string& nick) const;

    // JOIN/PART
    JoinResult Join(std::shared_ptr<CPeer> peer, const std::string& channel,
                    const std::string& key = "");
    PartResult Part(std::shared_ptr<CPeer> peer, const std::string& channel,
                    const std::string& reason = "");

    // Operator 管理
    bool PromoteToOperator(const std::string& oper_nick,
                           const std::string& channel,
                           const std::string& target_nick);
    bool DemoteOperator(const std::string& oper_nick,
                        const std::string& channel,
                        const std::string& target_nick);

    // 查询
    std::optional<WhoisInfo> Whois(const std::string& nick) const;
    std::vector<std::string> GetNamesList(const std::string& channel) const;

    // 诊断
    Diagnostics GetDiagnostics() const;
};
```

### 8.4 CMessage / CPrompt

消息传输层选择：
- **CMessage (TLS)**: 认证、敏感数据、普通聊天
- **CPrompt (TCP)**: JOIN/PART 通知、广播、AI 指令、Operator 管理命令、


---

