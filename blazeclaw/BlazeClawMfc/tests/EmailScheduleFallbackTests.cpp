#include "gateway/executors/EmailScheduleExecutor.h"

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>

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
}

using blazeclaw::gateway::executors::EmailScheduleExecutor;

TEST_CASE(
	"EmailScheduleExecutor uses mock_success backend with fallback chain configured",
	"[email][fallback][unit]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	modeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");

	const auto executor = EmailScheduleExecutor::Create();
	const auto prepare = executor(
		"email.schedule",
		std::string("{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Report\",\"body\":\"Tomorrow sunny\",\"sendAt\":\"13:00\"}"));
	REQUIRE(prepare.executed);
	REQUIRE(prepare.status == "needs_approval");

	const auto preparePayload = nlohmann::json::parse(prepare.output);
	const auto token = preparePayload.at("requiresApproval").at("approvalToken").get<std::string>();
	REQUIRE_FALSE(token.empty());

	const auto approve = executor(
		"email.schedule",
		std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token + "\",\"approve\":true}");
	REQUIRE(approve.executed);
	REQUIRE(approve.status == "ok");
	REQUIRE(approve.output.find("\"engine\":\"himalaya\"") != std::string::npos);
}

TEST_CASE(
	"EmailScheduleExecutor reports backend exhaustion when all backends fail",
	"[email][fallback][unit]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	modeEnv.Set("mock_failure");
	backendsEnv.Set("himalaya");

	const auto executor = EmailScheduleExecutor::Create();
	const auto prepare = executor(
		"email.schedule",
		std::string("{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Report\",\"body\":\"Tomorrow sunny\",\"sendAt\":\"13:00\"}"));
	REQUIRE(prepare.executed);
	REQUIRE(prepare.status == "needs_approval");

	const auto preparePayload = nlohmann::json::parse(prepare.output);
	const auto token = preparePayload.at("requiresApproval").at("approvalToken").get<std::string>();
	REQUIRE_FALSE(token.empty());

	const auto approve = executor(
		"email.schedule",
		std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token + "\",\"approve\":true}");
	REQUIRE_FALSE(approve.executed);
	REQUIRE(approve.status == "error");
	REQUIRE(approve.output.find("himalaya_send_failed") != std::string::npos);
}
