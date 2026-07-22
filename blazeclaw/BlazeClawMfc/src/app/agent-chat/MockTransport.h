#pragma once

#include "CIrcChatTransport.h"
#include "ITransport.h"
#include "TransportConfig.h"

#include <atomic>
#include <string>
#include <vector>

namespace blazeclaw::irc {

// Recording mock ITransport for bridge/unit tests (no real network).
class MockTransport final : public ITransport {
public:
	std::atomic<int> initialize_calls{0};
	std::atomic<int> shutdown_calls{0};
	std::atomic<int> start_receivers_calls{0};
	std::atomic<int> stop_receivers_calls{0};
	std::atomic<int> send_privmsg_nowait_calls{0};
	std::atomic<int> send_join_nowait_calls{0};

	bool initialize_result = true;
	bool send_privmsg_nowait_result = true;

	std::vector<std::string> last_privmsg_channels;
	std::vector<std::string> last_privmsg_messages;

	IrcPushCallback push_callback;
	ConnectionStateCallback connection_state_callback;
	TransportConfig transport_config;
	CallbackExecutor callback_executor;
	Diagnostics diagnostics;

	bool Initialize() override {
		++initialize_calls;
		return initialize_result;
	}
	void Shutdown() override { ++shutdown_calls; }
	void StartReceivers() override { ++start_receivers_calls; }
	void StopReceivers() override { ++stop_receivers_calls; }

	bool SendIrcMessageTcp(const std::string&, std::string* = nullptr) override {
		return false;
	}
	bool SendIrcMessageTls(const std::string&, std::string* = nullptr) override {
		return false;
	}

	bool SendPrivmsg(const std::string&, const std::string&) override {
		return false;
	}
	bool SendPrivmsgNoWait(const std::string& channel, const std::string& message) override {
		++send_privmsg_nowait_calls;
		last_privmsg_channels.push_back(channel);
		last_privmsg_messages.push_back(message);
		return send_privmsg_nowait_result;
	}
	bool SendPrivmsgAsAgentNoWait(
		const std::string& channel,
		const std::string& message,
		const std::string& = "炎图AI助手") override {
		return SendPrivmsgNoWait(channel, message);
	}

	bool SendIrcCommandTcp(const std::string&, const std::string&, const std::string& = "") override {
		return false;
	}
	bool SendIrcCommandTcpNoWait(const std::string&, const std::string&, const std::string& = "") override {
		return true;
	}

	// Sync request helpers: intentionally no-op in mocks. Prefer NoWait APIs
	// from UI/bridge code — sync Send* may block up to socket recv timeout.
	std::string SendCommandTcp(const std::string&) override { return {}; }
	std::string SendCommandTls(const std::string&) override { return {}; }
	bool SendCommandNoWait(const std::string&) override { return true; }
	bool SendCommandTlsNoWait(const std::string&) override { return true; }

	bool SendJoin(const std::string&) override { return false; }
	bool SendPart(const std::string&, const std::string& = "") override { return false; }
	bool SendKick(const std::string&, const std::string&, const std::string& = "") override {
		return false;
	}
	bool SendMode(const std::string&, const std::string&) override { return false; }
	bool SendTopic(const std::string&, const std::string&) override { return false; }

	bool SendJoinNoWait(const std::string&) override {
		++send_join_nowait_calls;
		return true;
	}
	bool SendPartNoWait(const std::string&, const std::string& = "") override { return true; }
	bool SendKickNoWait(const std::string&, const std::string&, const std::string& = "") override {
		return true;
	}
	bool SendModeNoWait(const std::string&, const std::string&) override { return true; }
	bool SendTopicNoWait(const std::string&, const std::string&) override { return true; }

	void SetPushCallback(IrcPushCallback callback) override {
		push_callback = std::move(callback);
	}
	void SetConnectionStateCallback(ConnectionStateCallback callback) override {
		connection_state_callback = std::move(callback);
	}

	void SetTransportConfig(const TransportConfig& config) override {
		transport_config = config;
	}
	TransportConfig GetTransportConfig() const override { return transport_config; }
	void SetCallbackExecutor(CallbackExecutor executor) override {
		callback_executor = std::move(executor);
	}

	Diagnostics GetDiagnostics() const override { return diagnostics; }

	// Test helper: deliver a push as if the transport received it.
	void EmitPush(const IrcPushEvent& event) {
		if (push_callback) {
			push_callback(event);
		}
	}
};

// Non-owning shared_ptr wrapper around CIrcChatTransport::Instance() for the
// app composition root. Keeps Instance() out of leaf consumers.
inline std::shared_ptr<ITransport> WrapSingletonTransport() {
	return std::shared_ptr<ITransport>(
		&CIrcChatTransport::Instance(),
		[](ITransport*) {});
}

} // namespace blazeclaw::irc
