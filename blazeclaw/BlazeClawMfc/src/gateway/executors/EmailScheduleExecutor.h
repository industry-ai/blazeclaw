#pragma once

#include "../GatewayToolRegistry.h"

namespace blazeclaw::gateway::executors {

class EmailScheduleExecutor {
public:
    static GatewayToolRegistry::RuntimeToolExecutor Create();
};

} // namespace blazeclaw::gateway::executors
