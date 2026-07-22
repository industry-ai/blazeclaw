#include "pch.h"

#include "../src/app/agent-chat/CIrcChatTransport.h"
#include "../src/app/agent-chat/FakeNetworkClient.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <thread>

namespace {

bool WaitUntil(
	const std::function<bool()>& condition,
	std::chrono::milliseconds timeout,
	std::chrono::milliseconds step = std::chrono::milliseconds(5)) {
	auto start = std::chrono::steady_clock::now();
	while (!condition()) {
		if (std::chrono::steady_clock::now() - start >= timeout) {
			return false;
		}
		std::this_thread::sleep_for(step);
	}
	return true;
}

} // namespace

TEST_CASE("CIrcChatTransport reconnect diagnostics and backoff counters update with FakeNetworkClient", "[agent-chat][transport][reconnect]") {
	auto fake = std::make_shared<blazeclaw::net::FakeNetworkClient>();
	fake->fail_tcp_connects_before_success = 2;

	blazeclaw::irc::TransportConfig cfg;
	cfg.initialReconnectBackoff = std::chrono::milliseconds(10);
	cfg.maxReconnectBackoff = std::chrono::milliseconds(30);
	cfg.callbackDispatchMode = blazeclaw::irc::CallbackDispatchMode::CallerThread;

	blazeclaw::irc::CIrcChatTransport transport(fake, cfg);
	REQUIRE(transport.Initialize());
	transport.StartReceivers();

	fake->SimulateDisconnect(true);

	REQUIRE(WaitUntil([&transport]() {
		return transport.GetDiagnostics().reconnect_successes >= 1;
	}, std::chrono::milliseconds(1200)));

	const auto diag = transport.GetDiagnostics();
	REQUIRE(diag.reconnect_attempts >= 3);
	REQUIRE(diag.reconnect_failures >= 2);
	REQUIRE(diag.reconnect_successes >= 1);
	REQUIRE(diag.last_backoff_ms >= 10);
	REQUIRE(fake->connect_tcp_calls.load() >= 3);
	REQUIRE(fake->start_receivers_calls.load() >= 2);

	transport.Shutdown();
}
