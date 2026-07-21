#include "pch.h"
#include "CNetwork_c.h"
#include "Logger.h"

#include <mutex>

#include <nlohmann/json.hpp>

CNetwork_c::CNetwork_c()
    : tls_connected_(false),
      tcp_connected_(false) {
}

CNetwork_c::~CNetwork_c() {
    Disconnect();
}

bool CNetwork_c::Connect(const std::string& ip, int port, bool use_tls) {
    if (use_tls) {
        return ConnectTls(server_config_.tls_host,
                        server_config_.tls_port );
    } else {
        return ConnectTcp(server_config_.tcp_host ,
                        server_config_.tcp_port );
    }
}

bool CNetwork_c::ConnectTcp(const std::string& ip, int port) {
    // Hold the lock during the entire connect + start receivers sequence
    // to prevent race with disconnect callback
    std::lock_guard<std::recursive_mutex> lock(push_receiver_mutex_);

    if (tcp_connected_.load()) {
        DisconnectTcp();
    }

    const std::string server_ip = server_config_.tcp_host;
    const int server_port = server_config_.tcp_port;

    if (!tcp_connection_.Connect(server_ip, server_port, false)) {
        LOG_ERROR("[CNetwork_c] ConnectTcp failed: {}:{}", server_ip, server_port);
        return false;
    }

    tcp_connected_.store(true);
    LOG_INFO("[CNetwork_c] TCP Connected: {}:{}", server_ip, server_port);
    TRACE(_T("[CNetwork_c] ConnectTcp SUCCESS: tcp_connected_=true, calling StartPushReceivers\n"));

    StartPushReceivers();

    TRACE(_T("[CNetwork_c] ConnectTcp: StartPushReceivers returned\n"));

    // 触发上层重连成功事件，让 CIrcChatTransport 把 backoff 重置。
    // 关键修复：之前只有 disconnect 时才发 (is_tcp=true, is_connected=false)，
    // 上层无法感知 reconnect succeeded → backoff 不重置 → 后续每次重试都用 30s 长 backoff。
    {
        std::lock_guard<std::mutex> lock(connection_state_mutex_);
        if (connection_state_callback_) {
            try {
                connection_state_callback_(true, true);
            } catch (...) {
                LOG_ERROR("[CNetwork_c] state callback threw on connect");
            }
        }
    }
    return true;
}

bool CNetwork_c::ConnectTls(const std::string& ip, int port) {
    if (tls_connected_.load()) {
        DisconnectTls();
    }

    const std::string server_ip = ip.empty() ? server_config_.tls_host : ip;
    const int server_port = (port <= 0) ? server_config_.tls_port : port;

    if (!tls_connection_.Connect(server_ip, server_port, true)) {
        LOG_ERROR("[CNetwork_c] ConnectTls failed: {}:{}", server_ip, server_port);
        return false;
    }

    tls_connected_.store(true);
    LOG_INFO("[CNetwork_c] TLS Connected: {}:{}", server_ip, server_port);

    // 触发上层重连成功事件（同上，ConnectTcp 的注释）
    {
        std::lock_guard<std::mutex> lock(connection_state_mutex_);
        if (connection_state_callback_) {
            try {
                connection_state_callback_(false, true);
            } catch (...) {
                LOG_ERROR("[CNetwork_c] state callback threw on tls connect");
            }
        }
    }
    return true;
}

void CNetwork_c::Disconnect() {
    DisconnectTcp();
    DisconnectTls();
}

void CNetwork_c::DisconnectTcp() {
    if (tcp_connected_.exchange(false)) {
        TRACE(_T("[CNetwork_c] DisconnectTcp called: was true -> false. Caller:\n"));
        tcp_connection_.Close();
        LOG_INFO("[CNetwork_c] TCP Disconnected");
    }
}

void CNetwork_c::DisconnectTls() {
    if (tls_connected_.exchange(false)) {
        tls_connection_.Close();
        LOG_INFO("[CNetwork_c] TLS Disconnected");
    }
}

std::string CNetwork_c::SendRequest(uint16_t type, const std::string& payload) {
    // Default: use TLS connection
    return SendRequestTls(type, payload);
}

std::string CNetwork_c::SendRequestTcp(uint16_t type, const std::string& payload) {
    if (!tcp_connected_.load()) {
        LOG_WARN("[CNetwork_c] TCP not connected; request not sent (type={})", type);
        return "";
    }

    LOG_DEBUG("[CNetwork_c] Sending via TCP: type={} payload_bytes={}", type, payload.size());
    std::string response = tcp_connection_.SendRequest(type, payload);

    if (response.empty()) {
        LOG_WARN("[CNetwork_c] TCP request failed or empty response (type={})", type);
    }
    return response;
}

std::string CNetwork_c::SendRequestTls(uint16_t type, const std::string& payload) {
    if (!tls_connected_.load()) {
        LOG_WARN("[CNetwork_c] TLS not connected; request not sent (type={})", type);
        return "";
    }

    LOG_DEBUG("[CNetwork_c] Sending via TLS: type={} payload_bytes={}", type, payload.size());
    std::string response = tls_connection_.SendRequest(type, payload);

    if (response.empty()) {
        LOG_WARN("[CNetwork_c] TLS request failed or empty response (type={})", type);
    }
    return response;
}

bool CNetwork_c::SendTcpNoWait(uint16_t type, const std::string& payload) {
    if (!tcp_connected_.load()) {
        return false;
    }
    return tcp_connection_.SendNoWait(type, payload);
}

bool CNetwork_c::SendTcpNoWaitWithSession(uint16_t type, const std::string& payload, uint64_t session_id) {
    if (!tcp_connected_.load()) {
        return false;
    }
    return tcp_connection_.SendNoWaitWithSession(type, payload, session_id);
}

bool CNetwork_c::SendTlsNoWait(uint16_t type, const std::string& payload) {
    if (!tls_connected_.load()) {
        return false;
    }
    return tls_connection_.SendNoWait(type, payload);
}

uint32_t CNetwork_c::SendTcpNoWaitWithCallback(uint16_t type,
                                               const std::string& payload,
                                               std::function<void(uint32_t seq, const std::string& payload)> callback) {
    if (!tcp_connected_.load()) {
        return 0;
    }
    return tcp_connection_.SendNoWaitWithCallback(type, payload, std::move(callback));
}

bool CNetwork_c::IsConnected() const {
    return tls_connected_.load() || tcp_connected_.load();
}

bool CNetwork_c::IsTcpConnected() const {
    return tcp_connected_.load();
}

bool CNetwork_c::IsTlsConnected() const {
    return tls_connected_.load();
}

bool CNetwork_c::StartPushReceivers() {
    TRACE(_T("[CNetwork_c] StartPushReceivers: called, tcp_connected_=%d\n"), tcp_connected_.load() ? 1 : 0);

    std::lock_guard<std::recursive_mutex> lock(push_receiver_mutex_);

    // Start TCP receiver if connected
    if (tcp_connected_.load()) {
        LOG_INFO("[CNetwork_c] StartPushReceivers: TCP connected, setting callbacks");
        tcp_connection_.SetDisconnectCallback([this]() {
            // Use push_receiver_mutex_ to prevent race with StartPushReceivers
            std::lock_guard<std::recursive_mutex> lock(push_receiver_mutex_);
            TRACE(_T("[CNetwork_c] DISCONNECT CALLBACK FIRED\n"));
            if (tcp_connected_.exchange(false)) {
                LOG_INFO("[CNetwork_c] TCP connection aborted (detected via disconnect callback)");
                std::lock_guard<std::mutex> lock2(connection_state_mutex_);
                if (connection_state_callback_) {
                    LOG_INFO("[CNetwork_c] Calling connection_state_callback_ with is_tcp=true, is_connected=false");
                    connection_state_callback_(true, false);
                }
            }
        });
        LOG_INFO("[CNetwork_c] StartPushReceivers: disconnect callback set");

        tcp_connection_.SetPushCallback([this](const AppProtoHeader& header, const std::string& payload) {
            // Async responses are handled in CConnection_c::StartPushReceiver (seq-based matching).
            // This callback receives only true push events (no matching async request).
            TcpPushCallback cb;
            {
                std::lock_guard<std::mutex> lock(tcp_push_mutex_);
                cb = tcp_push_callback_;
            }
            if (cb) {
                cb(payload);
            }
        });

        // 鍦ㄥ惎鍔?PushReceiver 涔嬪墠鍐嶆妫€鏌ヨ繛鎺ョ姸鎬?
        if (!tcp_connected_.load()) {
            LOG_WARN("[CNetwork_c] StartPushReceivers: TCP disconnected before starting receiver");
            return false;
        }

        LOG_INFO("[CNetwork_c] StartPushReceivers: starting TCP receiver");
        tcp_connection_.StartPushReceiver();
        LOG_INFO("[CNetwork_c] StartPushReceivers: TCP receiver started");
    }

    // Start TLS receiver if connected
    //if (tls_connected_.load()) {
    //    tls_connection_.SetDisconnectCallback([this]() {
    //        if (tls_connected_.exchange(false)) {
    //            LOG_WARN("[CNetwork_c] TLS connection aborted (detected via disconnect callback)");
    //            std::lock_guard<std::mutex> lock(connection_state_mutex_);
    //            if (connection_state_callback_) {
    //                connection_state_callback_(false, false);
    //            }
    //        }
    //    });
    //    tls_connection_.SetPushCallback([this](const AppProtoHeader& header, const std::string& payload) {
    //        // Async responses are handled in CConnection_c::StartPushReceiver (seq-based matching).
    //        // This callback receives only true push events (no matching async request).
    //        TlsPushCallback cb;
    //        {
    //            std::lock_guard<std::mutex> lock(tls_push_mutex_);
    //            cb = tls_push_callback_;
    //        }
    //        if (cb) {
    //            try {
    //                cb(payload);
    //            } catch (const std::exception& e) {
    //                LOG_ERROR("[CNetwork_c] TLS push callback exception: {}", e.what());
    //            }
    //        } else {
    //            LOG_WARN("[CNetwork_c] TLS push: no callback registered");
    //        }
    //    });
    //    tls_connection_.StartPushReceiver();
    //}

    return true;
}

void CNetwork_c::StopPushReceivers() {
    tcp_connection_.StopPushReceiver();
    tls_connection_.StopPushReceiver();
}

void CNetwork_c::SetTcpPushCallback(TcpPushCallback callback) {
    std::lock_guard<std::mutex> lock(tcp_push_mutex_);
    tcp_push_callback_ = std::move(callback);
}

void CNetwork_c::SetTlsPushCallback(TlsPushCallback callback) {
    std::lock_guard<std::mutex> lock(tls_push_mutex_);
    tls_push_callback_ = std::move(callback);
}

void CNetwork_c::SetConnectionStateCallback(ConnectionStateCallback callback) {
    std::lock_guard<std::mutex> lock(connection_state_mutex_);
    connection_state_callback_ = std::move(callback);
}

void CNetwork_c::SetHeartbeatConfig(std::chrono::milliseconds interval,
                                    std::chrono::milliseconds step) {
    tcp_connection_.SetHeartbeatInterval(interval);
    tcp_connection_.SetHeartbeatStep(step);
    tls_connection_.SetHeartbeatInterval(interval);
    tls_connection_.SetHeartbeatStep(step);
    LOG_INFO("[CNetwork_c] Heartbeat config applied: interval={}ms step={}ms",
             static_cast<long long>(interval.count()),
             static_cast<long long>(step.count()));
}

