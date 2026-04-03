#include "gateway/ExtensionLifecycleManager.h"
#include "gateway/GatewayHost.h"
#include "gateway/GatewayJsonUtils.h"
#include "gateway/GatewayProtocolModels.h"
#include "gateway/GatewayToolRegistry.h"
#include "gateway/PluginHostAdapter.h"
#include "gateway/executors/EmailScheduleExecutor.h"
#include "config/ConfigModels.h"

#include <catch2/catch_all.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdlib>

using namespace blazeclaw::gateway;

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

	{
		std::ofstream out((tmpRoot / "extensions.catalog.json").string());
		out << "{\"version\":1,\"extensions\":[{\"id\":\"ops-tools\",\"path\":\"ops/blazeclaw.extension.json\",\"enabled\":true}]}";
	}

	ExtensionLifecycleManager lifecycle;
	GatewayToolRegistry registry;

	REQUIRE(lifecycle.LoadCatalog((tmpRoot / "extensions.catalog.json").string()) == 1);

	const auto activated = lifecycle.ActivateAll(registry);
	REQUIRE(activated.size() == 1);
	REQUIRE(activated.front().success);

	const auto afterActivate = registry.List();
	REQUIRE_FALSE(afterActivate.empty());
	REQUIRE(std::any_of(afterActivate.begin(), afterActivate.end(), [](const ToolCatalogEntry& t) {
		return t.id == "weather.lookup";
		}));

	const auto deactivated = lifecycle.DeactivateAll(registry);
	REQUIRE(deactivated.size() == 1);
	REQUIRE(deactivated.front().success);

	const auto afterDeactivate = registry.List();
	REQUIRE(std::none_of(afterDeactivate.begin(), afterDeactivate.end(), [](const ToolCatalogEntry& t) {
		return t.id == "weather.lookup";
		}));

	std::filesystem::remove_all(tmpRoot);
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
	REQUIRE(host.Start(gatewayConfig));

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
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"message\":\"Check today's weather in Wuhan, write a short report, and email it to jichengwhu@163.com now.\"}"),
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

	{
		GatewayHost host;
		blazeclaw::config::GatewayConfig gatewayConfig;
		REQUIRE(host.Start(gatewayConfig));

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

		host.Stop();
	}

	{
		GatewayHost host;
		blazeclaw::config::GatewayConfig gatewayConfig;
		REQUIRE(host.Start(gatewayConfig));

		const auto getResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "chat-persist-1-get-after-restart",
				.method = "gateway.runtime.taskDeltas.get",
				.paramsJson = std::string("{\"runId\":\"chat-persist-1\"}"),
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
	REQUIRE(host.Start(gatewayConfig));

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
	REQUIRE(host.Start(gatewayConfig));

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
	REQUIRE(host.Start(gatewayConfig));

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
	"Parity coverage: chat runtime timeout propagates backend error and error terminal",
	"[parity][chat][events][timeout]") {
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.Start(gatewayConfig));

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
	REQUIRE(host.Start(gatewayConfig));

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
					[](const std::string& requestedTool, const std::optional<std::string>&) {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "needs_approval",
							.output = "{\"requiresApproval\":{\"approvalToken\":\"token-123\",\"approvalTokenExpiresAtEpochMs\":1735691000000}}",
						};
					} };
			}

			return GatewayToolRegistry::RuntimeToolExecutor{};
		});

	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.Start(gatewayConfig));

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
