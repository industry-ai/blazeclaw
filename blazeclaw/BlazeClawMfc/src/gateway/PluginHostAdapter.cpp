#include "pch.h"
#include "PluginHostAdapter.h"

#include "executors/LobsterExecutor.h"
#include <unordered_map>
#include <mutex>

using namespace std::string_literals;

namespace {
    std::unordered_map<std::string, blazeclaw::gateway::PluginExecutorFactory> g_extensionAdapters;
    std::unordered_map<std::string, blazeclaw::gateway::PluginExecutorFactory> g_toolAdapters;
    std::mutex g_adapterMutex;
}

namespace blazeclaw::gateway {

GatewayToolRegistry::RuntimeToolExecutor PluginHostAdapter::CreateExecutor(
    const std::string& extensionId,
    const std::string& toolId,
    const std::string& execPath) {
    if (execPath.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_adapterMutex);

    // Tool-specific adapters take precedence
    auto itTool = g_toolAdapters.find(toolId);
    if (itTool != g_toolAdapters.end() && itTool->second) {
        try {
            return itTool->second(extensionId, toolId, execPath);
        } catch (...) {
            return nullptr;
        }
    }

    // Extension-specific adapters next
    auto itExt = g_extensionAdapters.find(extensionId);
    if (itExt != g_extensionAdapters.end() && itExt->second) {
        try {
            return itExt->second(extensionId, toolId, execPath);
        } catch (...) {
            return nullptr;
        }
    }

    // Fallback: built-in lobster handling for compatibility
    if (extensionId == "lobster"s || toolId == "lobster"s) {
        return blazeclaw::gateway::executors::LobsterExecutor::Create(execPath);
    }

    return nullptr;
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

// Register built-in adapters at static init
struct PluginHostAdapterRegisterDefaults {
    PluginHostAdapterRegisterDefaults() {
        PluginHostAdapter::RegisterExtensionAdapter("lobster", [](const std::string&, const std::string&, const std::string& execPath) {
            return blazeclaw::gateway::executors::LobsterExecutor::Create(execPath);
        });
    }
};

static PluginHostAdapterRegisterDefaults s_pluginHostAdapterDefaults;

} // namespace blazeclaw::gateway
