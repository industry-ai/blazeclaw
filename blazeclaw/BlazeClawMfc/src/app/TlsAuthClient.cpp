#include "pch.h"
#include "TlsAuthClient.h"

#include "CNetwork_c.h"
#include "Client.h"
#include "Logger.h"

TlsAuthClient::TlsAuthClient(CNetwork_c& network)
    : network_(network) {
    // Ensure CClient is wired to the same network instance.
    CClient::Instance().Init(network_);
}

TlsAuthClient::~TlsAuthClient() {
    Disconnect();
}

bool TlsAuthClient::Login(const std::string& email, const std::string& passwd) {
    if (is_logged_in_) {
        Disconnect();
    }

    email_ = email;

    // Delegate login/token extraction to the canonical implementation.
    const std::string token = CClient::Instance().LoginWithPassword(email, passwd, "LOGIN");
    if (token.empty()) {
        LOG_ERROR("[TlsAuthClient] Login failed for email={}", email);
        email_.clear();
        access_token_.clear();
        is_logged_in_ = false;
        return false;
    }

    access_token_ = token;
    is_logged_in_ = true;

    LOG_INFO("[TlsAuthClient] Login succeeded for email={} (token redacted)", email);
    return true;
}

bool TlsAuthClient::SendSmsCode(const std::string& phoneNumber) {
    // Delegate to CClient implementation
    bool result = CClient::Instance().SendSmsCode(phoneNumber);
    if (result) {
        LOG_INFO("[TlsAuthClient] SMS code sent successfully for phone={}", phoneNumber);
    } else {
        LOG_ERROR("[TlsAuthClient] Failed to send SMS code for phone={}", phoneNumber);
    }
    return result;
}

bool TlsAuthClient::LoginWithSms(const std::string& phoneNumber, const std::string& code) {
    if (is_logged_in_) {
        Disconnect();
    }

    // Delegate to CClient implementation
    const std::string token = CClient::Instance().LoginWithSms(phoneNumber, code);
    if (token.empty()) {
        LOG_ERROR("[TlsAuthClient] SMS login failed for phone={}", phoneNumber);
        access_token_.clear();
        is_logged_in_ = false;
        return false;
    }

    access_token_ = token;
    is_logged_in_ = true;

    LOG_INFO("[TlsAuthClient] SMS login succeeded for phone={} (token redacted)", phoneNumber);
    return true;
}

bool TlsAuthClient::IsLoggedIn() const {
    return is_logged_in_;
}

std::string TlsAuthClient::GetEmail() const {
    return email_;
}

std::string TlsAuthClient::GetAccessToken() const {
    return access_token_;
}

void TlsAuthClient::Disconnect() {
    if (!is_logged_in_) {
        return;
    }

    network_.Disconnect();
    is_logged_in_ = false;
    email_.clear();
    access_token_.clear();

    LOG_INFO("[TlsAuthClient] Disconnected");
}

std::string TlsAuthClient::SendRequest(uint16_t type, const std::string& payload) {
    if (!is_logged_in_) {
        LOG_ERROR("[TlsAuthClient] Not logged in; cannot send request type={}", type);
        return "";
    }

    if (!network_.IsConnected()) {
        LOG_ERROR("[TlsAuthClient] Network not connected; cannot send request type={}", type);
        return "";
    }

    return network_.SendRequest(type, payload);
}

