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
				rawPayload.find("\"state\":\"aborted\"") != std::string::npos) {
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
					if (state == "final") {
						trace.finalAssistantText = text;
					}
				}

				if (state == "final" || state == "error" || state == "aborted") {
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

	host.Stop();
}
