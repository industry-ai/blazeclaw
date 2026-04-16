#include "gateway/GatewayHost.h"

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

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

			const auto payload = nlohmann::json::parse(
				pollResponse.payloadJson.value());
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
	REQUIRE(weatherOkCount == 1);

	const int schedulePrepareCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=needs_approval");
	REQUIRE(schedulePrepareCount == 1);

	const int scheduleOkCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=ok");
	REQUIRE(scheduleOkCount == 1);

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
	REQUIRE(weatherOkCount == 1);

	const int schedulePrepareCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=needs_approval");
	REQUIRE(schedulePrepareCount == 1);

	const int scheduleApproveCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=ok");
	REQUIRE(scheduleApproveCount == 1);

	const int invalidArgsCount = CountExactMatch(
		pollTrace.assistantTexts,
		"tools.execute.result tool=email.schedule status=invalid_args");
	REQUIRE(invalidArgsCount == 0);

	host.Stop();
}
