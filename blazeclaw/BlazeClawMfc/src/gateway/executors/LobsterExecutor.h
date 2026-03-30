#pragma once

#include "../GatewayToolRegistry.h"

#include <string>
#include <optional>
#include <vector>
#include <functional>
#include "../GatewayJsonUtils.h"

namespace blazeclaw::gateway::executors {

class LobsterExecutor {
public:
    enum class ProcessRunOutcome {
        Completed,
        SpawnFailed,
        TimedOut,
        OutputLimitExceeded,
    };

    struct ProcessRunResult {
        ProcessRunOutcome outcome = ProcessRunOutcome::SpawnFailed;
        std::string stdoutText;
        int exitCode = -1;
    };

    using ProcessRunner = std::function<ProcessRunResult(
        const std::string& execPath,
        const std::vector<std::string>& argv,
        unsigned long timeoutMs,
        std::size_t maxStdoutBytes)>;

    struct Settings {
        unsigned long timeoutMs = 20000;
        std::size_t maxStdoutBytes = 512000;
        std::size_t maxArgumentChars = 16384;
        bool enforceWorkspaceCwd = true;
        std::vector<std::string> allowedWorkspaceRoots;
        ProcessRunner processRunner;
    };

    // Create a runtime executor callable matching GatewayToolRegistry::RuntimeToolExecutor
    // The returned callable captures any configuration needed (e.g., default exec path).
    static GatewayToolRegistry::RuntimeToolExecutor Create(
        const std::string& defaultExecPath,
        Settings settings = {});
};

} // namespace blazeclaw::gateway::executors
