#pragma once

#include "GatewayToolRegistry.h"
#include <string>

namespace blazeclaw::gateway {

class PluginHostAdapter {
public:
    // Create a runtime executor for the given extension/tool if a known adapter exists.
    // Returns nullptr when no adapter is available for the provided execPath/tool.
    static GatewayToolRegistry::RuntimeToolExecutor CreateExecutor(
        const std::string& extensionId,
        const std::string& toolId,
        const std::string& execPath);
};

} // namespace blazeclaw::gateway
