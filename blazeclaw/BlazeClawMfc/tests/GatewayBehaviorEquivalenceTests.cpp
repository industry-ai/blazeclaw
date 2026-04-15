#include <catch2/catch_all.hpp>

#include "config/ConfigModels.h"
#include "gateway/GatewayHost.h"

#include <string>
#include <vector>

using namespace blazeclaw::gateway;

namespace {

	struct ChatScenarioResult {
		bool sendOk = false;
		std::string payload;
		std::vector<std::string> states;
	};

	std::vector<std::string> ExtractStates(const std::string& payloadJson)
	{
		std::vector<std::string> states;
		const std::string needle = "\"state\":\"";
		std::size_t cursor = 0;
		while (true) {
			const std::size_t begin = payloadJson.find(needle, cursor);
			if (begin == std::string::npos) {
				break;
			}

			const std::size_t valueBegin = begin + needle.size();
			const std::size_t valueEnd = payloadJson.find('"', valueBegin);
			if (valueEnd == std::string::npos) {
				break;
			}

			states.push_back(payloadJson.substr(valueBegin, valueEnd - valueBegin));
			cursor = valueEnd + 1;
		}

		return states;
	}

	void ConfigureHost(
		GatewayHost& host,
		const std::string& orchestrationPath)
	{
		blazeclaw::config::GatewayConfig gatewayConfig;
		REQUIRE(host.StartLocalOnly(gatewayConfig));
		host.SetEmbeddedOrchestrationPath(orchestrationPath);
		host.SetChatRuntimeCallback(
			[](const GatewayHost::ChatRuntimeRequest& request) {
				GatewayHost::ChatRuntimeResult result;
				result.ok = true;
				result.assistantText =
					"reply:" +
					(request.message.empty() ? std::string("empty") : request.message);
				result.assistantDeltas = {
					"tools.execute.start tool=weather.lookup",
					"delta:" + request.message,
				};
				result.taskDeltas = {
					GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
						.index = 0,
						.runId = request.runId,
						.sessionId = request.sessionKey,
						.phase = "final",
						.status = "completed",
						.stepLabel = "run_terminal",
					},
				};
				return result;
			});
	}

	ChatScenarioResult RunChatScenario(
		GatewayHost& host,
		const std::string& requestId,
		const std::string& paramsJson,
		const std::string& sessionKey)
	{
		const auto sendResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = requestId,
				.method = "chat.send",
				.paramsJson = paramsJson,
			});

		ChatScenarioResult result;
		result.sendOk = sendResponse.ok;
		if (sendResponse.payloadJson.has_value()) {
			result.payload = sendResponse.payloadJson.value();
		}

		for (int i = 0; i < 5; ++i) {
			const auto pollResponse = host.RouteRequest(
				blazeclaw::gateway::protocol::RequestFrame{
					.id = requestId + "-poll-" + std::to_string(i),
					.method = "chat.events.poll",
					.paramsJson =
						std::string("{\"sessionKey\":\"") +
						sessionKey +
						"\",\"limit\":20}",
				});
			REQUIRE(pollResponse.ok);
			if (!pollResponse.payloadJson.has_value()) {
				continue;
			}

			auto states = ExtractStates(pollResponse.payloadJson.value());
			result.states.insert(
				result.states.end(),
				states.begin(),
				states.end());
		}

		return result;
	}

} // namespace

TEST_CASE(
	"Parity behavior-equivalence: text, slash, attachment, abort, idempotency, reconnect scenarios",
	"[parity][behavior-equivalence]")
{
	GatewayHost stageHost;
	GatewayHost legacyHost;
	ConfigureHost(stageHost, "stage_pipeline_full");
	ConfigureHost(legacyHost, "legacy_only");

	SECTION("standard text Q&A (English + Chinese)")
	{
		const auto stageEn = RunChatScenario(
			stageHost,
			"parity-en-stage",
			"{\"sessionKey\":\"main\",\"message\":\"hello world\",\"idempotencyKey\":\"idem-en\"}",
			"main");
		const auto legacyEn = RunChatScenario(
			legacyHost,
			"parity-en-legacy",
			"{\"sessionKey\":\"main\",\"message\":\"hello world\",\"idempotencyKey\":\"idem-en\"}",
			"main");
		REQUIRE(stageEn.sendOk);
		REQUIRE(legacyEn.sendOk);
		REQUIRE_FALSE(stageEn.states.empty());
		REQUIRE_FALSE(legacyEn.states.empty());
		REQUIRE(stageEn.states.back() == legacyEn.states.back());

		const auto stageZh = RunChatScenario(
			stageHost,
			"parity-zh-stage",
			"{\"sessionKey\":\"main\",\"message\":\"\\u4f60\\u597d\\uff0c\\u8bf7\\u7b80\\u8981\\u4ecb\\u7ecd\\u4e00\\u4e0b\",\"idempotencyKey\":\"idem-zh\"}",
			"main");
		const auto legacyZh = RunChatScenario(
			legacyHost,
			"parity-zh-legacy",
			"{\"sessionKey\":\"main\",\"message\":\"\\u4f60\\u597d\\uff0c\\u8bf7\\u7b80\\u8981\\u4ecb\\u7ecd\\u4e00\\u4e0b\",\"idempotencyKey\":\"idem-zh\"}",
			"main");
		REQUIRE(stageZh.sendOk);
		REQUIRE(legacyZh.sendOk);
		REQUIRE_FALSE(stageZh.states.empty());
		REQUIRE_FALSE(legacyZh.states.empty());
		REQUIRE(stageZh.states.back() == legacyZh.states.back());
	}

	SECTION("/skill and custom command invocation")
	{
		const auto stageSkill = RunChatScenario(
			stageHost,
			"parity-skill-stage",
			"{\"sessionKey\":\"main\",\"message\":\"/skill weather tomorrow\",\"idempotencyKey\":\"idem-skill\"}",
			"main");
		const auto legacySkill = RunChatScenario(
			legacyHost,
			"parity-skill-legacy",
			"{\"sessionKey\":\"main\",\"message\":\"/skill weather tomorrow\",\"idempotencyKey\":\"idem-skill\"}",
			"main");
		REQUIRE(stageSkill.sendOk);
		REQUIRE(legacySkill.sendOk);
		REQUIRE(stageSkill.states.back() == legacySkill.states.back());
	}

	SECTION("attachment-only message")
	{
		const std::string attachmentParams =
			"{\"sessionKey\":\"main\",\"message\":\"\",\"idempotencyKey\":\"idem-attach\","
			"\"attachments\":[{\"type\":\"image\",\"mimeType\":\"image/png\",\"content\":\"x\"}]}";
		const auto stageAttachment = RunChatScenario(
			stageHost,
			"parity-attachment-stage",
			attachmentParams,
			"main");
		const auto legacyAttachment = RunChatScenario(
			legacyHost,
			"parity-attachment-legacy",
			attachmentParams,
			"main");
		REQUIRE(stageAttachment.sendOk);
		REQUIRE(legacyAttachment.sendOk);
		REQUIRE(stageAttachment.states.back() == legacyAttachment.states.back());
	}

	SECTION("stop/abort while run active")
	{
		const auto stageSend = stageHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-abort-stage",
				.method = "chat.send",
				.paramsJson =
					std::string("{\"sessionKey\":\"main\",\"message\":\"long run\"}"),
			});
		const auto legacySend = legacyHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-abort-legacy",
				.method = "chat.send",
				.paramsJson =
					std::string("{\"sessionKey\":\"main\",\"message\":\"long run\"}"),
			});
		REQUIRE(stageSend.ok);
		REQUIRE(legacySend.ok);

		const auto stageAbort = stageHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-abort-stage-rpc",
				.method = "chat.abort",
				.paramsJson =
					std::string("{\"sessionKey\":\"main\",\"runId\":\"parity-abort-stage\"}"),
			});
		const auto legacyAbort = legacyHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-abort-legacy-rpc",
				.method = "chat.abort",
				.paramsJson =
					std::string("{\"sessionKey\":\"main\",\"runId\":\"parity-abort-legacy\"}"),
			});
		REQUIRE(stageAbort.ok);
		REQUIRE(legacyAbort.ok);
	}

	SECTION("idempotency replay returns same payload")
	{
		const std::string replayParams =
			"{\"sessionKey\":\"main\",\"message\":\"idempotency replay\",\"idempotencyKey\":\"idem-replay-1\"}";
		const auto stageFirst = stageHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-replay-stage-1",
				.method = "chat.send",
				.paramsJson = replayParams,
			});
		const auto stageSecond = stageHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-replay-stage-2",
				.method = "chat.send",
				.paramsJson = replayParams,
			});
		const auto legacyFirst = legacyHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-replay-legacy-1",
				.method = "chat.send",
				.paramsJson = replayParams,
			});
		const auto legacySecond = legacyHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-replay-legacy-2",
				.method = "chat.send",
				.paramsJson = replayParams,
			});
		REQUIRE(stageFirst.ok);
		REQUIRE(stageSecond.ok);
		REQUIRE(legacyFirst.ok);
		REQUIRE(legacySecond.ok);
		REQUIRE(stageFirst.payloadJson == stageSecond.payloadJson);
		REQUIRE(legacyFirst.payloadJson == legacySecond.payloadJson);
	}

	SECTION("late-join reconnect path keeps active run event flow")
	{
		const auto stageFirst = stageHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-reconnect-run",
				.method = "chat.send",
				.paramsJson = std::string(
					"{\"sessionKey\":\"reconnect\",\"message\":\"run one\","
					"\"clientConnectionId\":\"conn-a\",\"hasConnectedClient\":true,"
					"\"clientCaps\":[\"TOOL_EVENTS\"]}"),
			});
		const auto stageSecond = stageHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-reconnect-run",
				.method = "chat.send",
				.paramsJson = std::string(
					"{\"sessionKey\":\"reconnect\",\"message\":\"run one\","
					"\"clientConnectionId\":\"conn-b\",\"hasConnectedClient\":true,"
					"\"clientCaps\":[\"TOOL_EVENTS\"]}"),
			});
		const auto legacyFirst = legacyHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-reconnect-run-legacy",
				.method = "chat.send",
				.paramsJson = std::string(
					"{\"sessionKey\":\"reconnect\",\"message\":\"run one\","
					"\"clientConnectionId\":\"conn-a\",\"hasConnectedClient\":true,"
					"\"clientCaps\":[\"TOOL_EVENTS\"]}"),
			});
		const auto legacySecond = legacyHost.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "parity-reconnect-run-legacy",
				.method = "chat.send",
				.paramsJson = std::string(
					"{\"sessionKey\":\"reconnect\",\"message\":\"run one\","
					"\"clientConnectionId\":\"conn-b\",\"hasConnectedClient\":true,"
					"\"clientCaps\":[\"TOOL_EVENTS\"]}"),
			});
		REQUIRE(stageFirst.ok);
		REQUIRE(stageSecond.ok);
		REQUIRE(legacyFirst.ok);
		REQUIRE(legacySecond.ok);
	}

	stageHost.Stop();
	legacyHost.Stop();
}
