#include "pch.h"
#include "Client.h"

#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CNetwork_c.h"
#include "IoData_cFactory.h"
#include "LogSinks.h"
#include "Logger.h"
#include "config_client.h"

// Base64URL解码函数
static std::string base64url_decode(const std::string& input) {
    std::string encoded = input;
    // 替换base64url特殊字符
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '-') encoded[i] = '+';
        else if (encoded[i] == '_') encoded[i] = '/';
    }
    // 补全padding
    size_t padding = 4 - (encoded.size() % 4);
    if (padding < 4) {
        encoded.append(padding, '=');
    }
    
    // 标准base64解码
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    // Use size_t to match std::string::find return type and avoid narrowing warnings (C4267).
    std::vector<size_t> vec(4);

    for (size_t i = 0; i < encoded.size(); i += 4) {
        for (size_t j = 0; j < 4; ++j) {
            vec[j] = base64_chars.find(encoded[i + j]);
        }
        // Found values are indexes into base64_chars. Use explicit casts when appending to string.
        decoded += static_cast<char>((vec[0] << 2) | (vec[1] >> 4));
        if (vec[2] != std::string::npos) {
            decoded += static_cast<char>((vec[1] << 4) | (vec[2] >> 2));
        }
        if (vec[3] != std::string::npos) {
            decoded += static_cast<char>((vec[2] << 6) | vec[3]);
        }
    }
    
    return decoded;
}

// JWT解析函数，提取sub和phone
static std::pair<std::string, std::string> parse_jwt(const std::string& jwt) {
    std::string sub, phone;
    
    // 分割JWT的三部分
    size_t first_dot = jwt.find('.');
    size_t second_dot = jwt.find('.', first_dot + 1);
    
    if (first_dot == std::string::npos || second_dot == std::string::npos) {
        LOG_ERROR("[CClient] Invalid JWT format");
        return {sub, phone};
    }
    
    // 提取payload部分
    std::string payload_encoded = jwt.substr(first_dot + 1, second_dot - first_dot - 1);
    std::string payload_decoded = base64url_decode(payload_encoded);
    
    // 解析JSON
    try {
        using nlohmann::json;
        json root = json::parse(payload_decoded);
        
        if (root.contains("sub")) {
            sub = root["sub"].get<std::string>();
        }
        if (root.contains("phone")) {
            phone = root["phone"].get<std::string>();
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("[CClient] JSON parse failed while parsing JWT: {}", ex.what());
    }
    
    return {sub, phone};
}

// 获取当前时间戳（毫秒）
static uint64_t GetCurrentTimestamp() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// ============================================================================
// 新中台消息格式解析辅助函数
// ============================================================================

/**
 * 解析中台返回的三元素数组响应格式
 * 
 * 成功时格式（JSON数组）：
 * [
 *   { "status": "SESSION_OK", "jwt": "...", "API_KEY": "...", "vip_level": 1, "name": "..." },
 *   { ... }  // _node_user_on_hyperedge 整行
 *   { ... }  // auth.users 整行
 * ]
 * 
 * 失败时格式（JSON对象）：
 * { "status": "SESSION_INVALID" }
 * { "status": "JWT_REFRESH_FAILED" }
 * 
 * @param response 中台响应字符串
 * @param sessionSummary 输出：会话摘要（数组第一个元素）
 * @param nodeUserRow 输出：_node_user_on_hyperedge 行数据
 * @param authUserRow 输出：auth.users 行数据
 * @return true 如果是数组格式，false 如果是对象格式（失败）
 */
static bool ParseMiddleResponse(const std::string& response, 
                                nlohmann::json& sessionSummary,
                                nlohmann::json& nodeUserRow,
                                nlohmann::json& authUserRow) {
    try {
        using nlohmann::json;
        json root = json::parse(response);
        
        // 检查是否为数组格式（成功情况）
        if (root.is_array() && root.size() >= 3) {
            sessionSummary = root[0];
            nodeUserRow = root[1];
            authUserRow = root[2];
            return true;
        }
        
        // 失败情况：单对象格式
        sessionSummary = root;
        nodeUserRow = json::object();
        authUserRow = json::object();
        return false;
    } catch (const std::exception& ex) {
        LOG_ERROR("[CClient] ParseMiddleResponse: JSON parse failed: {}", ex.what());
        return false;
    }
}

// Registry for command handlers (keys are MsgType)
static std::unordered_map<MsgType, CClient::CommandHandler> g_command_registry;
static std::mutex g_registry_mutex;

void CClient::service_loop(CClient* self) {
    if (!self) {
        LOG_ERROR("[CClient] service_loop: self is null");
        return;
    }

    if (!self->task_queue_) {
        LOG_ERROR("[CClient] service_loop: task_queue_ is null");
        return;
    }

    CNetwork_c* pNetwork = self->network_;
    if (!pNetwork) {
        LOG_ERROR("[CClient] service_loop: network is null");
        return;
    }

    LOG_INFO("[CClient] Service loop started");

    // Blocking pop: wake up via TaskQueue::stop().
    for (;;) {
        auto ioData = self->task_queue_->pop();
        if (!ioData) {
            break; // queue stopped + drained
        }

        MsgType msgType = static_cast<MsgType>(ioData->type);
        CClient::CommandHandler handler = CClient::lookup_command(msgType);
        if (handler) {
            handler(ioData);
        }
        else {
            LOG_WARN("[CClient] No handler registered for MsgType={}", static_cast<int>(msgType));
        }
    }

    LOG_INFO("[CClient] Service loop exiting");
}

void CClient::Init(CNetwork_c& network) {
    network_ = &network;

    // Load server address from config file
    // NOTE: If the file is missing, ConfigClient keeps previous/default values.
    // Get executable directory
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    std::filesystem::path exeDir = exePath.parent_path();
    std::filesystem::path configPath = exeDir / L"client.conf";
    
    ConfigClient::instance().loadFromPath(configPath.string());

    server_ip_ = ConfigClient::instance().getHost();
    server_port_ = ConfigClient::instance().getPort();

    TLS_server_ip_ = ConfigClient::instance().getTlsHost();
    TLS_server_port_ = ConfigClient::instance().getTlsPort();
}

int CClient::start() {
    Logger::Instance().SetLevel(LogLevel::Info);
    Logger::Instance().AddSink(std::make_shared<ConsoleSink>());
    Logger::Instance().AddSink(std::make_shared<FileSink>("client.log"));

    // Validate before starting worker thread.
    if (network_ == nullptr) {
        LOG_ERROR("[CClient] Init(network) must be called before start()");
        return 1;
    }
    if (!task_queue_) {
        LOG_ERROR("[CClient] TaskQueue is null");
        return 1;
    }

    std::thread serviceThread(&CClient::service_loop, this);

    // Credentials must not be hardcoded in source. Read from config instead.
    const std::string password = ConfigClient::instance().getLoginPassword();
    if (password.empty()) {
        LOG_ERROR("[CClient] Missing login password; set login_password in client.conf");
        task_queue_->stop();
        serviceThread.join();
        return 1;
    }

    const std::string email = ConfigClient::instance().getLoginEmail();
    LOG_INFO("[CClient] Starting email/password login test (email={})", email);

    const std::string sessionToken = LoginWithPassword(email, password, "LOGIN");
    if (sessionToken.empty()) {
        LOG_ERROR("[CClient] Login failed");
        task_queue_->stop();
        serviceThread.join();
        return 1;
    }

    LOG_INFO("[CClient] Login succeeded (token redacted) logged_in={}", IsLoggedIn() ? "true" : "false");

    const std::string retrievedToken = GetSessionToken();
    if (retrievedToken != sessionToken) {
        LOG_ERROR("[CClient] Token retrieval verification failed");
        network_->Disconnect();
        task_queue_->stop();
        serviceThread.join();
        return 1;
    }

    LOG_INFO("[CClient] Token retrieval verification passed");

    network_->Disconnect();
    task_queue_->stop();
    serviceThread.join();
    return 0;
}

uint64_t CClient::GetSessionId() const noexcept {
    return session_id_.load();
}

void CClient::SetSessionId(uint64_t session_id) noexcept {
    session_id_.store(session_id);
}

void CClient::SetServerAddress(const std::string& ip, int port) {
    server_ip_ = ip;
    server_port_ = port;
}

std::string CClient::LoginWithPassword(const std::string& password) {
    return LoginWithPassword("test@gmail.com", password, "LOGIN");
}

std::string CClient::LoginWithPassword(const std::string& email, const std::string& password, const std::string& businessType) {
    if (network_ == nullptr) {
        LOG_ERROR("[CClient] network is null; call Init(network) first");
        return "";
    }

    if (is_logged_in_) {
        session_token_.clear();
        is_logged_in_ = false;
    }

    if (!network_->IsConnected()) {
        if (!network_->Connect(TLS_server_ip_, TLS_server_port_)) {
            LOG_ERROR("[CClient] Connect failed");
            return "";
        }
    }

    std::ostringstream jsonStream;
    jsonStream << "{\"business\":\"" << businessType
               << "\",\"email\":\"" << email
               << "\",\"passwd\":\"" << password << "\"}";
    const std::string payload = jsonStream.str();

    LOG_INFO("[CClient] Sending login request email={} businessType={} payload_bytes={}",
             email, businessType, payload.size());

    const std::string response = network_->SendRequest(
        static_cast<uint16_t>(MsgType::AuthRequest),
        payload);

    if (response.empty()) {
        LOG_WARN("[CClient] Login request failed: empty response");
        return "";
    }

    LOG_INFO("[CClient] Login response received bytes={}", response.size());

    if (response == "error" || response.find("error") != std::string::npos) {
        LOG_ERROR("[CClient] Server returned error (bytes={})", response.size());
        return "";
    }

    session_token_ = ExtractSessionToken(response);
    if (session_token_.empty()) {
        LOG_ERROR("[CClient] No session token found in response (bytes={})", response.size());
        return "";
    }

    is_logged_in_ = true;
    LOG_INFO("[CClient] Login succeeded");

    // 设置token过期时间（55分钟，在1小时有效期前5分钟判定为无效）
    token_expiry_time_ = GetCurrentTimestamp() + (55 * 60 * 1000);
    LOG_INFO("[CClient] Token expiry time set to {} ms from now", token_expiry_time_ - GetCurrentTimestamp());

    current_token_info_ = ParseTokenInfo(response);
    return session_token_;
}

bool CClient::SendSmsCode(const std::string& phoneNumber) {
    if (network_ == nullptr) {
        LOG_ERROR("[CClient] network is null; call Init(network) first");
        return false;
    }

    if (!network_->IsConnected()) {
        if (!network_->Connect(TLS_server_ip_, TLS_server_port_)) {
            LOG_ERROR("[CClient] Connect failed");
            return false;
        }
    }

    std::ostringstream jsonStream;
    jsonStream << "{\"business\":\"PHONE_OTP_SEND\",\"phone\":\"" << phoneNumber << "\"}";
    const std::string payload = jsonStream.str();

    LOG_INFO("[CClient] Sending SMS code request phone={} payload_bytes={}",
             phoneNumber, payload.size());

    const std::string response = network_->SendRequest(
        static_cast<uint16_t>(MsgType::OtpRequest),
        payload);

    if (response.empty()) {
        LOG_WARN("[CClient] Send SMS code request failed: empty response");
        return false;
    }

    LOG_INFO("[CClient] SMS code response received bytes={}", response.size());

    if (response == "error" || response.find("error") != std::string::npos) {
        LOG_ERROR("[CClient] Server returned error (bytes={})", response.size());
        return false;
    }

    // Check if response is "ok" (新格式) 或包含 "access_token":"ok" (旧格式)
    if (response == "ok" || (response.find("access_token") != std::string::npos && response.find("ok") != std::string::npos)) {
        LOG_INFO("[CClient] SMS code sent successfully");
        return true;
    }

    LOG_ERROR("[CClient] Invalid response format");
    return false;
}

std::string CClient::LoginWithSms(const std::string& phoneNumber, const std::string& code) {
    if (network_ == nullptr) {
        LOG_ERROR("[CClient] network is null; call Init(network) first");
        return "";
    }

    if (is_logged_in_) {
        session_token_.clear();
        is_logged_in_ = false;
    }

    // 每次登录都重新连接，确保会话是新的
    if (network_->IsConnected()) {
        network_->Disconnect();
    }

    if (!network_->Connect(TLS_server_ip_, TLS_server_port_)) {
        LOG_ERROR("[CClient] Connect failed");
        return "";
    }

    std::ostringstream jsonStream;
    jsonStream << "{\"business\":\"PHONE_OTP_VERIFY\",\"phone\":\"" << phoneNumber
               << "\",\"otp\":\"" << code << "\"}";
    const std::string payload = jsonStream.str();

    LOG_INFO("[CClient] Sending SMS login request phone={} payload_bytes={}",
             phoneNumber, payload.size());

    const std::string response = network_->SendRequest(
        static_cast<uint16_t>(MsgType::LoginSms),
        payload);

    if (response.empty()) {
        LOG_WARN("[CClient] SMS login request failed: empty response");
        return "";
    }

    LOG_INFO("[CClient] SMS login response received bytes={}", response.size());

    if (response == "error" || response.find("error") != std::string::npos) {
        LOG_ERROR("[CClient] Server returned error (bytes={})", response.size());
        return "";
    }

    // 解析响应，支持新的数组格式
    bool loginSuccess = false;
    try {
        using nlohmann::json;
        json root = json::parse(response);
        
        json sessionSummary;
        
        // 检查是否为新的数组格式（成功响应）
        if (root.is_array() && root.size() >= 3) {
            LOG_INFO("[CClient] LoginWithSms: detected array response format (new middleware format)");
            sessionSummary = root[0];
            
            // 验证码登录成功时 status 为 "success"，token 字段为 "access_token"
            std::string status;
            if (sessionSummary.contains("status")) {
                status = sessionSummary["status"].get<std::string>();
            }
            
            if (status == "success") {
                // 提取 token（支持 access_token 和 jwt）
                if (sessionSummary.contains("access_token")) {
                    session_token_ = sessionSummary["access_token"].get<std::string>();
                    LOG_INFO("[CClient] SMS login: token received from 'access_token' field");
                } else if (sessionSummary.contains("jwt")) {
                    session_token_ = sessionSummary["jwt"].get<std::string>();
                    LOG_INFO("[CClient] SMS login: token received from 'jwt' field");
                }
                
                if (!session_token_.empty()) {
                    loginSuccess = true;
                }
            }
        }
    } catch (const std::exception& ex) {
        LOG_DEBUG("[CClient] LoginWithSms: JSON parse failed, trying fallback: {}", ex.what());
    }
    
    // 如果新格式解析失败，尝试旧的解析方式
    if (!loginSuccess) {
        session_token_ = ExtractSessionToken(response);
        if (session_token_.empty()) {
            LOG_ERROR("[CClient] No session token found in response (bytes={})", response.size());
            return "";
        }
        loginSuccess = true;
    }

    is_logged_in_ = true;
    LOG_INFO("[CClient] SMS login succeeded");

    // 设置token过期时间（55分钟，在1小时有效期前5分钟判定为无效）
    token_expiry_time_ = GetCurrentTimestamp() + (55 * 60 * 1000);
    LOG_INFO("[CClient] Token expiry time set to {} ms from now", token_expiry_time_ - GetCurrentTimestamp());

    // Get session ID from network and save to file
    if (network_) {
        uint64_t session_id = network_->GetLastResponseSessionId();
        SetSessionId(session_id);
        LOG_INFO("[CClient] Session ID: {}", session_id);

        // Save session ID to file
        try {
            // Get executable directory
            wchar_t buffer[MAX_PATH];
            GetModuleFileNameW(NULL, buffer, MAX_PATH);
            std::filesystem::path exePath(buffer);
            std::filesystem::path exeDir = exePath.parent_path();
            std::filesystem::path sessionFilePath = exeDir / L"session_id.txt";
            
            std::ofstream session_file(sessionFilePath.wstring());
            if (session_file.is_open()) {
                session_file << session_id;
                session_file.close();
                LOG_INFO("[CClient] Session ID saved to {}", sessionFilePath.string());
            } else {
                LOG_ERROR("[CClient] Failed to open session_id.txt for writing");
            }
        } catch (const std::exception& ex) {
            LOG_ERROR("[CClient] Exception saving session ID: {}", ex.what());
        }
    }

    current_token_info_ = ParseTokenInfo(response);
    return session_token_;
}

std::string CClient::GetSessionToken() {
    // 检查token是否过期
    if (IsTokenExpired()) {
        // 尝试刷新token
        if (RefreshToken()) {
            LOG_INFO("[CClient] Token refreshed successfully");
        } else {
            LOG_ERROR("[CClient] Failed to refresh token");
        }
    }
    return session_token_;
}

bool CClient::IsLoggedIn() const {
    return is_logged_in_;
}

bool CClient::IsTokenExpired() const {
    // 如果未登录或token为空，视为已过期
    if (!is_logged_in_ || session_token_.empty()) {
        return true;
    }
    
    // 检查是否超过过期时间
    uint64_t current_time = GetCurrentTimestamp();
    return current_time > token_expiry_time_;
}

bool CClient::RefreshToken() {
    if (!network_) {
        LOG_ERROR("[CClient] network is null; call Init(network) first");
        return false;
    }
    
    uint64_t session_id = GetSessionId();
    if (session_id == 0) {
        LOG_ERROR("[CClient] No session_id available for token refresh");
        return false;
    }
    
    if (!network_->IsConnected()) {
        if (!network_->Connect(TLS_server_ip_, TLS_server_port_)) {
            LOG_ERROR("[CClient] Connect failed");
            return false;
        }
    }
    
    // Send Session_verify request to refresh token
    std::ostringstream payload_stream;
    payload_stream << session_id;
    const std::string payload = payload_stream.str();
    
    LOG_INFO("[CClient] Refreshing token with session_id: {}", session_id);
    
    const std::string response = network_->SendRequest(
        static_cast<uint16_t>(MsgType::Session_verify),
        payload);
    
    if (response.empty()) {
        LOG_WARN("[CClient] Token refresh request failed: empty response");
        return false;
    }
    
    LOG_INFO("[CClient] Token refresh response received: {}", response);
    
    // 解析响应
    try {
        // Try to parse as JSON first
        using nlohmann::json;
        json root = json::parse(response);
        
        json sessionSummary;
        
        // 检查是否为新的数组格式（成功响应）
        if (root.is_array() && root.size() >= 3) {
            LOG_INFO("[CClient] RefreshToken: detected array response format (new middleware format)");
            sessionSummary = root[0];
        } else {
            // 旧的对象格式
            sessionSummary = root;
        }
        
        std::string status;
        if (sessionSummary.contains("status")) {
            status = sessionSummary["status"].get<std::string>();
        }
        
        if (status == "SESSION_OK") {
            // Session is valid, update token if provided
            LOG_INFO("[CClient] Token refresh successful");
            
            // Get JWT token from response if available (支持 jwt 和 access_token)
            if (sessionSummary.contains("jwt")) {
                session_token_ = sessionSummary["jwt"].get<std::string>();
                LOG_INFO("[CClient] New JWT token received from 'jwt' field");
            } else if (sessionSummary.contains("access_token")) {
                session_token_ = sessionSummary["access_token"].get<std::string>();
                LOG_INFO("[CClient] New JWT token received from 'access_token' field");
            }
            
            // 更新过期时间（55分钟，在1小时有效期前5分钟判定为无效）
            token_expiry_time_ = GetCurrentTimestamp() + (55 * 60 * 1000);
            LOG_INFO("[CClient] Token expiry time updated to {} ms from now", token_expiry_time_ - GetCurrentTimestamp());
            
            return true;
        } else if (status == "SESSION_INVALID" || status == "SESSION_CONFLICT" || status == "JWT_REFRESH_FAILED") {
            // Session is invalid, conflict, or JWT refresh failed, need to re-login
            LOG_INFO("[CClient] Session {}: {}, need to re-login", 
                     status == "SESSION_INVALID" ? "invalid" : (status == "SESSION_CONFLICT" ? "conflict" : "JWT refresh failed"), status);
            is_logged_in_ = false;
            session_token_.clear();
            return false;
        } else {
            // Unknown status in JSON response
            LOG_ERROR("[CClient] Unknown status in JSON response: {}", status);
            return false;
        }
    } catch (const std::exception& ex) {
        // JSON parse failed, try to parse as plain text
        LOG_DEBUG("[CClient] JSON parse failed, trying plain text: {}", ex.what());
        
        if (response == "SESSION_OK") {
            // Session is valid
            LOG_INFO("[CClient] Token refresh successful (plain text)");
            
            // 更新过期时间（55分钟，在1小时有效期前5分钟判定为无效）
            token_expiry_time_ = GetCurrentTimestamp() + (55 * 60 * 1000);
            LOG_INFO("[CClient] Token expiry time updated to {} ms from now", token_expiry_time_ - GetCurrentTimestamp());
            
            return true;
        } else if (response == "SESSION_INVALID" || response == "SESSION_CONFLICT" || response == "JWT_REFRESH_FAILED") {
            // Session is invalid, conflict, or JWT refresh failed, need to re-login
            LOG_INFO("[CClient] Session {}: {}, need to re-login", 
                     response == "SESSION_INVALID" ? "invalid" : (response == "SESSION_CONFLICT" ? "conflict" : "JWT refresh failed"), response);
            is_logged_in_ = false;
            session_token_.clear();
            return false;
        } else {
            // Unknown response
            LOG_ERROR("[CClient] Unknown response: {}", response);
            return false;
        }
    }
}

std::string CClient::AutoLogin() {
    // Get executable directory
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    std::filesystem::path exeDir = exePath.parent_path();
    std::filesystem::path sessionFilePath = exeDir / L"session_id.txt";
    
    // Check if session_id.txt exists
    if (!std::filesystem::exists(sessionFilePath)) {
        LOG_INFO("[CClient] AutoLogin: session_id.txt not found at {}", sessionFilePath.string());
        return "";
    }

    // Read session_id from file
    uint64_t session_id = 0;
    try {
        std::ifstream session_file(sessionFilePath.wstring());
        if (!session_file.is_open()) {
            LOG_ERROR("[CClient] AutoLogin: Failed to open session_id.txt at {}", sessionFilePath.string());
            return "";
        }
        session_file >> session_id;
        session_file.close();
        LOG_INFO("[CClient] AutoLogin: Read session_id: {} from {}", session_id, sessionFilePath.string());
    } catch (const std::exception& ex) {
        LOG_ERROR("[CClient] AutoLogin: Exception reading session_id.txt: {}", ex.what());
        return "";
    }

    if (session_id == 0) {
        LOG_ERROR("[CClient] AutoLogin: Invalid session_id");
        return "";
    }

    // Connect to server if not connected
    if (!network_) {
        LOG_ERROR("[CClient] AutoLogin: network is null; call Init(network) first");
        return "";
    }

    if (!network_->IsConnected()) {
        if (!network_->Connect(TLS_server_ip_, TLS_server_port_)) {
            LOG_ERROR("[CClient] AutoLogin: Connect failed");
            return "";
        }
    }

    // Send Session_verify request
    std::ostringstream payload_stream;
    payload_stream << session_id;
    const std::string payload = payload_stream.str();

    LOG_INFO("[CClient] AutoLogin: Sending Session_verify request with session_id: {}", session_id);

    const std::string response = network_->SendRequest(
        static_cast<uint16_t>(MsgType::Session_verify),
        payload);

    if (response.empty()) {
        LOG_WARN("[CClient] AutoLogin: Session_verify request failed: empty response");
        return "";
    }

    LOG_INFO("[CClient] AutoLogin: Session_verify response received: {}", response);

    // Parse response
    try {
        // Try to parse as JSON first
        using nlohmann::json;
        json root = json::parse(response);
        
        json sessionSummary;
        json nodeUserRow;
        json authUserRow;
        
        // 检查是否为新的数组格式（成功响应）
        bool isArrayFormat = false;
        if (root.is_array() && root.size() >= 3) {
            LOG_INFO("[CClient] AutoLogin: detected array response format (new middleware format)");
            sessionSummary = root[0];
            nodeUserRow = root[1];
            authUserRow = root[2];
            isArrayFormat = true;
        } else {
            // 旧的对象格式
            sessionSummary = root;
        }
        
        std::string status;
        if (sessionSummary.contains("status")) {
            status = sessionSummary["status"].get<std::string>();
        }
        
        if (status == "SESSION_OK") {
            // Session is valid, login successful
            LOG_INFO("[CClient] AutoLogin: Session is valid");
            is_logged_in_ = true;
            SetSessionId(session_id);
            
            // Get JWT token from response (支持 jwt 和 access_token 两种字段名)
            if (sessionSummary.contains("jwt")) {
                session_token_ = sessionSummary["jwt"].get<std::string>();
                LOG_INFO("[CClient] AutoLogin: JWT token received from 'jwt' field");
            } else if (sessionSummary.contains("access_token")) {
                session_token_ = sessionSummary["access_token"].get<std::string>();
                LOG_INFO("[CClient] AutoLogin: JWT token received from 'access_token' field");
            } else {
                session_token_ = "";
                LOG_WARN("[CClient] AutoLogin: No JWT token in response");
            }
            
            // 设置token过期时间（55分钟，在1小时有效期前5分钟判定为无效）
            token_expiry_time_ = GetCurrentTimestamp() + (55 * 60 * 1000);
            LOG_INFO("[CClient] AutoLogin: Token expiry time set to {} ms from now", token_expiry_time_ - GetCurrentTimestamp());
            
            current_token_info_ = ParseTokenInfo(response);
            return "SESSION_OK";
        } else if (status == "SESSION_INVALID" || status == "SESSION_CONFLICT" || status == "JWT_REFRESH_FAILED") {
            // Session is invalid, conflict, or JWT refresh failed, need to login with SMS
            LOG_INFO("[CClient] AutoLogin: Session {}: {}", 
                     status == "SESSION_INVALID" ? "invalid" : (status == "SESSION_CONFLICT" ? "conflict" : "JWT refresh failed"), status);
            return status;
        } else {
            // Unknown status in JSON response
            LOG_ERROR("[CClient] AutoLogin: Unknown status in JSON response: {}", status);
            return "";
        }
    } catch (const std::exception& ex) {
        // JSON parse failed, try to parse as plain text
        LOG_DEBUG("[CClient] AutoLogin: JSON parse failed, trying plain text: {}", ex.what());
        
        if (response == "SESSION_OK") {
            // Session is valid, login successful
            LOG_INFO("[CClient] AutoLogin: Session is valid (plain text)");
            is_logged_in_ = true;
            SetSessionId(session_id);
            // For SESSION_OK, the token is not provided in the response
            // We'll use an empty token for now
            session_token_ = "";
            
            // 设置token过期时间（55分钟，在1小时有效期前5分钟判定为无效）
            token_expiry_time_ = GetCurrentTimestamp() + (55 * 60 * 1000);
            LOG_INFO("[CClient] AutoLogin: Token expiry time set to {} ms from now", token_expiry_time_ - GetCurrentTimestamp());
            
            current_token_info_ = ParseTokenInfo(response);
            return "SESSION_OK";
        } else if (response == "SESSION_INVALID" || response == "SESSION_CONFLICT" || response == "JWT_REFRESH_FAILED") {
            // Session is invalid, conflict, or JWT refresh failed, need to login with SMS
            LOG_INFO("[CClient] AutoLogin: Session {}: {}", 
                     response == "SESSION_INVALID" ? "invalid" : (response == "SESSION_CONFLICT" ? "conflict" : "JWT refresh failed"), response);
            return response;
        } else {
            // Unknown response
            LOG_ERROR("[CClient] AutoLogin: Unknown response: {}", response);
            return "";
        }
    }
}

std::string CClient::ExtractSessionToken(const std::string& jsonResponse) {
    using nlohmann::json;

    // 首先尝试直接返回响应作为 token，适应新的格式
    if (!jsonResponse.empty() && jsonResponse != "error" && jsonResponse != "ok") {
        // 检查是否包含 "error" 字符串
        if (jsonResponse.find("error") == std::string::npos) {
            // 直接返回响应作为 token
            LOG_INFO("[CClient] Using direct response as session token");
            return jsonResponse;
        }
    }

    // 如果响应是 "ok" 或 "error"，则尝试解析 JSON
    json root;
    try {
        root = json::parse(jsonResponse);
    } catch (const std::exception& ex) {
        LOG_ERROR("[CClient] JSON parse failed while extracting session token: {}", ex.what());
        return "";
    }

    auto tryGetString = [&](const char* key) -> std::string {
        auto it = root.find(key);
        if (it == root.end() || it->is_null()) {
            return "";
        }
        if (it->is_string()) {
            return it->get<std::string>();
        }

        // Some servers return numeric tokens or other scalars; normalize to string.
        if (it->is_number_integer()) {
            return std::to_string(it->get<int64_t>());
        }
        if (it->is_number_unsigned()) {
            return std::to_string(it->get<uint64_t>());
        }
        if (it->is_number_float()) {
            return std::to_string(it->get<double>());
        }
        if (it->is_boolean()) {
            return it->get<bool>() ? "true" : "false";
        }

        return "";
    };

    // Preserve prior behavior: accept multiple common key names.
    std::string token = tryGetString("sessionToken");
    if (token.empty()) token = tryGetString("session_token");
    if (token.empty()) token = tryGetString("token");
    if (token.empty()) token = tryGetString("access_token");

    return token;
}

CClient::TokenInfo CClient::ParseTokenInfo(const std::string& jsonResponse) {
    using nlohmann::json;

    TokenInfo info;

    json root;
    json sessionSummary;
    
    try {
        root = json::parse(jsonResponse);
    } catch (const std::exception& ex) {
        LOG_ERROR("[CClient] JSON parse failed while parsing token info: {}", ex.what());
        return info;
    }

    // 检查是否为新的数组格式（成功响应）
    if (root.is_array() && root.size() >= 3) {
        LOG_INFO("[CClient] ParseTokenInfo: detected array response format");
        sessionSummary = root[0];
    } else {
        // 旧的对象格式或失败响应
        sessionSummary = root;
    }

    // 提取token（支持不同字段名：jwt 或 access_token）
    if (sessionSummary.contains("jwt")) {
        info.token = sessionSummary["jwt"].get<std::string>();
        LOG_INFO("[CClient] ParseTokenInfo: extracted token from 'jwt' field");
    } else if (sessionSummary.contains("access_token")) {
        info.token = sessionSummary["access_token"].get<std::string>();
        LOG_INFO("[CClient] ParseTokenInfo: extracted token from 'access_token' field");
    } else {
        info.token = ExtractSessionToken(jsonResponse);
    }

    // 提取vip_level和name
    if (sessionSummary.contains("vip_level")) {
        if (sessionSummary["vip_level"].is_number()) {
            info.vip_level = sessionSummary["vip_level"].get<int>();
        } else if (sessionSummary["vip_level"].is_string()) {
            try {
                info.vip_level = std::stoi(sessionSummary["vip_level"].get<std::string>());
            } catch (...) {
                LOG_WARN("[CClient] ParseTokenInfo: failed to parse vip_level as integer");
            }
        }
    }
    if (sessionSummary.contains("name")) {
        info.name = sessionSummary["name"].get<std::string>();
    }

    // 从JWT中提取sub和phone
    if (!info.token.empty()) {
        auto [sub, phone] = parse_jwt(info.token);
        info.sub = sub;
        info.phone = phone;
    }

    // Standard OAuth-ish field.
    if (auto it = sessionSummary.find("expires_in"); it != sessionSummary.end() && !it->is_null()) {
        try {
            if (it->is_number_integer()) {
                info.expires_in = it->get<int64_t>();
            } else if (it->is_string()) {
                info.expires_in = std::stoll(it->get<std::string>());
            }
        } catch (...) {
            // Keep default if malformed.
        }
    }

    if (auto it = sessionSummary.find("refresh_token"); it != sessionSummary.end() && it->is_string()) {
        info.refresh_token = it->get<std::string>();
    }

    return info;
}

CClient::TokenInfo CClient::GetTokenInfo() const {
    return current_token_info_;
}

std::string CClient::GetDoctorAndPatientList() {
    if (network_ == nullptr) {
        LOG_ERROR("[CClient] network is null; call Init(network) first");
        return "";
    }

    if (!network_->IsConnected()) {
        if (!network_->Connect(TLS_server_ip_, TLS_server_port_)) {
            LOG_ERROR("[CClient] Connect failed");
            return "";
        }
    }

    // 发送获取医生和患者列表请求，payload为空
    // 使用107 (MedicalInfo) 请求类型，服务器会在108响应中返回两个列表
    LOG_INFO("[CClient] Sending request for doctor and patient list");
    const std::string response = network_->SendRequest(
        static_cast<uint16_t>(MsgType::MedicalInfo),
        "");

    if (response.empty()) {
        LOG_WARN("[CClient] Get doctor and patient list request failed: empty response");
        return "[]";
    }

    LOG_INFO("[CClient] Doctor and patient list response received bytes={}", response.size());

    if (response == "error" || response.find("error") != std::string::npos) {
        LOG_ERROR("[CClient] Server returned error (bytes={})");
        return "[]";
    }

    // 直接返回原始JSON响应，不做解析
    // 解析由web端完成
    return response;
}

void CClient::RegisterCommandHandler(MsgType type, CommandHandler handler) {
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    g_command_registry[type] = std::move(handler);
}

CClient::CommandHandler CClient::lookup_command(const MsgType type) {
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    auto it = g_command_registry.find(type);
    if (it == g_command_registry.end()) {
        // Handler not found
		return nullptr;
    }

    // Found handler
    return it->second;
}

void CClient::connectTCP() {
    if (network_ == nullptr) {
        LOG_ERROR("[CClient] connectTCP(): network is null; call Init(network) first");
        return;
	}
    if (!task_queue_) {
        LOG_ERROR("[CClient] connectTCP(): task_queue_ is null");
        return;
    }
	if (network_->IsConnected()) {
        LOG_INFO("[CClient] connectTCP(): Already connected to server");
        return;
    }

    auto task = CIoData_cFactory::Create(static_cast<uint8_t>(MsgType::ConnectTcp), 0 /*payload_len*/);
    if (!task) {
        LOG_ERROR("[CClient] connectTCP(): failed to allocate task");
        return;
    }

    if (!task_queue_->push(std::move(task))) {
        LOG_WARN("[CClient] connectTCP(): task queue stopped; task not enqueued");
    }
}

void CClient::connectTLS() {
    if (network_ == nullptr) {
        LOG_ERROR("[CClient] connectTLS(): network is null; call Init(network) first");
        return;
	}
    if (!task_queue_) {
        LOG_ERROR("[CClient] connectTLS(): task_queue_ is null");
        return;
    }
	if (network_->IsConnected()) {
        LOG_INFO("[CClient] connectTLS(): Already connected to server");
        return;
    }

    auto task = CIoData_cFactory::Create(static_cast<uint8_t>(MsgType::ConnectTls), 0 /*payload_len*/);
    if (!task) {
        LOG_ERROR("[CClient] connectTLS(): failed to allocate task");
        return;
    }

    if (!task_queue_->push(std::move(task))) {
        LOG_WARN("[CClient] connectTLS(): task queue stopped; task not enqueued");
    }
}

// --- ctor/dtor ---
CClient::CClient()
{
    try{
        task_queue_ = std::make_unique<TaskQueue>();
    }
    catch (const std::exception& ex) {
        LOG_ERROR("[CClient] TaskQueue creation failed: {}", ex.what());
    }
}

