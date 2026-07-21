#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "TransportConfig.h"

namespace blazeclaw::irc {

    struct IrcPushEvent; // Reuse existing type from CIrcChatTransport.h for now.

    class ITransport {
    public:
        using IrcPushCallback = std::function<void(const IrcPushEvent&)>;
        using ConnectionStateCallback =
            std::function<void(bool is_tcp, bool is_connected)>;

        struct Diagnostics {
            uint64_t messages_sent_tcp = 0;
            uint64_t messages_sent_tls = 0;
            uint64_t push_events = 0;
            bool tcp_connected = false;
            bool tls_connected = false;
        };

        virtual ~ITransport() = default;

        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual void StartReceivers() = 0;
        virtual void StopReceivers() = 0;

        virtual bool SendIrcMessageTcp(
            const std::string& payload,
            std::string* response = nullptr) = 0;

        virtual bool SendIrcMessageTls(
            const std::string& payload,
            std::string* response = nullptr) = 0;

        virtual bool SendPrivmsg(
            const std::string& channel,
            const std::string& message) = 0;

        virtual bool SendPrivmsgNoWait(
            const std::string& channel,
            const std::string& message) = 0;

        virtual bool SendPrivmsgAsAgentNoWait(
            const std::string& channel,
            const std::string& message,
            const std::string& from = "炎图AI助手") = 0;

        virtual bool SendIrcCommandTcp(
            const std::string& channel,
            const std::string& cmd,
            const std::string& message = "") = 0;

        virtual bool SendIrcCommandTcpNoWait(
            const std::string& channel,
            const std::string& cmd,
            const std::string& message = "") = 0;

        virtual std::string SendCommandTcp(const std::string& body) = 0;
        virtual std::string SendCommandTls(const std::string& body) = 0;

        virtual bool SendCommandNoWait(const std::string& body) = 0;
        virtual bool SendCommandTlsNoWait(const std::string& body) = 0;

        virtual bool SendJoin(const std::string& channel) = 0;
        virtual bool SendPart(
            const std::string& channel,
            const std::string& reason = "") = 0;

        virtual bool SendKick(
            const std::string& channel,
            const std::string& target,
            const std::string& reason = "") = 0;

        virtual bool SendMode(
            const std::string& channel,
            const std::string& mode) = 0;

        virtual bool SendTopic(
            const std::string& channel,
            const std::string& topic) = 0;

        virtual bool SendJoinNoWait(const std::string& channel) = 0;
        virtual bool SendPartNoWait(
            const std::string& channel,
            const std::string& reason = "") = 0;

        virtual bool SendKickNoWait(
            const std::string& channel,
            const std::string& target,
            const std::string& reason = "") = 0;

        virtual bool SendModeNoWait(
            const std::string& channel,
            const std::string& mode) = 0;

        virtual bool SendTopicNoWait(
            const std::string& channel,
            const std::string& topic) = 0;

        virtual void SetPushCallback(IrcPushCallback callback) = 0;
        virtual void SetConnectionStateCallback(
            ConnectionStateCallback callback) = 0;

        // Inject transport behavior (backoff/heartbeat/callback affinity).
        // Call before Initialize when overriding defaults or blazeclaw.conf.
        virtual void SetTransportConfig(const TransportConfig& config) = 0;
        virtual TransportConfig GetTransportConfig() const = 0;

        // Optional executor used when callbackDispatchMode == ExternalExecutor
        // (e.g. post to MFC UI/main thread). Ignored for other modes.
        virtual void SetCallbackExecutor(CallbackExecutor executor) = 0;

        virtual Diagnostics GetDiagnostics() const = 0;
    };

} // namespace blazeclaw::irc