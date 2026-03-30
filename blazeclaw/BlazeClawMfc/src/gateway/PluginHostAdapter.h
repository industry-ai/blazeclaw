#pragma once

#include "GatewayToolRegistry.h"
#include <string>
#include <functional>

namespace blazeclaw::gateway {

using PluginExecutorFactory = std::function<GatewayToolRegistry::RuntimeToolExecutor(
    const std::string& extensionId,
    const std::string& toolId,
    const std::string& execPath)>;

class PluginHostAdapter {
public:
    // Create a runtime executor for the given extension/tool if a known adapter exists.
    // Returns nullptr when no adapter is available for the provided execPath/tool.
    static GatewayToolRegistry::RuntimeToolExecutor CreateExecutor(
        const std::string& extensionId,
        const std::string& toolId,
        const std::string& execPath);

    // Register an adapter factory for a specific extension id.
    // The factory may return a RuntimeToolExecutor when it can handle the provided execPath/tool.
    static void RegisterExtensionAdapter(const std::string& extensionId, PluginExecutorFactory factory);

    // Register an adapter factory for a specific tool id.
    static void RegisterToolAdapter(const std::string& toolId, PluginExecutorFactory factory);

    // Unregister previously registered adapters.
    static void UnregisterExtensionAdapter(const std::string& extensionId);
    static void UnregisterToolAdapter(const std::string& toolId);
};

} // namespace blazeclaw::gateway
