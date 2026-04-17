#include "gateway/executors/EmailScheduleExecutor.h"

#include "config/ConfigModels.h"
#include "core/EmailFallbackRuntimeCoordinator.h"
#include "core/EmailPreflightHealthService.h"
#include "core/EmailPolicyOrchestrationService.h"
#include "core/EmailRuntimeDiagnosticsProjector.h"
#include "core/diagnostics/DiagnosticsSnapshot.h"

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

TEST_CASE(
	"Email fallback policy precedence prefers tool over capability and default",
	"[email][fallback][policy]") {
	blazeclaw::config::EmailFallbackPolicyProfilesMapConfig policy;

	auto& defaults = policy.defaults;
	defaults.id = L"default-policy";
	defaults.backends = { L"himalaya" };
	defaults.actions.unavailable = L"continue";
	defaults.actions.authError = L"stop";
	defaults.actions.execError = L"retry_then_continue";
	defaults.retry.maxAttempts = 1;
	defaults.retry.retryDelayMs = 0;
	defaults.approval.requiresApproval = true;
	defaults.approval.tokenTtlMinutes = 60;

	auto& capability = policy.capability[L"email.send"];
	capability.id = L"capability-policy";
	capability.backends = { L"imap-smtp-email" };
	capability.actions.unavailable = L"stop";
	capability.actions.authError = L"continue";
	capability.actions.execError = L"stop";
	capability.retry.maxAttempts = 2;
	capability.retry.retryDelayMs = 100;
	capability.approval.requiresApproval = false;
	capability.approval.tokenTtlMinutes = 30;

	auto& tool = policy.tool[L"email.schedule"];
	tool.id = L"tool-policy";
	tool.backends = { L"custom-backend" };
	tool.actions.unavailable = L"continue";
	tool.actions.authError = L"continue";
	tool.actions.execError = L"stop";
	tool.retry.maxAttempts = 3;
	tool.retry.retryDelayMs = 250;
	tool.approval.requiresApproval = false;
	tool.approval.tokenTtlMinutes = 45;

	const auto resolved = blazeclaw::config::ResolveEmailFallbackPolicy(
		policy,
		L"email.schedule",
		L"email.send");

	REQUIRE(resolved.profileId == L"tool-policy");
	REQUIRE(resolved.backends.size() == 1);
	REQUIRE(resolved.backends[0] == L"custom-backend");
	REQUIRE(resolved.onExecError == L"stop");
	REQUIRE(resolved.retryMaxAttempts == 3);
	REQUIRE(resolved.retryDelayMs == 250);
	REQUIRE_FALSE(resolved.requiresApproval);
	REQUIRE(resolved.approvalTokenTtlMinutes == 45);
}

TEST_CASE(
	"Email fallback policy precedence prefers capability over default",
	"[email][fallback][policy]") {
	blazeclaw::config::EmailFallbackPolicyProfilesMapConfig policy;

	auto& defaults = policy.defaults;
	defaults.id = L"default-policy";
	defaults.backends = { L"himalaya" };
	defaults.actions.authError = L"stop";
	defaults.retry.maxAttempts = 1;

	auto& capability = policy.capability[L"email.send"];
	capability.id = L"capability-policy";
	capability.backends = { L"imap-smtp-email" };
	capability.actions.authError = L"continue";
	capability.retry.maxAttempts = 4;

	const auto resolved = blazeclaw::config::ResolveEmailFallbackPolicy(
		policy,
		L"other.tool",
		L"email.send");

	REQUIRE(resolved.profileId == L"capability-policy");
	REQUIRE(resolved.backends.size() == 1);
	REQUIRE(resolved.backends[0] == L"imap-smtp-email");
	REQUIRE(resolved.onAuthError == L"continue");
	REQUIRE(resolved.retryMaxAttempts == 4);
}

TEST_CASE(
	"Email fallback policy precedence falls back to default when no overrides match",
	"[email][fallback][policy]") {
	blazeclaw::config::EmailFallbackPolicyProfilesMapConfig policy;

	auto& defaults = policy.defaults;
	defaults.id = L"default-policy";
	defaults.backends = { L"himalaya", L"imap-smtp-email" };
	defaults.actions.unavailable = L"continue";
	defaults.actions.authError = L"stop";
	defaults.actions.execError = L"retry_then_continue";
	defaults.retry.maxAttempts = 2;
	defaults.retry.retryDelayMs = 50;
	defaults.approval.requiresApproval = true;
	defaults.approval.tokenTtlMinutes = 90;

	const auto resolved = blazeclaw::config::ResolveEmailFallbackPolicy(
		policy,
		L"tool.unknown",
		L"capability.unknown");

	REQUIRE(resolved.profileId == L"default-policy");
	REQUIRE(resolved.backends.size() == 2);
	REQUIRE(resolved.backends[0] == L"himalaya");
	REQUIRE(resolved.backends[1] == L"imap-smtp-email");
	REQUIRE(resolved.onUnavailable == L"continue");
	REQUIRE(resolved.onAuthError == L"stop");
	REQUIRE(resolved.onExecError == L"retry_then_continue");
	REQUIRE(resolved.retryMaxAttempts == 2);
	REQUIRE(resolved.retryDelayMs == 50);
	REQUIRE(resolved.requiresApproval);
	REQUIRE(resolved.approvalTokenTtlMinutes == 90);
}

TEST_CASE(
	"EmailPolicyOrchestrationService resolves fallback policy with config parity",
	"[email][fallback][service]") {
	blazeclaw::config::AppConfig config;
	auto& defaults = config.email.policy.defaults;
	defaults.id = L"default-policy";
	defaults.backends = { L"himalaya" };
	defaults.actions.execError = L"retry_then_continue";
	defaults.retry.maxAttempts = 2;

	auto& tool = config.email.policy.tool[L"email.schedule"];
	tool.id = L"tool-policy";
	tool.backends = { L"imap-smtp-email" };
	tool.actions.execError = L"stop";
	tool.retry.maxAttempts = 3;
	tool.retry.retryDelayMs = 120;
	tool.approval.requiresApproval = false;

	const blazeclaw::core::EmailPolicyOrchestrationService service;
	const auto resolved = service.ResolveFallbackPolicy(
		config,
		L"email.schedule",
		L"email.send");

	REQUIRE(resolved.profileId == L"tool-policy");
	REQUIRE(resolved.backends.size() == 1);
	REQUIRE(resolved.backends[0] == L"imap-smtp-email");
	REQUIRE(resolved.onExecError == L"stop");
	REQUIRE(resolved.retryMaxAttempts == 3);
	REQUIRE(resolved.retryDelayMs == 120);
	REQUIRE_FALSE(resolved.requiresApproval);
}

TEST_CASE(
	"EmailPolicyOrchestrationService builds gateway binding with runtime gate",
	"[email][fallback][service]") {
	blazeclaw::config::EmailFallbackConfig emailConfig;
	emailConfig.preflight.enabled = true;

	blazeclaw::core::EmailPolicyOrchestrationService::ResolvedEmailFallbackPolicy
		resolvedPolicy;
	resolvedPolicy.profileId = L"tool-policy";
	resolvedPolicy.backends = { L"himalaya", L"imap-smtp-email" };
	resolvedPolicy.onUnavailable = L"continue";
	resolvedPolicy.onAuthError = L"stop";
	resolvedPolicy.onExecError = L"retry_then_continue";
	resolvedPolicy.retryMaxAttempts = 4;
	resolvedPolicy.retryDelayMs = 200;
	resolvedPolicy.requiresApproval = true;
	resolvedPolicy.approvalTokenTtlMinutes = 45;

	const blazeclaw::core::EmailPolicyOrchestrationService service;
	const auto enabledBinding = service.BuildGatewayPolicyBinding(
		emailConfig,
		true,
		true,
		resolvedPolicy);
	REQUIRE(enabledBinding.preflightEnabled);
	REQUIRE(enabledBinding.runtimeEnabled);
	REQUIRE(enabledBinding.runtimeEnforce);
	REQUIRE(enabledBinding.backends.size() == 2);
	REQUIRE(enabledBinding.retryMaxAttempts == 4);
	REQUIRE(enabledBinding.retryDelayMs == 200);
	REQUIRE(enabledBinding.profileId == "tool-policy");

	const auto disabledBinding = service.BuildGatewayPolicyBinding(
		emailConfig,
		false,
		false,
		resolvedPolicy);
	REQUIRE_FALSE(disabledBinding.runtimeEnabled);
	REQUIRE_FALSE(disabledBinding.runtimeEnforce);
	REQUIRE(disabledBinding.retryMaxAttempts == 1);
	REQUIRE(disabledBinding.retryDelayMs == 0);
	REQUIRE(disabledBinding.profileId == "legacy-policy");
}

TEST_CASE(
	"EmailFallbackRuntimeCoordinator classifies embedded fallback cases",
	"[email][fallback][service]") {
	const blazeclaw::core::EmailFallbackRuntimeCoordinator coordinator;

	const auto timeoutDecision = coordinator.EvaluateEmbeddedFailure(
		"embedded_deadline_exceeded",
		"deadline_exceeded");
	REQUIRE(timeoutDecision.shouldFallback);
	REQUIRE(timeoutDecision.fallbackReason == "embedded_deadline_exceeded");

	const auto reasonDecision = coordinator.EvaluateEmbeddedFailure(
		"",
		"tool_execution_failed");
	REQUIRE(reasonDecision.shouldFallback);
	REQUIRE(reasonDecision.fallbackReason == "tool_execution_failed");

	const auto noFallbackDecision = coordinator.EvaluateEmbeddedFailure(
		"unknown_error",
		"policy_blocked");
	REQUIRE_FALSE(noFallbackDecision.shouldFallback);
}

TEST_CASE(
	"EmailPreflightHealthService returns runtime health index",
	"[email][fallback][service]") {
	ScopedEnvVar probeHimalaya("BLAZECLAW_EMAIL_PROBE_HIMALAYA");
	ScopedEnvVar probeNode("BLAZECLAW_EMAIL_PROBE_NODE");
	ScopedEnvVar probeSkill("BLAZECLAW_EMAIL_PROBE_IMAP_SMTP_SKILL");
	ScopedEnvVar capabilityOverride("BLAZECLAW_EMAIL_CAPABILITY_STATE_OVERRIDE");

	probeHimalaya.Set("ready");
	probeNode.Set("ready");
	probeSkill.Set("ready");
	capabilityOverride.Set("ready");

	const blazeclaw::core::EmailPreflightHealthService service;
	const auto health = service.BuildRuntimeHealthIndex(true);
	REQUIRE(health.emailSendState == "ready");
	REQUIRE_FALSE(health.probes.empty());
}

TEST_CASE(
	"EmailRuntimeDiagnosticsProjector maps email diagnostics fields",
	"[email][fallback][service]") {
	blazeclaw::config::EmailFallbackConfig emailConfig;
	emailConfig.preflight.enabled = true;
	emailConfig.policyProfiles.enabled = true;
	emailConfig.policyProfiles.enforce = true;

	blazeclaw::core::EmailPolicyOrchestrationService::ResolvedEmailFallbackPolicy
		resolvedPolicy;
	resolvedPolicy.profileId = L"tool-policy";
	resolvedPolicy.backends = { L"himalaya", L"imap-smtp-email" };
	resolvedPolicy.onUnavailable = L"continue";
	resolvedPolicy.onAuthError = L"stop";
	resolvedPolicy.onExecError = L"retry_then_continue";
	resolvedPolicy.retryMaxAttempts = 3;
	resolvedPolicy.retryDelayMs = 150;
	resolvedPolicy.requiresApproval = true;
	resolvedPolicy.approvalTokenTtlMinutes = 90;

	blazeclaw::gateway::executors::RuntimeHealthIndex healthIndex;
	healthIndex.emailSendState = "degraded";
	healthIndex.generatedAtEpochMs = 100;
	healthIndex.ttlMs = 60000;
	healthIndex.probes = {
		blazeclaw::gateway::executors::DependencyProbeResult{
			.key = "backend:himalaya",
			.state = "ready",
		},
		blazeclaw::gateway::executors::DependencyProbeResult{
			.key = "runtime:node",
			.state = "unavailable",
		},
	};

	blazeclaw::core::DiagnosticsSnapshot snapshot;
	const blazeclaw::core::EmailRuntimeDiagnosticsProjector projector;
	projector.Apply(
		blazeclaw::core::EmailRuntimeDiagnosticsProjector::Context{
			.emailConfig = emailConfig,
			.policyRolloutMode = L"runtime",
			.policyEnforceChannel = L"email.send",
			.policyCanaryEligible = true,
			.rollbackBridgeEnabled = false,
			.runtimeEnabled = true,
			.runtimeEnforce = true,
			.resolvedPolicy = resolvedPolicy,
			.healthIndex = healthIndex,
			.fallbackAttempts = 5,
			.fallbackSuccess = 3,
			.fallbackFailure = 2,
		},
		snapshot);

	REQUIRE(snapshot.emailPreflightEnabled);
	REQUIRE(snapshot.emailPolicyProfilesEnabled);
	REQUIRE(snapshot.emailPolicyProfilesEnforce);
	REQUIRE(snapshot.emailPolicyProfilesRuntimeEnabled);
	REQUIRE(snapshot.emailPolicyProfilesRuntimeEnforce);
	REQUIRE(snapshot.emailResolvedPolicyId == "tool-policy");
	REQUIRE(snapshot.emailResolvedBackends.size() == 2);
	REQUIRE(snapshot.emailCapabilityState == "degraded");
	REQUIRE(snapshot.emailProbeReadyCount == 1);
	REQUIRE(snapshot.emailProbeUnavailableCount == 1);
	REQUIRE(snapshot.emailFallbackAttempts == 5);
	REQUIRE(snapshot.emailFallbackSuccess == 3);
	REQUIRE(snapshot.emailFallbackFailure == 2);
}

TEST_CASE(
	"EmailScheduleExecutor exec_error stop blocks backend fallback",
	"[email][fallback][scenario]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionExec.Set("stop");

	const auto executor = EmailScheduleExecutor::Create();
	const auto prepare = executor(
		"email.schedule",
		std::string("{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Report\",\"body\":\"Tomorrow sunny\",\"sendAt\":\"13:00\"}"));
	REQUIRE(prepare.executed);
	REQUIRE(prepare.status == "needs_approval");

	const auto token = nlohmann::json::parse(prepare.output)
		.at("requiresApproval")
		.at("approvalToken")
		.get<std::string>();
	REQUIRE_FALSE(token.empty());

	const auto approve = executor(
		"email.schedule",
		std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token +
		"\",\"approve\":true}");
	REQUIRE_FALSE(approve.executed);
	REQUIRE(approve.status == "error");
	REQUIRE(approve.output.find("himalaya_send_failed") != std::string::npos);
}

TEST_CASE(
	"EmailScheduleExecutor retry_then_continue transitions to fallback backend",
	"[email][fallback][scenario]") {
	ScopedEnvVar modeEnv("BLAZECLAW_EMAIL_DELIVERY_MODE");
	ScopedEnvVar imapModeEnv("BLAZECLAW_EMAIL_IMAP_SMTP_MODE");
	ScopedEnvVar backendsEnv("BLAZECLAW_EMAIL_DELIVERY_BACKENDS");
	ScopedEnvVar profileEnabled("BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED");
	ScopedEnvVar profileEnforce("BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE");
	ScopedEnvVar actionExec("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR");
	ScopedEnvVar retryMaxAttempts("BLAZECLAW_EMAIL_POLICY_RETRY_MAX_ATTEMPTS");
	ScopedEnvVar retryDelayMs("BLAZECLAW_EMAIL_POLICY_RETRY_DELAY_MS");

	modeEnv.Set("mock_failure");
	imapModeEnv.Set("mock_success");
	backendsEnv.Set("himalaya,imap-smtp-email");
	profileEnabled.Set("true");
	profileEnforce.Set("true");
	actionExec.Set("retry_then_continue");
	retryMaxAttempts.Set("2");
	retryDelayMs.Set("0");

	const auto executor = EmailScheduleExecutor::Create();
	const auto prepare = executor(
		"email.schedule",
		std::string("{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Report\",\"body\":\"Tomorrow sunny\",\"sendAt\":\"13:00\"}"));
	REQUIRE(prepare.executed);
	REQUIRE(prepare.status == "needs_approval");

	const auto token = nlohmann::json::parse(prepare.output)
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
