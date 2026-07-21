#include "pch.h"
#include "NetworkClientAdapter.h"

#include "../CNetwork_c.h"

namespace blazeclaw::net {

    bool CNetworkClientAdapter::ConnectTcp(const std::string& ip, int port) {
        return CNetwork_c::Instance().ConnectTcp(ip, port);
    }

    bool CNetworkClientAdapter::ConnectTls(const std::string& ip, int port) {
        return CNetwork_c::Instance().ConnectTls(ip, port);
    }

    void CNetworkClientAdapter::DisconnectTcp() {
        CNetwork_c::Instance().DisconnectTcp();
    }

    void CNetworkClientAdapter::DisconnectTls() {
        CNetwork_c::Instance().DisconnectTls();
    }

    void CNetworkClientAdapter::DisconnectAll() {
        CNetwork_c::Instance().Disconnect();
    }

    bool CNetworkClientAdapter::IsTcpConnected() const {
        return CNetwork_c::Instance().IsTcpConnected();
    }

    bool CNetworkClientAdapter::IsTlsConnected() const {
        return CNetwork_c::Instance().IsTlsConnected();
    }

    std::string CNetworkClientAdapter::SendRequestTcp(
        uint16_t type,
        const std::string& payload) {
        return CNetwork_c::Instance().SendRequestTcp(type, payload);
    }

    std::string CNetworkClientAdapter::SendRequestTls(
        uint16_t type,
        const std::string& payload) {
        return CNetwork_c::Instance().SendRequestTls(type, payload);
    }

    bool CNetworkClientAdapter::SendTcpNoWait(
        uint16_t type,
        const std::string& payload) {
        return CNetwork_c::Instance().SendTcpNoWait(type, payload);
    }

    bool CNetworkClientAdapter::SendTlsNoWait(
        uint16_t type,
        const std::string& payload) {
        return CNetwork_c::Instance().SendTlsNoWait(type, payload);
    }

    bool CNetworkClientAdapter::SendTcpNoWaitWithSession(
        uint16_t type,
        const std::string& payload,
        uint64_t session_id) {
        return CNetwork_c::Instance().SendTcpNoWaitWithSession(
            type,
            payload,
            session_id);
    }

    bool CNetworkClientAdapter::StartPushReceivers() {
        return CNetwork_c::Instance().StartPushReceivers();
    }

    void CNetworkClientAdapter::StopPushReceivers() {
        CNetwork_c::Instance().StopPushReceivers();
    }

    void CNetworkClientAdapter::SetTcpPushCallback(PushCallback callback) {
        CNetwork_c::Instance().SetTcpPushCallback(std::move(callback));
    }

    void CNetworkClientAdapter::SetTlsPushCallback(PushCallback callback) {
        CNetwork_c::Instance().SetTlsPushCallback(std::move(callback));
    }

    void CNetworkClientAdapter::SetConnectionStateCallback(
        ConnectionStateCallback callback) {
        CNetwork_c::Instance().SetConnectionStateCallback(std::move(callback));
    }

    void CNetworkClientAdapter::SetHeartbeatConfig(
        std::chrono::milliseconds interval,
        std::chrono::milliseconds step) {
        CNetwork_c::Instance().SetHeartbeatConfig(interval, step);
    }

} // namespace blazeclaw::net