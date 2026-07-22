#pragma once

#include "INetworkClient.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace blazeclaw::net {

// In-memory fake for reconnect/backoff and callback tests. No real sockets.
class FakeNetworkClient final : public INetworkClient {
public:
	std::atomic<int> connect_tcp_calls{0};
	std::atomic<int> connect_tls_calls{0};
	std::atomic<int> start_receivers_calls{0};
	std::atomic<int> stop_receivers_calls{0};

	// Fail this many ConnectTcp attempts before succeeding (0 = always succeed).
	int fail_tcp_connects_before_success = 0;
	int fail_tls_connects_before_success = 0;

	std::chrono::milliseconds heartbeat_interval{};
	std::chrono::milliseconds heartbeat_step{};

	bool ConnectTcp(const std::string& /*ip*/ = "", int /*port*/ = 0) override {
		++connect_tcp_calls;
		if (connect_tcp_calls.load() <= fail_tcp_connects_before_success) {
			tcp_connected_ = false;
			return false;
		}
		tcp_connected_ = true;
		return true;
	}

	bool ConnectTls(const std::string& /*ip*/ = "", int /*port*/ = 0) override {
		++connect_tls_calls;
		if (connect_tls_calls.load() <= fail_tls_connects_before_success) {
			tls_connected_ = false;
			return false;
		}
		tls_connected_ = true;
		return true;
	}

	void DisconnectTcp() override { tcp_connected_ = false; }
	void DisconnectTls() override { tls_connected_ = false; }
	void DisconnectAll() override {
		tcp_connected_ = false;
		tls_connected_ = false;
	}

	bool IsTcpConnected() const override { return tcp_connected_; }
	bool IsTlsConnected() const override { return tls_connected_; }

	std::string SendRequestTcp(uint16_t /*type*/, const std::string& /*payload*/) override {
		return {};
	}
	std::string SendRequestTls(uint16_t /*type*/, const std::string& /*payload*/) override {
		return {};
	}

	bool SendTcpNoWait(uint16_t /*type*/, const std::string& /*payload*/) override {
		return tcp_connected_;
	}
	bool SendTlsNoWait(uint16_t /*type*/, const std::string& /*payload*/) override {
		return tls_connected_;
	}
	bool SendTcpNoWaitWithSession(
		uint16_t /*type*/,
		const std::string& /*payload*/,
		uint64_t /*session_id*/) override {
		return tcp_connected_;
	}

	bool StartPushReceivers() override {
		++start_receivers_calls;
		return true;
	}
	void StopPushReceivers() override { ++stop_receivers_calls; }

	void SetTcpPushCallback(PushCallback callback) override {
		std::lock_guard<std::mutex> lock(mutex_);
		tcp_push_callback_ = std::move(callback);
	}
	void SetTlsPushCallback(PushCallback callback) override {
		std::lock_guard<std::mutex> lock(mutex_);
		tls_push_callback_ = std::move(callback);
	}
	void SetConnectionStateCallback(ConnectionStateCallback callback) override {
		std::lock_guard<std::mutex> lock(mutex_);
		connection_state_callback_ = std::move(callback);
	}

	void SetHeartbeatConfig(
		std::chrono::milliseconds interval,
		std::chrono::milliseconds step) override {
		heartbeat_interval = interval;
		heartbeat_step = step;
	}

	// Test helpers: simulate network events.
	void SimulateDisconnect(bool is_tcp) {
		if (is_tcp) {
			tcp_connected_ = false;
		} else {
			tls_connected_ = false;
		}
		ConnectionStateCallback cb;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			cb = connection_state_callback_;
		}
		if (cb) {
			cb(is_tcp, false);
		}
	}

	void SimulateTcpPush(const std::string& payload) {
		PushCallback cb;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			cb = tcp_push_callback_;
		}
		if (cb) {
			cb(payload);
		}
	}

	void SetTcpConnected(bool connected) { tcp_connected_ = connected; }
	void SetTlsConnected(bool connected) { tls_connected_ = connected; }

private:
	mutable std::mutex mutex_;
	std::atomic<bool> tcp_connected_{false};
	std::atomic<bool> tls_connected_{false};
	PushCallback tcp_push_callback_;
	PushCallback tls_push_callback_;
	ConnectionStateCallback connection_state_callback_;
};

} // namespace blazeclaw::net
