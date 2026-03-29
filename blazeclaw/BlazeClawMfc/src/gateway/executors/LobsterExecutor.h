#pragma once

#include "../GatewayToolRegistry.h"

#include <string>
#include <optional>
#include <vector>
#include <sstream>
#include <windows.h>
#include <memory>
#include <algorithm>
#include "../GatewayJsonUtils.h"

namespace blazeclaw::gateway::executors {

class LobsterExecutor {
public:
    // Create a runtime executor callable matching GatewayToolRegistry::RuntimeToolExecutor
    // The returned callable captures any configuration needed (e.g., default exec path).
    static GatewayToolRegistry::RuntimeToolExecutor Create(const std::string& defaultExecPath);
};

} // namespace blazeclaw::gateway::executors
