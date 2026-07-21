#pragma once

#include "INetworkClient.h"

namespace blazeclaw::net {

    class CNetworkClientAdapter final : public INetworkClient {
    public:
        CNetworkClientAdapter() = default;
        ~CNetworkClientAdapter() override = default;

        bool ConnectTcp(const std::string& ip = "", int port = 0) override;
        bool ConnectTls(const std::string& ip = "", int port = 0) override;

        void DisconnectTcp() override;
        void DisconnectTls() override;
        void DisconnectAll() override;

        bool IsTcpConnected() const override;
        bool IsTlsConnected() const override;

        std::string SendRequestTcp(uint16_t type, const std::string& payload) override;
        std::string SendRequestTls(uint16_t type, const std::string& payload) override;

        bool SendTcpNoWait(uint16_t type, const std::string& payload) override;
        bool SendTlsNoWait(uint16_t type, const std::string& payload) override;

        bool SendTcpNoWaitWithSession(
            uint16_t type,
            const std::string& payload,
            uint64_t session_id) override;

        bool StartPushReceivers() override;
        void StopPushReceivers() override;

        void SetTcpPushCallback(PushCallback callback) override;
        void SetTlsPushCallback(PushCallback callback) override;
        void SetConnectionStateCallback(ConnectionStateCallback callback) override;
        void SetHeartbeatConfig(
            std::chrono::milliseconds interval,
            std::chrono::milliseconds step) override;
    };

} // namespace blazeclaw::net