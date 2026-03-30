#include "pch.h"
#include "PluginHostAdapter.h"

#include "executors/LobsterExecutor.h"

namespace blazeclaw::gateway {

GatewayToolRegistry::RuntimeToolExecutor PluginHostAdapter::CreateExecutor(
    const std::string& extensionId,
    const std::string& toolId,
    const std::string& execPath) {

    if (execPath.empty()) {
        return nullptr;
    }

    // Known adapter mappings
    if (extensionId == "lobster" || toolId == "lobster") {
        return blazeclaw::gateway::executors::LobsterExecutor::Create(execPath);
    }

    // No adapter available for this extension/tool
    return nullptr;
}

} // namespace blazeclaw::gateway
