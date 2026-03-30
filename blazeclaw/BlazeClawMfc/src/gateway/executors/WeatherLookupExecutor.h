#pragma once

#include "../GatewayToolRegistry.h"

namespace blazeclaw::gateway::executors {

class WeatherLookupExecutor {
public:
    static GatewayToolRegistry::RuntimeToolExecutor Create();
};

} // namespace blazeclaw::gateway::executors
