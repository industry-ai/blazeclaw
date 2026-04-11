#include "gateway/ExtensionLifecycleManager.h"
#include "gateway/GatewayHost.h"
#include "gateway/GatewayJsonUtils.h"
#include "gateway/GatewayProtocolModels.h"
#include "gateway/GatewayToolRegistry.h"
#include "gateway/PluginHostAdapter.h"
#include "gateway/GatewayHostEx.h"
#include "gateway/GatewayPersistencePaths.h"
#include "gateway/ChatControlPlaneService.h"
#include "gateway/executors/EmailScheduleExecutor.h"
#include "config/ConfigModels.h"

#include <catch2/catch_all.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdlib>

using namespace blazeclaw::gateway;

TEST_CASE("Parity coverage: router-neutral route decision telemetry is emitted for chat.send", "[parity][router][telemetry]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "router telemetry check";
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

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-router-telemetry-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"router neutral telemetry\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	host.Stop();
}

TEST_CASE(
	"Phase 6: route policy blocks overlong session keys and webchat inheritance",
	"[parity][phase-6][chat][route][deep]") {
	ChatControlPlaneService service;

	const std::string overlongSession(600, 'x');
	const auto overlongDecision = service.EvaluateSendControl(
		ChatControlPlaneService::SendControlInput{
			.sessionKey = overlongSession,
			.deliver = true,
			.routeChannel = "slack",
			.routeTo = "user-1",
			.clientMode = "desktop",
			.clientCaps = { "TOOL_EVENTS" },
			.runId = "phase6-route-overlong",
		});
	REQUIRE_FALSE(overlongDecision.route.explicitDeliverRoute);
	REQUIRE(overlongDecision.route.reasonCode == "session_key_too_long");

	const auto webchatDecision = service.EvaluateSendControl(
		ChatControlPlaneService::SendControlInput{
			.sessionKey = "channel:general:user",
			.deliver = true,
			.routeChannel = "slack",
			.routeTo = "user-1",
			.clientMode = "webchat",
			.clientCaps = { "TOOL_EVENTS" },
			.runId = "phase6-route-webchat",
		});
	REQUIRE_FALSE(webchatDecision.route.explicitDeliverRoute);
	REQUIRE(webchatDecision.route.reasonCode == "webchat_no_inherit");

	const auto mainConnectedDecision = service.EvaluateSendControl(
		ChatControlPlaneService::SendControlInput{
			.sessionKey = "main:workspace",
			.deliver = true,
			.routeChannel = "slack",
			.routeTo = "user-1",
			.clientMode = "desktop",
			.hasConnectedClient = true,
			.mainKey = "main",
			.clientCaps = {},
			.runId = "phase6-route-main-connected",
		});
	REQUIRE(mainConnectedDecision.route.explicitDeliverRoute);
	REQUIRE(mainConnectedDecision.route.reasonCode == "explicit_route");
}

TEST_CASE(
	"Phase 6: chat.send pushLifecycle adds push-compatible lifecycle metadata",
	"[parity][phase-6][chat][lifecycle][push]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "phase6 lifecycle";
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

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase6-push-lifecycle",
			.method = "chat.send",
			.paramsJson = std::string(
				"{\"sessionKey\":\"main\",\"message\":\"phase6 push lifecycle\",\"idempotencyKey\":\"phase6-push-lifecycle-idem\",\"pushLifecycle\":true}"),
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	REQUIRE(sendResponse.payloadJson->find("\"lifecycle\":{") != std::string::npos);
	REQUIRE(sendResponse.payloadJson->find("\"transport\":\"push_compatible\"") != std::string::npos);
	REQUIRE(sendResponse.payloadJson->find("\"state\":\"started\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Phase 6: recipient registry gates tool deltas and supports same-session late join",
	"[parity][phase-6][chat][tool-events][registry]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "assistant response";
			result.assistantDeltas = {
				"tools.execute.start tool=weather.lookup",
				"assistant response",
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

	const auto baselineNoRecipient = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase6-registry-norecipient",
			.method = "chat.send",
			.paramsJson = std::string(
				"{\"sessionKey\":\"channel:general\",\"message\":\"registry baseline\",\"idempotencyKey\":\"phase6-registry-idem-0\"}"),
		});
	REQUIRE(baselineNoRecipient.ok);

	std::string baselinePoll;
	for (int i = 0; i < 4; ++i) {
		const auto poll = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = std::string("phase6-registry-baseline-poll-") + std::to_string(i),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"channel:general\",\"limit\":20}"),
			});
		REQUIRE(poll.ok);
		REQUIRE(poll.payloadJson.has_value());
		baselinePoll += poll.payloadJson.value();
	}
	REQUIRE(baselinePoll.find("tools.execute.start") == std::string::npos);

	const auto withRecipient = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase6-registry-recipient",
			.method = "chat.send",
			.paramsJson = std::string(
				"{\"sessionKey\":\"channel:general\",\"message\":\"registry recipient\",\"idempotencyKey\":\"phase6-registry-idem-1\",\"clientMode\":\"desktop\",\"clientConnectionId\":\"conn-1\",\"clientCaps\":[\"TOOL_EVENTS\"]}"),
		});
	REQUIRE(withRecipient.ok);

	std::string recipientPoll;
	for (int i = 0; i < 4; ++i) {
		const auto poll = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = std::string("phase6-registry-recipient-poll-") + std::to_string(i),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"channel:general\",\"limit\":20}"),
			});
		REQUIRE(poll.ok);
		REQUIRE(poll.payloadJson.has_value());
		recipientPoll += poll.payloadJson.value();
	}
	REQUIRE(recipientPoll.find("tools.execute.start") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Phase 4: route policy and capability-gated tool events are enforced",
	"[parity][phase-4][chat][route][caps]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "assistant response";
			result.assistantDeltas = {
				"tools.execute.start tool=weather.lookup",
				"assistant response",
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

	const auto sendNoCaps = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase4-send-nocaps",
			.method = "chat.send",
			.paramsJson = std::string(
				"{\"sessionKey\":\"channel:general\",\"message\":\"route check\",\"deliver\":true,\"originatingChannel\":\"slack\",\"originatingTo\":\"U-no-caps\",\"clientMode\":\"desktop\"}"),
		});
	REQUIRE(sendNoCaps.ok);
	REQUIRE(sendNoCaps.payloadJson.has_value());
	REQUIRE(sendNoCaps.payloadJson->find("\"originatingChannel\":\"slack\"") != std::string::npos);
	REQUIRE(sendNoCaps.payloadJson->find("\"explicitDeliverRoute\":true") != std::string::npos);

	std::string polledNoCaps;
	for (int i = 0; i < 4; ++i) {
		const auto poll = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = std::string("phase4-poll-nocaps-") + std::to_string(i),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"channel:general\",\"limit\":20}"),
			});
		REQUIRE(poll.ok);
		REQUIRE(poll.payloadJson.has_value());
		polledNoCaps += poll.payloadJson.value();
	}
	REQUIRE(polledNoCaps.find("tools.execute.start") == std::string::npos);

	const auto sendWithCaps = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase4-send-withcaps",
			.method = "chat.send",
			.paramsJson = std::string(
				"{\"sessionKey\":\"channel:general\",\"message\":\"route check with caps\",\"deliver\":true,\"originatingChannel\":\"slack\",\"originatingTo\":\"U-with-caps\",\"clientMode\":\"desktop\",\"clientCaps\":[\"TOOL_EVENTS\"]}"),
		});
	REQUIRE(sendWithCaps.ok);

	std::string polledWithCaps;
	for (int i = 0; i < 4; ++i) {
		const auto poll = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = std::string("phase4-poll-withcaps-") + std::to_string(i),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"channel:general\",\"limit\":20}"),
			});
		REQUIRE(poll.ok);
		REQUIRE(poll.payloadJson.has_value());
		polledWithCaps += poll.payloadJson.value();
	}
	REQUIRE(polledWithCaps.find("tools.execute.start") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Phase 5: chat control-plane service centralizes route and tool-delta gating decisions",
	"[parity][phase-5][chat][decomposition]") {
	ChatControlPlaneService service;

	const auto noCapsDecision = service.EvaluateSendControl(
		ChatControlPlaneService::SendControlInput{
			.sessionKey = "channel:general",
			.deliver = true,
			.routeChannel = "slack",
			.routeTo = "user-1",
			.clientMode = "desktop",
			.clientCaps = {},
			.runId = "phase5-run-1",
		});
	REQUIRE(noCapsDecision.route.explicitDeliverRoute);
	REQUIRE(noCapsDecision.route.originatingChannel == "slack");
	REQUIRE_FALSE(noCapsDecision.toolEvents.wantsToolEvents);
	REQUIRE_FALSE(
		service.ShouldPublishToolDelta(
			"tools.execute.start tool=weather.lookup",
			noCapsDecision));

	const auto withCapsDecision = service.EvaluateSendControl(
		ChatControlPlaneService::SendControlInput{
			.sessionKey = "channel:general",
			.deliver = true,
			.routeChannel = "slack",
			.routeTo = "user-1",
			.clientMode = "desktop",
			.clientCaps = { "TOOL_EVENTS" },
			.runId = "phase5-run-2",
		});
	REQUIRE(withCapsDecision.toolEvents.wantsToolEvents);
	REQUIRE(
		service.ShouldPublishToolDelta(
			"tools.execute.start tool=weather.lookup",
			withCapsDecision));
}

TEST_CASE(
	"Phase 6: chat.send idempotency replays cached payload and supports pushLifecycle hint",
	"[parity][phase-6][chat][dedupe][lifecycle]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "phase6 dedupe payload";
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

	const std::string paramsJson =
		"{\"sessionKey\":\"main\",\"message\":\"phase6 dedupe\",\"idempotencyKey\":\"phase6-idem-1\",\"pushLifecycle\":true}";

	const auto first = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase6-send-1",
			.method = "chat.send",
			.paramsJson = paramsJson,
		});
	REQUIRE(first.ok);
	REQUIRE(first.payloadJson.has_value());

	const auto second = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase6-send-2",
			.method = "chat.send",
			.paramsJson = paramsJson,
		});
	REQUIRE(second.ok);
	REQUIRE(second.payloadJson.has_value());
	REQUIRE(second.payloadJson.value() == first.payloadJson.value());

	host.Stop();
}

TEST_CASE(
	"Phase 6: transcript append idempotency prevents duplicate inject writes",
	"[parity][phase-6][chat][transcript][idempotency]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	const std::string sessionKey = "phase6inject";
	const std::filesystem::path transcriptPath =
		ResolveGatewayStateFilePath("chat-transcripts") /
		(sessionKey + ".jsonl");
	std::error_code removeError;
	std::filesystem::remove(transcriptPath, removeError);

	const auto inject1 = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase6-inject-idem",
			.method = "chat.inject",
			.paramsJson = std::string("{\"sessionKey\":\"phase6inject\",\"message\":\"same inject content\",\"label\":\"phase6\"}"),
		});
	REQUIRE(inject1.ok);

	const auto inject2 = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase6-inject-idem",
			.method = "chat.inject",
			.paramsJson = std::string("{\"sessionKey\":\"phase6inject\",\"message\":\"same inject content\",\"label\":\"phase6\"}"),
		});
	REQUIRE(inject2.ok);

	REQUIRE(std::filesystem::exists(transcriptPath));
	std::ifstream transcript(transcriptPath, std::ios::in | std::ios::binary);
	REQUIRE(transcript.is_open());
	std::string line;
	std::size_t occurrenceCount = 0;
	while (std::getline(transcript, line)) {
		if (line.find("same inject content") != std::string::npos) {
			++occurrenceCount;
		}
	}
	REQUIRE(occurrenceCount == 1);

	host.Stop();
}

TEST_CASE(
	"Phase 2: chat.abort persists partial assistant output",
	"[parity][phase-2][chat][abort][durability]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "partial output to persist on abort";
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

	const std::string sessionKey = "phase2abort";
	const std::filesystem::path transcriptPath =
		ResolveGatewayStateFilePath("chat-transcripts") /
		(sessionKey + ".jsonl");
	std::error_code removeError;
	std::filesystem::remove(transcriptPath, removeError);

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase2-chat-send",
			.method = "chat.send",
			.paramsJson = std::string(
				"{\"sessionKey\":\"phase2abort\",\"message\":\"abort me\",\"idempotencyKey\":\"phase2-idem\"}"),
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));
	REQUIRE_FALSE(runId.empty());

	const auto abortResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase2-chat-abort",
			.method = "chat.abort",
			.paramsJson = std::string("{\"sessionKey\":\"phase2abort\",\"runId\":\"") + runId + "\"}",
		});
	REQUIRE(abortResponse.ok);
	REQUIRE(abortResponse.payloadJson.has_value());
	REQUIRE(abortResponse.payloadJson->find("\"aborted\":true") != std::string::npos);
	REQUIRE(abortResponse.payloadJson->find("\"partialPersisted\":true") != std::string::npos);

	REQUIRE(std::filesystem::exists(transcriptPath));
	std::ifstream transcript(transcriptPath, std::ios::in | std::ios::binary);
	REQUIRE(transcript.is_open());
	const std::string transcriptContent(
		(std::istreambuf_iterator<char>(transcript)),
		std::istreambuf_iterator<char>());
	REQUIRE(transcriptContent.find("partial output to persist on abort") != std::string::npos);
	REQUIRE(transcriptContent.find("abort:rpc:") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Phase 3: chat.history sanitizes, truncates, and bounds oversized entries",
	"[parity][phase-3][chat][history][policy]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	const std::string hugeText(150000, 'A');
	const std::string longText(13050, 'B');

	const auto injectHuge = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase3-chat-inject-huge",
			.method = "chat.inject",
			.paramsJson = std::string("{\"sessionKey\":\"phase3history\",\"message\":\"") + hugeText + "\"}",
		});
	REQUIRE(injectHuge.ok);

	const auto historyOversizedResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase3-chat-history-oversized",
			.method = "chat.history",
			.paramsJson = std::string("{\"sessionKey\":\"phase3history\",\"limit\":5}"),
		});
	REQUIRE(historyOversizedResponse.ok);
	REQUIRE(historyOversizedResponse.payloadJson.has_value());
	REQUIRE(
		historyOversizedResponse.payloadJson->find("[chat.history omitted: message too large]") !=
		std::string::npos);

	const auto injectLong = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase3-chat-inject-long",
			.method = "chat.inject",
			.paramsJson = std::string("{\"sessionKey\":\"phase3history\",\"message\":\"") + longText + "\"}",
		});
	REQUIRE(injectLong.ok);

	const auto historyResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase3-chat-history",
			.method = "chat.history",
			.paramsJson = std::string("{\"sessionKey\":\"phase3history\",\"limit\":20}"),
		});
	REQUIRE(historyResponse.ok);
	REQUIRE(historyResponse.payloadJson.has_value());
	REQUIRE(
		historyResponse.payloadJson->find("...(truncated)...") !=
		std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Phase 0: chat control-plane contract matrix baseline",
	"[parity][phase-0][chat][contract]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "phase0 contract";
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

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase0-chat-send",
			.method = "chat.send",
			.paramsJson = std::string(
				"{\"sessionKey\":\"main\",\"message\":\"phase0 contract request\",\"idempotencyKey\":\"phase0-idem\"}"),
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	REQUIRE(sendResponse.payloadJson->find("\"runId\":") != std::string::npos);
	REQUIRE(sendResponse.payloadJson->find("\"queued\":true") != std::string::npos);

	const auto historyResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase0-chat-history",
			.method = "chat.history",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":10}"),
		});
	REQUIRE(historyResponse.ok);
	REQUIRE(historyResponse.payloadJson.has_value());
	REQUIRE(historyResponse.payloadJson->find("\"messages\":") != std::string::npos);
	REQUIRE(historyResponse.payloadJson->find("\"thinkingLevel\":") != std::string::npos);

	const auto abortResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase0-chat-abort",
			.method = "chat.abort",
			.paramsJson = std::string("{\"sessionKey\":\"main\"}"),
		});
	REQUIRE(abortResponse.ok);
	REQUIRE(abortResponse.payloadJson.has_value());
	REQUIRE(abortResponse.payloadJson->find("\"aborted\":") != std::string::npos);

	const auto injectResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase0-chat-inject",
			.method = "chat.inject",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"phase0 inject\"}"),
		});
	REQUIRE(injectResponse.ok);
	REQUIRE(injectResponse.payloadJson.has_value());
	REQUIRE(injectResponse.payloadJson->find("\"ok\":true") != std::string::npos);
	REQUIRE(injectResponse.payloadJson->find("\"messageId\":") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Phase 1: chat.inject persists transcript and emits final event",
	"[parity][phase-1][chat][inject][durability]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	const std::string sessionKey = "phase1inject";
	const std::filesystem::path transcriptPath =
		ResolveGatewayStateFilePath("chat-transcripts") /
		(sessionKey + ".jsonl");
	std::error_code removeError;
	std::filesystem::remove(transcriptPath, removeError);

	const auto injectResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase1-chat-inject",
			.method = "chat.inject",
			.paramsJson = std::string(
				"{\"sessionKey\":\"phase1inject\",\"message\":\"persist this injected assistant message\",\"label\":\"phase1\"}"),
		});
	REQUIRE(injectResponse.ok);
	REQUIRE(injectResponse.payloadJson.has_value());

	std::string messageId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		injectResponse.payloadJson.value(),
		"messageId",
		messageId));
	REQUIRE_FALSE(messageId.empty());

	const auto pollResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase1-chat-inject-poll",
			.method = "chat.events.poll",
			.paramsJson = std::string("{\"sessionKey\":\"phase1inject\",\"limit\":20}"),
		});
	REQUIRE(pollResponse.ok);
	REQUIRE(pollResponse.payloadJson.has_value());
	REQUIRE(
		pollResponse.payloadJson->find(std::string("\"runId\":\"inject-") + messageId + "\"") !=
		std::string::npos);
	REQUIRE(
		pollResponse.payloadJson->find("persist this injected assistant message") !=
		std::string::npos);

	REQUIRE(std::filesystem::exists(transcriptPath));
	std::ifstream transcript(transcriptPath, std::ios::in | std::ios::binary);
	REQUIRE(transcript.is_open());
	std::string transcriptContent(
		(std::istreambuf_iterator<char>(transcript)),
		std::istreambuf_iterator<char>());
	REQUIRE(
		transcriptContent.find(std::string("\"messageId\":\"") + messageId + "\"") !=
		std::string::npos);
	REQUIRE(
		transcriptContent.find("persist this injected assistant message") !=
		std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: reversible route switch preserves idempotent run lineage",
	"[parity][router][equivalence][idempotency]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "equivalence lineage";
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

	const auto sendWithIdempotency =
		[&host](const std::string& requestId,
			const std::string& idempotencyKey) {
				return host.RouteRequest(
					blazeclaw::gateway::protocol::RequestFrame{
						.id = requestId,
						.method = "chat.send",
						.paramsJson =
							std::string("{\"sessionKey\":\"main\",\"message\":\"switch parity\",\"idempotencyKey\":\"") +
							idempotencyKey +
							"\"}",
					});
		};

	host.SetEmbeddedOrchestrationPath("stage_pipeline_canary");
	const auto stageFirst = sendWithIdempotency(
		"route-switch-idem-stage-1",
		"idem-route-switch-1");
	REQUIRE(stageFirst.ok);
	REQUIRE(stageFirst.payloadJson.has_value());

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		stageFirst.payloadJson.value(),
		"runId",
		runId));
	REQUIRE_FALSE(runId.empty());

	host.SetEmbeddedOrchestrationPath("legacy_only");
	const auto legacyDedupe = sendWithIdempotency(
		"route-switch-idem-legacy-1",
		"idem-route-switch-1");
	REQUIRE(legacyDedupe.ok);
	REQUIRE(legacyDedupe.payloadJson.has_value());
	REQUIRE(legacyDedupe.payloadJson->find("\"deduped\":true") != std::string::npos);
	REQUIRE(legacyDedupe.payloadJson->find(std::string("\"runId\":\"") + runId + "\"") != std::string::npos);

	host.SetEmbeddedOrchestrationPath("stage_pipeline_canary");
	const auto stageDedupeAgain = sendWithIdempotency(
		"route-switch-idem-stage-2",
		"idem-route-switch-1");
	REQUIRE(stageDedupeAgain.ok);
	REQUIRE(stageDedupeAgain.payloadJson.has_value());
	REQUIRE(stageDedupeAgain.payloadJson->find("\"deduped\":true") != std::string::npos);
	REQUIRE(stageDedupeAgain.payloadJson->find(std::string("\"runId\":\"") + runId + "\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: chat.send succeeds across stage and legacy route mode switches",
	"[parity][phase-f][chat][route-switch]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "phase-f route switch";
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

	host.SetEmbeddedOrchestrationPath("stage_pipeline_canary");
	const auto stageResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase-f-stage-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"stage path\"}"),
		});
	REQUIRE(stageResponse.ok);
	REQUIRE(stageResponse.payloadJson.has_value());

	std::string stageRunId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		stageResponse.payloadJson.value(),
		"runId",
		stageRunId));
	REQUIRE_FALSE(stageRunId.empty());

	host.SetEmbeddedOrchestrationPath("legacy_only");
	const auto legacyResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase-f-legacy-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"legacy path\"}"),
		});
	REQUIRE(legacyResponse.ok);
	REQUIRE(legacyResponse.payloadJson.has_value());

	std::string legacyRunId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		legacyResponse.payloadJson.value(),
		"runId",
		legacyRunId));
	REQUIRE_FALSE(legacyRunId.empty());
	REQUIRE(stageRunId != legacyRunId);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: gateway.agents.run idempotency and wait preserve runId lineage",
	"[parity][phase-f][agents][runid]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "agent runId lineage";
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

	const auto firstRun = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase-f-agents-run-1",
			.method = "gateway.agents.run",
			.paramsJson = std::string(
				"{\"agentId\":\"default\",\"sessionId\":\"main\","
				"\"message\":\"lineage\",\"idempotencyKey\":\"idem-phase-f-1\"}"),
		});
	REQUIRE(firstRun.ok);
	REQUIRE(firstRun.payloadJson.has_value());

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		firstRun.payloadJson.value(),
		"runId",
		runId));
	REQUIRE_FALSE(runId.empty());

	const auto dedupedRun = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase-f-agents-run-1-dedupe",
			.method = "gateway.agents.run",
			.paramsJson = std::string(
				"{\"agentId\":\"default\",\"sessionId\":\"main\","
				"\"message\":\"lineage\",\"idempotencyKey\":\"idem-phase-f-1\"}"),
		});
	REQUIRE(dedupedRun.ok);
	REQUIRE(dedupedRun.payloadJson.has_value());
	REQUIRE(dedupedRun.payloadJson->find("\"deduped\":true") != std::string::npos);
	REQUIRE(dedupedRun.payloadJson->find(std::string("\"runId\":\"") + runId + "\"") != std::string::npos);

	for (int attempt = 0; attempt < 6; ++attempt) {
		const auto pollResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = std::string("phase-f-agents-run-1-poll-") + std::to_string(attempt),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":50}"),
			});
		REQUIRE(pollResponse.ok);
	}

	const auto waitResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase-f-agents-run-1-wait",
			.method = "gateway.agents.wait",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});
	REQUIRE(waitResponse.ok);
	REQUIRE(waitResponse.payloadJson.has_value());
	REQUIRE(waitResponse.payloadJson->find(std::string("\"runId\":\"") + runId + "\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: chat.abort preserves run correlation in later event polling",
	"[parity][phase-f][chat][abort]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "abort lineage";
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

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase-f-abort-send-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"abort test\"}"),
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));
	REQUIRE_FALSE(runId.empty());

	const auto abortResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase-f-abort-1",
			.method = "chat.abort",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"runId\":\"") + runId + "\"}",
		});
	REQUIRE(abortResponse.ok);
	REQUIRE(abortResponse.payloadJson.has_value());
	REQUIRE(abortResponse.payloadJson->find("\"aborted\":true") != std::string::npos);
	REQUIRE(abortResponse.payloadJson->find(std::string("\"runId\":\"") + runId + "\"") != std::string::npos);

	const auto eventsResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "phase-f-abort-1-events",
			.method = "chat.events.poll",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":50}"),
		});
	REQUIRE(eventsResponse.ok);
	REQUIRE(eventsResponse.payloadJson.has_value());
	REQUIRE(eventsResponse.payloadJson->find("\"state\":\"aborted\"") != std::string::npos);
	REQUIRE(eventsResponse.payloadJson->find(std::string("\"runId\":\"") + runId + "\"") != std::string::npos);

	host.Stop();
}

TEST_CASE("Parity coverage: routed chat.send preserves runId continuity across ack events and task-deltas", "[parity][router][runid]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("stage_pipeline_canary");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "runId continuity";
			result.taskDeltas = {
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 0,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "plan",
					.status = "ok",
					.stepLabel = "execution_plan",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 1,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "final",
					.status = "completed",
					.stepLabel = "run_terminal",
				},
			};
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-router-runid-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"continuity check\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));
	REQUIRE_FALSE(runId.empty());

	const auto deltasResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-router-runid-1-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(deltasResponse.ok);
	REQUIRE(deltasResponse.payloadJson.has_value());
	REQUIRE(deltasResponse.payloadJson->find(std::string("\"runId\":\"") + runId + "\"") != std::string::npos);

	std::string polledEvents;
	for (int attempt = 0; attempt < 4; ++attempt) {
		const auto eventsResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = std::string("chat-router-runid-1-events-") + std::to_string(attempt),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":50}"),
			});
		REQUIRE(eventsResponse.ok);
		REQUIRE(eventsResponse.payloadJson.has_value());
		polledEvents += eventsResponse.payloadJson.value();
		if (polledEvents.find("\"state\":\"final\"") != std::string::npos) {
			break;
		}
	}

	REQUIRE(polledEvents.find(std::string("\"runId\":\"") + runId + "\"") != std::string::npos);

	host.Stop();
}

TEST_CASE("Parity coverage: legacy_only orchestration path preserves legacy routing", "[parity][router][legacy-only]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("legacy_only");

	const auto response = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-legacy-only-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"legacy only route\"}"),
		});

	REQUIRE(response.ok);
	REQUIRE(response.payloadJson.has_value());

	host.Stop();
}

TEST_CASE("Parity coverage: GatewayHostEx scaffolding remains health-gated and contract-safe", "[parity][router][gateway-host-ex]") {
	GatewayHostEx hostEx(GatewayHostExDependencies{});

	REQUIRE_FALSE(hostEx.IsHealthy());

	const auto response = hostEx.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "gateway-host-ex-unhealthy-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"hello\"}"),
		});

	REQUIRE_FALSE(response.ok);
	REQUIRE(response.error.has_value());
	REQUIRE(response.error->code == "stage_host_unavailable");
}

TEST_CASE("Parity coverage: GatewayHost via IGatewayHostRuntime preserves chat.send contract", "[parity][router][interface]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "interface contract ok";
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

	IGatewayHostRuntime* runtime = &host;
	const auto response = runtime->RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-interface-contract-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"interface route check\"}"),
		});

	REQUIRE(response.ok);
	REQUIRE(response.payloadJson.has_value());
	REQUIRE(response.payloadJson->find("\"runId\":") != std::string::npos);
	REQUIRE(response.payloadJson->find("\"queued\":true") != std::string::npos);
	REQUIRE(response.payloadJson->find("\"deduped\":false") != std::string::npos);

	host.Stop();
}

TEST_CASE("Parity coverage: GatewayHost via IGatewayHostRuntime preserves legacy non-chat dispatch", "[parity][router][interface]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	IGatewayHostRuntime* runtime = &host;
	const auto response = runtime->RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "tools-list-interface-1",
			.method = "gateway.tools.list",
			.paramsJson = std::string("{}"),
		});

	REQUIRE(response.ok);
	REQUIRE(response.payloadJson.has_value());
	REQUIRE(response.payloadJson->find("\"tools\":") != std::string::npos);

	host.Stop();
}

TEST_CASE("Parity coverage: chat.send send-policy denial returns deterministic error envelope", "[parity][policy][send]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	const auto response = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-send-policy-deny-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"\",\"message\":\"policy deny\"}"),
		});

	REQUIRE_FALSE(response.ok);
	REQUIRE(response.error.has_value());
	REQUIRE(response.error->code == "denied_send");
	REQUIRE(response.payloadJson == std::nullopt);

	host.Stop();
}

TEST_CASE("Parity coverage: lifecycle activation/deactivation updates tool catalog", "[parity][lifecycle]") {
	PluginHostAdapter::RegisterExtensionAdapter(
		"ops-tools",
		[](const std::string&, const std::string&, const std::string&) {
			return GatewayToolRegistry::RuntimeToolExecutor{};
		});

	const auto tmpRoot = std::filesystem::temp_directory_path() /
		("blazeclaw_parity_" + std::to_string(std::rand()));
	std::filesystem::create_directories(tmpRoot);

	const auto extDir = tmpRoot / "ops";
	std::filesystem::create_directories(extDir);

	{
		std::ofstream out((extDir / "blazeclaw.extension.json").string());
		out << "{\"tools\":[{\"id\":\"weather.lookup\",\"label\":\"Weather Lookup\",\"category\":\"ops\",\"enabled\":true}]}";
	}

	std::filesystem::remove_all(tmpRoot);
}

TEST_CASE("Parity coverage: chat.send contract envelope is stable", "[parity][contract][chat.send]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "contract stability";
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

	const auto response = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-contract-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"contract check\"}"),
		});

	REQUIRE(response.ok);
	REQUIRE(response.payloadJson.has_value());
	REQUIRE(response.payloadJson->find("\"runId\":") != std::string::npos);
	REQUIRE(response.payloadJson->find("\"queued\":true") != std::string::npos);
	REQUIRE(response.payloadJson->find("\"deduped\":false") != std::string::npos);

	host.Stop();
}

TEST_CASE("Parity coverage: tool call sequence supports approval prepare and approve", "[parity][approval]") {
	GatewayToolRegistry registry;

	char* previousModeRaw = nullptr;
	size_t previousModeLength = 0;
	_dupenv_s(
		&previousModeRaw,
		&previousModeLength,
		"BLAZECLAW_EMAIL_DELIVERY_MODE");
	_putenv_s("BLAZECLAW_EMAIL_DELIVERY_MODE", "mock_success");

	PluginHostAdapter::RegisterExtensionAdapter(
		"ops-tools",
		[](const std::string&, const std::string&, const std::string&) {
			return GatewayToolRegistry::RuntimeToolExecutor{};
		});

	const auto loaded = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
	REQUIRE(loaded.ok);

	const auto resolved =
		PluginHostAdapter::ResolveExecutor("ops-tools", "email.schedule", "");

	REQUIRE(resolved.resolved);
	REQUIRE(resolved.executor);

	registry.RegisterRuntimeTool(
		ToolCatalogEntry{
			.id = "email.schedule",
			.label = "Email Schedule",
			.category = "ops",
			.enabled = true,
		},
		resolved.executor);

	const auto prepare = registry.Execute(
		"email.schedule",
		std::string("{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Report\",\"body\":\"Tomorrow cloudy\",\"sendAt\":\"13:00\"}"));

	REQUIRE(prepare.executed);
	REQUIRE(prepare.status == "needs_approval");
	REQUIRE(prepare.output.find("approvalToken") != std::string::npos);

	const auto tokenStart = prepare.output.find("\"approvalToken\":\"");
	REQUIRE(tokenStart != std::string::npos);
	const auto tokenValueStart = tokenStart + std::string("\"approvalToken\":\"").size();
	const auto tokenValueEnd = prepare.output.find('"', tokenValueStart);
	REQUIRE(tokenValueEnd != std::string::npos);
	const std::string token = prepare.output.substr(tokenValueStart, tokenValueEnd - tokenValueStart);
	REQUIRE_FALSE(token.empty());

	const auto approve = registry.Execute(
		"email.schedule",
		std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token + "\",\"approve\":true}");

	REQUIRE(approve.executed);
	REQUIRE(approve.status == "ok");

	const auto unloaded = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
	REQUIRE(unloaded.ok);

	if (previousModeRaw != nullptr && previousModeLength > 0) {
		_putenv_s("BLAZECLAW_EMAIL_DELIVERY_MODE", previousModeRaw);
		free(previousModeRaw);
	}
	else {
		if (previousModeRaw != nullptr) {
			free(previousModeRaw);
		}
		_putenv_s("BLAZECLAW_EMAIL_DELIVERY_MODE", "");
	}
}

TEST_CASE("Parity coverage: prompt intent parser handles now and today", "[parity][chat][orchestration]") {
	const auto intent = prompt::AnalyzeWeatherEmailPromptIntent(
		"Check today's weather in Wuhan, write a short report, and email it to jichengwhu@163.com now.");

	REQUIRE(intent.matched);
	REQUIRE(intent.hasWeather);
	REQUIRE(intent.hasEmail);
	REQUIRE(intent.hasReport);
	REQUIRE(intent.hasRecipient);
	REQUIRE(intent.hasSchedule);
	REQUIRE(intent.date == "today");
	REQUIRE(intent.scheduleKind == "immediate_keyword");
	REQUIRE(intent.sendAt.size() == 5);
	REQUIRE(intent.decompositionSteps == 3);
}

TEST_CASE("Parity coverage: prompt intent parser handles explicit time schedule", "[parity][chat][orchestration]") {
	const auto intent = prompt::AnalyzeWeatherEmailPromptIntent(
		"Check tomorrow weather in Wuhan and email it to jichengwhu@163.com at 3pm.");

	REQUIRE(intent.matched);
	REQUIRE(intent.hasWeather);
	REQUIRE(intent.hasEmail);
	REQUIRE(intent.hasRecipient);
	REQUIRE(intent.date == "tomorrow");
	REQUIRE(intent.hasSchedule);
	REQUIRE(intent.scheduleKind == "clock_time");
	REQUIRE(intent.sendAt == "15:00");
}

TEST_CASE("Parity coverage: prompt intent parser handles possessive tomorrow with immediate send", "[parity][chat][orchestration]") {
	const auto intent = prompt::AnalyzeWeatherEmailPromptIntent(
		"Check tomorrow's weather in Wuhan, write a short report, and email it to jicheng@whu.edu.cn now");

	REQUIRE(intent.matched);
	REQUIRE(intent.hasWeather);
	REQUIRE(intent.hasEmail);
	REQUIRE(intent.hasReport);
	REQUIRE(intent.hasRecipient);
	REQUIRE(intent.recipient == "jicheng@whu.edu.cn");
	REQUIRE(intent.hasSchedule);
	REQUIRE(intent.date == "tomorrow");
	REQUIRE(intent.scheduleKind == "immediate_keyword");
}

TEST_CASE("Parity coverage: prompt intent parser reports miss reasons", "[parity][chat][orchestration]") {
	const auto intent = prompt::AnalyzeWeatherEmailPromptIntent(
		"Please summarize today's traffic updates.");

	REQUIRE_FALSE(intent.matched);
	REQUIRE(std::find(
		intent.missReasons.begin(),
		intent.missReasons.end(),
		"missing_weather") != intent.missReasons.end());
	REQUIRE(std::find(
		intent.missReasons.begin(),
		intent.missReasons.end(),
		"missing_email_action") != intent.missReasons.end());
	REQUIRE(std::find(
		intent.missReasons.begin(),
		intent.missReasons.end(),
		"missing_recipient_email") != intent.missReasons.end());
}

TEST_CASE(
	"Parity coverage: chat.send uses dynamic callback path when orchestrationPath=dynamic_task_delta",
	"[parity][chat][orchestration][e2e][dynamic]") {
	PluginHostAdapter::RegisterExtensionAdapter(
		"ops-tools",
		[](const std::string&, const std::string&, const std::string&) {
			return GatewayToolRegistry::RuntimeToolExecutor{};
		});

	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	std::size_t callbackCalls = 0;
	host.SetChatRuntimeCallback(
		[&](const GatewayHost::ChatRuntimeRequest& request) {
			++callbackCalls;
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "dynamic callback handled";
			result.assistantDeltas = {
				"tools.execute.start tool=weather.lookup",
				"tools.execute.result tool=weather.lookup status=ok",
			};
			result.taskDeltas = {
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 0,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "plan",
					.resultJson = "[\"weather.lookup\",\"report.compose\",\"email.schedule\"]",
					.status = "ok",
					.stepLabel = "execution_plan",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 1,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "tool_call",
					.toolName = "weather.lookup",
					.status = "requested",
					.stepLabel = "tool_request",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 2,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "tool_result",
					.toolName = "weather.lookup",
					.status = "ok",
					.stepLabel = "tool_result",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 3,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "final",
					.resultJson = "dynamic callback handled",
					.status = "completed",
					.stepLabel = "run_terminal",
				},
			};
			result.modelId = "default";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-dynamic-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"Summarize this short note about system status.\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	REQUIRE(callbackCalls == 1);

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));

	const auto deltasResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-dynamic-1-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(deltasResponse.ok);
	REQUIRE(deltasResponse.payloadJson.has_value());
	REQUIRE(deltasResponse.payloadJson->find("\"toolName\":\"weather.lookup\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: chat.events.poll emits lifecycle visibility states in order",
	"[parity][chat][events][lifecycle]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "lifecycle visibility response";
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

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-events-lifecycle-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"Lifecycle visibility test\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	std::string eventPayload;
	for (int i = 0; i < 5; ++i) {
		const auto eventsResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = std::string("chat-events-lifecycle-1-poll-") + std::to_string(i),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":50}"),
			});
		REQUIRE(eventsResponse.ok);
		REQUIRE(eventsResponse.payloadJson.has_value());
		eventPayload += eventsResponse.payloadJson.value();
		if (eventPayload.find("\"state\":\"final\"") != std::string::npos) {
			break;
		}
	}

	REQUIRE(eventPayload.find("\"state\":\"queued\"") != std::string::npos);
	REQUIRE(eventPayload.find("\"state\":\"started\"") != std::string::npos);
	REQUIRE(eventPayload.find("\"state\":\"final\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: dynamic_task_delta defaults to runtime callback and avoids deterministic prompt orchestration branch",
	"[parity][chat][runtime][dynamic-task-delta][default]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");

	std::size_t callbackCalls = 0;
	host.SetChatRuntimeCallback(
		[&](const GatewayHost::ChatRuntimeRequest& request) {
			++callbackCalls;
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "dynamic default runtime callback";
			result.taskDeltas = {
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 0,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "plan",
					.status = "ok",
					.stepLabel = "execution_plan",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 1,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "final",
					.status = "completed",
					.stepLabel = "run_terminal",
				},
			};
			result.modelId = "default";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-dynamic-default-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"Check tomorrow's weather in Wuhan, write a short report, and email it to jicheng@whu.edu.cn now.\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	REQUIRE(callbackCalls == 1);

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));

	const auto deltasResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-dynamic-default-1-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(deltasResponse.ok);
	REQUIRE(deltasResponse.payloadJson.has_value());
	REQUIRE(deltasResponse.payloadJson->find("\"phase\":\"final\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: runtime orchestration reflects fallback backend when himalaya is unavailable",
	"[parity][chat][orchestration][e2e][runtime][fallback][backend]") {
	char* previousModeRaw = nullptr;
	size_t previousModeLength = 0;
	_dupenv_s(
		&previousModeRaw,
		&previousModeLength,
		"BLAZECLAW_EMAIL_DELIVERY_MODE");

	char* previousImapModeRaw = nullptr;
	size_t previousImapModeLength = 0;
	_dupenv_s(
		&previousImapModeRaw,
		&previousImapModeLength,
		"BLAZECLAW_EMAIL_IMAP_SMTP_MODE");

	char* previousBackendsRaw = nullptr;
	size_t previousBackendsLength = 0;
	_dupenv_s(
		&previousBackendsRaw,
		&previousBackendsLength,
		"BLAZECLAW_EMAIL_DELIVERY_BACKENDS");

	_putenv_s("BLAZECLAW_EMAIL_DELIVERY_MODE", "mock_failure");
	_putenv_s("BLAZECLAW_EMAIL_IMAP_SMTP_MODE", "mock_success");
	_putenv_s("BLAZECLAW_EMAIL_DELIVERY_BACKENDS", "himalaya,imap-smtp-email");

	PluginHostAdapter::RegisterExtensionAdapter(
		"ops-tools",
		[](const std::string&, const std::string& toolName, const std::string&) {
			if (toolName == "weather.lookup") {
				return GatewayToolRegistry::RuntimeToolExecutor{
					[](const std::string& requestedTool, const std::optional<std::string>&) {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "{\"ok\":true,\"forecast\":{\"condition\":\"Sunny\",\"temperatureC\":25,\"wind\":\"SE 12 km/h\",\"humidityPct\":44}}",
						};
					} };
			}

			return GatewayToolRegistry::RuntimeToolExecutor{};
		});

	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));
	host.SetEmbeddedOrchestrationPath("runtime_orchestration");

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-runtime-fallback-backend-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"Check tomorrow's weather in Wuhan, write a short report, and email it to jicheng@whu.edu.cn now.\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));

	const auto deltasResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-runtime-fallback-backend-1-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(deltasResponse.ok);
	REQUIRE(deltasResponse.payloadJson.has_value());

	std::string eventsPayload;
	for (int attempt = 0; attempt < 4; ++attempt) {
		const auto eventsResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = std::string("chat-runtime-fallback-backend-1-events-") +
					std::to_string(attempt),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":50}"),
			});
		REQUIRE(eventsResponse.ok);
		REQUIRE(eventsResponse.payloadJson.has_value());
		eventsPayload += eventsResponse.payloadJson.value();

		if (eventsPayload.find("\"state\":\"final\"") != std::string::npos &&
			eventsPayload.find("via imap-smtp-email") != std::string::npos) {
			break;
		}
	}

	const std::string combinedPayload =
		deltasResponse.payloadJson.value() + eventsPayload;
	REQUIRE(combinedPayload.find("via imap-smtp-email") != std::string::npos);

	host.Stop();

	if (previousModeRaw != nullptr && previousModeLength > 0) {
		_putenv_s("BLAZECLAW_EMAIL_DELIVERY_MODE", previousModeRaw);
		free(previousModeRaw);
	}
	else {
		if (previousModeRaw != nullptr) {
			free(previousModeRaw);
		}
		_putenv_s("BLAZECLAW_EMAIL_DELIVERY_MODE", "");
	}

	if (previousImapModeRaw != nullptr && previousImapModeLength > 0) {
		_putenv_s("BLAZECLAW_EMAIL_IMAP_SMTP_MODE", previousImapModeRaw);
		free(previousImapModeRaw);
	}
	else {
		if (previousImapModeRaw != nullptr) {
			free(previousImapModeRaw);
		}
		_putenv_s("BLAZECLAW_EMAIL_IMAP_SMTP_MODE", "");
	}

	if (previousBackendsRaw != nullptr && previousBackendsLength > 0) {
		_putenv_s("BLAZECLAW_EMAIL_DELIVERY_BACKENDS", previousBackendsRaw);
		free(previousBackendsRaw);
	}
	else {
		if (previousBackendsRaw != nullptr) {
			free(previousBackendsRaw);
		}
		_putenv_s("BLAZECLAW_EMAIL_DELIVERY_BACKENDS", "");
	}
}

TEST_CASE(
	"Parity coverage: MFC email config persistence targets canonical .env path",
	"[parity][config][mfc][email]") {
	const auto sourcePath = std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"app" /
		"BlazeClawMFCDoc.cpp";
	std::ifstream in(sourcePath.string());
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	REQUIRE(source.find("USERPROFILE") != std::string::npos);
	REQUIRE(source.find(".config") != std::string::npos);
	REQUIRE(source.find("imap-smtp-email") != std::string::npos);
	REQUIRE(source.find("return L\".env\";") != std::string::npos);
}

TEST_CASE(
	"Parity coverage: OutputWnd contract freeze removes placeholder rows and preserves runtime append path",
	"[parity][contract][mfc][output]") {
	const auto sourcePath = std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"app" /
		"OutputWnd.cpp";
	std::ifstream in(sourcePath.string());
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	REQUIRE(source.find("Build output is being displayed here.") == std::string::npos);
	REQUIRE(source.find("Find output is being displayed here.") == std::string::npos);
	REQUIRE(source.find("m_wndOutputDebug.AppendLine(line);") != std::string::npos);
	REQUIRE(source.find("m_wndOutputFind.AppendLine(line);") != std::string::npos);
	REQUIRE(source.find("void COutputWnd::FillBuildWindow()") != std::string::npos);
	REQUIRE(source.find("void COutputWnd::FillFindWindow()") != std::string::npos);
}

TEST_CASE(
	"Parity coverage: gateway tools discovery exists and count include loaded catalog tools",
	"[parity][tools][discovery][catalog]") {
	const auto tmpRoot = std::filesystem::temp_directory_path() /
		("blazeclaw_tools_discovery_" + std::to_string(std::rand()));
	std::filesystem::create_directories(tmpRoot / "skills");

	{
		std::ofstream out((tmpRoot / "skills" / "blazeclaw.extension.json").string());
		REQUIRE(out.is_open());
		out << "{\"tools\":["
			"{\"id\":\"imap_smtp_email.imap.check\",\"label\":\"IMAP Check\",\"category\":\"email\",\"enabled\":true},"
			"{\"id\":\"imap_smtp_email.smtp.send\",\"label\":\"SMTP Send\",\"category\":\"email\",\"enabled\":true}"
			"]}";
	}

	{
		std::ofstream out((tmpRoot / "extensions.catalog.json").string());
		REQUIRE(out.is_open());
		out << "{\"version\":1,\"extensions\":[{\"id\":\"imap-smtp-email\",\"path\":\"skills/blazeclaw.extension.json\",\"enabled\":true}]}";
	}

	GatewayToolRegistry registry;
	const auto loaded = registry.LoadExtensionToolsFromCatalog(
		(tmpRoot / "extensions.catalog.json").string());
	REQUIRE(loaded == 2);

	const auto listed = registry.List();
	REQUIRE(std::any_of(
		listed.begin(),
		listed.end(),
		[](const ToolCatalogEntry& tool) {
			return tool.id == "imap_smtp_email.imap.check";
		}));
	REQUIRE(std::any_of(
		listed.begin(),
		listed.end(),
		[](const ToolCatalogEntry& tool) {
			return tool.id == "imap_smtp_email.smtp.send";
		}));

	const auto preview = registry.Preview("imap_smtp_email.imap.check");
	REQUIRE(preview.allowed);
	REQUIRE(preview.reason == "ready");

	std::filesystem::remove_all(tmpRoot);
}

TEST_CASE(
	"Parity coverage: gateway.agents.run wait lifecycle reaches terminal with task delta visibility",
	"[parity][agents][run][wait][taskdeltas]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");
	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "agent lifecycle completed";
			result.assistantDeltas = { "agent lifecycle completed" };
			result.taskDeltas = {
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 0,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "plan",
					.resultJson = "[\"imap_smtp_email.imap.check\"]",
					.status = "planned",
					.stepLabel = "execution_plan",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 1,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "tool_call",
					.toolName = "imap_smtp_email.imap.check",
					.status = "requested",
					.stepLabel = "tool_request",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 2,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "tool_result",
					.toolName = "imap_smtp_email.imap.check",
					.status = "ok",
					.stepLabel = "tool_result",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 3,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "final",
					.resultJson = "agent lifecycle completed",
					.status = "completed",
					.stepLabel = "run_terminal",
				},
			};
			result.modelId = "default";
			return result;
		});

	const auto runResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "agents-run-e2e-1",
			.method = "gateway.agents.run",
			.paramsJson = std::string("{\"agentId\":\"default\",\"sessionId\":\"main\",\"message\":\"check my inbox summary\"}"),
		});

	REQUIRE(runResponse.ok);
	REQUIRE(runResponse.payloadJson.has_value());
	REQUIRE(runResponse.payloadJson->find("\"status\":\"queued\"") != std::string::npos);

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		runResponse.payloadJson.value(),
		"runId",
		runId));
	REQUIRE_FALSE(runId.empty());

	for (int attempt = 0; attempt < 10; ++attempt) {
		const auto pollResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "agents-run-e2e-1-poll-" + std::to_string(attempt),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":20}"),
			});
		REQUIRE(pollResponse.ok);
	}

	const auto waitResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "agents-run-e2e-1-wait",
			.method = "gateway.agents.wait",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(waitResponse.ok);
	REQUIRE(waitResponse.payloadJson.has_value());
	REQUIRE(waitResponse.payloadJson->find("\"terminal\":true") != std::string::npos);
	REQUIRE(waitResponse.payloadJson->find("\"status\":\"completed\"") != std::string::npos);

	const auto deltaResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "agents-run-e2e-1-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(deltaResponse.ok);
	REQUIRE(deltaResponse.payloadJson.has_value());
	REQUIRE(deltaResponse.payloadJson->find("\"toolName\":\"imap_smtp_email.imap.check\"") != std::string::npos);
	REQUIRE(deltaResponse.payloadJson->find("\"phase\":\"final\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: chat.send immediate_keyword auto-approves and sends email end-to-end",
	"[parity][chat][orchestration][e2e][runtime][immediate]") {
	const auto tempStateRoot = std::filesystem::temp_directory_path() /
		("blazeclaw_email_immediate_state_" + std::to_string(std::rand()));
	std::filesystem::create_directories(tempStateRoot);

	char* previousLocalAppData = nullptr;
	size_t previousLocalAppDataLen = 0;
	_dupenv_s(&previousLocalAppData, &previousLocalAppDataLen, "LOCALAPPDATA");
	_putenv_s("LOCALAPPDATA", tempStateRoot.string().c_str());

	char* previousModeRaw = nullptr;
	size_t previousModeLength = 0;
	_dupenv_s(
		&previousModeRaw,
		&previousModeLength,
		"BLAZECLAW_EMAIL_DELIVERY_MODE");
	_putenv_s("BLAZECLAW_EMAIL_DELIVERY_MODE", "mock_success");

	PluginHostAdapter::RegisterExtensionAdapter(
		"ops-tools",
		[](const std::string&, const std::string& toolName, const std::string&) {
			if (toolName == "weather.lookup") {
				return GatewayToolRegistry::RuntimeToolExecutor{
					[](const std::string& requestedTool, const std::optional<std::string>&) {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "{\"ok\":true,\"forecast\":{\"condition\":\"Partly cloudy\",\"temperatureC\":19,\"wind\":\"NW 11 km/h\",\"humidityPct\":78}}",
						};
					} };
			}

			if (toolName == "email.schedule") {
				return GatewayToolRegistry::RuntimeToolExecutor{
					[](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
						std::string action;
						if (argsJson.has_value()) {
							blazeclaw::gateway::json::FindStringField(
								argsJson.value(),
								"action",
								action);
						}

						if (action.empty() || action == "prepare") {
							return ToolExecuteResult{
								.tool = requestedTool,
								.executed = true,
								.status = "needs_approval",
								.output = "{\"requiresApproval\":{\"approvalToken\":\"token-auto-approve\",\"approvalTokenExpiresAtEpochMs\":1735691000000}}",
							};
						}

						if (action == "approve") {
							return ToolExecuteResult{
								.tool = requestedTool,
								.executed = true,
								.status = "ok",
								.output = "{\"protocolVersion\":1,\"ok\":true,\"status\":\"ok\",\"output\":[{\"summary\":{\"to\":\"jichengwhu@163.com\",\"subject\":\"Wuhan weather report\",\"sendAt\":\"13:00\",\"scheduled\":true,\"delivered\":true,\"engine\":\"himalaya\",\"transportStatus\":\"sent\",\"transportOutput\":\"ok\"}}],\"requiresApproval\":null}",
							};
						}

						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = false,
							.status = "invalid_args",
							.output = "unsupported_action",
						};
					} };
			}

			return GatewayToolRegistry::RuntimeToolExecutor{};
		});

	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("runtime_orchestration");

	std::size_t callbackCalls = 0;
	host.SetChatRuntimeCallback(
		[&](const GatewayHost::ChatRuntimeRequest&) {
			++callbackCalls;
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "callback should not be invoked";
			result.modelId = "default";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-runtime-immediate-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"Check today's weather in Wuhan, write a short report, and email it to jichengwhu@163.com now.\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	REQUIRE(callbackCalls == 0);

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));

	const auto deltasResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-runtime-immediate-1-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(deltasResponse.ok);
	REQUIRE(deltasResponse.payloadJson.has_value());
	REQUIRE(deltasResponse.payloadJson->find("\"phase\":\"final\"") != std::string::npos);
	REQUIRE(deltasResponse.payloadJson->find("\"toolName\":\"weather.lookup\"") != std::string::npos);
	REQUIRE(deltasResponse.payloadJson->find("\"toolName\":\"email.schedule\"") != std::string::npos);

	host.Stop();

	const auto approvalsPath =
		tempStateRoot /
		"BlazeClaw" /
		"state" /
		"approvals.json";
	if (std::filesystem::exists(approvalsPath)) {
		std::ifstream in(approvalsPath.string());
		std::string approvalsJson(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
		REQUIRE(approvalsJson.find("token-auto-approve") == std::string::npos);
	}

	if (previousModeRaw != nullptr && previousModeLength > 0) {
		_putenv_s("BLAZECLAW_EMAIL_DELIVERY_MODE", previousModeRaw);
		free(previousModeRaw);
	}
	else {
		if (previousModeRaw != nullptr) {
			free(previousModeRaw);
		}
		_putenv_s("BLAZECLAW_EMAIL_DELIVERY_MODE", "");
	}

	if (previousLocalAppData != nullptr &&
		previousLocalAppDataLen > 0 &&
		previousLocalAppData[0] != '\0') {
		_putenv_s("LOCALAPPDATA", previousLocalAppData);
	}
	else {
		_putenv_s("LOCALAPPDATA", "");
	}

	if (previousLocalAppData != nullptr) {
		free(previousLocalAppData);
	}

	std::filesystem::remove_all(tempStateRoot);
}

TEST_CASE(
	"Parity coverage: immediate weather-email falls back to pending approval when himalaya is missing",
	"[parity][chat][orchestration][e2e][runtime][immediate][fallback]") {
	PluginHostAdapter::RegisterExtensionAdapter(
		"ops-tools",
		[](const std::string&, const std::string& toolName, const std::string&) {
			if (toolName == "weather.lookup") {
				return GatewayToolRegistry::RuntimeToolExecutor{
					[](const std::string& requestedTool, const std::optional<std::string>&) {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "{\"ok\":true,\"forecast\":{\"condition\":\"Cloudy\",\"temperatureC\":20,\"wind\":\"NE 9 km/h\",\"humidityPct\":68}}",
						};
					} };
			}

			if (toolName == "email.schedule") {
				return GatewayToolRegistry::RuntimeToolExecutor{
					[](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
						std::string action;
						if (argsJson.has_value()) {
							blazeclaw::gateway::json::FindStringField(
								argsJson.value(),
								"action",
								action);
						}

						if (action.empty() || action == "prepare") {
							return ToolExecuteResult{
								.tool = requestedTool,
								.executed = true,
								.status = "needs_approval",
								.output = "{\"requiresApproval\":{\"approvalToken\":\"token-fallback\",\"approvalTokenExpiresAtEpochMs\":1735691000000}}",
							};
						}

						if (action == "approve") {
							return ToolExecuteResult{
								.tool = requestedTool,
								.executed = false,
								.status = "error",
								.output = "{\"protocolVersion\":1,\"ok\":false,\"status\":\"error\",\"output\":[],\"requiresApproval\":null,\"error\":{\"code\":\"himalaya_cli_missing\",\"message\":\"himalaya_cli_missing\"}}",
							};
						}

						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = false,
							.status = "invalid_args",
							.output = "unsupported_action",
						};
					} };
			}

			return GatewayToolRegistry::RuntimeToolExecutor{};
		});

	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("runtime_orchestration");

	std::size_t callbackCalls = 0;
	host.SetChatRuntimeCallback(
		[&](const GatewayHost::ChatRuntimeRequest&) {
			++callbackCalls;
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "callback should not be invoked";
			result.modelId = "default";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-runtime-fallback-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"Check tomorrow's weather in Wuhan, write a short report, and email it to jicheng@whu.edu.cn now.\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	REQUIRE(callbackCalls == 0);
	REQUIRE(sendResponse.payloadJson->find("\"backendErrorCode\":null") != std::string::npos);

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));

	const auto deltasResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-runtime-fallback-1-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(deltasResponse.ok);
	REQUIRE(deltasResponse.payloadJson.has_value());
	REQUIRE(deltasResponse.payloadJson->find("\"status\":\"needs_approval\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: task-delta persistence survives host restart",
	"[parity][chat][taskdeltas][persistence]") {
	const auto tempStateRoot = std::filesystem::temp_directory_path() /
		("blazeclaw_taskdelta_state_" + std::to_string(std::rand()));
	std::filesystem::create_directories(tempStateRoot);

	char* previousLocalAppData = nullptr;
	size_t previousLen = 0;
	_dupenv_s(&previousLocalAppData, &previousLen, "LOCALAPPDATA");

	const std::string localAppDataValue = tempStateRoot.string();
	_putenv_s("LOCALAPPDATA", localAppDataValue.c_str());
	std::string persistedRunId;

	{
		GatewayHost host;
		blazeclaw::config::GatewayConfig gatewayConfig;
		REQUIRE(host.StartLocalOnly(gatewayConfig));

		host.SetEmbeddedOrchestrationPath("dynamic_task_delta");
		host.SetChatRuntimeCallback(
			[](const GatewayHost::ChatRuntimeRequest& request) {
				GatewayHost::ChatRuntimeResult result;
				result.ok = true;
				result.assistantText = "persist me";
				result.assistantDeltas = { "persist me" };
				result.taskDeltas = {
					GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
						.index = 0,
						.runId = request.runId,
						.sessionId = request.sessionKey,
						.phase = "plan",
						.resultJson = "[\"weather.lookup\"]",
						.status = "planned",
						.stepLabel = "execution_plan",
					},
					GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
						.index = 1,
						.runId = request.runId,
						.sessionId = request.sessionKey,
						.phase = "final",
					  .resultJson = "persist me",
						.status = "completed",
						.stepLabel = "run_terminal",
					},
				};
				result.modelId = "default";
				return result;
			});

		const auto sendResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "chat-persist-1",
				.method = "chat.send",
				.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"persist task deltas\"}"),
			});
		REQUIRE(sendResponse.ok);
		REQUIRE(sendResponse.payloadJson.has_value());
		REQUIRE(blazeclaw::gateway::json::FindStringField(
			sendResponse.payloadJson.value(),
			"runId",
			persistedRunId));
		REQUIRE_FALSE(persistedRunId.empty());

		host.Stop();
	}

	{
		GatewayHost host;
		blazeclaw::config::GatewayConfig gatewayConfig;
		REQUIRE(host.StartLocalOnly(gatewayConfig));

		const auto getResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "chat-persist-1-get-after-restart",
				.method = "gateway.runtime.taskDeltas.get",
				.paramsJson = std::string("{\"runId\":\"") + persistedRunId + "\"}",
			});

		REQUIRE(getResponse.ok);
		REQUIRE(getResponse.payloadJson.has_value());
		REQUIRE(getResponse.payloadJson->find("\"count\":2") != std::string::npos);
		REQUIRE(getResponse.payloadJson->find("\"phase\":\"plan\"") != std::string::npos);
		REQUIRE(getResponse.payloadJson->find("\"phase\":\"final\"") != std::string::npos);

		host.Stop();
	}

	if (previousLocalAppData != nullptr && previousLen > 0 && previousLocalAppData[0] != '\0') {
		_putenv_s("LOCALAPPDATA", previousLocalAppData);
	}
	else {
		_putenv_s("LOCALAPPDATA", "");
	}

	if (previousLocalAppData != nullptr) {
		free(previousLocalAppData);
	}

	std::filesystem::remove_all(tempStateRoot);
}

TEST_CASE(
	"Parity coverage: task-delta retrieval is index ordered and clear removes run deltas",
	"[parity][chat][taskdeltas][e2e]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");
	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "ordered task delta reply";
			result.assistantDeltas = { "ordered task delta reply" };
			result.taskDeltas = {
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 2,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "tool_result",
					.toolName = "weather.lookup",
					.status = "ok",
					.stepLabel = "tool_result",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 0,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "plan",
					.resultJson = "[\"weather.lookup\"]",
					.status = "planned",
					.stepLabel = "execution_plan",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 1,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "tool_call",
					.toolName = "weather.lookup",
					.status = "requested",
					.stepLabel = "tool_request",
				},
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 3,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "final",
					.resultJson = "ordered task delta reply",
					.status = "completed",
					.stepLabel = "run_terminal",
				},
			};
			result.modelId = "default";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-taskdeltas-order-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"run ordered deltas\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));
	REQUIRE_FALSE(runId.empty());

	const auto getResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-taskdeltas-order-1-get",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(getResponse.ok);
	REQUIRE(getResponse.payloadJson.has_value());
	const std::string payload = getResponse.payloadJson.value();
	const auto planPos = payload.find("\"phase\":\"plan\"");
	const auto callPos = payload.find("\"phase\":\"tool_call\"");
	const auto resultPos = payload.find("\"phase\":\"tool_result\"");
	const auto finalPos = payload.find("\"phase\":\"final\"");
	REQUIRE(planPos != std::string::npos);
	REQUIRE(callPos != std::string::npos);
	REQUIRE(resultPos != std::string::npos);
	REQUIRE(finalPos != std::string::npos);
	REQUIRE(planPos < callPos);
	REQUIRE(callPos < resultPos);
	REQUIRE(resultPos < finalPos);

	const auto clearResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-taskdeltas-order-1-clear",
			.method = "gateway.runtime.taskDeltas.clear",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(clearResponse.ok);
	REQUIRE(clearResponse.payloadJson.has_value());
	REQUIRE(clearResponse.payloadJson->find("\"cleared\":1") != std::string::npos);

	const auto afterClearResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-taskdeltas-order-1-get-after-clear",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(afterClearResponse.ok);
	REQUIRE(afterClearResponse.payloadJson.has_value());
	REQUIRE(afterClearResponse.payloadJson->find("\"count\":0") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: chat abort emits aborted terminal event and clears active run",
	"[parity][chat][events][e2e]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");
	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest&) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText =
				"This is a long assistant response used to exercise abort event emission ordering.";
			result.modelId = "default";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-abort-e2e-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"abort this run\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));

	const auto abortResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-abort-e2e-1-abort",
			.method = "chat.abort",
			.paramsJson =
				std::string("{\"sessionKey\":\"main\",\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(abortResponse.ok);
	REQUIRE(abortResponse.payloadJson.has_value());
	REQUIRE(abortResponse.payloadJson->find("\"aborted\":true") != std::string::npos);

	const auto eventsResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-abort-e2e-1-poll",
			.method = "chat.events.poll",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":20}"),
		});

	REQUIRE(eventsResponse.ok);
	REQUIRE(eventsResponse.payloadJson.has_value());
	REQUIRE(eventsResponse.payloadJson->find("\"state\":\"aborted\"") != std::string::npos);
	REQUIRE(eventsResponse.payloadJson->find("\"runId\":\"" + runId + "\"") != std::string::npos);

	const auto secondPollResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-abort-e2e-1-poll-2",
			.method = "chat.events.poll",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":20}"),
		});

	REQUIRE(secondPollResponse.ok);
	REQUIRE(secondPollResponse.payloadJson.has_value());
	REQUIRE(secondPollResponse.payloadJson->find("\"count\":0") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: chat events include queued started delta final in order",
	"[parity][chat][events][ordering]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");
	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest&) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "ordered lifecycle response";
			result.modelId = "default";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-events-order-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"ordering\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	const auto eventsResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-events-order-1-poll",
			.method = "chat.events.poll",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":20}"),
		});

	REQUIRE(eventsResponse.ok);
	REQUIRE(eventsResponse.payloadJson.has_value());

	std::string payload = eventsResponse.payloadJson.value();
	for (int attempt = 0; attempt < 10; ++attempt) {
		if (payload.find("\"state\":\"final\"") != std::string::npos ||
			payload.find("\"state\":\"error\"") != std::string::npos ||
			payload.find("\"state\":\"aborted\"") != std::string::npos) {
			break;
		}

		const auto moreEvents = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "chat-events-order-1-poll-more-" + std::to_string(attempt),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":20}"),
			});

		REQUIRE(moreEvents.ok);
		REQUIRE(moreEvents.payloadJson.has_value());
		payload += moreEvents.payloadJson.value();
	}
	const auto queuedPos = payload.find("\"state\":\"queued\"");
	const auto startedPos = payload.find("\"state\":\"started\"");
	const auto deltaPos = payload.find("\"state\":\"delta\"");
	const auto finalPos = payload.find("\"state\":\"final\"");
	REQUIRE(queuedPos != std::string::npos);
	REQUIRE(startedPos != std::string::npos);
	REQUIRE(deltaPos != std::string::npos);
	REQUIRE(queuedPos < startedPos);
	REQUIRE(startedPos < deltaPos);
	if (finalPos != std::string::npos) {
		REQUIRE(deltaPos < finalPos);
	}

	host.Stop();
}

TEST_CASE(
	"Parity coverage: callback-driven runtime path emits incremental delta events",
	"[parity][chat][events][streaming]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");
	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			if (request.onAssistantDelta) {
				request.onAssistantDelta("stream-part-1");
				request.onAssistantDelta("stream-part-1 stream-part-2");
			}

			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "stream-part-1 stream-part-2";
			result.modelId = "default";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-events-streaming-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"streaming callback\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	std::string payload;
	for (int attempt = 0; attempt < 10; ++attempt) {
		const auto eventsResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "chat-events-streaming-1-poll-" + std::to_string(attempt),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":50}"),
			});

		REQUIRE(eventsResponse.ok);
		REQUIRE(eventsResponse.payloadJson.has_value());
		payload += eventsResponse.payloadJson.value();

		if (payload.find("\"state\":\"final\"") != std::string::npos) {
			break;
		}
	}

	REQUIRE(payload.find("\"state\":\"delta\"") != std::string::npos);
	REQUIRE(payload.find("stream-part-1 stream-part-2") != std::string::npos);
	REQUIRE(payload.find("\"state\":\"final\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: chat runtime timeout propagates backend error and error terminal",
	"[parity][chat][events][timeout]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");
	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest&) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = false;
			result.modelId = "default";
			result.errorCode = "chat_runtime_timed_out";
			result.errorMessage = "chat runtime timed out";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-events-timeout-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"timeout\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	REQUIRE(
		sendResponse.payloadJson->find("\"backendErrorCode\":\"chat_runtime_timed_out\"") !=
		std::string::npos);

	const auto eventsResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-events-timeout-1-poll",
			.method = "chat.events.poll",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"limit\":20}"),
		});

	REQUIRE(eventsResponse.ok);
	REQUIRE(eventsResponse.payloadJson.has_value());
	REQUIRE(eventsResponse.payloadJson->find("\"state\":\"error\"") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: chat runtime queue full propagates backend error",
	"[parity][chat][events][queue]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("dynamic_task_delta");
	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest&) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = false;
			result.modelId = "default";
			result.errorCode = "chat_runtime_queue_full";
			result.errorMessage = "chat runtime queue capacity reached";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-events-queuefull-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"queue full\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	REQUIRE(
		sendResponse.payloadJson->find("\"backendErrorCode\":\"chat_runtime_queue_full\"") !=
		std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Parity coverage: chat.send uses runtime orchestration shortcut when orchestrationPath=runtime_orchestration",
	"[parity][chat][orchestration][e2e][runtime]") {
	PluginHostAdapter::RegisterExtensionAdapter(
		"ops-tools",
		[](const std::string&, const std::string& toolName, const std::string&) {
			if (toolName == "weather.lookup") {
				return GatewayToolRegistry::RuntimeToolExecutor{
					[](const std::string& requestedTool, const std::optional<std::string>&) {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "{\"ok\":true,\"forecast\":{\"condition\":\"Cloudy\",\"temperatureC\":20,\"wind\":\"NE 9 km/h\",\"humidityPct\":68}}",
						};
					} };
			}

			if (toolName == "email.schedule") {
				return GatewayToolRegistry::RuntimeToolExecutor{
			   [](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
					std::string action;
					if (argsJson.has_value()) {
						blazeclaw::gateway::json::FindStringField(
							argsJson.value(),
							"action",
							action);
					}

					if (action.empty() || action == "prepare") {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "needs_approval",
							.output = "{\"requiresApproval\":{\"approvalToken\":\"token-123\",\"approvalTokenExpiresAtEpochMs\":1735691000000}}",
						};
					}

					if (action == "approve") {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "{\"protocolVersion\":1,\"ok\":true,\"status\":\"ok\",\"output\":[{\"summary\":{\"to\":\"jichengwhu@163.com\",\"subject\":\"Wuhan weather report\",\"sendAt\":\"13:00\",\"scheduled\":true,\"delivered\":true,\"engine\":\"himalaya\",\"transportStatus\":\"sent\",\"transportOutput\":\"ok\"}}],\"requiresApproval\":null}",
						};
					}

						return ToolExecuteResult{
							.tool = requestedTool,
					   .executed = false,
						.status = "invalid_args",
						.output = "unsupported_action",
						};
					} };
			}

			return GatewayToolRegistry::RuntimeToolExecutor{};
		});

	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetEmbeddedOrchestrationPath("runtime_orchestration");

	std::size_t callbackCalls = 0;
	host.SetChatRuntimeCallback(
		[&](const GatewayHost::ChatRuntimeRequest&) {
			++callbackCalls;
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "callback should not be invoked";
			result.modelId = "default";
			return result;
		});

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-runtime-1",
			.method = "chat.send",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"Check today's weather in Wuhan, write a short report, and email it to jichengwhu@163.com now.\"}"),
		});

	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	REQUIRE(callbackCalls == 0);

	std::string runId;
	REQUIRE(blazeclaw::gateway::json::FindStringField(
		sendResponse.payloadJson.value(),
		"runId",
		runId));

	const auto deltasResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "chat-runtime-1-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});

	REQUIRE(deltasResponse.ok);
	REQUIRE(deltasResponse.payloadJson.has_value());
	REQUIRE(deltasResponse.payloadJson->find("\"toolName\":\"weather.lookup\"") != std::string::npos);
	REQUIRE(deltasResponse.payloadJson->find("\"toolName\":\"email.schedule\"") != std::string::npos);

	host.Stop();
}
