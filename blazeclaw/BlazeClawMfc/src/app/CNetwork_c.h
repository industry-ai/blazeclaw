#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <future>

#include "Connection_c.h"

// 网络通信封装类
// 负责通过 TCP/TLS 与中台进行通信
class CNetwork_c {
public:
    static CNetwork_c& Instance() {
        static CNetwork_c instance;
        return instance;
    }

    CNetwork_c(const CNetwork_c&) = delete;
    CNetwork_c& operator=(const CNetwork_c&) = delete;
    CNetwork_c(CNetwork_c&&) = delete;
    CNetwork_c& operator=(CNetwork_c&&) = delete;

    // Connect to server
    // `use_tls`: true = TLS connection (default), false = plaintext TCP
    bool Connect(const std::string& ip, int port, bool use_tls = true);

    // Connect specifically to TCP port (for AI prompts)
    bool ConnectTcp(const std::string& ip = "", int port = 0);

    // Connect specifically to TLS port (for sensitive data)
    bool ConnectTls(const std::string& ip = "", int port = 0);

    // Disconnect all connections
    void Disconnect();

    // Disconnect specific connection
    void DisconnectTcp();
    void DisconnectTls();

    // Synchronous request/response
    // Default: uses TLS connection
    std::string SendRequest(uint16_t type, const std::string& payload);

    // Send via specific connection type
    std::string SendRequestTcp(uint16_t type, const std::string& payload);
    std::string SendRequestTls(uint16_t type, const std::string& payload);

    // Fire-and-forget: send without reading a response.
    // Safe to use concurrently with the push receiver thread.
    bool SendTcpNoWait(uint16_t type, const std::string& payload);
    bool SendTlsNoWait(uint16_t type, const std::string& payload);

    // Fire-and-forget with explicit session_id.  Agent broadcasts use session_id=0.
    bool SendTcpNoWaitWithSession(uint16_t type, const std::string& payload, uint64_t session_id);


    uint32_t SendTcpNoWaitWithCallback(uint16_t type,
                                       const std::string& payload,
                                       std::function<void(uint32_t seq, const std::string& payload)> callback);

    // Connection state
    bool IsConnected() const;
    bool IsTcpConnected() const;
    bool IsTlsConnected() const;

    // Start/stop background push receivers for both TCP and TLS
    bool StartPushReceivers();
    void StopPushReceivers();

    // Set push callback for TCP/TLS connections
    using TcpPushCallback = std::function<void(const std::string& payload)>;
    using TlsPushCallback = std::function<void(const std::string& payload)>;
    void SetTcpPushCallback(TcpPushCallback callback);
    void SetTlsPushCallback(TlsPushCallback callback);

    // Set connection state change callback (called when TCP/TLS connection goes down)
    using ConnectionStateCallback = std::function<void(bool is_tcp, bool is_connected)>;
    void SetConnectionStateCallback(ConnectionStateCallback callback);

    // Apply config-driven app-layer heartbeat intervals to both connections.
    void SetHeartbeatConfig(std::chrono::milliseconds interval,
                            std::chrono::milliseconds step);

    // Get references to underlying connections (for setting disconnect callbacks, etc.)
    CConnection_c& GetTcpConnection() { return tcp_connection_; }
    CConnection_c& GetTlsConnection() { return tls_connection_; }

    // Set session_id (used by device-side requests, session_id = 0 means unset).
    void SetSessionId(uint64_t sessionId) {
        tls_connection_.SetSessionId(sessionId);
        tcp_connection_.SetSessionId(SessionWordSwap64(sessionId));
    }

    // Get the session ID from the last response
    uint64_t GetLastResponseSessionId() const {
        return tls_connection_.GetLastResponseSessionId();
    }

    // Get the session ID currently used for outgoing TCP frames (the
    // SessionWordSwap64-transformed wire value). Use this when you need the
    // exact bytes the server sees in the AppProto header for HTTP body fields.
    uint64_t GetTcpWireSessionId() const {
        return tcp_connection_.GetNextSessionId();
    }

    std::atomic<bool> running{ true };

    // Server configuration
    struct ServerConfig {
        std::string tcp_host;
        int tcp_port;
        std::string tls_host;
        int tls_port;
    };

    ServerConfig server_config_;

private:
    CNetwork_c();
    ~CNetwork_c();

private:
    CConnection_c tls_connection_;  // TLS connection (9443)
    CConnection_c tcp_connection_;  // TCP connection (8765)
    std::atomic<bool> tls_connected_{ false };
    std::atomic<bool> tcp_connected_{ false };

    // Push callbacks for TCP/TLS
    TcpPushCallback tcp_push_callback_;
    TlsPushCallback tls_push_callback_;
    mutable std::mutex tcp_push_mutex_;
    mutable std::mutex tls_push_mutex_;

    // Connection state change callback
    ConnectionStateCallback connection_state_callback_;
    mutable std::mutex connection_state_mutex_;

    // Recursive mutex to allow nested locking from ConnectTcp -> StartPushReceivers
    // Protects tcp_connected_ state during connect + start receiver sequence
    mutable std::recursive_mutex push_receiver_mutex_;
};