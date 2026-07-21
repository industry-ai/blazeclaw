#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace blazeclaw::net {

    class INetworkClient {
    public:
        using PushCallback = std::function<void(const std::string& payload)>;
        using ConnectionStateCallback =
            std::function<void(bool is_tcp, bool is_connected)>;

        virtual ~INetworkClient() = default;

        virtual bool ConnectTcp(
            const std::string& ip = "",
            int port = 0) = 0;

        virtual bool ConnectTls(
            const std::string& ip = "",
            int port = 0) = 0;

        virtual void DisconnectTcp() = 0;
        virtual void DisconnectTls() = 0;
        virtual void DisconnectAll() = 0;

        virtual bool IsTcpConnected() const = 0;
        virtual bool IsTlsConnected() const = 0;

        virtual std::string SendRequestTcp(
            uint16_t type,
            const std::string& payload) = 0;

        virtual std::string SendRequestTls(
            uint16_t type,
            const std::string& payload) = 0;

        virtual bool SendTcpNoWait(
            uint16_t type,
            const std::string& payload) = 0;

        virtual bool SendTlsNoWait(
            uint16_t type,
            const std::string& payload) = 0;

        virtual bool SendTcpNoWaitWithSession(
            uint16_t type,
            const std::string& payload,
            uint64_t session_id) = 0;

        virtual bool StartPushReceivers() = 0;
        virtual void StopPushReceivers() = 0;

        virtual void SetTcpPushCallback(PushCallback callback) = 0;
        virtual void SetTlsPushCallback(PushCallback callback) = 0;
        virtual void SetConnectionStateCallback(
            ConnectionStateCallback callback) = 0;

        // Optional: apply app-layer heartbeat knobs to underlying connections.
        // Default no-op so fakes/mocks need not implement it.
        virtual void SetHeartbeatConfig(
            std::chrono::milliseconds /*interval*/,
            std::chrono::milliseconds /*step*/) {}
    };

} // namespace blazeclaw::net