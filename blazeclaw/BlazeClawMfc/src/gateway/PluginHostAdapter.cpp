#include "pch.h"
#include "PluginHostAdapter.h"

#include "executors/EmailScheduleExecutor.h"
#include "executors/LobsterExecutor.h"
#include "executors/WeatherLookupExecutor.h"
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace {
	std::unordered_map<std::string, blazeclaw::gateway::PluginExecutorFactory> g_extensionAdapters;
	std::unordered_map<std::string, blazeclaw::gateway::PluginExecutorFactory> g_toolAdapters;
	std::unordered_set<std::string> g_loadedExtensions;
	std::mutex g_adapterMutex;
}

namespace blazeclaw::gateway {

	PluginLoadResult PluginHostAdapter::LoadExtensionRuntime(
		const std::string& extensionId) {
		if (extensionId.empty()) {
			return PluginLoadResult{
				.ok = false,
				.code = "invalid_extension",
				.message = "extension_id_required",
			};
		}

		std::lock_guard<std::mutex> lock(g_adapterMutex);

		if (g_loadedExtensions.find(extensionId) != g_loadedExtensions.end()) {
			return PluginLoadResult{
				.ok = true,
				.code = "already_loaded",
				.message = {},
			};
		}

		if (g_extensionAdapters.find(extensionId) == g_extensionAdapters.end()) {
			return PluginLoadResult{
				.ok = false,
				.code = "adapter_not_registered",
				.message = extensionId,
			};
		}

		g_loadedExtensions.insert(extensionId);
		return PluginLoadResult{
			.ok = true,
			.code = "loaded",
			.message = {},
		};
	}

	PluginLoadResult PluginHostAdapter::UnloadExtensionRuntime(
		const std::string& extensionId) {
		if (extensionId.empty()) {
			return PluginLoadResult{
				.ok = false,
				.code = "invalid_extension",
				.message = "extension_id_required",
			};
		}

		std::lock_guard<std::mutex> lock(g_adapterMutex);
		const auto erased = g_loadedExtensions.erase(extensionId);
		return PluginLoadResult{
			.ok = true,
			.code = erased > 0 ? "unloaded" : "not_loaded",
			.message = {},
		};
	}

	PluginExecutorResolveResult PluginHostAdapter::ResolveExecutor(
		const std::string& extensionId,
		const std::string& toolId,
		const std::string& execPath) {
		std::lock_guard<std::mutex> lock(g_adapterMutex);

		if (g_loadedExtensions.find(extensionId) == g_loadedExtensions.end()) {
			return PluginExecutorResolveResult{
				.executor = nullptr,
				.resolved = false,
				.code = "extension_not_loaded",
				.message = extensionId,
			};
		}

		// Extension-specific adapters first.
		// This allows extension-level test/runtime overrides (e.g., ops-tools)
		// while still falling back to shared tool adapters when extension factory
		// intentionally returns null.
		auto itExt = g_extensionAdapters.find(extensionId);
		if (itExt != g_extensionAdapters.end() && itExt->second) {
			try {
				auto executor = itExt->second(extensionId, toolId, execPath);
				if (executor) {
					return PluginExecutorResolveResult{
						.executor = std::move(executor),
						.resolved = true,
						.code = "extension_adapter_resolved",
						.message = {},
					};
				}
			}
			catch (...) {
				return PluginExecutorResolveResult{
					.executor = nullptr,
					.resolved = false,
					.code = "extension_adapter_exception",
					.message = extensionId,
				};
			}
		}

		// Tool-specific adapters as fallback.
		auto itTool = g_toolAdapters.find(toolId);
		if (itTool != g_toolAdapters.end() && itTool->second) {
			try {
				auto executor = itTool->second(extensionId, toolId, execPath);
				if (!executor) {
					return PluginExecutorResolveResult{
						.executor = nullptr,
						.resolved = false,
						.code = "tool_adapter_null_executor",
						.message = toolId,
					};
				}

				return PluginExecutorResolveResult{
					.executor = std::move(executor),
					.resolved = true,
					.code = "tool_adapter_resolved",
					.message = {},
				};
			}
			catch (...) {
				return PluginExecutorResolveResult{
					.executor = nullptr,
					.resolved = false,
					.code = "tool_adapter_exception",
					.message = toolId,
				};
			}
		}

		return PluginExecutorResolveResult{
			.executor = nullptr,
			.resolved = false,
			.code = "adapter_not_registered",
			.message = extensionId,
		};
	}

	void PluginHostAdapter::RegisterExtensionAdapter(const std::string& extensionId, PluginExecutorFactory factory) {
		std::lock_guard<std::mutex> lock(g_adapterMutex);
		if (extensionId.empty()) return;
		g_extensionAdapters[extensionId] = std::move(factory);
	}

	void PluginHostAdapter::RegisterToolAdapter(const std::string& toolId, PluginExecutorFactory factory) {
		std::lock_guard<std::mutex> lock(g_adapterMutex);
		if (toolId.empty()) return;
		g_toolAdapters[toolId] = std::move(factory);
	}

	void PluginHostAdapter::UnregisterExtensionAdapter(const std::string& extensionId) {
		std::lock_guard<std::mutex> lock(g_adapterMutex);
		if (extensionId.empty()) return;
		g_extensionAdapters.erase(extensionId);
	}

	void PluginHostAdapter::UnregisterToolAdapter(const std::string& toolId) {
		std::lock_guard<std::mutex> lock(g_adapterMutex);
		if (toolId.empty()) return;
		g_toolAdapters.erase(toolId);
	}

	void PluginHostAdapter::EnsureDefaultAdaptersRegistered() {
		std::lock_guard<std::mutex> lock(g_adapterMutex);

		if (g_extensionAdapters.find("lobster") == g_extensionAdapters.end()) {
			g_extensionAdapters["lobster"] =
				[](const std::string&, const std::string&, const std::string& execPath) {
				return blazeclaw::gateway::executors::LobsterExecutor::Create(execPath);
				};
		}

		if (g_extensionAdapters.find("ops-tools") == g_extensionAdapters.end()) {
			g_extensionAdapters["ops-tools"] =
				[](const std::string&, const std::string&, const std::string&) {
				return GatewayToolRegistry::RuntimeToolExecutor{};
				};
		}

		if (g_toolAdapters.find("weather.lookup") == g_toolAdapters.end()) {
			g_toolAdapters["weather.lookup"] =
				[](const std::string&, const std::string&, const std::string&) {
				return blazeclaw::gateway::executors::WeatherLookupExecutor::Create();
				};
		}

		if (g_toolAdapters.find("email.schedule") == g_toolAdapters.end()) {
			g_toolAdapters["email.schedule"] =
				[](const std::string&, const std::string&, const std::string&) {
				return blazeclaw::gateway::executors::EmailScheduleExecutor::Create();
				};
		}
	}

	// Register built-in adapters at static init
	struct PluginHostAdapterRegisterDefaults {
		PluginHostAdapterRegisterDefaults() {
			PluginHostAdapter::EnsureDefaultAdaptersRegistered();
		}
	};

	static PluginHostAdapterRegisterDefaults s_pluginHostAdapterDefaults;

} // namespace blazeclaw::gateway
