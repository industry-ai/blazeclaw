#pragma once

#include "GatewayToolRegistry.h"
#include <string>
#include <functional>

namespace blazeclaw::gateway {

using PluginExecutorFactory = std::function<GatewayToolRegistry::RuntimeToolExecutor(
    const std::string& extensionId,
    const std::string& toolId,
    const std::string& execPath)>;

struct PluginLoadResult {
    bool ok = false;
    std::string code;
    std::string message;
};

struct PluginExecutorResolveResult {
    GatewayToolRegistry::RuntimeToolExecutor executor;
    bool resolved = false;
    std::string code;
    std::string message;
};

class PluginHostAdapter {
public:
    // Load runtime backend for an extension before resolving executors.
    static PluginLoadResult LoadExtensionRuntime(const std::string& extensionId);

    // Unload runtime backend for an extension.
    static PluginLoadResult UnloadExtensionRuntime(const std::string& extensionId);

    // Resolve a runtime executor for the given extension/tool.
    // Returns deterministic code/message when resolution fails.
    static PluginExecutorResolveResult ResolveExecutor(
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
