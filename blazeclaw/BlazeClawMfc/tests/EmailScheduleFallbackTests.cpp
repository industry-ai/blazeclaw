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

TEST_CASE(
	"Email runtime health classifies ready degraded unavailable via deterministic overrides",
	"[email][fallback][health]") {
	ScopedEnvVar probeHimalaya("BLAZECLAW_EMAIL_PROBE_HIMALAYA");
	ScopedEnvVar probeNode("BLAZECLAW_EMAIL_PROBE_NODE");
	ScopedEnvVar probeSkill("BLAZECLAW_EMAIL_PROBE_IMAP_SMTP_SKILL");
	ScopedEnvVar capabilityOverride("BLAZECLAW_EMAIL_CAPABILITY_STATE_OVERRIDE");

	auto assertState = [](
		const blazeclaw::gateway::executors::RuntimeHealthIndex& health,
		const std::string& expected) {
			REQUIRE(health.emailSendState == expected);
			REQUIRE_FALSE(health.probes.empty());
		};

	probeHimalaya.Set("ready");
	probeNode.Set("ready");
	probeSkill.Set("ready");
	capabilityOverride.Set("");
	assertState(EmailScheduleExecutor::GetRuntimeHealthIndex(true), "ready");

	probeHimalaya.Set("unavailable");
	probeNode.Set("ready");
	probeSkill.Set("unavailable");
	assertState(EmailScheduleExecutor::GetRuntimeHealthIndex(true), "degraded");

	probeHimalaya.Set("unavailable");
	probeNode.Set("unavailable");
	probeSkill.Set("unavailable");
	assertState(EmailScheduleExecutor::GetRuntimeHealthIndex(true), "unavailable");

	capabilityOverride.Set("degraded");
	assertState(EmailScheduleExecutor::GetRuntimeHealthIndex(true), "degraded");
}

TEST_CASE(
	"EmailScheduleExecutor fallback matrix handles node missing then himalaya ready",
	"[email][fallback][matrix]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionUnavailable("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");

	modeEnv.Set("mock_success");
	imapModeEnv.Set("mock_failure");
	backendsEnv.Set("imap-smtp-email,himalaya");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionUnavailable.Set("continue");
	actionExec.Set("continue");

	const auto executor = EmailScheduleExecutor::Create();
	const auto prepare = executor(
		"email.schedule",
		std::string("{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Report\",\"body\":\"Tomorrow sunny\",\"sendAt\":\"13:00\"}"));
	REQUIRE(prepare.executed);
	REQUIRE(prepare.status == "needs_approval");

	const auto token =
		nlohmann::json::parse(prepare.output)
		.at("requiresApproval")
		.at("approvalToken")
		.get<std::string>();
	REQUIRE_FALSE(token.empty());

	const auto approve = executor(
		"email.schedule",
		std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token +
		"\",\"approve\":true}");
	REQUIRE(approve.executed);
	REQUIRE(approve.status == "ok");
	REQUIRE(approve.output.find("\"engine\":\"himalaya\"") != std::string::npos);
}

TEST_CASE(
	"EmailScheduleExecutor fallback matrix handles himalaya missing then imap-smtp ready",
	"[email][fallback][matrix]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
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

	const auto executor = EmailScheduleExecutor::Create();
	const auto prepare = executor(
		"email.schedule",
		std::string("{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Report\",\"body\":\"Tomorrow sunny\",\"sendAt\":\"13:00\"}"));
	REQUIRE(prepare.executed);
	REQUIRE(prepare.status == "needs_approval");

	const auto token =
		nlohmann::json::parse(prepare.output)
		.at("requiresApproval")
		.at("approvalToken")
		.get<std::string>();
	REQUIRE_FALSE(token.empty());

	const auto approve = executor(
		"email.schedule",
		std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token +
		"\",\"approve\":true}");
	REQUIRE(approve.executed);
	REQUIRE(approve.status == "ok");
	REQUIRE(approve.output.find("\"engine\":\"imap-smtp-email\"") != std::string::npos);
}
