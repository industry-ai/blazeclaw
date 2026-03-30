#include "gateway/executors/LobsterExecutor.h"

#include <catch2/catch_all.hpp>
#include <filesystem>

using blazeclaw::gateway::executors::LobsterExecutor;

TEST_CASE("LobsterExecutor argument validation", "[lobster][args]") {
    auto executor = LobsterExecutor::Create("dummy");

    {
        const std::optional<std::string> noArgs = std::nullopt;
        const auto result = executor("lobster", noArgs);
        REQUIRE_FALSE(result.executed);
        REQUIRE(result.status == "invalid_args");
        REQUIRE(result.output.find("action_required") != std::string::npos);
    }

    {
        const std::string args = "{\"action\":\"run\"}";
        const auto result = executor("lobster", args);
        REQUIRE_FALSE(result.executed);
        REQUIRE(result.status == "invalid_args");
        REQUIRE(result.output.find("pipeline_required") != std::string::npos);
    }

    {
        const std::string args = "{\"action\":\"resume\"}";
        const auto result = executor("lobster", args);
        REQUIRE_FALSE(result.executed);
        REQUIRE(result.status == "invalid_args");
        REQUIRE(result.output.find("token_required") != std::string::npos);
    }
}

TEST_CASE("LobsterExecutor enforces cwd workspace policy", "[lobster][cwd]") {
    LobsterExecutor::Settings settings;
    settings.allowedWorkspaceRoots = {
        (std::filesystem::temp_directory_path() / "allowed-root").string()
    };
    settings.processRunner = [](const std::string&, const std::vector<std::string>&, unsigned long, std::size_t) {
        return LobsterExecutor::ProcessRunResult{
            LobsterExecutor::ProcessRunOutcome::Completed,
            "{\"protocolVersion\":1,\"ok\":true,\"status\":\"ok\",\"output\":[],\"requiresApproval\":null}",
            0,
        };
    };

    auto executor = LobsterExecutor::Create("dummy", settings);
    const std::string args = "{\"action\":\"run\",\"pipeline\":\"x\",\"cwd\":\"C:/outside/root\"}";
    const auto result = executor("lobster", args);

    REQUIRE_FALSE(result.executed);
    REQUIRE(result.status == "invalid_args");
    REQUIRE(result.output.find("invalid_cwd_outside_workspace") != std::string::npos);
}

TEST_CASE("LobsterExecutor maps timeout and output-limit outcomes", "[lobster][guardrails]") {
    {
        LobsterExecutor::Settings settings;
        settings.processRunner = [](const std::string&, const std::vector<std::string>&, unsigned long, std::size_t) {
            return LobsterExecutor::ProcessRunResult{
                LobsterExecutor::ProcessRunOutcome::TimedOut,
                "partial",
                -1,
            };
        };

        auto executor = LobsterExecutor::Create("dummy", settings);
        const std::string args = "{\"action\":\"run\",\"pipeline\":\"x\"}";
        const auto result = executor("lobster", args);

        REQUIRE_FALSE(result.executed);
        REQUIRE(result.status == "timeout");
        REQUIRE(result.output.find("\"code\":\"timeout\"") != std::string::npos);
    }

    {
        LobsterExecutor::Settings settings;
        settings.processRunner = [](const std::string&, const std::vector<std::string>&, unsigned long, std::size_t) {
            return LobsterExecutor::ProcessRunResult{
                LobsterExecutor::ProcessRunOutcome::OutputLimitExceeded,
                "too much output",
                -1,
            };
        };

        auto executor = LobsterExecutor::Create("dummy", settings);
        const std::string args = "{\"action\":\"run\",\"pipeline\":\"x\"}";
        const auto result = executor("lobster", args);

        REQUIRE_FALSE(result.executed);
        REQUIRE(result.status == "error");
        REQUIRE(result.output.find("output_limit_exceeded") != std::string::npos);
    }
}

TEST_CASE("LobsterExecutor normalizes success and nonzero-exit results", "[lobster][envelope]") {
    {
        LobsterExecutor::Settings settings;
        settings.processRunner = [](const std::string&, const std::vector<std::string>&, unsigned long, std::size_t) {
            return LobsterExecutor::ProcessRunResult{
                LobsterExecutor::ProcessRunOutcome::Completed,
                "logs\n{\"protocolVersion\":1,\"ok\":true,\"status\":\"needs_approval\",\"output\":[],\"requiresApproval\":{\"resumeToken\":\"r1\"}}",
                0,
            };
        };

        auto executor = LobsterExecutor::Create("dummy", settings);
        const std::string args = "{\"action\":\"run\",\"pipeline\":\"x\"}";
        const auto result = executor("lobster", args);

        REQUIRE(result.executed);
        REQUIRE(result.status == "needs_approval");
        REQUIRE(result.output.find("resumeToken") != std::string::npos);
    }

    {
        LobsterExecutor::Settings settings;
        settings.processRunner = [](const std::string&, const std::vector<std::string>&, unsigned long, std::size_t) {
            return LobsterExecutor::ProcessRunResult{
                LobsterExecutor::ProcessRunOutcome::Completed,
                "{\"status\":\"error\",\"message\":\"failed\"}",
                2,
            };
        };

        auto executor = LobsterExecutor::Create("dummy", settings);
        const std::string args = "{\"action\":\"resume\",\"token\":\"t\",\"approve\":true}";
        const auto result = executor("lobster", args);

        REQUIRE_FALSE(result.executed);
        REQUIRE(result.status == "error");
        REQUIRE(result.output.find("executor_nonzero_exit") != std::string::npos);
    }

    {
        LobsterExecutor::Settings settings;
        settings.processRunner = [](const std::string&, const std::vector<std::string>&, unsigned long, std::size_t) {
            return LobsterExecutor::ProcessRunResult{
                LobsterExecutor::ProcessRunOutcome::Completed,
                "plain text only",
                0,
            };
        };

        auto executor = LobsterExecutor::Create("dummy", settings);
        const std::string args = "{\"action\":\"run\",\"pipeline\":\"x\"}";
        const auto result = executor("lobster", args);

        REQUIRE_FALSE(result.executed);
        REQUIRE(result.status == "error");
        REQUIRE(result.output.find("invalid_executor_output") != std::string::npos);
    }
}
