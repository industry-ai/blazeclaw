#include "agentchat/AgentChatBridgeHost.h"
#include "agentchat/AgentChatOrchestratorAdapter.h"

#include <chrono>
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

	nlohmann::json ParseBody(const std::string& body) {
		return nlohmann::json::parse(body);
	}
}

TEST_CASE("Native collaboration dispatch stores and returns current display", "[agentchat][native][collaboration]") {
	AgentChatBridgeHost host;
	ConfigureHostForTests(host);

	const nlohmann::json instruction = {
		{ "instruction", {
			{ "protocol", "agentchat.collaboration" },
			{ "version", 1 },
			{ "action", "device.open_content" },
			{ "conversationId", "room-1" },
			{ "dispatchId", "disp-1" },
			{ "dedupeKey", "disp-1" },
			{ "timestamp", static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count()) },
			{ "ttlMs", 30000 },
			{ "payload", {
				{ "title", "课程内容" },
				{ "contentType", "webview" },
				{ "url", "https://example.com/content/1" },
			} },
		} },
	};

	const auto dispatchResponse = host.HandleRequest(
		"POST",
		"/api/collaboration/dispatch",
		instruction.dump());
	REQUIRE(dispatchResponse.statusCode == 200);
	const auto dispatchBody = ParseBody(dispatchResponse.body);
	REQUIRE(dispatchBody.value("ok", false));
	REQUIRE(dispatchBody.value("status", std::string()) == "accepted");
	REQUIRE(dispatchBody.contains("currentDisplay"));

	const auto currentDisplayResponse = host.HandleRequest(
		"GET",
		"/api/collaboration/current-display/room-1",
		"{}");
	REQUIRE(currentDisplayResponse.statusCode == 200);
	const auto currentDisplayBody = ParseBody(currentDisplayResponse.body);
	REQUIRE(currentDisplayBody.value("ok", false));
	REQUIRE(currentDisplayBody.contains("currentDisplay"));
	REQUIRE(currentDisplayBody["currentDisplay"].value("title", std::string()) == "课程内容");
}

TEST_CASE("Native tts synthesize route uses orchestrator tts.convert", "[agentchat][native][tts]") {
	AgentChatBridgeHost host;
	ConfigureHostForTests(host);

	auto adapter = std::make_shared<CallbackAgentChatOrchestratorAdapter>();
	adapter->SetRouter([](const RequestFrame& request) -> ResponseFrame {
		REQUIRE(request.method == "tts.convert");
		const auto params = nlohmann::json::parse(request.paramsJson.value_or("{}"));
		REQUIRE(params.value("text", std::string()) == "你好，欢迎来到 BlazeClaw。");
		return ResponseFrame{
			.id = request.id,
			.ok = true,
			.payloadJson = nlohmann::json{
				{ "audioPath", "artifacts/tts/tts-1.wav" },
				{ "provider", "default" },
			}.dump(),
			.error = std::nullopt,
		};
	});
	host.SetOrchestratorAdapter(adapter);

	const auto response = host.HandleRequest(
		"POST",
		"/api/tts/synthesize",
		nlohmann::json{ { "text", "你好，欢迎来到 BlazeClaw。" } }.dump());
	REQUIRE(response.statusCode == 200);
	const auto body = ParseBody(response.body);
	REQUIRE(body.value("ok", false));
	REQUIRE(body.value("source", std::string()) == "native-tts");
	REQUIRE(body.value("text", std::string()) == "你好，欢迎来到 BlazeClaw。");
	REQUIRE(body.value("audioUrl", std::string()) == "artifacts/tts/tts-1.wav");
}

TEST_CASE("Native agent stream emits delta before final", "[agentchat][native][sse]") {
	AgentChatBridgeHost host;
	ConfigureHostForTests(host);

	auto adapter = std::make_shared<CallbackAgentChatOrchestratorAdapter>();
	int pollCount = 0;
	adapter->SetRouter([&pollCount](const RequestFrame& request) -> ResponseFrame {
		if (request.method == "chat.send") {
			return ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = nlohmann::json{ { "runId", "run-1" } }.dump(),
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
								{ "runId", "run-1" },
								{ "state", "delta" },
								{ "message", nlohmann::json{ { "text", "你好" } } },
							},
							nlohmann::json{
								{ "runId", "run-1" },
								{ "state", "final" },
								{ "message", nlohmann::json{ { "text", "你好，已完成" } } },
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
			{ "message", "你好" },
			{ "stream", true },
		}.dump());

	REQUIRE(response.statusCode == 200);
	REQUIRE(response.contentType.find("text/event-stream") != std::string::npos);
	const auto deltaIndex = response.body.find("\"type\":\"delta\"");
	const auto finalIndex = response.body.find("\"type\":\"final\"");
	REQUIRE(deltaIndex != std::string::npos);
	REQUIRE(finalIndex != std::string::npos);
	REQUIRE(deltaIndex < finalIndex);
}

TEST_CASE("Native reminder request builds cron.add payload contract", "[agentchat][native][reminder][cron]") {
	AgentChatBridgeHost host;
	ConfigureHostForTests(host);

	std::optional<nlohmann::json> capturedCronAddParams;
	auto adapter = std::make_shared<CallbackAgentChatOrchestratorAdapter>();
	adapter->SetRouter([&capturedCronAddParams](const RequestFrame& request) -> ResponseFrame {
		if (request.method == "cron.add") {
			capturedCronAddParams = nlohmann::json::parse(request.paramsJson.value_or("{}"), nullptr, false);
			return ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = nlohmann::json{ { "ok", true } }.dump(),
				.error = std::nullopt,
			};
		}

		return ResponseFrame{
			.id = request.id,
			.ok = false,
			.payloadJson = std::nullopt,
			.error = blazeclaw::gateway::protocol::ErrorShape{
				.code = "unsupported_method",
				.message = request.method,
				.detailsJson = std::nullopt,
				.retryable = false,
				.retryAfterMs = std::nullopt,
			},
		};
	});
	host.SetOrchestratorAdapter(adapter);

	const auto response = host.HandleRequest(
		"POST",
		"/api/blazeclaw-agent",
		nlohmann::json{
			{ "sessionKey", "main" },
			{ "message", "10分钟后提醒我喝水" },
			{ "conversationId", "#room-1" },
			{ "stream", false },
		}.dump());

	REQUIRE(response.statusCode == 200);
	const auto responseBody = ParseBody(response.body);
	REQUIRE(responseBody.value("ok", false));
	REQUIRE(responseBody.value("text", std::string()).find("已设置提醒") != std::string::npos);

	REQUIRE(capturedCronAddParams.has_value());
	const auto& cron = capturedCronAddParams.value();
	REQUIRE(cron.value("deleteAfterRun", false));
	REQUIRE(cron.value("sessionTarget", std::string()) == "isolated");
	REQUIRE(cron.value("wakeMode", std::string()) == "now");
	REQUIRE(cron.contains("schedule"));
	REQUIRE(cron["schedule"].value("kind", std::string()) == "at");
	REQUIRE(cron.contains("payload"));
	REQUIRE(cron["payload"].value("kind", std::string()) == "agentTurn");
	REQUIRE(cron["payload"].contains("toolsAllow"));
	REQUIRE(cron["payload"]["toolsAllow"].is_array());
	REQUIRE(cron["payload"]["toolsAllow"].size() == 1);
	REQUIRE(cron["payload"]["toolsAllow"][0].get<std::string>() == "exec");
}
