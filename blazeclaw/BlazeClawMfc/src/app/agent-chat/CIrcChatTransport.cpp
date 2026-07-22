#include "pch.h"
#include "CIrcChatTransport.h"
#include "IrcMessageParser.h"
#include "NetworkClientAdapter.h"
#include "PushDispatcher.h"
#include "WorkerThread.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>

#include "../BlazeClawMfcApp.h"
#include "../Logger.h"
#include "../../config/ConfigModels.h"

#include <nlohmann/json.hpp>

namespace blazeclaw::irc {

namespace {

// 单调时钟，用于内部诊断（消息间隔、重试退避等）
uint64_t GetCurrentTimestampMs() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

// 墙钟 Unix 时间戳（秒）— 协议层发送的 ts 必须用 system_clock：
// 1783562987 这种格式是 Unix epoch (1970-01-01) 以来的秒数，
// 不能用 steady_clock（设备启动时间起算）。
int64_t GetCurrentUnixSeconds() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

TransportConfig TransportConfigFromRuntime(
    const blazeclaw::config::AgentChatRuntimeConfig& runtime) {
    TransportConfig cfg;
    if (runtime.transportInitialReconnectBackoffMs > 0) {
        cfg.initialReconnectBackoff =
            std::chrono::milliseconds{runtime.transportInitialReconnectBackoffMs};
    }
    if (runtime.transportMaxReconnectBackoffMs > 0) {
        cfg.maxReconnectBackoff =
            std::chrono::milliseconds{runtime.transportMaxReconnectBackoffMs};
    }
    if (cfg.maxReconnectBackoff < cfg.initialReconnectBackoff) {
        cfg.maxReconnectBackoff = cfg.initialReconnectBackoff;
    }
    if (runtime.transportHeartbeatIntervalMs > 0) {
        cfg.heartbeatInterval =
            std::chrono::milliseconds{runtime.transportHeartbeatIntervalMs};
    }
    if (runtime.transportHeartbeatStepMs > 0) {
        cfg.heartbeatStep =
            std::chrono::milliseconds{runtime.transportHeartbeatStepMs};
    }
    cfg.callbackDispatchMode =
        TransportConfig::ParseDispatchMode(runtime.transportCallbackDispatchMode);
    return cfg;
}

} // anonymous namespace

// CIrcChatTransport implementation

CIrcChatTransport::CIrcChatTransport()
    : CIrcChatTransport(std::make_shared<blazeclaw::net::CNetworkClientAdapter>()) {
}

CIrcChatTransport::CIrcChatTransport(
    std::shared_ptr<blazeclaw::net::INetworkClient> network_client)
    : CIrcChatTransport(std::move(network_client), TransportConfig{}) {
}

CIrcChatTransport::CIrcChatTransport(
    std::shared_ptr<blazeclaw::net::INetworkClient> network_client,
    TransportConfig config)
    : reconnect_backoff_(config.initialReconnectBackoff),
      network_client_(std::move(network_client)),
      push_dispatcher_(std::make_unique<PushDispatcher>()),
      transport_config_(std::move(config)),
      // Default-constructed TransportConfig is not an explicit override; app conf
      // may still refresh on Initialize. Call SetTransportConfig to force override.
      transport_config_overridden_(false) {
    if (!network_client_) {
        network_client_ = std::make_shared<blazeclaw::net::CNetworkClientAdapter>();
    }
}

CIrcChatTransport::~CIrcChatTransport() noexcept {
    try {
        Shutdown();
    } catch (...) {
        // Destructors must not throw.
    }
}

bool CIrcChatTransport::EnsureCallbacksRegisteredLocked() {
    if (callbacks_registered_) {
        return true;
    }

    if (!network_client_) {
        LOG_ERROR("[CIrcChatTransport] EnsureCallbacksRegisteredLocked: network client is null");
        return false;
    }

    TRACE(_T("[CIrcChatTransport] Initialize: setting up push callbacks...\n"));
    LOG_INFO("[CIrcChatTransport] Initialize: setting up TCP and TLS push callbacks");

    network_client_->SetConnectionStateCallback([this](bool is_tcp, bool is_connected) {
        // Reconnect scheduling must stay on the network callback thread so it
        // never waits on UI/dispatcher delivery.
        if (is_connected == false) {
            LOG_WARN("[CIrcChatTransport] Detected disconnect (is_tcp={}), scheduling auto-reconnect", is_tcp);
            ScheduleAutoReconnect(is_tcp);
        }

        DispatchCallback([this, is_tcp, is_connected]() {
            std::function<void(bool, bool)> cb_copy;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                cb_copy = connection_state_callback_;
            }
            if (!cb_copy) {
                LOG_WARN("[CIrcChatTransport] state changed (tcp={} connected={}) but no upper callback registered",
                         is_tcp, is_connected);
                return;
            }
            try {
                cb_copy(is_tcp, is_connected);
            } catch (const std::exception& e) {
                LOG_ERROR("[CIrcChatTransport] state callback exception: {}", e.what());
            } catch (...) {
                LOG_ERROR("[CIrcChatTransport] state callback unknown exception");
            }
        });
    });

    // Set up TCP push callback to parse and forward to registered callback
    network_client_->SetTcpPushCallback([this](const std::string& payload) {
        TRACE(_T("[CIrcChatTransport] === RAW TCP PUSH === size=%zu\n"), payload.size());
        if (payload.size() < 500) {
            std::string preview = payload.size() > 200 ? payload.substr(0, 200) + "..." : payload;
            TRACE(_T("[CIrcChatTransport] RAW payload: %s\n"), CA2T(preview.c_str()));
        }

        if (payload.empty()) return;

        auto event = ParseIrcMessage(payload);
        ++push_events_;

        // 过滤掉某些不需要转发的 push 类型
        // 例如：某些系统消息、ping/pong、心跳、在线状态通知
        if (event.type == IrcPushEventType::Unknown) {
            // Unknown 类型通常是服务端 JSON push（非标准 IRC 消息），
            // 检查是否是有效的业务数据，非业务数据直接丢弃
            try {
                auto parsed = nlohmann::json::parse(payload, nullptr, false);
                if (!parsed.is_discarded() && parsed.is_object()) {
                    // 按 event 字段过滤（服务端推送的事件通知）
                    std::string event_type = parsed.value("event", "");
                    static const std::set<std::string> filtered_events = {
                        "JOIN", "ONLINE", "OFFLINE", "LEAVE"
                    };
                    if (filtered_events.count(event_type) > 0) {
                        TRACE(_T("[CIrcChatTransport] Filtered event=%s\n"), CA2T(event_type.c_str()));
                        return;
                    }

                    // 按 push_type 字段过滤
                    std::string push_type = parsed.value("push_type", "");
                    static const std::set<std::string> filtered_push_types = {
                        "heartbeat", "keepalive", "system_notify"
                    };
                    if (filtered_push_types.count(push_type) > 0) {
                        TRACE(_T("[CIrcChatTransport] Filtered push_type=%s\n"), CA2T(push_type.c_str()));
                        return;
                    }
                }
            } catch (...) {
                // 解析 JSON 失败，忽略
            }
        }

        std::string type_str = event.type == IrcPushEventType::Privmsg ? "Privmsg" :
                                event.type == IrcPushEventType::Unknown ? "Unknown" : "other";
        TRACE(_T("[CIrcChatTransport] Parsed: type=%d(%s) channel=%s sender=%s\n"),
              static_cast<int>(event.type), CA2T(type_str.c_str()),
              CA2T(event.channel.c_str()), CA2T(event.sender_nick.c_str()));

        DispatchCallback([this, event]() {
            IrcPushCallback cb;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                cb = push_callback_;
            }

            if (!cb) {
                TRACE(_T("[CIrcChatTransport] TCP push: no user callback registered\n"));
                return;
            }

            TRACE(_T("[CIrcChatTransport] Invoking user callback...\n"));
            try {
                cb(event);
                TRACE(_T("[CIrcChatTransport] User callback completed\n"));
            } catch (const std::exception& e) {
                TRACE(_T("[CIrcChatTransport] TCP push callback exception: %s\n"), CA2T(e.what()));
            }
        });
    });

    // Set up TLS push callback to parse and forward to registered callback
    network_client_->SetTlsPushCallback([this](const std::string& payload) {
        if (!payload.empty()) {
            auto event = ParseIrcMessage(payload);
            ++push_events_;

            DispatchCallback([this, event]() {
                IrcPushCallback cb;
                {
                    std::lock_guard<std::mutex> lock(callbacks_mutex_);
                    cb = push_callback_;
                }

                if (!cb) {
                    return;
                }

                try {
                    cb(event);
                } catch (const std::exception& e) {
                    LOG_ERROR("[CIrcChatTransport] TLS push callback exception: {}", e.what());
                }
            });
        }
    });

    callbacks_registered_ = true;
    return true;
}

bool CIrcChatTransport::Initialize() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (initialized_.load()) {
        TRACE(_T("[CIrcChatTransport] Initialize: already initialized, skipping\n"));
        return true;
    }

    RefreshTransportConfigFromAppIfNeeded();
    ApplyTransportConfigLocked();

    if (!EnsureCallbacksRegisteredLocked()) {
        LOG_ERROR("[CIrcChatTransport] Initialize failed: callback registration failed");
        return false;
    }

    if (!push_dispatcher_) {
        push_dispatcher_ = std::make_unique<PushDispatcher>();
    }
    if (!push_dispatcher_->Start()) {
        LOG_ERROR("[CIrcChatTransport] Initialize failed: push dispatcher start failed");
        return false;
    }

    initialized_.store(true);
    TRACE(_T("[CIrcChatTransport] Initialize: DONE, initialized_=true\n"));
    return true;
}

void CIrcChatTransport::Shutdown() {
    bool should_stop_receivers = false;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (!initialized_.load() &&
            !receivers_started_ &&
            !reconnect_running_.load() &&
            !heartbeat_worker_.Joinable() &&
            !reconnect_worker_.Joinable()) {
            LOG_INFO("[CIrcChatTransport] Shutdown: already stopped, skipping");
            return;
        }

        should_stop_receivers = receivers_started_;
        receivers_started_ = false;
        initialized_.store(false);
    }

    if (should_stop_receivers && network_client_) {
        network_client_->StopPushReceivers();
    }

    // Ensure all owned background threads observe stop state before joins.
    heartbeat_running_.store(false);

    // 关键修复：先告诉 ReconnectLoop 退出，�?join�?
    // 否则 Shutdown �?join 一个还�?sleep_for / connect 的线程会永远卡�?
    // 由于 Shutdown 拿不�?lock 来原子地 "set false + join"，直接用 exchange�?
    //   - 如果 ReconnectLoop 没在跑（exchange 返回 false），无需 join�?
    //   - 如果 ReconnectLoop 在跑（exchange 返回 true），�?false 让它下一轮退出�?
    if (reconnect_running_.exchange(false)) {
        // ReconnectLoop 正在跑，告诉它退�?
        LOG_INFO("[CIrcChatTransport] Shutdown: signaling ReconnectLoop to exit");
    }
    if (reconnect_worker_.Joinable()) {
        reconnect_worker_.Join();
        LOG_INFO("[CIrcChatTransport] Shutdown: reconnect thread joined");
    }

    if (heartbeat_worker_.Joinable()) {
        heartbeat_worker_.Join();
        LOG_INFO("[CIrcChatTransport] Shutdown: heartbeat thread joined");
    }

    if (push_dispatcher_) {
        push_dispatcher_->Stop();
    }

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (network_client_) {
            network_client_->SetTcpPushCallback({});
            network_client_->SetTlsPushCallback({});
            network_client_->SetConnectionStateCallback({});
        }
        callbacks_registered_ = false;
    }

    LOG_INFO("[CIrcChatTransport] Shutdown complete");
}

void CIrcChatTransport::StartReceivers() {
    TRACE("[CIrcChatTransport] StartReceivers: called, initialized_={}", initialized_.load());
    if (!initialized_.load()) {
        TRACE("[CIrcChatTransport] StartReceivers: calling Initialize()");
        Initialize();
    }

    if (!initialized_.load()) {
        TRACE("[CIrcChatTransport] StartReceivers called but Initialize failed");
        return;
    }

    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (receivers_started_) {
        TRACE("[CIrcChatTransport] StartReceivers: already started, skipping");
        return;
    }

    TRACE("[CIrcChatTransport] StartReceivers: calling network.StartPushReceivers()");
    
    // 关键诊断：检查 push callback 是否已设置
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        TRACE("[CIrcChatTransport] StartReceivers: push_callback_ is {}set", 
                 push_callback_ ? "" : "NOT ");
    }
    
    if (!network_client_) {
        LOG_ERROR("[CIrcChatTransport] StartReceivers: network client is null");
        return;
    }
    network_client_->StartPushReceivers();
    receivers_started_ = true;
    LOG_INFO("[CIrcChatTransport] Push receivers started");
}

void CIrcChatTransport::StopReceivers() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (!receivers_started_) {
        LOG_INFO("[CIrcChatTransport] StopReceivers: already stopped, skipping");
        return;
    }

    if (!network_client_) {
        LOG_WARN("[CIrcChatTransport] StopReceivers: network client is null");
        receivers_started_ = false;
        return;
    }

    network_client_->StopPushReceivers();
    receivers_started_ = false;
    LOG_INFO("[CIrcChatTransport] Push receivers stopped");
}

void CIrcChatTransport::ScheduleAutoReconnect(bool is_tcp) {
    // 重入幂等：如果已经在重连线程里跑，本次调用直接返回，避免每帧断连多起一个线程�?
    if (reconnect_running_.exchange(true)) {
        LOG_INFO("[CIrcChatTransport] ScheduleAutoReconnect: another reconnect already running, skip");
        return;
    }
    // 关键修复：std::thread 的赋值运算符要求左值不�?joinable，否�?std::terminate() �?abort()�?
    // 上一�?reconnect_thread_ 如果�?joinable（ReconnectLoop 自然 return 但没�?join/detach），
    // 直接 reconnect_thread_ = std::thread(...) 会触�?abort�?
    // 这里在每次调度前 join 一下，确保 reconnect_thread_ 不是 joinable 状态�?
    reconnect_worker_.Join();
    if (!reconnect_worker_.Start([this, is_tcp]() { ReconnectLoop(is_tcp); })) {
        LOG_ERROR("[CIrcChatTransport] ScheduleAutoReconnect failed to spawn thread");
        reconnect_running_.store(false);
    }
}

void CIrcChatTransport::ReconnectLoop(bool is_tcp) {
    LOG_INFO("[CIrcChatTransport] ReconnectLoop started (is_tcp={})", is_tcp);
    if (!network_client_) {
        LOG_ERROR("[CIrcChatTransport] ReconnectLoop: network client is null");
        reconnect_running_.store(false);
        return;
    }

    TransportConfig cfg;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        cfg = transport_config_;
    }

    // 重置 backoff
    reconnect_backoff_ = cfg.initialReconnectBackoff;
    last_backoff_ms_.store(static_cast<uint64_t>(reconnect_backoff_.count()));

    int attempt = 0;
    while (reconnect_running_.load()) {
        attempt++;
        // 检查当前是否已经重连上了（其他路径可能已经成功）
        if (is_tcp && network_client_->IsTcpConnected()) {
            LOG_INFO("[CIrcChatTransport] ReconnectLoop: TCP already connected, exit");
            break;
        }
        if (!is_tcp && network_client_->IsTlsConnected()) {
            LOG_INFO("[CIrcChatTransport] ReconnectLoop: TLS already connected, exit");
            break;
        }

        // 检查 configured host/port 通过 ServerConfig
        bool ok = false;
        try {
            ++reconnect_attempts_;
            LOG_INFO(
                "[CIrcChatTransport] ReconnectLoop: attempt={} is_tcp={} backoff_ms={}",
                attempt,
                is_tcp,
                static_cast<long long>(reconnect_backoff_.count()));
            if (is_tcp) {
                ok = network_client_->ConnectTcp();
            } else {
                ok = network_client_->ConnectTls();
            }
        } catch (...) {
            ok = false;
        }

        if (ok) {
            ++reconnect_successes_;
            LOG_INFO("[CIrcChatTransport] ReconnectLoop: connect succeeded on attempt {}", attempt);
            // 必须重新挂上 push 回调 + 重启 PushReceiver（disconnect callback 不会再起）
            try {
                StartReceivers();
            } catch (...) {
                LOG_ERROR("[CIrcChatTransport] ReconnectLoop: StartReceivers threw");
            }
            reconnect_backoff_ = cfg.initialReconnectBackoff; // 重置
            last_backoff_ms_.store(static_cast<uint64_t>(reconnect_backoff_.count()));
            break;
        }

        ++reconnect_failures_;
        LOG_WARN(
            "[CIrcChatTransport] ReconnectLoop: attempt={} failed is_tcp={} next_wait_ms={}",
            attempt,
            is_tcp,
            static_cast<long long>(reconnect_backoff_.count()));

        // 退出请求（reconnect_running_=false）的快速响应：sleep_for 按 200ms 间隔分段，
        // 这样 Shutdown 触发时最多等 200ms 而不是 backoff（最长可到 max）。
        auto remaining_ms = reconnect_backoff_.count();
        while (remaining_ms > 0 && reconnect_running_.load()) {
            const int step_ms = static_cast<int>(std::min<long long>(remaining_ms, 200));
            std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
            remaining_ms -= step_ms;
        }
        if (!reconnect_running_.load()) {
            LOG_INFO("[CIrcChatTransport] ReconnectLoop: shutdown requested, exiting");
            break;
        }
        // 退避：双倍，直到上限
        auto next = reconnect_backoff_.count() * 2;
        if (next > cfg.maxReconnectBackoff.count()) {
            next = cfg.maxReconnectBackoff.count();
        }
        reconnect_backoff_ = std::chrono::milliseconds(next);
        last_backoff_ms_.store(static_cast<uint64_t>(reconnect_backoff_.count()));
    }

    LOG_INFO("[CIrcChatTransport] ReconnectLoop exiting (is_tcp={})", is_tcp);
    reconnect_running_.store(false);
}

bool CIrcChatTransport::SendIrcMessageTcp(const std::string& payload, std::string* response) {
    if (!network_client_ || !network_client_->IsTcpConnected()) {
        return false;
    }

    std::string resp = network_client_->SendRequestTcp(
        static_cast<uint16_t>(MsgType::IrcMessageReq), payload);
    
    ++messages_sent_tcp_;
    
    if (response) {
        *response = resp;
    }
    
    return !resp.empty();
}

bool CIrcChatTransport::SendIrcMessageTls(const std::string& payload, std::string* response) {
    if (!network_client_) {
        return false;
    }

    // Prefer TLS when connected, otherwise fall back to the existing TCP
    // connection (the chatroom is always brought up with at least the TCP
    // path active, even if TLS login didn't take). This keeps the chatroom
    // working without forcing the caller to know which transport is up.
    if (!network_client_->IsTlsConnected() && network_client_->IsTcpConnected()) {
        return SendIrcMessageTcp(payload, response);
    }

    if (!network_client_->IsTlsConnected()) {
        return false;
    }

    std::string resp = network_client_->SendRequestTls(
        static_cast<uint16_t>(MsgType::IrcMessageReq), payload);

    ++messages_sent_tls_;

    if (response) {
        *response = resp;
    }

    return !resp.empty();
}

bool CIrcChatTransport::SendPrivmsg(const std::string& channel, const std::string& message) {
    std::string payload = BuildPrivmsgPayload(channel, message);
    return SendIrcMessageTls(payload);
}

bool CIrcChatTransport::SendPrivmsgNoWait(const std::string& channel, const std::string& message) {
    std::string payload = BuildPrivmsgPayload(channel, message);
    // TLS 优先，TCP 兜底。与 fire-and-forget 的发送语义一致：不读响应�?
    if (!network_client_) {
        return false;
    }
   /* if (network_client_->IsTlsConnected()) {
        return network_client_->SendTlsNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), payload);
    }*/
    if (network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), payload);
    }
    return false;
}

bool CIrcChatTransport::SendPrivmsgAsAgentNoWait(const std::string& channel, const std::string& message,
                                                  const std::string& from) {
    // 对齐 AIAssistant: 使用 AGENT_BROADCAST 命令，服务端识别后以 session_id=0 广播给所有成员
    auto escapeJson = [](const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        out += buf;
                    } else {
                        out.push_back(c);
                    }
            }
        }
        return out;
    };
    std::ostringstream oss;
    oss << "{";
    oss << "\"cmd\":\"AGENT_BROADCAST\",";
    oss << "\"channel\":\"" << escapeJson(channel) << "\",";
    oss << "\"message\":\"" << escapeJson(message) << "\",";
    oss << "\"from\":\"" << escapeJson(from) << "\",";
    oss << "\"nick\":\"" << escapeJson(from) << "\",";
    oss << "\"ts\":" << GetCurrentUnixSeconds();
    oss << "}";
    const std::string payload = oss.str();

    if (network_client_ && network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWaitWithSession(
            static_cast<uint16_t>(MsgType::IrcMessageReq), payload, 0);
    }
    return false;
}

bool CIrcChatTransport::SendJoin(const std::string& channel) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"JOIN\",\"channel\":\"" << channel << "\"}";
    return SendIrcMessageTcp(oss.str());
}

bool CIrcChatTransport::SendPart(const std::string& channel, const std::string& reason) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"PART\",\"channel\":\"" << channel << "\"";
    if (!reason.empty()) {
        oss << ",\"reason\":\"" << reason << "\"";
    }
    oss << "}";
    return SendIrcMessageTcp(oss.str());
}

bool CIrcChatTransport::SendKick(const std::string& channel, const std::string& target,
                                 const std::string& reason) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"KICK\",\"channel\":\"" << channel << "\",\"target\":\"" << target << "\"";
    if (!reason.empty()) {
        oss << ",\"reason\":\"" << reason << "\"";
    }
    oss << "}";
    return SendIrcMessageTls(oss.str());
}

bool CIrcChatTransport::SendMode(const std::string& channel, const std::string& mode) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"MODE\",\"channel\":\"" << channel << "\",\"mode\":\"" << mode << "\"}";
    return SendIrcMessageTls(oss.str());
}

bool CIrcChatTransport::SendTopic(const std::string& channel, const std::string& topic) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"TOPIC\",\"channel\":\"" << channel << "\",\"topic\":\"" << topic << "\"}";
    return SendIrcMessageTls(oss.str());
}

bool CIrcChatTransport::SendJoinNoWait(const std::string& channel) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"JOIN\",\"channel\":\"" << channel << "\"}";
    if (network_client_ && network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), oss.str());
    }
    return false;
}

bool CIrcChatTransport::SendPartNoWait(const std::string& channel, const std::string& reason) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"PART\",\"channel\":\"" << channel << "\"";
    if (!reason.empty()) {
        oss << ",\"reason\":\"" << reason << "\"";
    }
    oss << "}";
    if (network_client_ && network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), oss.str());
    }
    return false;
}

bool CIrcChatTransport::SendKickNoWait(const std::string& channel, const std::string& target,
                                       const std::string& reason) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"KICK\",\"channel\":\"" << channel << "\",\"target\":\"" << target << "\"";
    if (!reason.empty()) {
        oss << ",\"reason\":\"" << reason << "\"";
    }
    oss << "}";
    if (!network_client_) {
        return false;
    }
    if (network_client_->IsTlsConnected()) {
        return network_client_->SendTlsNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), oss.str());
    }
    if (network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), oss.str());
    }
    return false;
}

bool CIrcChatTransport::SendModeNoWait(const std::string& channel, const std::string& mode) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"MODE\",\"channel\":\"" << channel << "\",\"mode\":\"" << mode << "\"}";
    if (!network_client_) {
        return false;
    }
    if (network_client_->IsTlsConnected()) {
        return network_client_->SendTlsNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), oss.str());
    }
    if (network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), oss.str());
    }
    return false;
}

bool CIrcChatTransport::SendTopicNoWait(const std::string& channel, const std::string& topic) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"TOPIC\",\"channel\":\"" << channel << "\",\"topic\":\"" << topic << "\"}";
    if (!network_client_) {
        return false;
    }
    if (network_client_->IsTlsConnected()) {
        return network_client_->SendTlsNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), oss.str());
    }
    if (network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), oss.str());
    }
    return false;
}

bool CIrcChatTransport::SendCommandNoWait(const std::string& body) {
    if (network_client_ && network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWait(static_cast<uint16_t>(MsgType::NodeBindReq), body);
    }
    return false;
}

bool CIrcChatTransport::SendCommandTlsNoWait(const std::string& body) {
    if (!network_client_) {
        return false;
    }
    if (network_client_->IsTlsConnected()) {
        return network_client_->SendTlsNoWait(static_cast<uint16_t>(MsgType::NodeBindReq), body);
    }
    if (network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWait(static_cast<uint16_t>(MsgType::NodeBindReq), body);
    }
    return false;
}

bool CIrcChatTransport::SendIrcCommandTcp(const std::string& channel, const std::string& cmd,
                                         const std::string& message) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"cmd\":\"" << cmd << "\",";
    oss << "\"channel\":\"" << channel << "\"";
    if (!message.empty()) {
        oss << ",\"message\":\"" << message << "\"";
    }
    oss << "}";
    return SendIrcMessageTcp(oss.str());
}

bool CIrcChatTransport::SendIrcCommandTcpNoWait(const std::string& channel, const std::string& cmd,
                                                 const std::string& message) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"cmd\":\"" << cmd << "\",";
    oss << "\"channel\":\"" << channel << "\"";
    if (!message.empty()) {
        oss << ",\"message\":\"" << message << "\"";
    }
    oss << "}";
    if (network_client_ && network_client_->IsTcpConnected()) {
        return network_client_->SendTcpNoWait(static_cast<uint16_t>(MsgType::IrcMessageReq), oss.str());
    }
    return false;
}

std::string CIrcChatTransport::SendCommandTcp(const std::string& body) {
    if (!network_client_ || !network_client_->IsTcpConnected()) {
        return "";
    }
    return network_client_->SendRequestTcp(
        static_cast<uint16_t>(MsgType::NodeBindReq), body);
}

std::string CIrcChatTransport::SendCommandTls(const std::string& body) {
    if (!network_client_) {
        return "";
    }

    // TLS 优先，TCP 兜底（保持原有策略）�?
    if (!network_client_->IsTlsConnected() && network_client_->IsTcpConnected()) {
        return SendCommandTcp(body);
    }
    if (!network_client_->IsTlsConnected()) {
        return "";
    }
    return network_client_->SendRequestTls(
        static_cast<uint16_t>(MsgType::NodeBindReq), body);
}


void CIrcChatTransport::SetPushCallback(IrcPushCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    push_callback_ = callback;
    LOG_INFO("[CIrcChatTransport] SetPushCallback: callback {}set, initialized_={}", 
             callback ? "" : "NOT ", initialized_.load());
}

void CIrcChatTransport::SetConnectionStateCallback(std::function<void(bool, bool)> callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    connection_state_callback_ = std::move(callback);
}

void CIrcChatTransport::SetTransportConfig(const TransportConfig& config) {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        transport_config_ = config;
        if (transport_config_.maxReconnectBackoff < transport_config_.initialReconnectBackoff) {
            transport_config_.maxReconnectBackoff = transport_config_.initialReconnectBackoff;
        }
        transport_config_overridden_ = true;
        reconnect_backoff_ = transport_config_.initialReconnectBackoff;
        last_backoff_ms_.store(static_cast<uint64_t>(reconnect_backoff_.count()));
        LOG_INFO(
            "[CIrcChatTransport] SetTransportConfig: backoff={}..{}ms heartbeat={}ms/{}ms mode={}",
            static_cast<long long>(transport_config_.initialReconnectBackoff.count()),
            static_cast<long long>(transport_config_.maxReconnectBackoff.count()),
            static_cast<long long>(transport_config_.heartbeatInterval.count()),
            static_cast<long long>(transport_config_.heartbeatStep.count()),
            TransportConfig::DispatchModeToString(transport_config_.callbackDispatchMode));
    }
    ApplyTransportConfigLocked();
}

TransportConfig CIrcChatTransport::GetTransportConfig() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return transport_config_;
}

void CIrcChatTransport::SetCallbackExecutor(CallbackExecutor executor) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    callback_executor_ = std::move(executor);
    LOG_INFO("[CIrcChatTransport] SetCallbackExecutor: executor {}",
             callback_executor_ ? "set" : "cleared");
}

void CIrcChatTransport::RefreshTransportConfigFromAppIfNeeded() {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        if (transport_config_overridden_) {
            return;
        }
    }

    try {
        auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
        if (app == nullptr) {
            return;
        }
        TransportConfig loaded = TransportConfigFromRuntime(app->Config().agentChatRuntime);
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            if (transport_config_overridden_) {
                return;
            }
            transport_config_ = std::move(loaded);
            reconnect_backoff_ = transport_config_.initialReconnectBackoff;
            last_backoff_ms_.store(static_cast<uint64_t>(reconnect_backoff_.count()));
            LOG_INFO(
                "[CIrcChatTransport] Loaded TransportConfig from blazeclaw.conf: "
                "backoff={}..{}ms heartbeat={}ms/{}ms mode={}",
                static_cast<long long>(transport_config_.initialReconnectBackoff.count()),
                static_cast<long long>(transport_config_.maxReconnectBackoff.count()),
                static_cast<long long>(transport_config_.heartbeatInterval.count()),
                static_cast<long long>(transport_config_.heartbeatStep.count()),
                TransportConfig::DispatchModeToString(transport_config_.callbackDispatchMode));
        }
    } catch (...) {
        LOG_WARN("[CIrcChatTransport] Failed to load TransportConfig from app; using defaults");
    }
}

void CIrcChatTransport::ApplyTransportConfigLocked() {
    TransportConfig cfg;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        cfg = transport_config_;
    }

    if (network_client_) {
        network_client_->SetHeartbeatConfig(cfg.heartbeatInterval, cfg.heartbeatStep);
    }
}

void CIrcChatTransport::DispatchCallback(std::function<void()> task) {
    if (!task) {
        return;
    }

    CallbackDispatchMode mode = CallbackDispatchMode::DispatcherWorker;
    CallbackExecutor executor;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        mode = transport_config_.callbackDispatchMode;
        executor = callback_executor_;
    }

    switch (mode) {
    case CallbackDispatchMode::CallerThread:
        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR("[CIrcChatTransport] CallerThread callback exception: {}", e.what());
        } catch (...) {
            LOG_ERROR("[CIrcChatTransport] CallerThread callback unknown exception");
        }
        return;

    case CallbackDispatchMode::ExternalExecutor:
        if (executor) {
            try {
                executor(std::move(task));
            } catch (const std::exception& e) {
                LOG_ERROR("[CIrcChatTransport] ExternalExecutor post exception: {}", e.what());
            } catch (...) {
                LOG_ERROR("[CIrcChatTransport] ExternalExecutor post unknown exception");
            }
            return;
        }
        LOG_WARN("[CIrcChatTransport] ExternalExecutor mode without executor; falling back to dispatcher");
        [[fallthrough]];

    case CallbackDispatchMode::DispatcherWorker:
    default:
        if (!push_dispatcher_) {
            LOG_ERROR("[CIrcChatTransport] DispatchCallback: dispatcher is null");
            return;
        }
        push_dispatcher_->Post(std::move(task));
        return;
    }
}

ITransport::Diagnostics CIrcChatTransport::GetDiagnostics() const {
    ITransport::Diagnostics diag;
    diag.messages_sent_tcp = messages_sent_tcp_.load();
    diag.messages_sent_tls = messages_sent_tls_.load();
    diag.push_events = push_events_.load();
    diag.reconnect_attempts = reconnect_attempts_.load();
    diag.reconnect_successes = reconnect_successes_.load();
    diag.reconnect_failures = reconnect_failures_.load();
    diag.last_backoff_ms = last_backoff_ms_.load();
    diag.tcp_connected = network_client_ && network_client_->IsTcpConnected();
    diag.tls_connected = network_client_ && network_client_->IsTlsConnected();
    return diag;
}

std::string CIrcChatTransport::BuildPrivmsgPayload(const std::string& channel,
                                                   const std::string& message) {
    //auto escapeJson = [](const std::string& s) {
    //    std::string out;
    //    out.reserve(s.size() + 8);
    //    for (char c : s) {
    //        switch (c) {
    //            case '"':  out += "\\\""; break;
    //            case '\\': out += "\\\\"; break;
    //            case '\n': out += "\\n";  break;
    //            case '\r': out += "\\r";  break;
    //            case '\t': out += "\\t";  break;
    //            default:
    //                if (static_cast<unsigned char>(c) < 0x20) {
    //                    char buf[8];
    //                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
    //                    out += buf;
    //                } else {
    //                    out.push_back(c);
    //                }
    //        }
    //    }
    //    return out;
    //};
    //std::ostringstream oss;
    //oss << "{";
    //oss << "\"cmd\":\"PRIVMSG\",";
    //oss << "\"channel\":\"" << escapeJson(channel) << "\",";
    //oss << "\"message\":\"" << escapeJson(message) << "\",";
    //// ts Unix epoch 秒（墙钟），不是设备启动后的秒数
    //// 对齐 chat-bridge.mjs：Math.floor(Date.now() / 1000)
    //oss << "\"ts\":" << GetCurrentUnixSeconds();
    //oss << "}";
    //return oss.str();
    return IrcMessageParser::BuildPrivmsgPayload(channel, message);
}

IrcPushEvent CIrcChatTransport::ParseIrcMessage(const std::string& payload) {
    IrcPushEvent event;
    event.timestamp_ms = GetCurrentTimestampMs();
    event.raw_line = payload;

    if (!payload.empty() && payload[0] == '{') {
      
        size_t json_end = payload.find_last_of('}');
        std::string clean_payload;
        if (json_end != std::string::npos) {
            clean_payload = payload.substr(0, json_end + 1);
        } else {
            clean_payload = payload;
        }

        try {
            auto parsed = nlohmann::json::parse(clean_payload, nullptr, false);
            if (!parsed.is_discarded() && parsed.is_object()) {
                std::string event_name = parsed.value("event", std::string());
                std::string cmd = parsed.value("cmd", std::string());
                std::string channel = parsed.value("channel", std::string());

                // ── 系统事件（ONLINE/OFFLINE 等）不需要转发给前端 UI ──
                auto equals_ignore_case = [](const std::string& a, const std::string& b) {
                    if (a.size() != b.size()) return false;
                    for (size_t i = 0; i < a.size(); ++i) {
                        if (tolower(static_cast<unsigned char>(a[i])) != 
                            tolower(static_cast<unsigned char>(b[i]))) {
                            return false;
                        }
                    }
                    return true;
                };
                
                if (equals_ignore_case(event_name, "JOIN") ||
                    equals_ignore_case(event_name, "ONLINE") || 
                    equals_ignore_case(event_name, "OFFLINE") ||
                    equals_ignore_case(event_name, "CONNECTED") || 
                    equals_ignore_case(event_name, "DISCONNECTED") ||
                    equals_ignore_case(event_name, "HEARTBEAT") || 
                    equals_ignore_case(event_name, "HEARTBEAT_ACK") ||
                    equals_ignore_case(event_name, "PING") || 
                    equals_ignore_case(event_name, "PONG") ||
                    equals_ignore_case(event_name, "ERROR") ||
                    equals_ignore_case(event_name, "SYSTEM")) {
                    event.type = IrcPushEventType::Unknown;
                    event.message = "system_event_filtered";
                    return event;
                }

                // ── 服务端命令回执过滤：status="ok" 表示服务端已成功处理命令（JOIN/PART/
                // CREATE_ROOM 等），不需要推给前端 UI，否则前端每次操作都会收到大量回执。
                // {"event":"JOIN","status":"ok"} / {"event":"ONLINE","status":"ok"} 类回执帧需要过滤。
                // 只过滤 status=ok 且有 event/cmd 字段（真正是服务端回执，而不是普通消息）。
                // 但 KICKED / GROUP_INVITED 虽然也带 status=ok，却是服务端主动推送的事件，需要保留。
                if (parsed.contains("status")) {
                    auto status_val = parsed["status"];
                    if (status_val.is_string()) {
                        std::string status_str = status_val.get<std::string>();
                        if (equals_ignore_case(status_str, "ok") &&
                            (!event_name.empty() || !cmd.empty())) {
                            // KICKED / GROUP_INVITED / PRIVMSG / AGENT_BROADCAST 是服务端主动推送的事件，豁免过滤
                            if (equals_ignore_case(event_name, "KICKED") ||
                                equals_ignore_case(event_name, "GROUP_INVITED") ||
                                equals_ignore_case(event_name, "PRIVMSG") ||
                                equals_ignore_case(cmd, "PRIVMSG") ||
                                equals_ignore_case(cmd, "AGENT_BROADCAST")) {
                                // 不过滤，继续往下解析
                            } else {
                                event.type = IrcPushEventType::Unknown;
                                event.message = "server_ack_filtered";
                                return event;
                            }
                        }
                    }
                }

                // ── 过滤没有 message 字段的事件（这类事件可能是状态通知，不是用户消息）──
                // ONLINE/OFFLINE 等系统事件通常没有 message 字段�?message 为空
                bool hasMessage = parsed.contains("message") && !parsed["message"].is_null();
                if (!hasMessage && event_name.empty() && cmd.empty()) {
                    // 没有 event_name、cmd、message，可能是系统状态通知
                    event.type = IrcPushEventType::Unknown;
                    event.message = "no_message_filtered";
                    return event;
                }

                // PRIVMSG 信封（chat-server 回包）
                if (equals_ignore_case(event_name, "PRIVMSG") ||
                    equals_ignore_case(cmd, "PRIVMSG") ||
                    !cmd.empty()) {
                    event.channel = channel;

                    if (parsed.contains("message")) {
                        const auto& msg = parsed["message"];
                        if (msg.is_string()) {
                            event.message = msg.get<std::string>();
                        } else if (msg.is_object()) {
                            event.message = msg.value("content",
                                msg.value("text", std::string()));
                            if (event.sender_nick.empty()) {
                                event.sender_nick = msg.value("author",
                                    msg.value("author_id", std::string()));
                            }
                            if (event.sender_nick.empty()) {
                                event.sender_nick = parsed.value("from", std::string());
                            }
                        }
                    }

                    if (event.message.empty()) {
                        event.message = parsed.value("message", std::string());
                    }

                    if (parsed.contains("ts")) {
                        const auto& ts = parsed["ts"];
                        if (ts.is_number_integer()) {
                            event.timestamp_ms = static_cast<uint64_t>(ts.get<int64_t>()) * 1000ull;
                        } else if (ts.is_number_unsigned()) {
                            event.timestamp_ms = ts.get<uint64_t>();
                        } else if (ts.is_number_float()) {
                            event.timestamp_ms = static_cast<uint64_t>(ts.get<double>() * 1000.0);
                        }
                    }

                    event.type = IrcPushEventType::Privmsg;
                    if (event.sender_nick.empty()) {
                        event.sender_nick = parsed.value("from",
                            parsed.value("from_user",
                            parsed.value("from_phone",
                            parsed.value("sender", std::string()))));
                    }
                    return event;
                }

                // ── 成员/状态事件（JSON 格式）──
                if (event_name == "JOIN" || event_name == "PART" ||
                    event_name == "KICK" || event_name == "QUIT" ||
                    event_name == "NICK" || event_name == "BAN") {
                    event.channel = channel;
                    event.sender_nick = parsed.value("from", std::string());
                    event.sender_user = parsed.value("user", std::string());
                    event.message = parsed.value("reason", std::string());

                    if (event_name == "JOIN")       event.type = IrcPushEventType::Join;
                    else if (event_name == "PART")  event.type = IrcPushEventType::Part;
                    else if (event_name == "KICK")  event.type = IrcPushEventType::Kick;
                    else if (event_name == "BAN")   event.type = IrcPushEventType::Ban;
                    else if (event_name == "QUIT")  event.type = IrcPushEventType::Quit;
                    else if (event_name == "NICK")  event.type = IrcPushEventType::Nick;

                    
                    if (parsed.contains("room_members")) {
                        const auto& rm = parsed["room_members"];
                        if (rm.is_number()) {
                            const std::string members = "room_members=" +
                                std::to_string(rm.get<int64_t>());
                            if (!event.message.empty()) event.message += "; ";
                            event.message += members;
                        }
                    }
                    return event;
                }

                // ── 主题/通知事件（JSON 格式）──
                // {"event":"TOPIC","channel":"#xxx","topic":"..."}
                // {"event":"NOTICE","channel":"#xxx","message":"..."}
                if (event_name == "TOPIC") {
                    event.channel = channel;
                    event.message = parsed.value("topic", std::string());
                    event.type = IrcPushEventType::Topic;
                    return event;
                }
                if (event_name == "NOTICE") {
                    event.channel = channel;
                    event.message = parsed.value("message", std::string());
                    event.type = IrcPushEventType::Notice;
                    return event;
                }

                // ── 群邀请事件（JSON 格式）──
                // {"event":"GROUP_INVITED","inviter":"uuid","inviter_phone":"17786625683",
                //  "room_id":"#group_xxx","room_name":"xxx","ts":"..."}
                if (event_name == "GROUP_INVITED") {
                    event.channel = parsed.value("room_id", std::string());
                    event.sender_nick = parsed.value("inviter", std::string());
                    event.sender_user = parsed.value("inviter_phone", std::string());
                    // 将群信息编码到 message 中供前端解析
                    nlohmann::json inv_info;
                    inv_info["room_id"] = event.channel;
                    inv_info["room_name"] = parsed.value("room_name", std::string());
                    inv_info["inviter"] = event.sender_nick;
                    inv_info["inviter_phone"] = event.sender_user;
                    inv_info["ts"] = parsed.value("ts", std::string());
                    event.message = inv_info.dump();
                    event.type = IrcPushEventType::GroupInvited;
                    return event;
                }

                // ── 被踢通知（服务端主动推送，被踢方收到）──
                // {"event":"KICKED","channel":"#group_xxx","user_id":"<uuid>","ts":1779966000000}
                // {"event":"KICKED","channel":"#group_xxx","node_id":11,"ts":1779966000000}
                if (equals_ignore_case(event_name, "KICKED")) {
                    event.channel = channel;
                    // user_id / node_id 互斥，填充专用字段
                    if (parsed.contains("user_id") && !parsed["user_id"].is_null()) {
                        event.kicked_user_id = parsed.value("user_id", std::string());
                    }
                    if (parsed.contains("node_id") && !parsed["node_id"].is_null()) {
                        const auto& nid = parsed["node_id"];
                        if (nid.is_number_integer()) {
                            event.kicked_node_id = static_cast<int>(nid.get<int64_t>());
                        }
                    }
                    if (parsed.contains("ts")) {
                        const auto& ts = parsed["ts"];
                        if (ts.is_number_integer()) {
                            event.timestamp_ms = static_cast<uint64_t>(ts.get<int64_t>());
                        } else if (ts.is_number_unsigned()) {
                            event.timestamp_ms = ts.get<uint64_t>();
                        } else if (ts.is_number_float()) {
                            event.timestamp_ms = static_cast<uint64_t>(ts.get<double>());
                        }
                    }
                    // 构造一条友好消息供前端展示
                    std::ostringstream oss;
                    oss << event.channel << " ";
                    if (!event.kicked_user_id.empty()) {
                        oss << "user:" << event.kicked_user_id;
                    } else if (event.kicked_node_id != 0) {
                        oss << "node:" << event.kicked_node_id;
                    }
                    event.message = oss.str();
                    event.type = IrcPushEventType::Kicked;
                    return event;
                }
            }
        } catch (...) {
            // 不是 JSON 或解析失败，落到下面 raw IRC line 解析
        }
    }

    // ── 2) Raw IRC line 格式（RFC 1459）──
    // Try to parse as PRIVMSG
    std::string nick, user, host, channel, message;
    if (IrcMessageParser::ParsePrivmsgLine(
            payload, nick, user, host, channel, message)) {
        event.type = IrcPushEventType::Privmsg;
        event.sender_nick = nick;
        event.sender_user = user;
        event.sender_host = host;
        event.channel = channel;
        event.message = message;
        return event;
    }

    // Try as JOIN
    if (payload.find("JOIN") != std::string::npos) {
        event.type = IrcPushEventType::Join;
        IrcMessageParser::ParseJoinPartLine(
            payload,
            event.sender_nick,
            event.sender_user,
            event.sender_host,
            event.channel);
        return event;
    }

    // Try as PART
    if (payload.find("PART") != std::string::npos) {
        event.type = IrcPushEventType::Part;
        IrcMessageParser::ParseJoinPartLine(
            payload,
            event.sender_nick,
            event.sender_user,
            event.sender_host,
            event.channel);
        return event;
    }

    // Try as RPL_NAMREPLY (353) �?形如:
    //   :server 353 yournick = #channel :nick1 @nick2 +nick3
    //   :server 353 yournick @ #channel :nick1
    //   :server 353 yournick * #channel :nick1 nick2
    //   message 部分是逗号/空格分隔的昵称列表，前缀 @ = operator, + = voiced
    // 我们把第一个前缀�?@ / + �?nick 放进 sender_nick，channel 放进 event.channel�?
    // 整个 names 列表（去除前缀符）作为 message�?
    if (payload.find(" 353 ") != std::string::npos && payload[0] == ':') {
        // 简化：先去掉前缀 :xxx （server�?
        size_t p1 = payload.find(' ');
        if (p1 != std::string::npos) {
            size_t p2 = payload.find(' ', p1 + 1);
            if (p2 != std::string::npos) {
                size_t p3 = payload.find(' ', p2 + 1);
                if (p3 != std::string::npos) {
                    // p3 之后�?channel，可能前面有 = / @ / *（visibility 标记�?
                    size_t chan_start = p3 + 1;
                    while (chan_start < payload.size() && payload[chan_start] == ' ') ++chan_start;
                    // 跳过 = / @ / *
                    if (chan_start < payload.size() &&
                        (payload[chan_start] == '=' || payload[chan_start] == '@' ||
                         payload[chan_start] == '*')) {
                        ++chan_start;
                    }
                    size_t chan_end = payload.find(' ', chan_start);
                    if (chan_end != std::string::npos) {
                        std::string chan = payload.substr(chan_start, chan_end - chan_start);
                        // �?: （trailing 参数�?
                        size_t colon = payload.find(" :", chan_end);
                        std::string names;
                        if (colon != std::string::npos) {
                            names = payload.substr(colon + 2);
                        } else if (chan_end + 1 < payload.size()) {
                            // 没有冒号，剩余部分都�?names
                            names = payload.substr(chan_end + 1);
                        }
                        // 提取第一个非�?nick 作为 sender_nick（去�?@/+ 前缀�?
                        std::string first_nick;
                        size_t i = 0;
                        while (i < names.size() && (names[i] == ' ' || names[i] == ',')) ++i;
                        while (i < names.size() && names[i] != ' ' && names[i] != ',') {
                            if (names[i] == '@' || names[i] == '+' || names[i] == '%') {
                                ++i;
                                continue;
                            }
                            first_nick.push_back(names[i]);
                            ++i;
                        }
                        event.type = IrcPushEventType::Names;
                        event.sender_nick = first_nick;
                        event.channel = chan;
                        event.message = names;
                        return event;
                    }
                }
            }
        }
    }

    // Try as RPL_ENDOFNAMES (366)
    if (payload.find(" 366 ") != std::string::npos && payload[0] == ':') {
        size_t p1 = payload.find(' ');
        if (p1 != std::string::npos) {
            size_t p2 = payload.find(' ', p1 + 1);
            if (p2 != std::string::npos) {
                size_t p3 = payload.find(' ', p2 + 1);
                if (p3 != std::string::npos) {
                    size_t chan_start = p3 + 1;
                    size_t chan_end = payload.find(' ', chan_start);
                    if (chan_end == std::string::npos) chan_end = payload.size();
                    event.type = IrcPushEventType::EndOfNames;
                    event.channel = payload.substr(chan_start, chan_end - chan_start);
                    event.message = "";
                    return event;
                }
            }
        }
    }

    // Try as NOTICE
    if (payload.find("NOTICE") != std::string::npos) {
        event.type = IrcPushEventType::Notice;
        IrcMessageParser::ParsePrivmsgLine(
            payload,
            event.sender_nick,
            event.sender_user,
            event.sender_host,
            event.channel,
            event.message);
        return event;
    }

    if (!payload.empty() && payload[0] == ':') {
        event.type = IrcPushEventType::Privmsg;
        event.message = payload;
        return event;
    }

    // 不是已知 IRC 形式 当作无法识别的服务端系统通知
    event.type = IrcPushEventType::Unknown;
    event.message = "unparseable_push_dropped";
    return event;
}

} // namespace blazeclaw::irc
