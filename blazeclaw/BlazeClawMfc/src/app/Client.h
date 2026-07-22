#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "AppProtoHeader.h"
#include "IoData_c.h"
#include "TaskQueue.h"

class CNetwork_c;

namespace blazeclaw::irc {
class ITransport;
}

class CClient {
public:
	// CommandHandler: handlers are dispatched based on MsgType from AppProtoHeader::type
	using CommandHandler = std::function<void(std::shared_ptr<IoData_c> d)>;

	// Register a command handler for a specific MsgType.
	static void RegisterCommandHandler(MsgType type, CommandHandler handler);

	// Lookup a command handler for a specific MsgType.
    static CClient::CommandHandler lookup_command(const MsgType type);

    // Thread entry (type-safe).
    static void service_loop(CClient* self);

public:
	void    connectTCP();
	void    connectTLS();

	std::unique_ptr<TaskQueue>  task_queue_;

    int start();

    static CClient& Instance() {
        static CClient instance;
        return instance;
    }

    // Must be called once before any network operations.
    void Init(CNetwork_c& network);

    // Optional transport injection for chat receiver lifecycle wiring.
    // Composition root should set this to avoid singleton usage in leaf code.
    void SetChatTransport(std::shared_ptr<blazeclaw::irc::ITransport> transport);
    blazeclaw::irc::ITransport* GetChatTransport() const noexcept;

    // 邮箱密码登录
    std::string LoginWithPassword(const std::string& password);

    // 邮箱密码登录（可指定邮箱和业务类型）
    std::string LoginWithPassword(const std::string& email, const std::string& password, const std::string& businessType = "LOGIN");

    // 发送短信验证码
    bool SendSmsCode(const std::string& phoneNumber);

    // 短信验证码登录
    std::string LoginWithSms(const std::string& phoneNumber, const std::string& code);

    // 获取当前 sessionToken（会自动刷新过期的token）
    std::string GetSessionToken();

    // 检查是否已登录
    bool IsLoggedIn() const;

    // 检查token是否过期
    bool IsTokenExpired() const;

    // 刷新token
    bool RefreshToken();

    // 设置服务器地址（可选）
    void SetServerAddress(const std::string& ip, int port);

    uint64_t GetSessionId() const noexcept;
    void SetSessionId(uint64_t session_id) noexcept;

    // 自动登录（使用保存的session_id）
    std::string AutoLogin();

protected:
    CClient();
    ~CClient() = default;

private:
    //friend int main(); // allow main.cpp handlers to access private connection settings

    CNetwork_c* network_{ nullptr }; // provided by main.cpp
    mutable std::mutex chat_transport_mutex_;
    std::shared_ptr<blazeclaw::irc::ITransport> chat_transport_;

    std::string server_ip_;     // TCP middleware server IP
    int server_port_{ 0 };      // TCP middleware server port

    std::string TLS_server_ip_; // TLS middleware server IP
    int TLS_server_port_{ 0 };  // TLS middleware server port

    std::string session_token_;      // current session token
    bool is_logged_in_{ false };     // login state
    uint64_t token_expiry_time_{ 0 }; // token过期时间（毫秒时间戳）

    std::atomic<uint64_t> session_id_{ 0 };

public:
    // Token信息结构体
    struct TokenInfo {
        std::string token;
        int64_t expires_in{ 0 };
        std::string refresh_token;
        std::string sub;
        std::string phone;
        int vip_level{ 0 };
        std::string name;
    };
    
    // 从JSON响应中提取sessionToken
    std::string ExtractSessionToken(const std::string& jsonResponse);
    TokenInfo ParseTokenInfo(const std::string& jsonResponse);
    
    // 获取当前token信息
    TokenInfo GetTokenInfo() const;

    // 获取医生和患者列表（发送107请求，服务器在108响应中返回两个列表）
    std::string GetDoctorAndPatientList();

private:
    TokenInfo current_token_info_;
};