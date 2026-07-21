#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "CIrcChatTransport.h"
#include "NetworkTimeouts.h"

namespace blazeclaw::irc {

// Forward declarations
class CMgrChannels;

// Request/Response types for bridge communication
struct BridgeRequest {
    std::string request_id;
    std::string kind;  // "send_message", "join_channel", etc.
    std::string payload_json;
    std::string session_id;
    uint64_t timestamp_ms;
};

struct BridgeResponse {
    std::string request_id;
    bool ok;
    std::string error_message;
    std::string payload_json;
    uint64_t timestamp_ms;
};

struct BridgePush {
    std::string channel;  // "chatroom.bridge.push"
    std::string event_type;
    std::string payload_json;
    std::string session_id;
    uint64_t timestamp_ms;
};

// Bridge configuration
struct ChatRoomBridgeConfig {
    uint32_t push_queue_max_size = blazeclaw::net::kBridgePushQueueMaxSize;
    uint32_t request_timeout_ms = blazeclaw::net::kBridgeRequestTimeoutMs;
    uint32_t join_refresh_interval_ms = blazeclaw::net::kBridgeJoinRefreshIntervalMs;
};

// Dependencies for the bridge
struct ChatRoomBridgeDependencies {
    std::function<void(const std::string& json)> emit_to_web;
    std::function<bool(const std::string& kind, const std::string& payload, std::string& response)> route_to_network;
    std::function<std::string()> get_current_session_id;
    std::function<std::string()> get_current_nickname;
    // Send message to network layer
    std::function<bool(uint8_t msg_type, const std::string& payload, std::string& response)> send_request;
};

// Non-blocking: the network layer matches the response by AppProtoHeader::seq and
// invokes the callback from the receiver thread. The caller remains responsible for
// correlating that callback with its WebView requestId.
// Returns false immediately if TCP is not connected.
// timeout_cb: optional callback invoked on timeout. If it returns true, the retry succeeded
//            (and the original callback will be called by the retry).
bool SendAppProtoCommandAsyncWithCb(
    const std::string& command,
    const nlohmann::json& body,
    std::function<void(bool ok, const std::string& payload_or_error)> callback,
    std::function<bool(const std::string& cmd, const nlohmann::json& body)> timeout_cb = nullptr);

// CChatRoomBridge - Bridge between WebView2 and IRC business logic
class CChatRoomBridge {
public:
    CChatRoomBridge() = default;
    ~CChatRoomBridge() = default;

    CChatRoomBridge(const CChatRoomBridge&) = delete;
    CChatRoomBridge& operator=(const CChatRoomBridge&) = delete;
    CChatRoomBridge(CChatRoomBridge&&) = delete;
    CChatRoomBridge& operator=(CChatRoomBridge&&) = delete;

    // Singleton access
    static CChatRoomBridge& Instance() {
        static CChatRoomBridge instance;
        return instance;
    }

    // Initialize the bridge (legacy version for backward compatibility)
    void Initialize(ChatRoomBridgeDependencies deps, ChatRoomBridgeConfig config = {});

    // Initialize the bridge with IRC transport (uses existing CNetwork_c connection)
    void InitializeWithTransport(ChatRoomBridgeDependencies deps,
                                 ChatRoomBridgeConfig config = {});

    // Get IRC transport for advanced usage
    CIrcChatTransport& GetIrcTransport() { return CIrcChatTransport::Instance(); }

    // Shutdown
    void Shutdown();

    // Handle incoming web message (sync — returns response directly; blocks caller thread)
    bool HandleWebMessage(const BridgeRequest& req, BridgeResponse& resp);

    // Async version: non-blocking. Starts the operation and returns immediately.
    // When the response arrives (or on error/timeout), posts result to web via emit_to_web.
    // This should be used from the UI thread to avoid blocking.
    void HandleWebMessageAsync(const BridgeRequest& req);

    // Push event to web
    void EmitToWeb(const BridgePush& push);

    // Called when network sends a push event
    void OnNetworkPush(const std::string& event_type, const std::string& payload_json);

    // 发送 OpenClaw AI Agent 请求（后台线程，对齐 AIAssistant/JsBridge 的 openclaw 调用逻辑）
    // channel: IRC 频道名（如 #group_xxx）
    // message: 用户原始消息（含 @炎图AI助手 时由调用方决定是否触发）
    // openclawHost: OpenClaw 服务地址（默认 "192.168.20.12"）
    // openclawPort: OpenClaw 服务端口（默认 3000）
    // openclawPath: OpenClaw API 路径（默认 "/api/openclaw-agent"）
    // timeoutMs: HTTP 请求超时（默认 140000，对齐 AIAssistant）
    void SendOpenClawAgentRequest(const std::string& channel, const std::string& message,
                                  const std::string& openclawHost = "192.168.20.12",
                                  int openclawPort = 3000,
                                  const std::string& openclawPath = "/api/openclaw-agent",
                                  DWORD timeoutMs = 140000);

    // Get diagnostics
    struct Diagnostics {
        uint64_t requests_received = 0;
        uint64_t requests_handled = 0;
        uint64_t requests_failed = 0;
        uint64_t push_emitted = 0;
        uint64_t current_queue_size = 0;
        uint32_t active_sessions = 0;
    };
    Diagnostics GetDiagnostics() const;

private:
    // Request handlers (all non-blocking / fire-and-forget; the actual server
    // response is routed back to the web via emit_to_web).
    bool HandleSendMessage(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleSendPrompt(const BridgeRequest& req, BridgeResponse& resp);
    void HandleJoinChannelViaIrc(const BridgeRequest& req);
    void HandleSendMessageViaIrc(const BridgeRequest& req);
    bool HandlePartChannel(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleKickMember(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleBanMember(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleSetTopic(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleSetMode(const BridgeRequest& req, BridgeResponse& resp);
    bool HandlePromoteOperator(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleDemoteOperator(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleWhois(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleNames(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleCreateTopic(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleReplyTopic(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleListTopics(const BridgeRequest& req, BridgeResponse& resp);
    bool HandleCloseTopic(const BridgeRequest& req, BridgeResponse& resp);

    // Route request to appropriate handler
    bool DispatchRequest(const BridgeRequest& req, BridgeResponse& resp);

    // Build response JSON
    std::string BuildResponseJson(const BridgeResponse& resp);
    std::string BuildPushJson(const BridgePush& push);

    // Session management
    void RegisterSession(const std::string& session_id);
    void UnregisterSession(const std::string& session_id);
    void TrackJoinedChannel(const std::string& session_id, const std::string& channel);
    void UntrackJoinedChannel(const std::string& session_id, const std::string& channel);

    // 发送响应 JSON 到 WebView（requestId + ok + error + payload）
    void EmitResponse(const std::string& request_id, bool ok,
                      const std::string& error, const std::string& payload_json);

    // Timeout handling
    void HandleRequestTimeout(const std::string& request_id);

    bool initialized_ = false;
    ChatRoomBridgeDependencies deps_;
    ChatRoomBridgeConfig config_;
    
    mutable std::mutex sessions_mutex_;
    std::unordered_set<std::string> known_sessions_;

    mutable std::mutex joined_channels_mutex_;
    std::unordered_map<std::string, uint64_t> joined_channels_;  // "session:channel" -> joined_at_ms

    mutable std::mutex push_queue_mutex_;
    std::unordered_map<std::string, std::deque<BridgePush>> push_queues_;  // session -> queue

    mutable std::mutex pending_requests_mutex_;
    std::unordered_map<std::string, uint64_t> pending_requests_;  // request_id -> timestamp_ms

    std::atomic<uint64_t> requests_received_{0};
    std::atomic<uint64_t> requests_handled_{0};
    std::atomic<uint64_t> requests_failed_{0};
    std::atomic<uint64_t> push_emitted_{0};

    // 自动重连队列：当 TCP 连接断开时，请求进入队列；连接恢复后自动重试
    mutable std::mutex pending_requests_queue_mutex_;
    std::deque<BridgeRequest> pending_requests_queue_;  // 连接断开时排队的请求

    std::atomic<bool> is_connected_{true};  // TCP 连接状态（初始假设已连接）

    // Agent 消息追踪：存储"channel:message"键值，push 回调匹配后覆写 sender 并移除
    mutable std::mutex pending_agent_replies_mutex_;
    std::unordered_set<std::string> pending_agent_replies_;

    // 启动重连后重试排队的请求
    void RetryPendingRequests();
};

} // namespace blazeclaw::irc
