#include "gateway/PluginHostAdapter.h"
#include "gateway/GatewayToolRegistry.h"

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

using blazeclaw::gateway::PluginHostAdapter;

TEST_CASE("Weather lookup tool adapter resolves and returns forecast", "[ops-tools][weather]") {
	const auto load = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
	REQUIRE(load.ok);

	const auto resolved =
		PluginHostAdapter::ResolveExecutor("ops-tools", "weather.lookup", "");
	REQUIRE(resolved.resolved);
	REQUIRE(resolved.executor);

	const std::string args = "{\"city\":\"Wuhan\",\"date\":\"tomorrow\"}";
	const auto result = resolved.executor("weather.lookup", args);

	REQUIRE(result.executed);
	REQUIRE(result.status == "ok");

	const auto payload = nlohmann::json::parse(result.output);
	REQUIRE(payload.value("ok", false));
	REQUIRE(payload.at("forecast").at("city").get<std::string>() == "Wuhan");

	const auto unload = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
	REQUIRE(unload.ok);
}

TEST_CASE(
	"Email schedule adapter deterministic terminal behavior when both backends unavailable",
	"[ops-tools][email][fallback]") {
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

	const auto unloadStale = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
	REQUIRE(unloadStale.ok);

	const auto load = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
	REQUIRE(load.ok);

	const auto resolved =
		PluginHostAdapter::ResolveExecutor("ops-tools", "email.schedule", "");
	REQUIRE(resolved.resolved);
	REQUIRE(resolved.executor);

	const std::string prepareArgs =
		"{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Wuhan weather report\",\"body\":\"Tomorrow cloudy around 20C.\",\"sendAt\":\"13:00\",\"ttlMinutes\":60}";
	const auto prepareResult = resolved.executor("email.schedule", prepareArgs);
	REQUIRE(prepareResult.executed);
	REQUIRE(prepareResult.status == "needs_approval");

	const auto preparePayload = nlohmann::json::parse(prepareResult.output);
	const auto token = preparePayload.at("requiresApproval").at("approvalToken").get<std::string>();
	REQUIRE_FALSE(token.empty());

	const std::string approveArgs =
		std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token +
		"\",\"approve\":true}";
	const auto approveResult = resolved.executor("email.schedule", approveArgs);
	REQUIRE_FALSE(approveResult.executed);
	REQUIRE(approveResult.status == "error");
	const bool hasHimalayaFailure =
		approveResult.output.find("himalaya_send_failed") != std::string::npos;
	const bool hasImapFailure =
		approveResult.output.find("imap_smtp_send_failed") != std::string::npos;
	if (!hasHimalayaFailure && !hasImapFailure) {
		FAIL("expected himalaya_send_failed or imap_smtp_send_failed in approval output");
	}

	const auto unload = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
	REQUIRE(unload.ok);
}

TEST_CASE("Email schedule tool adapter prepare and approve flow", "[ops-tools][email]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	modeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");

	const auto unloadStale = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
	REQUIRE(unloadStale.ok);

	const auto load = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
	REQUIRE(load.ok);

	const auto resolved =
		PluginHostAdapter::ResolveExecutor("ops-tools", "email.schedule", "");
	REQUIRE(resolved.resolved);
	REQUIRE(resolved.executor);

	const std::string prepareArgs =
		"{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Wuhan weather report\",\"body\":\"Tomorrow cloudy around 20C.\",\"sendAt\":\"13:00\",\"ttlMinutes\":60}";
	const auto prepareResult = resolved.executor("email.schedule", prepareArgs);

	REQUIRE(prepareResult.executed);
	REQUIRE(prepareResult.status == "needs_approval");

	const auto preparePayload = nlohmann::json::parse(prepareResult.output);
	const auto token = preparePayload.at("requiresApproval").at("approvalToken").get<std::string>();
	REQUIRE_FALSE(token.empty());

	const std::string approveArgs =
		std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token + "\",\"approve\":true}";
	const auto approveResult = resolved.executor("email.schedule", approveArgs);

	REQUIRE(approveResult.executed);
	REQUIRE(approveResult.status == "ok");

	const auto approvePayload = nlohmann::json::parse(approveResult.output);
	REQUIRE(approvePayload.at("status").get<std::string>() == "ok");

	const auto invalidReuseResult = resolved.executor("email.schedule", approveArgs);
	REQUIRE_FALSE(invalidReuseResult.executed);
	REQUIRE(invalidReuseResult.status == "invalid_args");

	const auto unload = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
	REQUIRE(unload.ok);
}

TEST_CASE(
	"Email schedule tool adapter falls back when himalaya backend fails",
	"[ops-tools][email][fallback]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	modeEnv.Set("mock_failure");
	backendsEnv.Set("himalaya");

	const auto unloadStale = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
	REQUIRE(unloadStale.ok);

	const auto load = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
	REQUIRE(load.ok);

	const auto resolved =
		PluginHostAdapter::ResolveExecutor("ops-tools", "email.schedule", "");
	REQUIRE(resolved.resolved);
	REQUIRE(resolved.executor);

	const std::string prepareArgs =
		"{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Wuhan weather report\",\"body\":\"Tomorrow cloudy around 20C.\",\"sendAt\":\"13:00\",\"ttlMinutes\":60}";
	const auto prepareResult = resolved.executor("email.schedule", prepareArgs);
	REQUIRE(prepareResult.executed);
	REQUIRE(prepareResult.status == "needs_approval");

	const auto preparePayload = nlohmann::json::parse(prepareResult.output);
	const auto token = preparePayload.at("requiresApproval").at("approvalToken").get<std::string>();
	REQUIRE_FALSE(token.empty());

	const std::string approveArgs =
		std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token +
		"\",\"approve\":true}";
	const auto approveResult = resolved.executor("email.schedule", approveArgs);
	REQUIRE_FALSE(approveResult.executed);
	REQUIRE(approveResult.status == "error");
	REQUIRE(approveResult.output.find("himalaya_send_failed") != std::string::npos);

	const auto unload = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
	REQUIRE(unload.ok);
}
