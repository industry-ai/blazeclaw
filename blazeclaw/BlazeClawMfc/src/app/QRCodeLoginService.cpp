#include "pch.h"
#include "framework.h"
#include "QRCodeLoginService.h"
#include "CNetwork_c.h"
#include "Logger.h"
#include "config_client.h"
#include <nlohmann/json.hpp>
#include <filesystem>

using nlohmann::json;

QRCodeLoginService& QRCodeLoginService::Instance() {
    static QRCodeLoginService instance;
    return instance;
}

void QRCodeLoginService::Init(CNetwork_c* network) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_network = network;
}

void QRCodeLoginService::SetServerAddress(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_server_ip = ip;
    m_server_port = port;
}

bool QRCodeLoginService::CreateBind(const std::string& device_type,
                                    const std::string& device_name,
                                    const std::string& device_fingerprint) {
    std::lock_guard<std::mutex> lock(m_mutex);

    LOG_INFO("[QRCodeLoginService] ========== CreateBind ==========");
    LOG_INFO("[QRCodeLoginService] device_type: {}", device_type);
    LOG_INFO("[QRCodeLoginService] device_name: {}", device_name);
    LOG_INFO("[QRCodeLoginService] device_fingerprint: {}", device_fingerprint);

    // 加载配置文件
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    std::filesystem::path exeDir = exePath.parent_path();
    std::filesystem::path configPath = exeDir / L"client.conf";

    LOG_INFO("[QRCodeLoginService] Loading config from: {}", configPath.string());
    ConfigClient::instance().loadFromPath(configPath.string());

    // 检查配置
    std::string host = ConfigClient::instance().getHost();
    int port = ConfigClient::instance().getPort();

    LOG_INFO("[QRCodeLoginService] Config: host={}, port={}", host, port);

    // 检查配置是否有效
    if (host.empty()) {
        LOG_ERROR("[QRCodeLoginService] Host is empty! Using default 127.0.0.1");
        host = "127.0.0.1";
    }
    if (port <= 0) {
        LOG_ERROR("[QRCodeLoginService] Port is invalid! Using default 8765");
        port = 8765;
    }

    // 构建 CREATE_BIND 请求（与 TV 端一致）
    json req;
    req["cmd"] = "CREATE_BIND";
    req["device_type"] = device_type;
    req["device_name"] = device_name;
    req["device_fingerprint"] = device_fingerprint;

    std::string payload = req.dump();
    LOG_INFO("[QRCodeLoginService] Request payload: {}", payload);

    // 发送请求
    std::string response = SendNodeBindRequest(payload);
    if (response.empty()) {
        LOG_ERROR("[QRCodeLoginService] Empty response from server");
        LOG_ERROR("[QRCodeLoginService] Possible reasons: server not running, connection failed, or server returned empty response");
        return false;
    }

    LOG_INFO("[QRCodeLoginService] Response: {}", response);

    // 解析响应
    BindResult result = ParseNodeBindResponse(response);
    if (!result.success) {
        LOG_ERROR("[QRCodeLoginService] CreateBind failed: code={}, msg={}",
                  result.code, result.error_message);
        LOG_ERROR("[QRCodeLoginService] Server response indicates failure");
        return false;
    }

    // 构建二维码内容（与 TV 端一致）
    m_bind_token = result.bind_token;

    // 优先使用服务器返回的 qr_payload.url，否则用 bind_token 构建
    if (!result.qr_payload_url.empty()) {
        m_qr_payload = result.qr_payload_url;
    } else {
        // 构建与 TV 端相同的二维码内容格式
        json qr_content;
        qr_content["bind_token"] = m_bind_token;
        qr_content["scene"] = "bind_device_to_conversation";
        m_qr_payload = qr_content.dump();
    }

    m_status.store(QRCodeStatus::Pending);
    m_running.store(true);

    if (!result.expiresAt.empty()) {
        time_t expire_time = ParseIso8601Time(result.expiresAt);
        if (expire_time > 0) {
            m_expires_at = std::chrono::system_clock::from_time_t(expire_time);
            LOG_INFO("[QRCodeLoginService] QR code expires at: {}", result.expiresAt);
        }
    }

    UpdateStatus(QRCodeStatus::Pending, result);
    LOG_INFO("[QRCodeLoginService] Bind created successfully!");
    LOG_INFO("[QRCodeLoginService] bind_token: {}", m_bind_token);
    LOG_INFO("[QRCodeLoginService] qr_payload: {}", m_qr_payload);
    return true;
}

BindResult QRCodeLoginService::GetBindStatus(const std::string& bind_token) {
    std::lock_guard<std::mutex> lock(m_mutex);

    LOG_INFO("[QRCodeLoginService] ========== GetBindStatus ==========");
    LOG_INFO("[QRCodeLoginService] bind_token: {}", bind_token);

    // 构建 GET_BIND 请求（与 TV 端一致）
    json req;
    req["cmd"] = "GET_BIND";
    req["bind_token"] = bind_token;

    std::string payload = req.dump();
    LOG_INFO("[QRCodeLoginService] Request payload: {}", payload);

    // 发送请求
    std::string response = SendNodeBindRequest(payload);
    if (response.empty()) {
        LOG_ERROR("[QRCodeLoginService] Empty response from server");
        return {};
    }

    LOG_INFO("[QRCodeLoginService] Response: {}", response);

    // 解析响应
    BindResult result = ParseNodeBindResponse(response);

    // 根据 status 更新状态
    if (result.success) {
        if (result.status == QRCodeStatus::Bound) {
            m_status.store(QRCodeStatus::Bound);
            m_running.store(false);
        } else if (result.status == QRCodeStatus::Expired) {
            m_status.store(QRCodeStatus::Expired);
            m_running.store(false);
        }
        UpdateStatus(result.status, result);
    }

    return result;
}

std::string QRCodeLoginService::SendNodeBindRequest(const std::string& payload) {
    // 使用 CNetwork_c 单例
    CNetwork_c& network = CNetwork_c::Instance();

    // 获取服务器地址
    std::string ip = m_server_ip;
    int port = m_server_port;

    LOG_INFO("[QRCodeLoginService] Initial: ip='{}', port={}", ip, port);

    // 优先使用 8765 明文端口（根据文档，230/231 必须走明文）
    if (port <= 0) {
        port = ConfigClient::instance().getPort();
        LOG_INFO("[QRCodeLoginService] Got port from ConfigClient: {}", port);
    }
    if (ip.empty()) {
        ip = ConfigClient::instance().getHost();
        LOG_INFO("[QRCodeLoginService] Got host from ConfigClient: {}", ip);
    }

    // 如果仍然是默认值，使用本地地址
    if (ip.empty()) {
        ip = "127.0.0.1";
        LOG_INFO("[QRCodeLoginService] Using default IP: {}", ip);
    }
    if (port <= 0) {
        port = 8765; // 明文聊天端口
        LOG_INFO("[QRCodeLoginService] Using default port: {}", port);
    }

    LOG_INFO("[QRCodeLoginService] Connecting to {}:{} (plaintext, no TLS)", ip, port);

    // 使用明文 TCP 连接（8765 端口需要明文）
    if (!network.IsConnected()) {
        LOG_INFO("[QRCodeLoginService] Not connected, attempting to connect...");
        if (!network.Connect(ip, port, false)) { // false = 不使用 TLS
            LOG_ERROR("[QRCodeLoginService] Connect to {}:{} FAILED", ip, port);
            return "";
        }
        LOG_INFO("[QRCodeLoginService] Connected successfully");
    } else {
        LOG_INFO("[QRCodeLoginService] Already connected, re-using connection");
    }

    // 设置 session_id = 0（设备端请求，与 TV 端一致）
    network.SetSessionId(0);

    // 发送 230 请求（NodeBindReq）
    LOG_INFO("[QRCodeLoginService] Sending NodeBindReq type=230");
    const std::string response = network.SendRequestTcp(static_cast<uint16_t>(MsgType::NodeBindReq), payload);

    if (!response.empty()) {
        LOG_INFO("[QRCodeLoginService] Received response ({} bytes)", response.length());
    } else {
        LOG_ERROR("[QRCodeLoginService] Empty response received");
    }

    return response;
}

BindResult QRCodeLoginService::ParseNodeBindResponse(const std::string& json_response) {
    BindResult result;

    try {
        json root = json::parse(json_response);

        // 解析基本字段
        result.success = root.value("ok", false);
        result.code = root.value("code", 0);

        // 解析 status
        std::string status_str = root.value("status", "unknown");
        if (status_str == "pending") {
            result.status = QRCodeStatus::Pending;
        } else if (status_str == "scanned") {
            result.status = QRCodeStatus::Scanned;
        } else if (status_str == "bound") {
            result.status = QRCodeStatus::Bound;
        } else if (status_str == "expired") {
            result.status = QRCodeStatus::Expired;
        }

        // 提取关键字段
        result.bind_token = root.value("bind_token", "");
        result.chat_session_id = root.value("chat_session_id", "");
        result.conversation_id = root.value("conversation_id", "");
        result.node_id = root.value("node_id", "");
        result.device_id = root.value("device_id", "");
        result.expiresAt = root.value("expires_at", "");

        // 如果有错误消息
        if (root.contains("error")) {
            result.error_message = root["error"].get<std::string>();
        }

        // 获取 qr_payload（与 TV 端 TvBindQrDiagnostics.ets 一致）
        if (root.contains("qr_payload") && root["qr_payload"].is_object()) {
            json qr_payload = root["qr_payload"];
            result.bind_token = qr_payload.value("bind_token", result.bind_token);
            result.qr_payload_url = qr_payload.value("url", "");
        }

        LOG_INFO("[QRCodeLoginService] Parsed: ok={}, code={}, status={}, bind_token={}",
                 result.success, result.code, status_str, result.bind_token);

    } catch (const std::exception& ex) {
        LOG_ERROR("[QRCodeLoginService] Parse error: {}", ex.what());
        result.success = false;
        result.code = 500;
        result.error_message = ex.what();
    }

    return result;
}

void QRCodeLoginService::UpdateStatus(QRCodeStatus status, const BindResult& result) {
    if (m_callback) {
        m_callback(status, result);
    }
}

void QRCodeLoginService::Stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running.store(false);
    m_status.store(QRCodeStatus::Unknown);
    m_bind_token.clear();
    m_qr_payload.clear();
}

time_t QRCodeLoginService::ParseIso8601Time(const std::string& timeStr) {
    if (timeStr.empty()) {
        return 0;
    }

    struct tm tm = {};
    int milliseconds = 0;

    if (sscanf_s(timeStr.c_str(), "%d-%d-%dT%d:%d:%d.%dZ",
                 &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                 &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &milliseconds) == 7) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = 0;
        return _mkgmtime(&tm);
    }

    if (sscanf_s(timeStr.c_str(), "%d-%d-%dT%d:%d:%dZ",
                 &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                 &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = 0;
        return _mkgmtime(&tm);
    }

    return 0;
}

bool QRCodeLoginService::IsLocallyExpired() const {
    if (m_expires_at.time_since_epoch().count() == 0) {
        return false;
    }
    auto now = std::chrono::system_clock::now();
    return now >= m_expires_at;
}
