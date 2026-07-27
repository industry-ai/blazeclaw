#pragma once

#include <chrono>
#include <cstdint>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
#endif

namespace blazeclaw::net {

// ─── Socket 层超时 (SO_RCVTIMEO / SO_SNDTIMEO) ───
// 同步 SendRequest 路径(登录流程等)依赖此超时：5s 内服务端没回响应则
// recv 返回 -1 + WSAETIMEDOUT，SendRequest 返回 false。
// PushReceiver 路径此时走非 fatal 分支(sleep+continue)，不会自杀。
inline constexpr int kSocketRecvTimeoutMs = 5000;
inline constexpr int kSocketSendTimeoutMs = 5000;

// ─── TCP keepalive idle ───
// 默认 2 小时太长，30s 没活动就开始探测。
// 注意：使用 uint32_t 而非 DWORD，避免依赖 <winsock2.h> 的 typedef 顺序。
inline constexpr uint32_t kTcpKeepaliveIdleMs = 30 * 1000;

// ─── IRC 重连退避 ───
// Defaults for TransportConfig / NetworkTimeouts fallback.
// Prefer blazeclaw.conf keys:
//   agentchat.transport.initialReconnectBackoffMs
//   agentchat.transport.maxReconnectBackoffMs
// 每次失败 backoff *= 2，上限 30s；成功后重置为 1s。
inline constexpr std::chrono::milliseconds kInitialReconnectBackoff{ 1000 };
inline constexpr std::chrono::milliseconds kMaxReconnectBackoff{ 30000 };

// ─── 应用层心跳间隔 ───
// Prefer blazeclaw.conf:
//   agentchat.transport.heartbeatIntervalMs
//   agentchat.transport.heartbeatStepMs
// 服务端长连接每 3 分钟发送一次心跳 (LIST 命令)。
inline constexpr uint32_t kHeartbeatIntervalMs = 180000;

// ─── 心跳线程分段 sleep (ControlLoop 用) ───
inline constexpr int kHeartbeatStepMs = 30000;

// ─── ChatRoomBridge 请求超时 ───
// 异步请求超过此时间未收到响应，触发重试/超时回调。
inline constexpr uint32_t kBridgeRequestTimeoutMs = 30000;

// ─── ChatRoomBridge 发送后最大重试次数 ───
// 超时后通过 timeout_cb 重发的上限；超过则调用原始 callback(false, "request_timeout")。
inline constexpr int kBridgeMaxRetryCount = 2;

// ─── ChatRoomBridge join 刷新间隔 ───
inline constexpr uint32_t kBridgeJoinRefreshIntervalMs = 30000;

// ─── ChatRoomBridge push 队列上限 ───
inline constexpr uint32_t kBridgePushQueueMaxSize = 100;

} // namespace blazeclaw::net
