#include "pch.h"
#include "CNetwork_c.h"
#include "Logger.h"

CNetwork_c::CNetwork_c()
    : is_connected_(false) {
}

CNetwork_c::~CNetwork_c() {
    Disconnect();
}

bool CNetwork_c::Connect(const std::string& ip, int port, bool use_tls) {
    if (is_connected_.load()) {
        Disconnect();
    }

    const std::string server_ip = ip.empty() ? DEFAULT_SERVER_IP : ip;
    const int server_port = (port <= 0) ? DEFAULT_SERVER_PORT : port;

    if (!connection_.Connect(server_ip, server_port, use_tls)) {
        LOG_ERROR("[CNetwork_c] Connect failed: {}:{} use_tls={}", server_ip, server_port, use_tls);
        return false;
    }

    is_connected_.store(true);
    LOG_INFO("[CNetwork_c] Connected: {}:{} use_tls={}", server_ip, server_port, use_tls);
    return true;
}

std::string CNetwork_c::SendRequest(uint16_t type, const std::string& payload) {
    if (!is_connected_.load()) {
        LOG_WARN("[CNetwork_c] Not connected; request not sent (type={})", type);
        return "";
    }

    LOG_INFO("[CNetwork_c] Sending request type={} payload_bytes={}", type, payload.size());
    std::string response = connection_.SendRequest(type, payload);

    if (response.empty()) {
        LOG_WARN("[CNetwork_c] Request failed or empty response (type={})", type);
        return "";
    }

    LOG_INFO("[CNetwork_c] Received response bytes={}", response.size());
    return response;
}

void CNetwork_c::Disconnect() {
    if (is_connected_.exchange(false)) {
        connection_.Close();
        LOG_INFO("[CNetwork_c] Disconnected");
    }
}

bool CNetwork_c::IsConnected() const {
    return is_connected_.load();
}
