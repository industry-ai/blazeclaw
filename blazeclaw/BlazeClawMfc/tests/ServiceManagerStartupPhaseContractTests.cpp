#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

	std::string ReadServiceManagerSource()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"core" /
			"ServiceManager.cpp";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadServiceLifecycleStartupCoordinatorSource()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"core" /
			"ServiceLifecycleStartupCoordinator.cpp";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadManagedRuntimeConfigDiffCoordinatorHeader()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"core" /
			"ManagedRuntimeConfigDiffCoordinator.h";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadGatewayHostBindingCoordinatorSource()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"core" /
			"GatewayHostBindingCoordinator.cpp";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadChatRuntimeOrchestrationCoordinatorSource()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"core" /
			"ChatRuntimeOrchestrationCoordinator.cpp";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadOperatorDiagnosticsAssemblerSource()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"core" /
			"OperatorDiagnosticsAssembler.cpp";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadSkillsGatewayMethodHandlerHeader()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"core" /
			"SkillsGatewayMethodHandler.h";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadGatewayHostHandlersRuntimeSource()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"gateway" /
			"GatewayHost.Handlers.Runtime.cpp";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadGatewayConfigDiagnosticsHandlersSource()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"gateway" /
			"GatewayHost.Handlers.ConfigDiagnostics.cpp";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

} // namespace

TEST_CASE(
	"ServiceManager startup contract: Start delegates phases in order",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();

	const auto startPos = source.find(
		"bool ServiceManager::Start(const blazeclaw::config::AppConfig& config)");
	REQUIRE(startPos != std::string::npos);

	const auto configurePos = source.find("ConfigurePolicies(config);", startPos);
	const auto initializePos = source.find("InitializeModules();", startPos);
	const auto wirePos = source.find("WireGatewayCallbacks();", startPos);
	const auto finalizePos = source.find("return FinalizeStartup(config);", startPos);

	REQUIRE(configurePos != std::string::npos);
	REQUIRE(initializePos != std::string::npos);
	REQUIRE(wirePos != std::string::npos);
	REQUIRE(finalizePos != std::string::npos);

	REQUIRE(configurePos < initializePos);
	REQUIRE(initializePos < wirePos);
	REQUIRE(wirePos < finalizePos);
}

TEST_CASE(
	"ServiceManager startup contract: phase methods exist",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();

	REQUIRE(source.find("void ServiceManager::ConfigurePolicies(") != std::string::npos);
	REQUIRE(source.find("void ServiceManager::InitializeModules()") != std::string::npos);
	REQUIRE(source.find("void ServiceManager::WireGatewayCallbacks()") != std::string::npos);
	REQUIRE(source.find("bool ServiceManager::FinalizeStartup(") != std::string::npos);
}

TEST_CASE(
	"ServiceManager S0 contract: parity lifecycle trace export and gateway RPC surface",
	"[servicemanager][startup][contract][s0]")
{
	const std::string sm = ReadServiceManagerSource();
	REQUIRE(sm.find("ServiceManager::BuildGatewayParityLifecycleTraceJson(") != std::string::npos);
	REQUIRE(
		sm.find("m_diagnosticsReportBuilder.SerializeParityLifecycleContractJson(") !=
		std::string::npos);

	const std::string bind = ReadGatewayHostBindingCoordinatorSource();
	REQUIRE(bind.find("SetParityLifecycleExportCallback") != std::string::npos);
	REQUIRE(bind.find("BuildGatewayParityLifecycleTraceJson") != std::string::npos);

	const std::string configHandlers = ReadGatewayConfigDiagnosticsHandlersSource();
	REQUIRE(configHandlers.find("gateway.parity.lifecycle") != std::string::npos);
	REQUIRE(configHandlers.find("ExportParityLifecycleTraceJson") != std::string::npos);
}

TEST_CASE(
	"ServiceManager S1 contract: startup file snapshot, migrations, and auth bootstrap policy",
	"[servicemanager][startup][contract][s1]")
{
	const std::string sm = ReadServiceManagerSource();
	const auto finalizePos = sm.find("bool ServiceManager::FinalizeStartup(");
	REQUIRE(finalizePos != std::string::npos);
	const auto finalizeBody = sm.substr(finalizePos);

	REQUIRE(finalizeBody.find("RecordGatewayStartupConfigSnapshot(") != std::string::npos);
	REQUIRE(finalizeBody.find("RefreshGatewayAuthBootstrapDiagnostics(") != std::string::npos);
	REQUIRE(finalizeBody.find("m_state.gatewayLifecycle.startupMigrationsApplied") != std::string::npos);
	REQUIRE(
		finalizeBody.find("SuppressStartupMigrationsFromEnv()") != std::string::npos);
	REQUIRE(finalizeBody.find("appliedStartupMigrationsOut") != std::string::npos);
	REQUIRE(finalizeBody.find("queueManagedConfigInternalWriteHash") != std::string::npos);

	const auto coordinatorPath =
		std::filesystem::path("BlazeClawMfc") / "src" / "core" / "bootstrap" /
		"GatewayRuntimeBootstrapCoordinator.cpp";
	std::ifstream coord(coordinatorPath.string());
	REQUIRE(coord.is_open());
	const std::string coordinator(
		(std::istreambuf_iterator<char>(coord)),
		std::istreambuf_iterator<char>());
	REQUIRE(
		coordinator.find("RunStartupMigrations(") != std::string::npos);
	REQUIRE(
		coordinator.find("GatewayRuntimeBootstrap.migration") != std::string::npos);

	const auto configLoaderPath =
		std::filesystem::path("BlazeClawMfc") / "src" / "config" / "ConfigLoader.cpp";
	std::ifstream cl(configLoaderPath.string());
	REQUIRE(cl.is_open());
	const std::string loader(
		(std::istreambuf_iterator<char>(cl)),
		std::istreambuf_iterator<char>());
	REQUIRE(loader.find("BuildGatewayStartupConfigFileSnapshot(") != std::string::npos);

	const auto builderPath =
		std::filesystem::path("BlazeClawMfc") / "src" / "core" / "diagnostics" /
		"CDiagnosticsReportBuilder.cpp";
	std::ifstream b(builderPath.string());
	REQUIRE(b.is_open());
	const std::string dsrc(
		(std::istreambuf_iterator<char>(b)),
		std::istreambuf_iterator<char>());
	REQUIRE(dsrc.find("authBootstrapPathTag") != std::string::npos);
	REQUIRE(dsrc.find("startupConfigContentDigest") != std::string::npos);
}

TEST_CASE(
	"ServiceManager S2 contract: ResolveGatewayRuntimeConfig wiring and parity JSON",
	"[servicemanager][startup][contract][s2]")
{
	const std::string sm = ReadServiceManagerSource();
	REQUIRE(sm.find("resolvedRuntimeConfig") != std::string::npos);
	REQUIRE(
		sm.find(".resolvedRuntime = m_state.gatewayLifecycle.resolvedRuntimeConfig") !=
		std::string::npos);
	REQUIRE(
		sm.find("m_state.gatewayLifecycle.resolvedRuntimeConfig = startupResult.resolvedRuntime") !=
		std::string::npos);

	const auto coordPath = std::filesystem::path("BlazeClawMfc") / "src" / "core" / "bootstrap" /
		"GatewayRuntimeBootstrapCoordinator.cpp";
	std::ifstream cin(coordPath.string());
	REQUIRE(cin.is_open());
	const std::string coord(
		(std::istreambuf_iterator<char>(cin)),
		std::istreambuf_iterator<char>());
	REQUIRE(coord.find("GatewayRuntimeBootstrapCoordinator::ResolveGatewayRuntimeConfig(") != std::string::npos);
	REQUIRE(coord.find("DecisionFromResolved(") != std::string::npos);
	REQUIRE(coord.find("BLAZECLAW_GATEWAY_PORT") != std::string::npos);
	REQUIRE(coord.find("BLAZECLAW_GATEWAY_BIND") != std::string::npos);
	REQUIRE(coord.find("IsKnownStartupModeToken(") != std::string::npos);

	const auto modelsPath = std::filesystem::path("BlazeClawMfc") / "src" / "config" / "ConfigModels.h";
	std::ifstream min(modelsPath.string());
	REQUIRE(min.is_open());
	const std::string msrc(
		(std::istreambuf_iterator<char>(min)),
		std::istreambuf_iterator<char>());
	REQUIRE(msrc.find("struct GatewayResolvedRuntimeConfig") != std::string::npos);
	REQUIRE(msrc.find("GatewayResolvedStartupModeClass") != std::string::npos);

	const auto builderPath =
		std::filesystem::path("BlazeClawMfc") / "src" / "core" / "diagnostics" / "CDiagnosticsReportBuilder.cpp";
	std::ifstream b(builderPath.string());
	REQUIRE(b.is_open());
	const std::string dsrc(
		(std::istreambuf_iterator<char>(b)),
		std::istreambuf_iterator<char>());
	REQUIRE(dsrc.find("runtimeResolvedPort") != std::string::npos);
	REQUIRE(dsrc.find("runtimeResolvedBindSource") != std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: FinalizeStartup delegates bootstrap coordinator and failure cleanup",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	const auto finalizePos = source.find("bool ServiceManager::FinalizeStartup(");
	REQUIRE(finalizePos != std::string::npos);

	const auto finalizeBody = source.substr(finalizePos);

	REQUIRE(
		finalizeBody.find("m_gatewayRuntimeBootstrapCoordinator.ExecuteStartup") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("m_gatewayRuntimeBootstrapCoordinator.HandleStartupFailure") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("gateway startup failed; running in degraded local mode.") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.failed") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.degraded") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.failed") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.afterStart") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: Stop executes owned runtime cleanup",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	const auto stopPos = source.find("void ServiceManager::Stop()");
	REQUIRE(stopPos != std::string::npos);

	const auto stopBody = source.substr(stopPos);
	REQUIRE(
		stopBody.find("m_state.gatewayLifecycle.cleanupPath = \"normal_stop\";") !=
		std::string::npos);
	REQUIRE(
		stopBody.find("ExecuteGatewayOwnedRuntimeCleanup();") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: managed config auth generation enforcement exists",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	const auto applyDiffPos = source.find("bool ServiceManager::ApplyManagedRuntimeConfigDiff(");
	REQUIRE(applyDiffPos != std::string::npos);

	const auto applyDiffBody = source.substr(applyDiffPos);
	REQUIRE(
		applyDiffBody.find("HasAuthSensitiveConfigChanges(") !=
		std::string::npos);
	REQUIRE(
		applyDiffBody.find("gateway.authSessionGeneration") !=
		std::string::npos);
	REQUIRE(
		applyDiffBody.find("authSessionGenerationRejectCount") !=
		std::string::npos);
	REQUIRE(
		applyDiffBody.find("authGuard.warningMessage") !=
		std::string::npos);
	REQUIRE(
		applyDiffBody.find("EvaluateApplyPlan(") !=
		std::string::npos);
	REQUIRE(
		applyDiffBody.find("ApplyManagedRuntimeApplyPlan(") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: startup failure cleanup path is dedicated",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	const auto finalizePos = source.find("bool ServiceManager::FinalizeStartup(");
	REQUIRE(finalizePos != std::string::npos);

	const auto finalizeBody = source.substr(finalizePos);
	REQUIRE(
		finalizeBody.find("ExecuteGatewayStartupFailureCleanup(config, startupResult);") !=
		std::string::npos);

	REQUIRE(
		source.find("void ServiceManager::ExecuteGatewayStartupFailureCleanup(") !=
		std::string::npos);
	REQUIRE(
		source.find("m_state.gatewayLifecycle.cleanupPath = \"startup_failure\";") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: startup mode matrix labels are fully defined",
	"[servicemanager][startup][contract]")
{
	const auto coordinatorPath =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"bootstrap" /
		"GatewayRuntimeBootstrapCoordinator.cpp";
	std::ifstream in(coordinatorPath.string());
	REQUIRE(in.is_open());

	const std::string coordinatorSource(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	REQUIRE(coordinatorSource.find("raw == L\"disabled\"") != std::string::npos);
	REQUIRE(coordinatorSource.find("raw == L\"local_only\"") != std::string::npos);
	REQUIRE(coordinatorSource.find("raw == L\"full\" || raw == L\"transport\"") != std::string::npos);
	REQUIRE(coordinatorSource.find("return \"local_runtime_dispatch\";") != std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: lifecycle transition tracing is recorded",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	REQUIRE(
		source.find("void ServiceManager::RecordGatewayLifecycleTransition(") !=
		std::string::npos);
	REQUIRE(
		source.find("m_state.gatewayLifecycle.transitions.push_back") !=
		std::string::npos);
	REQUIRE(
		source.find("managed_reload.apply_observed") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: startup-failure cleanup includes non-gateway cleanup",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	REQUIRE(
		source.find("void ServiceManager::ExecuteNonGatewayRuntimeCleanup()") !=
		std::string::npos);
	REQUIRE(
		source.find("ExecuteNonGatewayRuntimeCleanup();") !=
		std::string::npos);
	REQUIRE(
		source.find("runtime_cancellation_state.cleared") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: skills host bridge callbacks gate host/UI persistence",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	REQUIRE(
		source.find("void ServiceManager::SetSkillsHostCallbacks(") !=
		std::string::npos);
	REQUIRE(
		source.find("m_skillsHostCallbacks.persistSkillConfigEnv") !=
		std::string::npos);
	REQUIRE(
		source.find("m_skillsHostCallbacks.refreshSkillView") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: runtime orchestration env policies resolve via bootstrap coordinator",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceLifecycleStartupCoordinatorSource();
	REQUIRE(
		source.find("ResolveRuntimeOrchestrationPolicySettings()") !=
		std::string::npos);
	REQUIRE(
		source.find("runtimeOrchestrationPolicy.startupSkillsRefreshEnabled") !=
		std::string::npos);
	REQUIRE(
		source.find("runtimeOrchestrationPolicy.startupHookBootstrapEnabled") !=
		std::string::npos);
	REQUIRE(
		source.find("runtimeOrchestrationPolicy.startupFixtureValidationEnabled") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager email contract: delegates runtime fallback, preflight health, and diagnostics projection",
	"[servicemanager][email][contract]")
{
	const std::string chatOrchestration = ReadChatRuntimeOrchestrationCoordinatorSource();
	REQUIRE(
		chatOrchestration.find("m_emailFallbackRuntimeCoordinator.EvaluateEmbeddedFailure(") !=
		std::string::npos);

	const std::string source = ReadServiceManagerSource();
	const std::string assembler = ReadOperatorDiagnosticsAssemblerSource();
	REQUIRE(
		source.find("m_emailPreflightHealthService.BuildRuntimeHealthIndex(false)") !=
		std::string::npos);
	REQUIRE(assembler.find("m_email.Apply(") != std::string::npos);
}

TEST_CASE(
	"ServiceManager phase1 contract: delegates chat orchestration, skills update handling, and skill projection",
	"[servicemanager][phase1][contract]")
{
	const std::string gatewayBinding = ReadGatewayHostBindingCoordinatorSource();
	const std::string chatOrchestration = ReadChatRuntimeOrchestrationCoordinatorSource();

	REQUIRE(
		gatewayBinding.find("PrepareChatRequest(") !=
		std::string::npos);
	REQUIRE(
		chatOrchestration.find("ExecuteProviderChatRuntimePath(") !=
		std::string::npos);
	REQUIRE(
		chatOrchestration.find("ExecuteChatRuntimeRequestBody(") !=
		std::string::npos);
	REQUIRE(
		gatewayBinding.find("m_skillsGatewayMethodHandler.HandleSkillsUpdate(") !=
		std::string::npos);

	const std::string source = ReadServiceManagerSource();
	REQUIRE(
		source.find("m_skillsCommandService.BuildEmbeddedToolBindings(") !=
		std::string::npos);
	REQUIRE(
		source.find("m_skillsGatewayProjectionService.BuildGatewaySkillEntry(") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager phase2 contract: delegates startup and managed config diff seams",
	"[servicemanager][phase2][contract]")
{
	const std::string lifecycle = ReadServiceLifecycleStartupCoordinatorSource();
	REQUIRE(
		lifecycle.find("m_skillsStartupCoordinator.Execute(") !=
		std::string::npos);
	REQUIRE(
		lifecycle.find("m_hooksStartupCoordinator.Execute(") !=
		std::string::npos);
	REQUIRE(
		lifecycle.find("m_fixtureStartupValidatorFacade.Execute(") !=
		std::string::npos);

	const std::string source = ReadServiceManagerSource();
	REQUIRE(
		source.find("m_managedRuntimeConfigDiffCoordinator.EvaluateApplyPlan(") !=
		std::string::npos);
	REQUIRE(
		source.find("m_managedRuntimeConfigDiffCoordinator.CoordinateLocalModelReload(") !=
		std::string::npos);
	REQUIRE(
		source.find("bool ServiceManager::ApplyManagedRuntimeApplyPlan(") !=
		std::string::npos);

	const std::string managedCoordinator = ReadManagedRuntimeConfigDiffCoordinatorHeader();
	REQUIRE(
		managedCoordinator.find("EvaluateApplyPlan(") != std::string::npos);
	REQUIRE(
		managedCoordinator.find("EvaluateAuthSessionGenerationGuard(") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager phase3 contract: delegates diagnostics projection to projector modules",
	"[servicemanager][phase3][contract]")
{
	const std::string source = ReadServiceManagerSource();
	REQUIRE(
		source.find("m_operatorDiagnosticsAssembler.Build(") !=
		std::string::npos);

	const std::string assembler = ReadOperatorDiagnosticsAssemblerSource();
	REQUIRE(assembler.find("m_gatewayLifecycle.Apply(") != std::string::npos);
	REQUIRE(assembler.find("m_embedded.Apply(") != std::string::npos);
	REQUIRE(assembler.find("m_modelRuntime.Apply(") != std::string::npos);
	REQUIRE(assembler.find("m_hooks.Apply(") != std::string::npos);
	REQUIRE(assembler.find("m_email.Apply(") != std::string::npos);
}

TEST_CASE(
	"ServiceManager phase4 contract: provider runtime, diagnostics assembler, prompt rewrite",
	"[servicemanager][phase4][contract]")
{
	const std::string source = ReadServiceManagerSource();

	REQUIRE(
		source.find("m_chatProviderRuntimeService.ExecuteProviderPath(") !=
		std::string::npos);
	REQUIRE(
		source.find("BuildChatProviderRuntimeBindings()") !=
		std::string::npos);
	REQUIRE(
		source.find("m_operatorDiagnosticsAssembler.Build(") !=
		std::string::npos);
	REQUIRE(
		source.find("OperatorDiagnosticsInputs") != std::string::npos);
	REQUIRE(
		source.find("RewriteInvocationPromptUtf8") !=
		std::string::npos);
	REQUIRE(
		source.find("m_deepSeekClient.InvokeGatewayChat(") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager gateway binding contract: skills and chat callbacks delegate to GatewayHostBindingCoordinator",
	"[servicemanager][gateway][contract]")
{
	const std::string source = ReadServiceManagerSource();
	REQUIRE(
		source.find("GatewayHostBindingCoordinator::RegisterSkillsRelatedCallbacks(*this)") !=
		std::string::npos);
	REQUIRE(
		source.find("GatewayHostBindingCoordinator::RegisterChatRuntimeCallbacks(*this)") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager gateway binding contract: Phase C policy and embeddings wire in GatewayHostBindingCoordinator",
	"[servicemanager][gateway][contract][phasec]")
{
	const std::string binding = ReadGatewayHostBindingCoordinatorSource();
	REQUIRE(binding.find("BindGatewayPolicyCallbacks(manager)") != std::string::npos);
	REQUIRE(binding.find("BindEmbeddingsCallbacks(manager)") != std::string::npos);
	REQUIRE(
		binding.find("void GatewayHostBindingCoordinator::BindGatewayPolicyCallbacks") !=
		std::string::npos);
	REQUIRE(
		binding.find("void GatewayHostBindingCoordinator::BindEmbeddingsCallbacks") !=
		std::string::npos);
	REQUIRE(binding.find("BuildGatewayPolicyBinding(") != std::string::npos);
	REQUIRE(binding.find("SetEmbeddingsGenerateCallback") != std::string::npos);
	REQUIRE(binding.find("SetEmbeddingsBatchCallback") != std::string::npos);

	const std::string managerSource = ReadServiceManagerSource();
	REQUIRE(managerSource.find("void ServiceManager::BindGatewayPolicyCallbacks") == std::string::npos);
	REQUIRE(managerSource.find("void ServiceManager::BindEmbeddingsCallbacks") == std::string::npos);
	REQUIRE(managerSource.find("void ServiceManager::BindToolRuntimeCallbacks") != std::string::npos);
}

TEST_CASE(
	"ServiceManager skills gateway contract: SkillsGatewayMethodHandler is single skills.update entry",
	"[servicemanager][skills][gateway][contract]")
{
	const std::string handlerHeader = ReadSkillsGatewayMethodHandlerHeader();
	REQUIRE(handlerHeader.find("Single entry") != std::string::npos);
	REQUIRE(handlerHeader.find("HandleSkillsUpdate") != std::string::npos);

	const std::string gatewayBinding = ReadGatewayHostBindingCoordinatorSource();
	REQUIRE(
		gatewayBinding.find("m_skillsGatewayMethodHandler.HandleSkillsUpdate(") !=
		std::string::npos);

	const std::string runtimeHandlers = ReadGatewayHostHandlersRuntimeSource();
	REQUIRE(runtimeHandlers.find("gateway.skills.update") != std::string::npos);
	REQUIRE(runtimeHandlers.find("m_skillsUpdateCallback(request)") != std::string::npos);
}

TEST_CASE(
	"ServiceManager skills refresh contract: agent descriptor policy and gateway publication coordinator",
	"[servicemanager][skills][contract]")
{
	const std::string source = ReadServiceManagerSource();
	REQUIRE(
		source.find("SkillsAgentCommandDescriptorPolicy::BuildDescriptors(") !=
		std::string::npos);
	REQUIRE(
		source.find("SkillsAgentCommandDescriptorPolicy::BuildReservedChatSlashCommandNamesNormalized(") !=
		std::string::npos);
	REQUIRE(
		source.find("SkillsGatewayPublicationCoordinator::RefreshProjection(*this)") !=
		std::string::npos);
	REQUIRE(
		source.find("SkillsGatewayPublicationCoordinator::PublishProjection(*this)") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: policy and module phases delegate to ServiceLifecycleStartupCoordinator",
	"[servicemanager][startup][contract][lifecycle]")
{
	const std::string managerSource = ReadServiceManagerSource();
	REQUIRE(
		managerSource.find(
			"ServiceLifecycleStartupCoordinator::ApplyConfigurePolicies(*this, config)") !=
		std::string::npos);
	REQUIRE(
		managerSource.find("ServiceLifecycleStartupCoordinator::RunInitializeModules(*this)") !=
		std::string::npos);

	const std::string lifecycleSource = ReadServiceLifecycleStartupCoordinatorSource();
	REQUIRE(
		lifecycleSource.find("ResolveHooksPolicySettings(") != std::string::npos);
	REQUIRE(
		lifecycleSource.find("ResolveEmailPolicySettings(") != std::string::npos);
	REQUIRE(
		lifecycleSource.find("AppendStartupTrace(\"ServiceManager.Start.policy.ready\")") !=
		std::string::npos);
}
