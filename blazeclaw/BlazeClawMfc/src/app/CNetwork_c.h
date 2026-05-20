#pragma once

#include <atomic>
#include <string>

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

    // Synchronous request/response only (thread-safe read model: single reader).
    std::string SendRequest(uint16_t type, const std::string& payload);

    void Disconnect();

    bool IsConnected() const;

    // 设置 session_id（用于设备端请求，session_id = 0）
    void SetSessionId(uint64_t sessionId) {
        connection_.SetSessionId(sessionId);
    }

    // Get the session ID from the last response
    uint64_t GetLastResponseSessionId() const {
        return connection_.GetLastResponseSessionId();
    }

    std::atomic<bool>   running{ true };

private:
    CNetwork_c();
    ~CNetwork_c();

private:
    CConnection_c connection_;
    std::atomic<bool> is_connected_{ false };

    static constexpr const char* DEFAULT_SERVER_IP = "192.168.0.211";
    static constexpr int DEFAULT_SERVER_PORT = 9443;
};
