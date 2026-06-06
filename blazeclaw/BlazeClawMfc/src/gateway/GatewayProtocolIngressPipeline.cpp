#include "pch.h"
#include "GatewayHostRuntimeBootstrapCoordinator.h"

#include "GatewayHost.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayPersistencePaths.h"
#include "PluginHostAdapter.h"
#include "Telemetry.h"
#include "python/PythonRuntimeDispatcher.h"
#include "cron/CronOpsService.h"

#include <Windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::gateway::GatewayHostRuntimeBootstrap {

	namespace {

		std::string ReadEnvironmentVariable(const char* name) {
			if (name == nullptr) {
				return {};
			}

			char* raw = nullptr;
			size_t size = 0;
			if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
				return {};
			}

			const std::string value(raw);
			free(raw);
			return value;
		}

		std::uint64_t CurrentEpochMs() {
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		}

	} // namespace

	std::string ResolveExtensionsCatalogPath() {
		const std::filesystem::path preferred =
			std::filesystem::path("blazeclaw") /
			"extensions" /
			"extensions.catalog.json";
		if (std::filesystem::exists(preferred)) {
			return preferred.string();
		}

		const std::filesystem::path fallback =
			std::filesystem::path("extensions") /
			"extensions.catalog.json";
		if (std::filesystem::exists(fallback)) {
			return fallback.string();
		}

		char modulePathBuffer[MAX_PATH] = {};
		const DWORD moduleChars =
			GetModuleFileNameA(nullptr, modulePathBuffer, MAX_PATH);
		if (moduleChars == 0 || moduleChars >= MAX_PATH) {
			return preferred.string();
		}

		const std::filesystem::path exeDir =
			std::filesystem::path(modulePathBuffer).parent_path();

		const std::vector<std::filesystem::path> relativeCandidates = {
			std::filesystem::path("..") / ".." / "extensions" / "extensions.catalog.json",
			std::filesystem::path("..") / ".." / ".." / "extensions" / "extensions.catalog.json",
			std::filesystem::path("..") / "blazeclaw" / "extensions" / "extensions.catalog.json",
		};

		for (const auto& relativeCandidate : relativeCandidates) {
			const std::filesystem::path candidate =
				std::filesystem::weakly_canonical(exeDir / relativeCandidate);
			if (std::filesystem::exists(candidate)) {
				return candidate.string();
			}
		}

		return preferred.string();
	}

	void EnsureOpsToolsRuntimeRegistered(GatewayToolRegistry& registry) {
		const ToolPreviewResult weatherPreview = registry.Preview("weather.lookup");
		const ToolPreviewResult emailPreview = registry.Preview("email.schedule");
		if (weatherPreview.allowed && emailPreview.allowed) {
			return;
		}

		const auto loadResult = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
		if (!loadResult.ok) {
			return;
		}

		const auto weatherResolve =
			PluginHostAdapter::ResolveExecutor("ops-tools", "weather.lookup", "");
		if (weatherResolve.resolved && weatherResolve.executor) {
			registry.RegisterRuntimeTool(
				ToolCatalogEntry{
					.id = "weather.lookup",
					.label = "Weather Lookup",
					.category = "data",
					.enabled = true,
				},
				weatherResolve.executor);
		}

		const auto emailResolve =
			PluginHostAdapter::ResolveExecutor("ops-tools", "email.schedule", "");
		if (emailResolve.resolved && emailResolve.executor) {
			registry.RegisterRuntimeTool(
				ToolCatalogEntry{
					.id = "email.schedule",
					.label = "Email Schedule",
					.category = "communication",
					.enabled = true,
				},
				emailResolve.executor);
		}
	}

	void EnsurePdfGeneratorRuntimeRegistered(GatewayToolRegistry& registry) {
		registry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "pdf_generator.generate",
				.label = "PDF Generator",
				.category = "document",
				.enabled = true,
			},
			python::PythonRuntimeDispatcher::CreateExecutor());
	}

	struct Access {
		static void ApplyRuntimeEnvironment(GatewayHost& host) {
			PluginHostAdapter::EnsureDefaultAdaptersRegistered();
			host.m_runtimeDeepSeekApiKey = ReadEnvironmentVariable("DEEPSEEK_API_KEY");
			const std::string deepSeekBaseUrl = ReadEnvironmentVariable("DEEPSEEK_BASE_URL");
			if (!deepSeekBaseUrl.empty()) {
				host.m_runtimeDeepSeekBaseUrl = deepSeekBaseUrl;
			}

			if (!host.m_runtimeDeepSeekApiKey.empty() &&
				(host.m_runtimeAgentModel == GatewayModel::kDefaultModelId ||
					host.m_runtimeAgentModel == GatewayModel::kReasonerModelId)) {
				host.m_runtimeAgentModel = host.m_runtimeDeepSeekDefaultModel;
			}
		}

		static std::string LoadExtensionsCatalog(GatewayHost& host) {
			const std::string catalogPath = ResolveExtensionsCatalogPath();
			const auto loadStartMs = CurrentEpochMs();
			host.m_toolRegistry.LoadExtensionToolsFromCatalog(catalogPath);
			const auto loadEndMs = CurrentEpochMs();
			EmitTelemetryEvent(
				"gateway.startup.extensions.catalog_load",
				std::string("{\"catalogPath\":") + JsonString(catalogPath) +
				",\"elapsedMs\":" +
				std::to_string(loadEndMs >= loadStartMs ? (loadEndMs - loadStartMs) : 0) +
				"}");
			return catalogPath;
		}

		static void LoadSkillRoots(GatewayHost& host) {
			host.m_runtimeResolvedSkillDirectories = host.ResolveAbsoluteSkillDirectories({
				"blazeclaw/skills-bundled",
				"blazeclaw/skills",
				"blazeclaw/skills-openclaw-original",
				"skills",
				"skills-openclaw-original",
			});
			host.EmitSkillRootDiagnostics("start_runtime_services", host.m_runtimeResolvedSkillDirectories);

			const auto aggregateStartMs = CurrentEpochMs();
			std::size_t startupSkillLoadedTotal = 0;
			for (const auto& directory : host.m_runtimeResolvedSkillDirectories) {
				const auto loadStartMs = CurrentEpochMs();
				const auto loadedCount = host.m_toolRegistry.LoadSkillToolsFromDirectory(directory);
				startupSkillLoadedTotal += loadedCount;
				const auto loadEndMs = CurrentEpochMs();
				EmitTelemetryEvent(
					"gateway.skills.root.load",
					std::string("{\"stage\":\"start_runtime_services\",\"root\":") + JsonString(directory) +
					",\"loadedCount\":" + std::to_string(loadedCount) +
					",\"elapsedMs\":" + std::to_string(loadEndMs >= loadStartMs ? (loadEndMs - loadStartMs) : 0) +
					"}");
			}

			const auto aggregateEndMs = CurrentEpochMs();
			EmitTelemetryEvent(
				"gateway.startup.skills.aggregate_load",
				std::string("{\"roots\":") + std::to_string(host.m_runtimeResolvedSkillDirectories.size()) +
				",\"loadedTotal\":" + std::to_string(startupSkillLoadedTotal) +
				",\"elapsedMs\":" +
				std::to_string(aggregateEndMs >= aggregateStartMs
					? (aggregateEndMs - aggregateStartMs)
					: 0) +
				"}");
		}

		static void ActivateExtensions(GatewayHost& host, const std::string& catalogPath) {
			const auto activationStartMs = CurrentEpochMs();
			host.m_extensionLifecycle.LoadCatalog(catalogPath);
			host.m_extensionLifecycle.ActivateAll(host.m_toolRegistry);
			const auto activationEndMs = CurrentEpochMs();
			EmitTelemetryEvent(
				"gateway.startup.extensions.activation",
				std::string("{\"catalogPath\":") + JsonString(catalogPath) +
				",\"elapsedMs\":" +
				std::to_string(activationEndMs >= activationStartMs
					? (activationEndMs - activationStartMs)
					: 0) +
				"}");

			const auto* extensionRegistry = host.m_pluginRuntimeState.RequireActiveRegistry(
				&host.m_extensionLifecycle.GetExtensions(),
				catalogPath,
				std::filesystem::current_path().string(),
				PluginRuntimeSubagentMode::GatewayBindable);
			for (const auto& extension : *extensionRegistry) {
				host.m_pluginRuntimeState.RecordImportedPluginId(extension.id);
			}
			host.m_pluginRuntimeState.ActivateRuntimeRegistry(
				extensionRegistry,
				catalogPath,
				std::filesystem::current_path().string(),
				PluginRuntimeSubagentMode::GatewayBindable,
				true,
				true);
		}

		static void RegisterOpsTools(GatewayHost& host) {
			EnsureOpsToolsRuntimeRegistered(host.m_toolRegistry);
			EnsurePdfGeneratorRuntimeRegistered(host.m_toolRegistry);

			host.m_toolRegistry.RegisterRuntimeTool(
				ToolCatalogEntry{
					.id = "python.script.run",
					.label = "Python Script Run",
					.category = "runtime",
					.enabled = true,
				},
				python::PythonRuntimeDispatcher::CreateExecutor());
			host.m_toolRegistry.RegisterRuntimeTool(
				ToolCatalogEntry{
					.id = "python.runtime.health",
					.label = "Python Runtime Health",
					.category = "runtime",
					.enabled = true,
				},
				python::PythonRuntimeDispatcher::CreateDiagnosticsExecutor());
			host.m_toolRegistry.RegisterRuntimeTool(
				ToolCatalogEntry{
					.id = "pdf_generator.generate",
					.label = "PDF Generator",
					.category = "document",
					.enabled = true,
				},
				python::PythonRuntimeDispatcher::CreateExecutor());
		}

		static void InitApprovalStore(GatewayHost& host) {
			host.m_approvalStore.Initialize(
				ResolveGatewayStateFilePath("approvals.json").string());
		}

		static void LoadTaskDeltas(GatewayHost& host) {
			host.LoadPersistedTaskDeltas();
		}

		static void WireCronScheduler(GatewayHost& host) {
			host.WireCronProductionIntegration();
			cron::GetCronOpsService().StartBackgroundScheduler();
		}

		static bool Run(GatewayHost& host) {
			ApplyRuntimeEnvironment(host);
			const std::string catalogPath = LoadExtensionsCatalog(host);
			LoadSkillRoots(host);
			ActivateExtensions(host, catalogPath);
			RegisterOpsTools(host);
			InitApprovalStore(host);
			LoadTaskDeltas(host);
			WireCronScheduler(host);
			return true;
		}
	};

	bool RunStartRuntimeServices(GatewayHost& host) {
		return Access::Run(host);
	}

} // namespace blazeclaw::gateway::GatewayHostRuntimeBootstrap
