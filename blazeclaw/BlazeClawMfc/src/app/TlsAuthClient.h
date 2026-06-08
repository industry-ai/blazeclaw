#pragma once

#include <string>
#include <cstdint>

class CNetwork_c;

// TLS认证客户端封装类
// 现在作为薄封装：复用 CNetwork_c / CClient，避免重复实现 TLS/协议/解析。
class TlsAuthClient {
public:
    explicit TlsAuthClient(CNetwork_c& network);
    ~TlsAuthClient();

    TlsAuthClient(const TlsAuthClient&) = delete;
    TlsAuthClient& operator=(const TlsAuthClient&) = delete;
    TlsAuthClient(TlsAuthClient&&) = delete;
    TlsAuthClient& operator=(TlsAuthClient&&) = delete;

    // 登录方法：只需传入email和passwd
    bool Login(const std::string& email, const std::string& passwd);

    // 发送短信验证码
    bool SendSmsCode(const std::string& phoneNumber);

    // 短信验证码登录
    bool LoginWithSms(const std::string& phoneNumber, const std::string& code);

    bool IsLoggedIn() const;

    std::string GetEmail() const;
    std::string GetAccessToken() const;

    void Disconnect();

    // 发送业务请求（登录后使用）
    std::string SendRequest(uint16_t type, const std::string& payload);

private:
    CNetwork_c& network_;
    std::string email_;
    std::string access_token_;
    bool is_logged_in_{ false };
};

