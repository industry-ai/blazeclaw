#include "pch.h"

#include <catch2/catch_all.hpp>

#include "app/chat-controller.h"
#include "gateway/GatewayProtocolModels.h"

#include <nlohmann/json.hpp>

using blazeclaw::app::chatcontroller::DispatchNativeChatControllerBridgeRequest;
using blazeclaw::app::chatcontroller::IsNativeChatControllerBridgeMethod;
using blazeclaw::gateway::protocol::RequestFrame;
using blazeclaw::gateway::protocol::ResponseFrame;

namespace {

	ResponseFrame Dispatch(
		const std::string& method,
		std::optional<std::string> paramsJson = std::nullopt)
	{
		return DispatchNativeChatControllerBridgeRequest(
			RequestFrame{
				.id = std::string("test-") + method,
				.method = method,
				.paramsJson = std::move(paramsJson),
			});
	}

	nlohmann::json ParsePayload(const ResponseFrame& response)
	{
		REQUIRE(response.ok);
		REQUIRE(response.payloadJson.has_value());
		const auto parsed = nlohmann::json::parse(response.payloadJson.value(), nullptr, false);
		REQUIRE(parsed.is_object());
		return parsed;
	}

	void ResetNativeControllerState()
	{
		auto response = Dispatch("chat.controller.reset", R"({})");
		REQUIRE(response.ok);
	}

} // namespace

TEST_CASE("native bridge whitelist includes Phase 1 handlers", "[chat-controller][native][unit]")
{
	CHECK(IsNativeChatControllerBridgeMethod("chat.controller.abort"));
	CHECK(IsNativeChatControllerBridgeMethod("chat.controller.loadHistory"));
	CHECK(IsNativeChatControllerBridgeMethod("chat.controller.getControlUiBootstrapConfig"));
	CHECK(IsNativeChatControllerBridgeMethod("chat.controller.getSpeechSessionStateSnapshot"));
	CHECK(IsNativeChatControllerBridgeMethod("chat.controller.assessTranscriptQuality"));
}

TEST_CASE("native dispatch abort returns lifecycle payload", "[chat-controller][native][integration]")
{
	ResetNativeControllerState();

	const auto response = Dispatch(
		"chat.controller.abort",
		R"({"runId":"prompt-run-42","sessionKey":"main"})");
	const auto payload = ParsePayload(response);

	REQUIRE(payload.contains("statePatch"));
	REQUIRE(payload["statePatch"].is_object());
	REQUIRE(payload["statePatch"].contains("chatSend"));
	CHECK(payload["statePatch"]["chatSend"].value("promptTerminalState", "") == "aborted");
	CHECK(payload.value("operation", "") == "abort");
	REQUIRE(payload.contains("uiOps"));
	REQUIRE(payload["uiOps"].is_array());
}

TEST_CASE("native dispatch loadHistory includes messages array", "[chat-controller][native][integration]")
{
	ResetNativeControllerState();

	const auto response = Dispatch(
		"chat.controller.loadHistory",
		R"({"sessionKey":"main","limit":200})");
	const auto payload = ParsePayload(response);

	REQUIRE(payload.contains("messages"));
	CHECK(payload["messages"].is_array());
	CHECK(payload.value("operation", "") == "loadHistory");
}

TEST_CASE("native dispatch bootstrap config carries controlUi shape", "[chat-controller][native][integration]")
{
	ResetNativeControllerState();

	const auto response = Dispatch(
		"chat.controller.getControlUiBootstrapConfig",
		R"({"basePath":"/chat","assistantName":"Assistant","assistantAvatar":"A","assistantAgentId":"agent-1"})");
	const auto payload = ParsePayload(response);

	REQUIRE(payload.contains("controlUi"));
	REQUIRE(payload["controlUi"].is_object());
	CHECK(payload["controlUi"].value("basePath", "") == "/chat");
	CHECK(payload["controlUi"].value("assistantName", "") == "Assistant");
	CHECK(payload["controlUi"].value("assistantAvatar", "") == "A");
	CHECK(payload["controlUi"].value("assistantAgentId", "") == "agent-1");
}

TEST_CASE("native dispatch speech session snapshot mirrors speech session", "[chat-controller][native][integration]")
{
	ResetNativeControllerState();

	const auto prime = Dispatch(
		"chat.controller.applySpeechLifecycleUpdate",
		R"({"payload":{"stage":"recording","runId":"speech-run-9","sessionId":"main"}})");
	REQUIRE(prime.ok);

	const auto update = Dispatch(
		"chat.controller.applySpeechLifecycleUpdate",
		R"({"payload":{"stage":"streaming","runId":"speech-run-9","sessionId":"main","text":"hello"}})");
	REQUIRE(update.ok);

	const auto response = Dispatch("chat.controller.getSpeechSessionStateSnapshot", R"({})");
	const auto payload = ParsePayload(response);

	REQUIRE(payload.contains("speechSession"));
	REQUIRE(payload["speechSession"].is_object());
	CHECK(payload["speechSession"].value("stage", "") == "streaming");
	CHECK(payload["speechSession"].value("runId", "") == "speech-run-9");
}

TEST_CASE("native dispatch transcript quality returns stable fields", "[chat-controller][native][integration]")
{
	ResetNativeControllerState();

	SECTION("empty transcript")
	{
		const auto response = Dispatch(
			"chat.controller.assessTranscriptQuality",
			R"({"text":""})");
		const auto payload = ParsePayload(response);

		CHECK(payload.value("accepted", true) == false);
		CHECK(payload.value("reason", "") == "empty transcript");
		REQUIRE(payload.contains("transcriptQuality"));
		REQUIRE(payload["transcriptQuality"].is_object());
		CHECK(payload["transcriptQuality"].value("accepted", true) == false);
	}

	SECTION("repetitive transcript")
	{
		const auto response = Dispatch(
			"chat.controller.assessTranscriptQuality",
			R"({"text":"aaaaaaaaaaaaaaaaaaaa"})");
		const auto payload = ParsePayload(response);

		CHECK(payload.value("accepted", true) == false);
		CHECK(payload.value("reason", "") == "repetitive transcript pattern detected");
		CHECK(payload.value("cleanedText", "").size() > 0);
	}
}
