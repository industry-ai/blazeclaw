#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>
#include "CNetwork_c.h"
#include "config_client.h"

enum class QRCodeStatus {
    Unknown = 0,
    Pending = 1,   // 待扫描
    Scanned = 2,   // 已扫描，待确认
    Bound = 3,      // 已绑定
    Expired = 4,    // 已过期
};

struct BindResult {
    bool success = false;
    int code = 0;
    QRCodeStatus status = QRCodeStatus::Unknown;
    std::string bind_token;
    std::string error_message;
    std::string chat_session_id;
    std::string conversation_id;
    std::string node_id;
    std::string device_id;
    std::string expiresAt;
    std::string qr_payload_url;
};

class QRCodeLoginService {
public:
    static QRCodeLoginService& Instance();

    // 初始化
    void Init(CNetwork_c* network);

    // 设置服务器地址
    void SetServerAddress(const std::string& ip, int port);

    // 创建绑定会话（生成二维码）
    bool CreateBind(const std::string& device_type,
                    const std::string& device_name,
                    const std::string& device_fingerprint);

    // 查询绑定状态（轮询）
    BindResult GetBindStatus(const std::string& bind_token);

    // 停止轮询
    void Stop();

    // 获取当前二维码内容
    std::string GetQrPayload() const { return m_qr_payload; }

    // 获取绑定 token
    std::string GetBindToken() const { return m_bind_token; }

    // 获取当前状态
    QRCodeStatus GetStatus() const { return m_status.load(); }

    // 设置状态回调
    void SetCallback(std::function<void(QRCodeStatus, const BindResult&)> callback) {
        m_callback = callback;
    }

    // 是否正在运行（轮询中）
    bool IsRunning() const { return m_running.load(); }

    // 检查二维码是否过期
    bool IsLocallyExpired() const;

private:
    QRCodeLoginService() = default;
    ~QRCodeLoginService() = default;
    QRCodeLoginService(const QRCodeLoginService&) = delete;
    QRCodeLoginService& operator=(const QRCodeLoginService&) = delete;

    // 发送 NodeBind 请求（使用网络单例）
    std::string SendNodeBindRequest(const std::string& payload);

    // 解析 NodeBind 响应
    BindResult ParseNodeBindResponse(const std::string& json_response);

    // 更新状态（通知回调）
    void UpdateStatus(QRCodeStatus status, const BindResult& result);

    // 检查本地过期时间
    time_t ParseIso8601Time(const std::string& timeStr);

    // 成员变量
    CNetwork_c* m_network = nullptr;
    std::string m_server_ip;
    int m_server_port = 0;
    std::string m_bind_token;
    std::string m_qr_payload;
    std::atomic<QRCodeStatus> m_status{QRCodeStatus::Unknown};
    std::atomic<bool> m_running{false};
    std::chrono::system_clock::time_point m_expires_at;  // 本地过期时间点
    std::mutex m_mutex;
    std::function<void(QRCodeStatus, const BindResult&)> m_callback;
};
