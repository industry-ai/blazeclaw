#include "pch.h"

#include "../src/app/Logger.h"
#include "../src/app/agent-chat/NetworkClientAdapter.h"

namespace blazeclaw::net {

bool CNetworkClientAdapter::ConnectTcp(const std::string&, int) { return false; }
bool CNetworkClientAdapter::ConnectTls(const std::string&, int) { return false; }

void CNetworkClientAdapter::DisconnectTcp() {}
void CNetworkClientAdapter::DisconnectTls() {}
void CNetworkClientAdapter::DisconnectAll() {}

bool CNetworkClientAdapter::IsTcpConnected() const { return false; }
bool CNetworkClientAdapter::IsTlsConnected() const { return false; }

std::string CNetworkClientAdapter::SendRequestTcp(uint16_t, const std::string&) { return {}; }
std::string CNetworkClientAdapter::SendRequestTls(uint16_t, const std::string&) { return {}; }

bool CNetworkClientAdapter::SendTcpNoWait(uint16_t, const std::string&) { return false; }
bool CNetworkClientAdapter::SendTlsNoWait(uint16_t, const std::string&) { return false; }

bool CNetworkClientAdapter::SendTcpNoWaitWithSession(uint16_t, const std::string&, uint64_t) {
	return false;
}

bool CNetworkClientAdapter::StartPushReceivers() { return false; }
void CNetworkClientAdapter::StopPushReceivers() {}

void CNetworkClientAdapter::SetTcpPushCallback(PushCallback) {}
void CNetworkClientAdapter::SetTlsPushCallback(PushCallback) {}
void CNetworkClientAdapter::SetConnectionStateCallback(ConnectionStateCallback) {}
void CNetworkClientAdapter::SetHeartbeatConfig(std::chrono::milliseconds, std::chrono::milliseconds) {}

} // namespace blazeclaw::net

Logger& Logger::Instance() {
	static Logger logger;
	return logger;
}

void Logger::SetLevel(LogLevel level) {
	std::lock_guard<std::mutex> lock(mutex_);
	level_ = level;
}

LogLevel Logger::GetLevel() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return level_;
}

void Logger::AddSink(std::shared_ptr<ILogSink> sink) {
	std::lock_guard<std::mutex> lock(mutex_);
	sinks_.push_back(std::move(sink));
}

void Logger::ClearSinks() {
	std::lock_guard<std::mutex> lock(mutex_);
	sinks_.clear();
}

void Logger::SetThreadContext(uint64_t, uint64_t) {}
void Logger::ClearThreadContext() {}

void Logger::Log(LogLevel, const std::string&, const char*, int, const char*) {}

ScopeTimer::ScopeTimer(std::string name)
	: name_(std::move(name)), start_(std::chrono::steady_clock::now()) {}

ScopeTimer::~ScopeTimer() = default;
