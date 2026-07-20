#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "AppProtoHeader.h"
#include "NetworkTimeouts.h"

namespace blazeclaw::irc {

enum class IrcPushEventType {
    Unknown = 0,
    Privmsg,
    Join,
    Part,
    Kick,      // 用户被踢出（自己踢人或被踢，IRC 原始行格式）
    Kicked,    // 被踢通知（服务端推送，被踢方收到）
    Ban,
    Mode,
    Topic,
    Notice,
    Ping,
    Nick,
    Quit,
    Error,
    IrcMessageResp,
    Names,        // RPL_NAMREPLY (353) — message 含逗号分隔的 names 列表
    EndOfNames,   // RPL_ENDOFNAMES (366) — names 列表结束的标记
    GroupInvited, // GROUP_INVITED — 被邀请加入群聊
};

struct IrcPushEvent {
    IrcPushEventType type;
    std::string sender_nick;
    std::string sender_user;
    std::string sender_host;
    std::string channel;
    std::string message;
    std::string raw_line;
    uint64_t timestamp_ms;
    // Kicked 事件专用字段（服务端推送时填充）
    std::string kicked_user_id;   // 被踢用户 UUID（user_id 字段）
    int kicked_node_id = 0;       // 被踢设备 ID（node_id 字段），0 表示非设备
};

using IrcPushCallback = std::function<void(const IrcPushEvent&)>;

class CIrcChatTransport {
public:
    CIrcChatTransport() = default;
    ~CIrcChatTransport() = default;

    CIrcChatTransport(const CIrcChatTransport&) = delete;
    CIrcChatTransport& operator=(const CIrcChatTransport&) = delete;
    CIrcChatTransport(CIrcChatTransport&&) = delete;
    CIrcChatTransport& operator=(CIrcChatTransport&&) = delete;

    static CIrcChatTransport& Instance() {
        static CIrcChatTransport instance;
        return instance;
    }

    bool Initialize();
    void Shutdown();
    void StartReceivers();
    void StopReceivers();

    bool SendIrcMessageTcp(const std::string& payload, std::string* response = nullptr);
    bool SendIrcMessageTls(const std::string& payload, std::string* response = nullptr);

    bool SendPrivmsg(const std::string& channel, const std::string& message);
    bool SendPrivmsgNoWait(const std::string& channel, const std::string& message);

    // Agent 身份发送 PRIVMSG，session_id=0（前端据此识别为服务端/Agent 消息）
    // from: 服务端回显时的发送者标识（如 "炎图AI助手"）
    bool SendPrivmsgAsAgentNoWait(const std::string& channel, const std::string& message,
                                  const std::string& from = "炎图AI助手");

    bool SendIrcCommandTcp(const std::string& channel, const std::string& cmd, const std::string& message = "");
    bool SendIrcCommandTcpNoWait(const std::string& channel, const std::string& cmd, const std::string& message = "");

    std::string SendCommandTcp(const std::string& body);
    std::string SendCommandTls(const std::string& body);
    bool SendCommandNoWait(const std::string& body);
    bool SendCommandTlsNoWait(const std::string& body);

    bool SendJoin(const std::string& channel);
    bool SendPart(const std::string& channel, const std::string& reason = "");
    bool SendKick(const std::string& channel, const std::string& target, const std::string& reason = "");
    bool SendMode(const std::string& channel, const std::string& mode);
    bool SendTopic(const std::string& channel, const std::string& topic);

    bool SendJoinNoWait(const std::string& channel);
    bool SendPartNoWait(const std::string& channel, const std::string& reason = "");
    bool SendKickNoWait(const std::string& channel, const std::string& target, const std::string& reason = "");
    bool SendModeNoWait(const std::string& channel, const std::string& mode);
    bool SendTopicNoWait(const std::string& channel, const std::string& topic);

    void SetPushCallback(IrcPushCallback callback);
    void SetConnectionStateCallback(std::function<void(bool is_tcp, bool is_connected)> callback);

    struct Diagnostics {
        uint64_t messages_sent_tcp = 0;
        uint64_t messages_sent_tls = 0;
        uint64_t push_events = 0;
        bool tcp_connected = false;
        bool tls_connected = false;
    };
    Diagnostics GetDiagnostics() const;

    static IrcPushEvent ParseIrcMessage(const std::string& payload);

    // 构建 IRC PRIVMSG JSON payload，供 bridge 层直接序列化发送。
    static std::string BuildPrivmsgPayload(const std::string& channel,
                                          const std::string& message);

private:

    // 自动重连：收到 (is_connected=false) 时调度重连，避免阻塞回调线程。
    // 重连后再次调用 StartReceivers 重新挂上 push 回调。
    // 退避策略：每次失败 backoff *= 2，上限 30s；成功后重置为 1s。
    void ScheduleAutoReconnect(bool is_tcp);

    // 重连线程循环：在 ScheduleAutoReconnect 起的线程里跑
    void ReconnectLoop(bool is_tcp);

    std::atomic<bool> reconnect_running_{ false };
    std::thread reconnect_thread_;
    std::chrono::milliseconds reconnect_backoff_{ blazeclaw::net::kInitialReconnectBackoff };  // 当前 backoff

    std::atomic<bool> initialized_{ false };
    mutable std::mutex callbacks_mutex_;
    IrcPushCallback push_callback_;

    std::atomic<bool> heartbeat_running_{ false };
    std::thread heartbeat_thread_;
    std::atomic<bool> tcp_connected_{ false };

    std::function<void(bool is_tcp, bool is_connected)> connection_state_callback_;

    std::atomic<uint64_t> messages_sent_tcp_{ 0 };
    std::atomic<uint64_t> messages_sent_tls_{ 0 };
    std::atomic<uint64_t> push_events_{ 0 };
};

}

using CIrcChatTransportPtr = std::shared_ptr<blazeclaw::irc::CIrcChatTransport>;
