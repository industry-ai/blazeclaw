#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "NetworkTimeouts.h"

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  // POSIX sockets
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <errno.h>

  using SOCKET = int;
  #ifndef INVALID_SOCKET
    #define INVALID_SOCKET (-1)
  #endif
  #ifndef SOCKET_ERROR
    #define SOCKET_ERROR (-1)
  #endif
  // Provide closesocket alias to minimize platform-specific code in .cpp
  static inline int closesocket(SOCKET s) { return ::close(s); }
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "AppProtoHeader.h"

// 字节序转换函数声明
inline uint64_t hton64(uint64_t value);
inline uint64_t ntoh64(uint64_t value);

inline uint64_t SessionWordSwap64(uint64_t value) {
    uint32_t hi = static_cast<uint32_t>(value >> 32);
    uint32_t lo = static_cast<uint32_t>(value & 0xFFFFFFFFu);
    return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

// 协议头字段设置和获取函数
inline void AppProtoHeader_SetPayloadLen(AppProtoHeader* h, uint32_t payload_len);
inline uint32_t AppProtoHeader_GetPayloadLen(const AppProtoHeader* h);
inline void AppProtoHeader_SetSeq(AppProtoHeader* h, uint32_t seq);
inline uint32_t AppProtoHeader_GetSeq(const AppProtoHeader* h);
inline void AppProtoHeader_SetTimestampMs(AppProtoHeader* h, uint64_t ts_ms);
inline uint64_t AppProtoHeader_GetTimestampMs(const AppProtoHeader* h);

// Avoid macro collision: some system/third-party headers or project settings may
// define a macro named 'Connect'. Undefine it so the class method `Connect` can exist.
#ifdef Connect
#undef Connect
#endif

// 接收操作结果分类。用于 recv_all 返回值和 ReadMessage 的 last_recv_status_ 存储。
// PushReceiver 据此区分"超时（继续）"和"对端关闭/致命错误（断开并重连）"。
// 关键修复（原 bug #7+）：之前 recv_all 只返回 bool，混在一起返回 false，PushReceiver 只能
// 用 get_last_error() 推测，在 Windows 上 recv 返回 0（peer FIN）时 get_last_error() 也是 0，
// 会走 fatal 分支错误地断连；且 5s timeout 也走同一路径容易误判。
enum class RecvStatus {
    Success,     // 读到数据
    Timeout,     // SO_RCVTIMEO 到期（服务端暂时没推数据），非 fatal —— sleep+continue
    Eof,         // peer FIN 或 TLS zero-return（对端正常关闭写半边），fatal → 触发重连
    FatalError,  // ECONNRESET / ENOTCONN 等致命错误，fatal → 触发重连
};

class CConnection_c {
public:
    CConnection_c();
    ~CConnection_c();

    // Connect to server.
    // `use_tls`: true = TLS connection (default), false = plaintext TCP
    // `host` may be a DNS name (preferred) or an IP.
    // When `host` is a DNS name, SNI and hostname verification are enabled.
    bool Connect(const std::string& host, int port, bool use_tls = true);

    // Send request and get response
    std::string SendRequest(uint16_t type, const std::string& payload);

    // Disconnect
    void Close();

    // Read a framed message; returns false on EOF/error.
    // Detailed error type is stored in last_recv_status_ — call get_last_recv_status()
    // AFTER ReadMessage returns false to distinguish timeout (retry/continue) from
    // fatal errors (Eof/FatalError → trigger reconnect).
 
    bool ReadMessage(AppProtoHeader& outHeader, std::string& outPayload,
                     int recv_timeout_ms = -1);

    // Query the detailed result of the last ReadMessage call.
    // Call this ONLY after ReadMessage returns false to determine error type.
    RecvStatus get_last_recv_status() const { return last_recv_status_.load(); }

    // Fire-and-forget: send a framed request and return immediately without reading a response.
    // Use this when the caller does not expect a response (e.g., chat messages) so that the
    // push receiver thread is the only consumer of incoming frames on this socket.
    bool SendNoWait(uint16_t type, const std::string& payload);

    // Fire-and-forget with explicit session_id.  Agent broadcasts must use
    // session_id=0 so the frontend identifies them as server/agent messages.
    bool SendNoWaitWithSession(uint16_t type, const std::string& payload, uint64_t session_id);

    // Fire-and-forget with callback: 分配 seq 并注册 callback，响应到达时通过 seq 匹配调用 callback。
    // 与 SendNoWait 不同的是：发送后会等待响应（由 PushReceiver 匹配 seq 并调用 callback）。
    // 返回分配的 seq（用于调试或取消）。不返回 future，因为是 fire-and-forget 模式。
    // callback 会在响应帧到达时被调用（通过 seq 匹配）。
    uint32_t SendNoWaitWithCallback(uint16_t type,
                                    const std::string& payload,
                                    std::function<void(uint32_t seq, const std::string& payload)> callback);

    using PushCallback = std::function<void(const AppProtoHeader& header, const std::string& payload)>;
    void SetPushCallback(PushCallback callback);

    // Set a callback invoked when the connection is detected as closed (EOF/error in receiver).
    // The callback runs on the receiver thread. Safe to use for triggering reconnection.
    using DisconnectCallback = std::function<void()>;
    void SetDisconnectCallback(DisconnectCallback callback);

    // Start a background thread to receive push messages
    bool StartPushReceiver();
    void StopPushReceiver();

    // Get the session ID from the last response
    uint64_t GetLastResponseSessionId() const {
        return last_response_session_id_.load();
    }

    // Set session ID for outgoing requests (used for device binding, session_id = 0)
    void SetSessionId(uint64_t session_id) {
        next_session_id_.store(session_id);
    }

    // Get current session ID used for outgoing requests (TCP path stores the
    // SessionWordSwap64-transformed value; this returns that wire-format value).
    uint64_t GetNextSessionId() const {
        return next_session_id_.load();
    }

private:
    SOCKET sock_;
    std::atomic<bool> is_connected_{ false };
    bool use_tls_;
    SSL_CTX* ssl_ctx_;
    SSL* ssl_;

    uint64_t conn_id_;
    uint32_t next_seq_;
    std::atomic<uint64_t> last_response_session_id_{ 0 };
    std::atomic<uint64_t> next_session_id_{ 0 };  // 下一个请求使用的 session_id（设备绑定时设为0）

    // Stores the detailed result of the last ReadMessage() call.
    // Only meaningful when ReadMessage() returns false.
    std::atomic<RecvStatus> last_recv_status_{ RecvStatus::Success };

    // Push receiver + push dispatch threads
    // - receiver_thread: reads from socket, resolves async futures, enqueues push messages
    // - push_thread: dequeues push messages and dispatches to user callback
    // This separation ensures async response resolution is never blocked by user callbacks.
    PushCallback push_callback_;
    std::atomic<bool> receiver_running_{ false };
    std::atomic<bool> push_running_{ false };
    // 线程 lambda 退出后置 true，用于 StartPushReceiver 判断"运行 flag=true 但线程已结束"
    // 的情况（fatal 分支走完后没 join 自己，只能下次重启时由调用方 join）。
    std::atomic<bool> receiver_finished_{ true };
    std::atomic<bool> push_finished_{ true };
    std::thread receiver_thread_;
    std::thread push_thread_;
    std::mutex push_mutex_;  // Protects push_callback_
    DisconnectCallback disconnect_callback_;

    // Lock-free push message queue (receiver -> push thread)
    struct PushMsg {
        AppProtoHeader header;
        std::string payload;
    };
    std::deque<PushMsg> push_queue_;
    std::mutex push_queue_mutex_;
    std::condition_variable push_queue_cv_;

mutable std::mutex async_mutex_;
    // Fire-and-forget-with-callback 注册表：SendNoWaitWithCallback 写入，
    // PushReceiver 在收到对应 seq 响应帧时调用。
    std::map<uint32_t, std::function<void(uint32_t seq, const std::string& payload)>> pending_async_done_;

    // 全局发送互斥锁：序列化所有 send_all 调用。
    // 关键：同一 socket 上不能有并发的写者（SendRequest / SendNoWait / SendNoWaitWithCallback
    // 三条路径），否则多线程同时 write 会让 TCP 字节流交叉，服务端读到非法帧 → RST → ECONNABORTED。
    // 只锁发送，不锁接收：读线程（PushReceiver）和写线程（任意调用方）是独立角色。
    mutable std::mutex send_mutex_;

    // 全局接收互斥锁：序列化所有 recv_all 调用。
    // 关键：SendRequest (同步 send+recv) 在自己的调用线程 recv，
    // 而 PushReceiver 线程也在 recv。两条路径并发 recv 同一 socket 会瓜分 TCP 字节流，
    // 导致一帧被切到两个线程 → 都解析失败 → 服务端 RST → ECONNABORTED。
    mutable std::mutex recv_mutex_;

    // next_seq_ 序列号互斥锁：保护 next_seq_ 自增。
    // 关键：SendRequest / SendNoWait / SendNoWaitWithCallback 都直接 next_seq_++（原子性 OK，
    // 但多线程同时自增会出现重复 seq）。所有路径必须用同一把锁串行化。
    mutable std::mutex seq_mutex_;

    // session_id 互斥锁：保护 next_session_id_ 的读写。
    mutable std::mutex session_mutex_;

    // ─── 应用层心跳 (PING/PONG) ───
    // P0 修复：服务端经常因长连接 idle (默认 60~120s) 主动 RST。
    // 解决：起一个独立线程，每 heartbeat_interval_ms 发送 Ping (type=21)。
    std::atomic<bool> heartbeat_running_{ false };
    std::thread heartbeat_thread_;
    std::chrono::milliseconds heartbeat_interval_ms_{ blazeclaw::net::kHeartbeatIntervalMs };

    bool send_all(const char* data, int len);
    // 接收:recv 超时由 socket 层 SO_RCVTIMEO 控制(Connect() 里 setsockopt),
    // recv_timeout_ms 参数保留仅为 API 兼容,内部不使用。
    // recv_all 必须明确告诉调用方"这是超时，不是 EOF"。
    // 之前只返回 bool，PushReceiver 只能靠 get_last_error() 推测，而 Windows 上
    // recv=0 时 get_last_error() 也是 0，会和 timeout 混在一起走 fatal 分支。
    RecvStatus recv_all(char* buf, int len, int recv_timeout_ms = -1);

    void initOpenSSL();
    void cleanupOpenSSL();

    // 启动/停止心跳线程
    void StartHeartbeat();
    void StopHeartbeat();
    void HeartbeatLoop();

    // 设置 TCP keep-alive (SO_KEEPALIVE + TCP_KEEPALIVE/CNT/INTVL)。
    // 必须在 connect() 成功后立即调用。
    bool EnableTcpKeepAlive(SOCKET s);
};


