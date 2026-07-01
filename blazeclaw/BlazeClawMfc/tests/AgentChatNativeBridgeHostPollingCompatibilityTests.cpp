#include "agentchat/AgentChatBridgeHost.h"
#include "agentchat/AgentChatOrchestratorAdapter.h"

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

namespace {
	using blazeclaw::agentchat::AgentChatBridgeConfig;
	using blazeclaw::agentchat::AgentChatBridgeHost;
	using blazeclaw::agentchat::CallbackAgentChatOrchestratorAdapter;
	using blazeclaw::gateway::protocol::RequestFrame;
	using blazeclaw::gateway::protocol::ResponseFrame;

	void ConfigureHostForTests(AgentChatBridgeHost& host) {
		AgentChatBridgeConfig config;
		config.enabled = true;
		config.enableHttpListener = false;
		config.enableUiInProcessAgentPath = false;
		config.enableHttpPushIngress = true;
		config.enablePushTransport = false;
		config.compatibilityOpenClawAliases = true;
		config.bindAddress = "127.0.0.1";
		config.port = 8788;
		REQUIRE(host.Initialize(config));
	}
}

TEST_CASE("Native agent stream accepts run_id and completed terminal state", "[agentchat][native][polling][compat]") {
	AgentChatBridgeHost host;
	ConfigureHostForTests(host);

	auto adapter = std::make_shared<CallbackAgentChatOrchestratorAdapter>();
	int pollCount = 0;
	adapter->SetRouter([&pollCount](const RequestFrame& request) -> ResponseFrame {
		if (request.method == "chat.send") {
			return ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = nlohmann::json{ { "run_id", "run-compat-1" } }.dump(),
				.error = std::nullopt,
			};
		}
		if (request.method == "chat.events.poll") {
			pollCount += 1;
			if (pollCount == 1) {
				return ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson = nlohmann::json{
						{ "events", nlohmann::json::array({
							nlohmann::json{
								{ "run_id", "run-compat-1" },
								{ "state", "completed" },
								{ "message", nlohmann::json{ { "text", "兼容模式完成" } } },
							},
						}) },
					}.dump(),
					.error = std::nullopt,
				};
			}
		}

		return ResponseFrame{
			.id = request.id,
			.ok = true,
			.payloadJson = nlohmann::json{ { "events", nlohmann::json::array() } }.dump(),
			.error = std::nullopt,
		};
	});
	host.SetOrchestratorAdapter(adapter);

	const auto response = host.HandleRequest(
		"POST",
		"/api/blazeclaw-agent",
		nlohmann::json{
			{ "sessionKey", "main" },
			{ "message", "tell a joke" },
			{ "stream", true },
		}.dump());

	REQUIRE(response.statusCode == 200);
	REQUIRE(response.contentType.find("text/event-stream") != std::string::npos);
	REQUIRE(response.body.find("\"type\":\"final\"") != std::string::npos);
	REQUIRE(response.body.find("兼容模式完成") != std::string::npos);
}
