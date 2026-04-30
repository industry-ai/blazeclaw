#include "gateway/GatewayHost.h"
#include "gateway/GatewayJsonUtils.h"

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <fstream>
#include <filesystem>

namespace {
	class ScopedEnvVar {
	public:
		explicit ScopedEnvVar(const char* name)
			: m_name(name == nullptr ? "" : name) {
			char* raw = nullptr;
			size_t len = 0;
			_dupenv_s(&raw, &len, m_name.c_str());
			if (raw != nullptr) {
				m_previous = std::string(raw);
				free(raw);
			}
		}

		~ScopedEnvVar() {
			if (m_previous.has_value()) {
				_putenv_s(m_name.c_str(), m_previous->c_str());
				return;
			}

			_putenv_s(m_name.c_str(), "");
		}

		void Set(const std::string& value) {
			_putenv_s(m_name.c_str(), value.c_str());
		}

	private:
		std::string m_name;
		std::optional<std::string> m_previous;
	};

	struct PollTrace {
		std::vector<std::string> assistantTexts;
		std::string finalAssistantText;
		bool terminalSeen = false;
	};

	PollTrace PollChatEventsUntilTerminal(
		blazeclaw::gateway::GatewayHost& host,
		const std::string& sessionKey,
		const std::string& requestId,
		const int maxPolls) {
		PollTrace trace;

		auto appendKnownMarker = [&trace](
			const std::string& rawPayload,
			const std::string& marker) {
				if (rawPayload.find(marker) == std::string::npos) {
					return;
				}

				if (std::find(
					trace.assistantTexts.begin(),
					trace.assistantTexts.end(),
					marker) != trace.assistantTexts.end()) {
					return;
				}

				trace.assistantTexts.push_back(marker);
			};

		for (int poll = 0; poll < maxPolls; ++poll) {
			if (poll > 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}

			const auto pollResponse = host.RouteRequest(
				blazeclaw::gateway::protocol::RequestFrame{
					.id = requestId + "-poll-" + std::to_string(poll),
					.method = "chat.events.poll",
					.paramsJson =
						std::string("{\"sessionKey\":\"") +
						sessionKey +
						"\",\"limit\":30}",
				});
			REQUIRE(pollResponse.ok);
			REQUIRE(pollResponse.payloadJson.has_value());

			const auto& rawPayload = pollResponse.payloadJson.value();
			appendKnownMarker(
				rawPayload,
				"tools.execute.result tool=weather.lookup status=ok");
			appendKnownMarker(
				rawPayload,
				"tools.execute.result tool=email.schedule status=needs_approval");
			appendKnownMarker(
				rawPayload,
				"tools.execute.result tool=email.schedule status=ok");
			appendKnownMarker(
				rawPayload,
				"tools.execute.result tool=email.schedule status=invalid_args");
			appendKnownMarker(rawPayload, "baidu_search_python");
			if (rawPayload.find("\"state\":\"final\"") != std::string::npos ||
				rawPayload.find("\"state\":\"error\"") != std::string::npos ||
				rawPayload.find("\"state\":\"aborted\"") != std::string::npos ||
				rawPayload.find("\"state\":\"needs_approval\"") != std::string::npos) {
				trace.terminalSeen = true;
			}

			nlohmann::json payload;
			try {
				payload = nlohmann::json::parse(
					rawPayload);
			}
			catch (...) {
				continue;
			}
			if (!payload.contains("events") ||
				!payload["events"].is_array()) {
				continue;
			}

			for (const auto& event : payload["events"]) {
				if (!event.is_object()) {
					continue;
				}

				const std::string state = event.value("state", std::string{});
				if (event.contains("message") &&
					event["message"].is_object() &&
					event["message"].contains("text") &&
					event["message"]["text"].is_string()) {
					const std::string text =
						event["message"]["text"].get<std::string>();
					trace.assistantTexts.push_back(text);
					if (state == "final" || state == "needs_approval") {
						trace.finalAssistantText = text;
					}
				}

				if (state == "final" || state == "error" || state == "aborted" || state == "needs_approval") {
					trace.terminalSeen = true;
				}
			}

			if (trace.terminalSeen) {
				break;
			}
		}

		return trace;
	}

	int CountExactMatch(
		const std::vector<std::string>& values,
		const std::string& expected) {
		int count = 0;
		for (const auto& value : values) {
			if (value == expected) {
				++count;
			}
		}

		return count;
	}

	bool ContainsExactMatch(
		const std::vector<std::string>& values,
		const std::string& expected) {
		for (const auto& value : values) {
			if (value == expected) {
				return true;
			}
		}

		return false;
	}

	nlohmann::json GetOrchestrationStatus(
		blazeclaw::gateway::GatewayHost& host,
		const std::string& requestId) {
		const auto statusResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = requestId,
				.method = "gateway.runtime.orchestration.status",
				.paramsJson = std::string("{}"),
			});
		REQUIRE(statusResponse.ok);
		REQUIRE(statusResponse.payloadJson.has_value());
		return nlohmann::json::parse(statusResponse.payloadJson.value());
	}

	void AssertParityOrchestrationDecision(
		const nlohmann::json& orchestrationStatus,
		const std::string& expectedReasonCode) {
		REQUIRE(orchestrationStatus.contains("orchestrationPath"));
		REQUIRE(orchestrationStatus["orchestrationPath"].is_object());

		const auto& path = orchestrationStatus["orchestrationPath"];
		REQUIRE(path.contains("decisionReasonCode"));
		REQUIRE(path["decisionReasonCode"].is_string());
		REQUIRE(path["decisionReasonCode"].get<std::string>() == expectedReasonCode);

		REQUIRE(path.contains("decompositionMetadataSource"));
		REQUIRE(path["decompositionMetadataSource"].is_string());
		REQUIRE(
			path["decompositionMetadataSource"].get<std::string>() ==
			"structural_orchestration_signals");
	}

	bool ContainsSubstring(
		const std::vector<std::string>& values,
		const std::string& expectedFragment) {
		for (const auto& value : values) {
			if (value.find(expectedFragment) != std::string::npos) {
				return true;
			}
		}

		return false;
	}

	std::string ReadFileText(const std::filesystem::path& path) {
		std::ifstream input(path, std::ios::binary);
		if (!input.is_open()) {
			return {};
		}
		return std::string(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
	}

}

TEST_CASE(
	"Weather-email immediate prompt does not emit duplicate email.schedule invalid_args",
	"[gateway][weather-email][email-schedule][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.StartLocalOnly(config));

	const std::string sessionKey = "weather-email-regression";
	const std::string requestId = "weather-email-regression-run";
	const std::string prompt =
		"Check tomorrow's weather in Wuhan, write a short report, and email it to "
		"jicheng@whu.edu.cn now.";
	const std::string sendPayload =
		std::string("{\"sessionKey\":\"") +
		sessionKey +
		"\",\"message\":\"" +
		prompt +
		"\",\"idempotencyKey\":\"weather-email-regression-idem\","
		"\"hasConnectedClient\":true,\"clientConnectionId\":\"test-conn-1\","
		"\"clientCaps\":[\"TOOL_EVENTS\"]}";

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = requestId,
			.method = "chat.send",
			.paramsJson = sendPayload,
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	const auto pollTrace = PollChatEventsUntilTerminal(
		host,
		sessionKey,
		requestId,
		60);

	const int invalidArgsCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=invalid_args");
	REQUIRE(invalidArgsCount == 0);

	const int weatherOkCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=weather.lookup status=ok");
	REQUIRE(weatherOkCount >= 1);

	const int schedulePrepareCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=needs_approval");
	REQUIRE(schedulePrepareCount >= 1);

	const int scheduleOkCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=ok");
	REQUIRE(scheduleOkCount >= 1);

	host.Stop();
}

TEST_CASE(
	"Weather lookup requires explicit city to match OpenClaw parity",
	"[gateway][weather-email][weather-lookup][city-required][regression]") {
	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.StartLocalOnly(config));

	const auto executeResult = host.ExecuteRuntimeTool(
		"weather.lookup",
		std::string("{\"date\":\"tomorrow\"}"));

	REQUIRE(executeResult.executed == false);
	REQUIRE(executeResult.status == "invalid_args");
	REQUIRE(executeResult.output.find("city_required") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Strict ordered preflight missing target emits visible error and preflight task-delta diagnostics",
	"[gateway][ordered-preflight][missing-target][regression]") {
	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.StartLocalOnly(config));

	const std::string sessionKey = "ordered-preflight-missing-target";
	const std::string requestId = "ordered-preflight-missing-target-run";
	const std::string prompt =
		"Call weather.lookup first, then call missing.tool.now.";
	const std::string sendPayload =
		std::string("{\"sessionKey\":\"") +
		sessionKey +
		"\",\"message\":\"" +
		prompt +
		"\",\"idempotencyKey\":\"ordered-preflight-missing-target-idem\"}";

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = requestId,
			.method = "chat.send",
			.paramsJson = sendPayload,
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	auto sendPayloadJson = nlohmann::json::parse(sendResponse.payloadJson.value());
	REQUIRE(sendPayloadJson.contains("runId"));
	REQUIRE(sendPayloadJson["runId"].is_string());
	const std::string runId = sendPayloadJson["runId"].get<std::string>();
	REQUIRE_FALSE(runId.empty());

	bool sawErrorTerminal = false;
	std::string terminalErrorCode;
	std::string terminalErrorMessage;
	std::string terminalVisibleText;
	for (int poll = 0; poll < 40; ++poll) {
		if (poll > 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}

		const auto pollResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = requestId + "-poll-" + std::to_string(poll),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"") +
					sessionKey +
					"\",\"limit\":40}",
			});
		REQUIRE(pollResponse.ok);
		REQUIRE(pollResponse.payloadJson.has_value());

		nlohmann::json pollPayload;
		try {
			pollPayload = nlohmann::json::parse(pollResponse.payloadJson.value());
		}
		catch (...) {
			continue;
		}
		if (!pollPayload.contains("events") ||
			!pollPayload["events"].is_array()) {
			continue;
		}

		for (const auto& event : pollPayload["events"]) {
			if (!event.is_object()) {
				continue;
			}

			if (event.value("state", std::string{}) != "error") {
				continue;
			}

			sawErrorTerminal = true;
			terminalErrorCode = event.value("errorCode", std::string{});
			terminalErrorMessage = event.value("errorMessage", std::string{});
			if (event.contains("message") &&
				event["message"].is_object() &&
				event["message"].contains("text") &&
				event["message"]["text"].is_string()) {
				terminalVisibleText = event["message"]["text"].get<std::string>();
			}
			break;
		}

		if (sawErrorTerminal) {
			break;
		}
	}

	REQUIRE(sawErrorTerminal);
	REQUIRE(terminalErrorCode == "ordered_sequence_target_unavailable");
	REQUIRE_FALSE(terminalErrorMessage.empty());
	REQUIRE_FALSE(terminalVisibleText.empty());
	REQUIRE(terminalVisibleText.find("Unable to execute the strict ordered workflow") != std::string::npos);

	const auto taskDeltaResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = requestId + "-task-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});
	REQUIRE(taskDeltaResponse.ok);
	REQUIRE(taskDeltaResponse.payloadJson.has_value());

	auto taskDeltaPayload = nlohmann::json::parse(taskDeltaResponse.payloadJson.value());
	REQUIRE(taskDeltaPayload.contains("taskDeltas"));
	REQUIRE(taskDeltaPayload["taskDeltas"].is_array());

	bool hasPreflight = false;
	bool hasFinal = false;
	bool hasToolResult = false;
	bool hasMissingPreflight = false;
	for (const auto& delta : taskDeltaPayload["taskDeltas"]) {
		if (!delta.is_object()) {
			continue;
		}

		const std::string phase = delta.value("phase", std::string{});
		if (phase == "preflight") {
			hasPreflight = true;
			if (delta.value("status", std::string{}) == "missing" ||
				delta.value("errorCode", std::string{}) == "step_target_unavailable") {
				hasMissingPreflight = true;
			}
		}
		if (phase == "final") {
			hasFinal = true;
		}
		if (phase == "tool_result") {
			hasToolResult = true;
		}
	}

	REQUIRE(hasPreflight);
	REQUIRE(hasFinal);
	REQUIRE(hasMissingPreflight);
	REQUIRE_FALSE(hasToolResult);

	host.Stop();
}

TEST_CASE(
	"Weather-email explicit location extraction keeps deterministic parity path",
	"[gateway][weather-email][email-schedule][explicit-location][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.StartLocalOnly(config));

	const std::string sessionKey = "weather-email-explicit-location-regression";
	const std::string requestId = "weather-email-explicit-location-regression-run";
	const std::string prompt =
		"Check tomorrow's weather in Beijing, write a short report, and email it to jicheng@whu.edu.cn now.";
	const std::string sendPayload =
		std::string("{\"sessionKey\":\"") +
		sessionKey +
		"\",\"message\":\"" +
		prompt +
		"\",\"idempotencyKey\":\"weather-email-explicit-location-idem\","
		"\"hasConnectedClient\":true,\"clientConnectionId\":\"test-conn-city\","
		"\"clientCaps\":[\"TOOL_EVENTS\"]}";

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = requestId,
			.method = "chat.send",
			.paramsJson = sendPayload,
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	const auto pollTrace = PollChatEventsUntilTerminal(
		host,
		sessionKey,
		requestId,
		60);

	REQUIRE(ContainsExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=weather.lookup status=ok"));
	REQUIRE(ContainsExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=needs_approval"));
	REQUIRE(CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=invalid_args") == 0);

	const auto orchestrationStatus =
		GetOrchestrationStatus(host, requestId + "-orchestration-status");
	AssertParityOrchestrationDecision(
		orchestrationStatus,
		"policy.deterministic.intent_override");

	host.Stop();
}

TEST_CASE(
	"Weather-email Chinese prompt follows deterministic parity path without search fallback",
	"[gateway][weather-email][email-schedule][chinese][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.StartLocalOnly(config));

	const std::string sessionKey = "weather-email-chinese-regression";
	const std::string requestId = "weather-email-chinese-regression-run";
	const std::string prompt =
		"查一下明天武汉的天气，写一个简短的报告，用电子邮件发送给 jicheng@whu.edu.cn 现在";
	const std::string sendPayload =
		std::string("{\"sessionKey\":\"") +
		sessionKey +
		"\",\"message\":\"" +
		prompt +
		"\",\"idempotencyKey\":\"weather-email-chinese-idem\","
		"\"hasConnectedClient\":true,\"clientConnectionId\":\"test-conn-zh\","
		"\"clientCaps\":[\"TOOL_EVENTS\"]}";

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = requestId,
			.method = "chat.send",
			.paramsJson = sendPayload,
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	const auto pollTrace = PollChatEventsUntilTerminal(
		host,
		sessionKey,
		requestId,
		60);

	REQUIRE(ContainsExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=weather.lookup status=ok"));
	REQUIRE(ContainsExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=needs_approval"));
	const bool emailTerminalObservedZh =
		ContainsExactMatch(
			pollTrace.assistantTexts,
			"tools.execute.result tool=email.schedule status=ok") ||
		ContainsExactMatch(
			pollTrace.assistantTexts,
			"tools.execute.result tool=email.schedule status=needs_approval");
	REQUIRE(emailTerminalObservedZh);
	REQUIRE(CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=invalid_args") == 0);
	REQUIRE(!ContainsSubstring(
		pollTrace.assistantTexts,
		"method_not_implemented"));
	REQUIRE(!ContainsSubstring(
		pollTrace.assistantTexts,
		"Provider unavailable (fallback estimate)"));
	REQUIRE(ContainsSubstring(
		pollTrace.assistantTexts,
		"orchestration.intent city=武汉 date=tomorrow source=structural_orchestration_signals"));
	REQUIRE(pollTrace.finalAssistantText.find("Provider unavailable (fallback estimate)") == std::string::npos);

	for (const auto& delta : pollTrace.assistantTexts) {
		REQUIRE(delta.find("baidu_search_python") == std::string::npos);
	}

	const auto orchestrationStatus =
		GetOrchestrationStatus(host, requestId + "-orchestration-status");
	AssertParityOrchestrationDecision(
		orchestrationStatus,
		"policy.deterministic.intent_override");

	host.Stop();
}

TEST_CASE(
	"Weather-email bilingual prompt follows deterministic parity path without search fallback",
	"[gateway][weather-email][email-schedule][bilingual][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.StartLocalOnly(config));

	const std::string sessionKey = "weather-email-bilingual-regression";
	const std::string requestId = "weather-email-bilingual-regression-run";
	const std::string prompt =
		"请 check tomorrow Wuhan weather，写一个 short report，并 email 给 jicheng@whu.edu.cn now";
	const std::string sendPayload =
		std::string("{\"sessionKey\":\"") +
		sessionKey +
		"\",\"message\":\"" +
		prompt +
		"\",\"idempotencyKey\":\"weather-email-bilingual-idem\","
		"\"hasConnectedClient\":true,\"clientConnectionId\":\"test-conn-bi\","
		"\"clientCaps\":[\"TOOL_EVENTS\"]}";

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = requestId,
			.method = "chat.send",
			.paramsJson = sendPayload,
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	const auto pollTrace = PollChatEventsUntilTerminal(
		host,
		sessionKey,
		requestId,
		60);

	REQUIRE(ContainsExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=weather.lookup status=ok"));
	REQUIRE(ContainsExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=needs_approval"));
	const bool emailTerminalObservedBi =
		ContainsExactMatch(
			pollTrace.assistantTexts,
			"tools.execute.result tool=email.schedule status=ok") ||
		ContainsExactMatch(
			pollTrace.assistantTexts,
			"tools.execute.result tool=email.schedule status=needs_approval");
	REQUIRE(emailTerminalObservedBi);
	REQUIRE(CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=invalid_args") == 0);

	for (const auto& delta : pollTrace.assistantTexts) {
		REQUIRE(delta.find("baidu_search_python") == std::string::npos);
	}

	const auto orchestrationStatus =
		GetOrchestrationStatus(host, requestId + "-orchestration-status");
	AssertParityOrchestrationDecision(
		orchestrationStatus,
		"policy.deterministic.intent_override");

	host.Stop();
}

TEST_CASE(
	"Weather-email immediate prompt reaches imap-smtp-email backend when fallback prerequisites exist",
	"[gateway][weather-email][email-schedule][imap-smtp][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.StartLocalOnly(config));

	const std::string sessionKey = "weather-email-imap-smtp-reachability";
	const std::string requestId = "weather-email-imap-smtp-reachability-run";
	const std::string prompt =
		"Check tomorrow's weather in Wuhan, write a short report, and email it to "
		"jicheng@whu.edu.cn now.";
	const std::string sendPayload =
		std::string("{\"sessionKey\":\"") +
		sessionKey +
		"\",\"message\":\"" +
		prompt +
		"\",\"idempotencyKey\":\"weather-email-imap-smtp-idem\","
		"\"hasConnectedClient\":true,\"clientConnectionId\":\"test-conn-2\","
		"\"clientCaps\":[\"TOOL_EVENTS\"]}";

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = requestId,
			.method = "chat.send",
			.paramsJson = sendPayload,
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());

	const auto pollTrace = PollChatEventsUntilTerminal(
		host,
		sessionKey,
		requestId,
		60);

	const int weatherOkCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=weather.lookup status=ok");
	REQUIRE(weatherOkCount >= 1);

	const int schedulePrepareCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=needs_approval");
	REQUIRE(schedulePrepareCount >= 1);

	const int scheduleApproveCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=ok");
	REQUIRE(scheduleApproveCount >= 1);

	const int invalidArgsCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=invalid_args");
	REQUIRE(invalidArgsCount == 0);

	host.Stop();
}

TEST_CASE(
	"Weather-email approval-required flow emits needs_approval terminal with structured metadata",
	"[gateway][weather-email][approval][terminal][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_failure");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.StartLocalOnly(config));

	const std::string sessionKey = "weather-email-approval-terminal";
	const std::string requestId = "weather-email-approval-terminal-run";
	const std::string prompt =
		"Check tomorrow's weather in Shanghai, write a short report, and email it to "
		"jichengwhu@163.com now.";
	const std::string sendPayload =
		std::string("{\"sessionKey\":\"") +
		sessionKey +
		"\",\"message\":\"" +
		prompt +
		"\",\"idempotencyKey\":\"weather-email-approval-terminal-idem\","
		"\"hasConnectedClient\":true,\"clientConnectionId\":\"test-conn-approval\","
		"\"clientCaps\":[\"TOOL_EVENTS\"]}";

	const auto sendResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = requestId,
			.method = "chat.send",
			.paramsJson = sendPayload,
		});
	REQUIRE(sendResponse.ok);
	REQUIRE(sendResponse.payloadJson.has_value());
	const auto sendPayloadJson = nlohmann::json::parse(sendResponse.payloadJson.value());
	REQUIRE(sendPayloadJson.contains("runId"));
	const std::string runId = sendPayloadJson.value("runId", std::string{});
	REQUIRE_FALSE(runId.empty());

	bool sawNeedsApproval = false;
	std::string approvalToken;
	std::string approvalNextAction;
	std::string terminalReason;
	bool approvalRequired = false;
	bool sawAssistantMessage = false;
	for (int poll = 0; poll < 50; ++poll) {
		if (poll > 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}

		const auto pollResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = requestId + "-poll-" + std::to_string(poll),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"") +
					sessionKey +
					"\",\"limit\":40}",
			});
		REQUIRE(pollResponse.ok);
		REQUIRE(pollResponse.payloadJson.has_value());

		nlohmann::json pollPayload;
		try {
			pollPayload = nlohmann::json::parse(pollResponse.payloadJson.value());
		}
		catch (...) {
			continue;
		}
		if (!pollPayload.contains("events") || !pollPayload["events"].is_array()) {
			continue;
		}

		for (const auto& event : pollPayload["events"]) {
			if (!event.is_object()) {
				continue;
			}

			if (event.value("state", std::string{}) != "needs_approval") {
				continue;
			}

			sawNeedsApproval = true;
			approvalRequired = event.value("approvalRequired", false);
			approvalToken = event.value("approvalToken", std::string{});
			approvalNextAction = event.value("approvalNextAction", std::string{});
			terminalReason = event.value("terminalReason", std::string{});
			if (event.contains("message") &&
				event["message"].is_object() &&
				event["message"].contains("text") &&
				event["message"]["text"].is_string()) {
				sawAssistantMessage = !event["message"]["text"].get<std::string>().empty();
			}
			break;
		}

		if (sawNeedsApproval) {
			break;
		}
	}

	REQUIRE(sawNeedsApproval);
	REQUIRE(approvalRequired);
	REQUIRE_FALSE(approvalToken.empty());
	REQUIRE(approvalNextAction == "email.schedule.approve");
	REQUIRE((terminalReason == "approval_required" || terminalReason == "fallback_backend_unavailable"));
	REQUIRE(sawAssistantMessage);

	const auto taskDeltaResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = requestId + "-task-deltas",
			.method = "gateway.runtime.taskDeltas.get",
			.paramsJson = std::string("{\"runId\":\"") + runId + "\"}",
		});
	REQUIRE(taskDeltaResponse.ok);
	REQUIRE(taskDeltaResponse.payloadJson.has_value());
	const auto taskDeltaPayload = nlohmann::json::parse(taskDeltaResponse.payloadJson.value());
	REQUIRE(taskDeltaPayload.contains("taskDeltas"));
	REQUIRE(taskDeltaPayload["taskDeltas"].is_array());
	bool finalNeedsApproval = false;
	for (const auto& delta : taskDeltaPayload["taskDeltas"]) {
		if (!delta.is_object()) {
			continue;
		}

		if (delta.value("phase", std::string{}) == "final" &&
			delta.value("status", std::string{}) == "needs_approval") {
			finalNeedsApproval = true;
			break;
		}
	}
	REQUIRE(finalNeedsApproval);

	host.Stop();
}

TEST_CASE(
	"Dispatch-only email approval failure returns structured remediation for missing backend",
	"[gateway][weather-email][email-schedule][approval][remediation][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");
	ScopedEnvVar localAppData("LOCALAPPDATA");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_failure");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");
	const std::filesystem::path tempStateRoot = std::filesystem::temp_directory_path() /
		("blazeclaw_approval_remediation_" + std::to_string(std::rand()));
	std::filesystem::create_directories(tempStateRoot);
	localAppData.Set(tempStateRoot.string());

	blazeclaw::gateway::GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto readinessResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "approval-remediation-readiness",
			.method = "gateway.email.backend.readiness",
			.paramsJson = "{}",
		});
	REQUIRE(readinessResponse.ok);
	REQUIRE(readinessResponse.payloadJson.has_value());
	const auto readinessPayload = nlohmann::json::parse(readinessResponse.payloadJson.value());
	REQUIRE(readinessPayload.value("tool", std::string{}) == "email.schedule");
	REQUIRE(readinessPayload.contains("ready"));
	REQUIRE(readinessPayload["ready"].is_boolean());
	REQUIRE(readinessPayload.value("ready", true) == false);
	REQUIRE(readinessPayload.contains("errorCode"));
	REQUIRE(readinessPayload.contains("remediation"));
	REQUIRE(readinessPayload.contains("missingDependency"));
	REQUIRE(readinessPayload.contains("installHint"));
	REQUIRE(readinessPayload.contains("configHint"));

	const auto prepareResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "approval-remediation-prepare",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"prepare\"," +
				"\"to\":\"jicheng@whu.edu.cn\"," +
				"\"subject\":\"Approval remediation test\"," +
				"\"body\":\"Approval remediation test body\"," +
				"\"sendAt\":\"13:00\"}}",
		});
	REQUIRE(prepareResponse.ok);
	REQUIRE(prepareResponse.payloadJson.has_value());
	const auto preparePayload = nlohmann::json::parse(prepareResponse.payloadJson.value());
	REQUIRE(preparePayload["output"].is_string());
	const auto prepareOutput = nlohmann::json::parse(preparePayload["output"].get<std::string>());
	REQUIRE(prepareOutput.contains("requiresApproval"));
	const std::string approvalToken =
		prepareOutput["requiresApproval"]["approvalToken"].get<std::string>();
	REQUIRE_FALSE(approvalToken.empty());

	const auto approveResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "approval-remediation-approve",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"approve\"," +
				"\"approvalToken\":\"" + approvalToken + "\"," +
				"\"approve\":true}}",
		});
	REQUIRE(approveResponse.ok);
	REQUIRE(approveResponse.payloadJson.has_value());
	const auto approvePayload = nlohmann::json::parse(approveResponse.payloadJson.value());
	REQUIRE(approvePayload["status"].get<std::string>() == "error");
	REQUIRE(approvePayload.contains("errorCode"));
	const std::string errorCode = approvePayload.value("errorCode", std::string{});
	REQUIRE((errorCode == "imap_smtp_skill_missing" || errorCode == "email_backend_unavailable"));
	REQUIRE(errorCode != "legacy_execution_failed");
	REQUIRE(approvePayload["output"].is_string());
	const auto approveOutput = nlohmann::json::parse(approvePayload["output"].get<std::string>());
	REQUIRE(approveOutput.contains("error"));
	REQUIRE(approveOutput["error"].is_object());
	const auto& errorObj = approveOutput["error"];
	REQUIRE(errorObj.contains("remediation"));
	REQUIRE(errorObj.contains("missingDependency"));
	REQUIRE(errorObj.contains("installHint"));
	REQUIRE(errorObj.contains("configHint"));
	REQUIRE(errorObj.contains("bucket"));
	REQUIRE_FALSE(errorObj.value("remediation", std::string{}).empty());
	REQUIRE_FALSE(errorObj.value("bucket", std::string{}).empty());

	host.Stop();
}

TEST_CASE(
	"Dispatch-only email approval retry succeeds after backend readiness is restored",
	"[gateway][weather-email][email-schedule][approval][retry][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");
	ScopedEnvVar localAppData("LOCALAPPDATA");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_failure");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");
	const std::filesystem::path tempStateRoot = std::filesystem::temp_directory_path() /
		("blazeclaw_approval_retry_" + std::to_string(std::rand()));
	std::filesystem::create_directories(tempStateRoot);
	localAppData.Set(tempStateRoot.string());

	blazeclaw::gateway::GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto prepareResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "approval-retry-prepare",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"prepare\"," +
				"\"to\":\"jicheng@whu.edu.cn\"," +
				"\"subject\":\"Approval retry test\"," +
				"\"body\":\"Approval retry test body\"," +
				"\"sendAt\":\"13:00\"}}",
		});
	REQUIRE(prepareResponse.ok);
	REQUIRE(prepareResponse.payloadJson.has_value());
	const auto preparePayload = nlohmann::json::parse(prepareResponse.payloadJson.value());
	const auto prepareOutput = nlohmann::json::parse(preparePayload["output"].get<std::string>());
	const std::string approvalToken =
		prepareOutput["requiresApproval"]["approvalToken"].get<std::string>();
	REQUIRE_FALSE(approvalToken.empty());

	const auto firstApproveResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "approval-retry-first-approve",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"approve\"," +
				"\"approvalToken\":\"" + approvalToken + "\"," +
				"\"approve\":true}}",
		});
	REQUIRE(firstApproveResponse.ok);
	REQUIRE(firstApproveResponse.payloadJson.has_value());
	const auto firstApprovePayload = nlohmann::json::parse(firstApproveResponse.payloadJson.value());
	REQUIRE(firstApprovePayload["status"].get<std::string>() == "error");

	imapModeEnv.Set("mock_success");

	const auto retryApproveResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "approval-retry-second-approve",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"approve\"," +
				"\"approvalToken\":\"" + approvalToken + "\"," +
				"\"approve\":true}}",
		});
	REQUIRE(retryApproveResponse.ok);
	REQUIRE(retryApproveResponse.payloadJson.has_value());
	const auto retryApprovePayload = nlohmann::json::parse(retryApproveResponse.payloadJson.value());
	REQUIRE(retryApprovePayload["status"].get<std::string>() == "ok");
	REQUIRE(retryApprovePayload["output"].is_string());
	REQUIRE(retryApprovePayload["output"].get<std::string>().find("approval_token_invalid") == std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Chinese weather-email parser extracts normalized city and date deterministically",
	"[gateway][weather-email][parser][chinese][regression]") {
	const auto intentTomorrow = blazeclaw::gateway::prompt::AnalyzeWeatherEmailPromptIntent(
		"查一下明天武汉的天气，写一个简短的报告，用电子邮件发送给 jicheng@whu.edu.cn");
	REQUIRE(intentTomorrow.city == "武汉");
	REQUIRE(intentTomorrow.date == "tomorrow");

	const auto intentToday = blazeclaw::gateway::prompt::AnalyzeWeatherEmailPromptIntent(
		"查一下今天北京天气，写一个简短报告并发送邮件给 jicheng@whu.edu.cn");
	REQUIRE(intentToday.city == "北京");
	REQUIRE(intentToday.date == "today");

	const auto intentShenzhen = blazeclaw::gateway::prompt::AnalyzeWeatherEmailPromptIntent(
		"在深圳查天气并发邮件给 jicheng@whu.edu.cn");
	REQUIRE(intentShenzhen.city == "深圳");

	const auto intentShanghai = blazeclaw::gateway::prompt::AnalyzeWeatherEmailPromptIntent(
		"查一下明天上海的天气，写一个简短的报告，用电子邮件发送给 jicheng@whu.edu.cn");
	REQUIRE(intentShanghai.city == "上海");
	REQUIRE(intentShanghai.date == "tomorrow");
	REQUIRE(intentShanghai.hasSchedule == true);
	REQUIRE(intentShanghai.scheduleKind == "immediate_keyword");
	REQUIRE(intentShanghai.sendAt != "13:00");

	const auto intentDeferred = blazeclaw::gateway::prompt::AnalyzeWeatherEmailPromptIntent(
		"查一下明天上海的天气，写一个简短的报告，稍后发送给 jicheng@whu.edu.cn");
	REQUIRE(intentDeferred.city == "上海");
	REQUIRE(intentDeferred.scheduleKind == "default_fallback");
	REQUIRE(intentDeferred.sendAt == "13:00");
}

TEST_CASE(
	"Runtime-dispatch-only mode executes email approval through gateway.tools.call.execute",
	"[gateway][weather-email][email-schedule][dispatch-only][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");
	ScopedEnvVar localAppData("LOCALAPPDATA");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");
	const std::filesystem::path tempStateRoot = std::filesystem::temp_directory_path() /
		("blazeclaw_dispatch_only_" + std::to_string(std::rand()));
	std::filesystem::create_directories(tempStateRoot);
	localAppData.Set(tempStateRoot.string());

	blazeclaw::gateway::GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto prepareResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "dispatch-only-prepare",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"prepare\"," +
				"\"to\":\"jicheng@whu.edu.cn\"," +
				"\"subject\":\"Dispatch-only approval test\"," +
				"\"body\":\"Dispatch-only approval test body\"," +
				"\"sendAt\":\"13:00\"}}",
		});
	REQUIRE(prepareResponse.ok);
	REQUIRE(prepareResponse.payloadJson.has_value());

	auto preparePayload = nlohmann::json::parse(prepareResponse.payloadJson.value());
	REQUIRE(preparePayload["tool"].get<std::string>() == "email.schedule");
	REQUIRE(preparePayload["status"].get<std::string>() == "needs_approval");
	REQUIRE(preparePayload["output"].is_string());
	REQUIRE(preparePayload["output"].get<std::string>().find("approvalToken") != std::string::npos);
	REQUIRE(preparePayload["output"].get<std::string>().find("method_not_implemented") == std::string::npos);

	auto prepareOutput = nlohmann::json::parse(preparePayload["output"].get<std::string>());
	REQUIRE(prepareOutput.contains("requiresApproval"));
	const std::string approvalToken =
		prepareOutput["requiresApproval"]["approvalToken"].get<std::string>();
	REQUIRE(!approvalToken.empty());
	const std::filesystem::path approvalsPath =
		tempStateRoot / "BlazeClaw" / "state" / "approvals.json";
	const std::string approvalsRaw = ReadFileText(approvalsPath);
	REQUIRE(!approvalsRaw.empty());
	REQUIRE(approvalsRaw.find(approvalToken) != std::string::npos);

	const auto approveResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "dispatch-only-approve",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"approve\"," +
				"\"approvalToken\":\"" + approvalToken + "\"," +
				"\"approve\":true}}",
		});
	REQUIRE(approveResponse.ok);
	REQUIRE(approveResponse.payloadJson.has_value());

	auto approvePayload = nlohmann::json::parse(approveResponse.payloadJson.value());
	REQUIRE(approvePayload["tool"].get<std::string>() == "email.schedule");
	const std::string approveStatus = approvePayload["status"].get<std::string>();
	REQUIRE((approveStatus == "ok" || approveStatus == "needs_approval"));
	REQUIRE(approvePayload["output"].is_string());
	REQUIRE(approvePayload["output"].get<std::string>().find("method_not_implemented") == std::string::npos);

	const auto reuseApproveResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "dispatch-only-approve-reuse",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"approve\"," +
				"\"approvalToken\":\"" + approvalToken + "\"," +
				"\"approve\":true}}",
		});
	REQUIRE(reuseApproveResponse.ok);
	REQUIRE(reuseApproveResponse.payloadJson.has_value());
	auto reuseApprovePayload = nlohmann::json::parse(reuseApproveResponse.payloadJson.value());
	REQUIRE(reuseApprovePayload["status"].get<std::string>() == "invalid_args");
	REQUIRE(reuseApprovePayload["output"].is_string());
	REQUIRE(reuseApprovePayload["output"].get<std::string>().find("approval_token_invalid") != std::string::npos);

	const auto malformedApproveResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "dispatch-only-approve-malformed",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"approve\"," +
				"\"approvalToken\":\"email-app\"," +
				"\"approve\":true}}",
		});
	REQUIRE(malformedApproveResponse.ok);
	REQUIRE(malformedApproveResponse.payloadJson.has_value());
	auto malformedApprovePayload = nlohmann::json::parse(malformedApproveResponse.payloadJson.value());
	REQUIRE(malformedApprovePayload["status"].get<std::string>() == "invalid_args");
	REQUIRE(malformedApprovePayload["output"].is_string());
	REQUIRE(malformedApprovePayload["output"].get<std::string>().find("approval_token_invalid") != std::string::npos);

	host.Stop();
}

TEST_CASE(
	"Dispatch-only approve remaps actionable code for argument-shape variants",
	"[gateway][weather-email][email-schedule][approval][args-shape][regression]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");
	ScopedEnvVar localAppData("LOCALAPPDATA");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_failure");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");
	const std::filesystem::path tempStateRoot = std::filesystem::temp_directory_path() /
		("blazeclaw_approval_args_shape_" + std::to_string(std::rand()));
	std::filesystem::create_directories(tempStateRoot);
	localAppData.Set(tempStateRoot.string());

	blazeclaw::gateway::GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto prepareResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "approval-args-shape-prepare",
			.method = "gateway.tools.call.execute",
			.paramsJson =
				std::string("{\"tool\":\"email.schedule\",\"args\":{") +
				"\"action\":\"prepare\"," +
				"\"to\":\"jicheng@whu.edu.cn\"," +
				"\"subject\":\"Approval args shape test\"," +
				"\"body\":\"Approval args shape test body\"," +
				"\"sendAt\":\"13:00\"}}",
		});
	REQUIRE(prepareResponse.ok);
	REQUIRE(prepareResponse.payloadJson.has_value());
	const auto preparePayload = nlohmann::json::parse(prepareResponse.payloadJson.value());
	const auto prepareOutput = nlohmann::json::parse(preparePayload["output"].get<std::string>());
	const std::string approvalToken =
		prepareOutput["requiresApproval"]["approvalToken"].get<std::string>();
	REQUIRE_FALSE(approvalToken.empty());

	auto assertMappedApprovalFailure = [&](const std::string& paramsJson, const std::string& reqId) {
		const auto approveResponse = host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = reqId,
				.method = "gateway.tools.call.execute",
				.paramsJson = paramsJson,
			});
		REQUIRE(approveResponse.ok);
		REQUIRE(approveResponse.payloadJson.has_value());
		const auto approvePayload = nlohmann::json::parse(approveResponse.payloadJson.value());
		REQUIRE(approvePayload["status"].get<std::string>() == "error");
		const std::string topCode = approvePayload.value("errorCode", std::string{});
		REQUIRE_FALSE(topCode.empty());
		REQUIRE(topCode != "legacy_execution_failed");
		REQUIRE((topCode == "imap_smtp_skill_missing" || topCode == "email_backend_unavailable"));
		const auto outputJson = nlohmann::json::parse(approvePayload.value("output", std::string("{}")));
		REQUIRE(outputJson.contains("error"));
		REQUIRE(outputJson["error"].is_object());
		REQUIRE(outputJson["error"].contains("code"));
		REQUIRE(outputJson["error"].contains("remediation"));
		};

	assertMappedApprovalFailure(
		std::string("{\"tool\":\"email.schedule\",\"arguments\":{") +
		"\"action\":\"approve\"," +
		"\"approvalToken\":\"" + approvalToken + "\"," +
		"\"approve\":true}}",
		"approval-args-shape-arguments");

	assertMappedApprovalFailure(
		std::string("{\"tool\":\"email.schedule\",\"payload\":\"{") +
		"\\\"action\\\":\\\"approve\\\"," +
		"\\\"approvalToken\\\":\\\"" + approvalToken + "\\\"," +
		"\\\"approve\\\":true}" +
		"\"}",
		"approval-args-shape-payload-string");

	host.Stop();
}
