#include "gateway/ExtensionLifecycleManager.h"
#include "gateway/PluginHostAdapter.h"
#include "gateway/GatewayToolRegistry.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

using namespace blazeclaw::gateway;

namespace {

const auto kLoadedState = ExtensionRuntimeState::loaded;
const auto kActiveState = ExtensionRuntimeState::active;
const auto kFailedState = ExtensionRuntimeState::failed;
const auto kDeactivatedState = ExtensionRuntimeState::deactivated;

}

TEST_CASE("ActivateAll binds executor for allowed execPath", "[extensionlifecycle]") {
    const auto tmpRoot = std::filesystem::temp_directory_path() / ("blazeclaw_exttest_" + std::to_string(std::rand()));
    std::filesystem::create_directories(tmpRoot);

    const auto manifestDir = tmpRoot / "lobster";
    std::filesystem::create_directories(manifestDir);

    const auto manifestPath = manifestDir / "blazeclaw.extension.json";
    {
        std::ofstream out(manifestPath.string());
        out << "{\"tools\":[{\"id\":\"lobster\",\"label\":\"Lobster\"}]}";
    }

    const auto dummyExe = manifestDir / "dummy.exe";
    {
        std::ofstream out(dummyExe.string());
        out << "dummy";
    }

    const auto catalogPath = tmpRoot / "extensions.catalog.json";
    {
        std::ofstream out(catalogPath.string());
        out << "{\"version\":1,\"extensions\":[{\"id\":\"lobster\",\"path\":\"lobster/blazeclaw.extension.json\",\"enabled\":true,\"execPath\":\"dummy.exe\"}]}";
    }

    // register a test adapter that returns the execPath as output
    PluginHostAdapter::RegisterExtensionAdapter("lobster", [](const std::string&, const std::string&, const std::string& execPath) {
        return GatewayToolRegistry::RuntimeToolExecutor([execPath](const std::string& requestedTool, const std::optional<std::string>&) {
            return ToolExecuteResult{requestedTool, true, "ok", execPath};
        });
    });

    ExtensionLifecycleManager mgr;
    GatewayToolRegistry registry;

    REQUIRE(mgr.LoadCatalog(catalogPath.string()) == 1);
    const auto activationResults = mgr.ActivateAll(registry);
    REQUIRE(activationResults.size() == 1);
    REQUIRE(activationResults.front().success);
    REQUIRE(activationResults.front().activatedTools == 1);

    const auto lobsterState = mgr.GetStateSnapshot("lobster");
    REQUIRE(lobsterState.has_value());
    REQUIRE(lobsterState->state == kActiveState);

    auto res = registry.Execute("lobster", std::nullopt);
    REQUIRE(res.executed);
    REQUIRE(res.output.find("dummy.exe") != std::string::npos);

    const auto deactivationResults = mgr.DeactivateAll(registry);
    REQUIRE(deactivationResults.size() == 1);
    REQUIRE(deactivationResults.front().success);
    REQUIRE(deactivationResults.front().activatedTools == 1);

    const auto lobsterDeactivatedState = mgr.GetStateSnapshot("lobster");
    REQUIRE(lobsterDeactivatedState.has_value());
    REQUIRE(lobsterDeactivatedState->state == kDeactivatedState);

    PluginHostAdapter::UnregisterExtensionAdapter("lobster");

    std::filesystem::remove_all(tmpRoot);
}

TEST_CASE("ActivateAll fails when runtime adapter is not registered", "[extensionlifecycle]") {
    const auto tmpRoot = std::filesystem::temp_directory_path() /
        ("blazeclaw_exttest_" + std::to_string(std::rand()));
    std::filesystem::create_directories(tmpRoot);

    const auto manifestDir = tmpRoot / "custom-runtime";
    std::filesystem::create_directories(manifestDir);

    {
        std::ofstream out((manifestDir / "blazeclaw.extension.json").string());
        out << "{\"tools\":[{\"id\":\"custom.tool\",\"label\":\"Custom\"}]}";
    }

    const auto dummyExe = manifestDir / "dummy.exe";
    {
        std::ofstream out(dummyExe.string());
        out << "dummy";
    }

    const auto catalogPath = tmpRoot / "extensions.catalog.json";
    {
        std::ofstream out(catalogPath.string());
        out << "{\"version\":1,\"extensions\":[{\"id\":\"custom-runtime\","
               "\"path\":\"custom-runtime/blazeclaw.extension.json\","
               "\"enabled\":true,\"execPath\":\"dummy.exe\"}]}";
    }

    ExtensionLifecycleManager mgr;
    GatewayToolRegistry registry;

    REQUIRE(mgr.LoadCatalog(catalogPath.string()) == 1);
    const auto activationResults = mgr.ActivateAll(registry);
    REQUIRE(activationResults.size() == 1);
    REQUIRE_FALSE(activationResults.front().success);
    REQUIRE(activationResults.front().code == "adapter_not_registered");

    const auto extensionState = mgr.GetStateSnapshot("custom-runtime");
    REQUIRE(extensionState.has_value());
    REQUIRE(extensionState->state == kFailedState);
    REQUIRE(extensionState->code == "adapter_not_registered");

    std::filesystem::remove_all(tmpRoot);
}

TEST_CASE("ActivateAll fails extension for execPath outside allowed roots", "[extensionlifecycle]") {
    const auto tmpRoot = std::filesystem::temp_directory_path() / ("blazeclaw_exttest_" + std::to_string(std::rand()));
    std::filesystem::create_directories(tmpRoot);

    const auto manifestDir = tmpRoot / "lobster";
    std::filesystem::create_directories(manifestDir);

    const auto manifestPath = manifestDir / "blazeclaw.extension.json";
    {
        std::ofstream out(manifestPath.string());
        out << "{\"tools\":[{\"id\":\"lobster\",\"label\":\"Lobster\"}]}";
    }

    const auto otherDir = tmpRoot.parent_path() / "outside_dir";
    std::filesystem::create_directories(otherDir);
    const auto outsideExe = otherDir / "dummy_outside.exe";
    {
        std::ofstream out(outsideExe.string());
        out << "dummy";
    }

    const auto catalogPath = tmpRoot / "extensions.catalog.json";
    {
        std::ofstream out(catalogPath.string());
        // execPath points to an absolute path outside manifest dir
        out << "{\"version\":1,\"extensions\":[{\"id\":\"lobster\",\"path\":\"lobster/blazeclaw.extension.json\",\"enabled\":true,\"execPath\":\"" << outsideExe.string() << "\"}]}";
    }

    // Register an adapter that would succeed if called (should not be called)
    bool adapterCalled = false;
    PluginHostAdapter::RegisterExtensionAdapter("lobster", [&](const std::string&, const std::string&, const std::string& execPath) {
        adapterCalled = true;
        return GatewayToolRegistry::RuntimeToolExecutor([execPath](const std::string& requestedTool, const std::optional<std::string>&) {
            return ToolExecuteResult{requestedTool, true, "ok", execPath};
        });
    });

    ExtensionLifecycleManager mgr;
    GatewayToolRegistry registry;

    REQUIRE(mgr.LoadCatalog(catalogPath.string()) == 1);
    const auto activationResults = mgr.ActivateAll(registry);
    REQUIRE(activationResults.size() == 1);
    REQUIRE_FALSE(activationResults.front().success);
    REQUIRE(activationResults.front().code == "exec_path_outside_allowed_roots");

    const auto lobsterState = mgr.GetStateSnapshot("lobster");
    REQUIRE(lobsterState.has_value());
    REQUIRE(lobsterState->state == kFailedState);
    REQUIRE(lobsterState->code == "exec_path_outside_allowed_roots");

    auto res = registry.Execute("lobster", std::nullopt);
    REQUIRE(!res.executed);
    REQUIRE(res.status == std::string("blocked"));
    REQUIRE(res.output == std::string("unknown_tool"));
    REQUIRE(!adapterCalled);

    PluginHostAdapter::UnregisterExtensionAdapter("lobster");
    std::filesystem::remove_all(tmpRoot);
    std::filesystem::remove_all(otherDir);
}

TEST_CASE("ActivateAll marks duplicate tool extension as failed", "[extensionlifecycle]") {
    const auto tmpRoot = std::filesystem::temp_directory_path() /
        ("blazeclaw_exttest_" + std::to_string(std::rand()));
    std::filesystem::create_directories(tmpRoot);

    const auto alphaDir = tmpRoot / "alpha";
    const auto betaDir = tmpRoot / "beta";
    std::filesystem::create_directories(alphaDir);
    std::filesystem::create_directories(betaDir);

    {
        std::ofstream out((alphaDir / "blazeclaw.extension.json").string());
        out << "{\"tools\":[{\"id\":\"weather.lookup\",\"label\":\"Weather\"}]}";
    }
    {
        std::ofstream out((betaDir / "blazeclaw.extension.json").string());
        out << "{\"tools\":[{\"id\":\"weather.lookup\",\"label\":\"Weather2\"}]}";
    }

    const auto catalogPath = tmpRoot / "extensions.catalog.json";
    {
        std::ofstream out(catalogPath.string());
        out << "{\"version\":1,\"extensions\":["
               "{\"id\":\"alpha\",\"path\":\"alpha/blazeclaw.extension.json\",\"enabled\":true},"
               "{\"id\":\"beta\",\"path\":\"beta/blazeclaw.extension.json\",\"enabled\":true}]"
               "}";
    }

    ExtensionLifecycleManager mgr;
    GatewayToolRegistry registry;

    REQUIRE(mgr.LoadCatalog(catalogPath.string()) == 2);
    const auto activationResults = mgr.ActivateAll(registry);
    REQUIRE(activationResults.size() == 2);
    REQUIRE(activationResults.front().success);
    REQUIRE_FALSE(activationResults.back().success);
    REQUIRE(activationResults.back().code == "duplicate_tool_id");

    const auto alphaState = mgr.GetStateSnapshot("alpha");
    REQUIRE(alphaState.has_value());
    REQUIRE(alphaState->state == kActiveState);

    const auto betaState = mgr.GetStateSnapshot("beta");
    REQUIRE(betaState.has_value());
    REQUIRE(betaState->state == kFailedState);
    REQUIRE(betaState->code == "duplicate_tool_id");
    REQUIRE(betaState->message == "weather.lookup");

    std::filesystem::remove_all(tmpRoot);
}

TEST_CASE("LoadCatalog tracks discovered and loaded states", "[extensionlifecycle]") {
    const auto tmpRoot = std::filesystem::temp_directory_path() /
        ("blazeclaw_exttest_" + std::to_string(std::rand()));
    std::filesystem::create_directories(tmpRoot);

    const auto validDir = tmpRoot / "valid";
    const auto missingDir = tmpRoot / "missing";
    std::filesystem::create_directories(validDir);
    std::filesystem::create_directories(missingDir);

    {
        std::ofstream out((validDir / "blazeclaw.extension.json").string());
        out << "{\"tools\":[{\"id\":\"lobster\",\"label\":\"Lobster\"}]}";
    }

    const auto catalogPath = tmpRoot / "extensions.catalog.json";
    {
        std::ofstream out(catalogPath.string());
        out << "{\"version\":1,\"extensions\":["
               "{\"id\":\"valid\",\"path\":\"valid/blazeclaw.extension.json\",\"enabled\":true},"
               "{\"id\":\"missing\",\"path\":\"missing/blazeclaw.extension.json\",\"enabled\":true}]"
               "}";
    }

    ExtensionLifecycleManager mgr;
    REQUIRE(mgr.LoadCatalog(catalogPath.string()) == 1);

    const auto validState = mgr.GetStateSnapshot("valid");
    REQUIRE(validState.has_value());
    REQUIRE(validState->state == kLoadedState);

    const auto missingState = mgr.GetStateSnapshot("missing");
    REQUIRE(missingState.has_value());
    REQUIRE(missingState->state == kFailedState);
    REQUIRE(missingState->code == "manifest_unreadable");

    std::filesystem::remove_all(tmpRoot);
}
