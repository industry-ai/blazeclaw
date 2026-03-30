#include "gateway/ExtensionLifecycleManager.h"
#include "gateway/PluginHostAdapter.h"
#include "gateway/GatewayToolRegistry.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

using namespace blazeclaw::gateway;

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
    mgr.ActivateAll(registry);

    auto res = registry.Execute("lobster", std::nullopt);
    REQUIRE(res.executed);
    REQUIRE(res.output.find("dummy.exe") != std::string::npos);

    PluginHostAdapter::UnregisterExtensionAdapter("lobster");

    std::filesystem::remove_all(tmpRoot);
}

TEST_CASE("ActivateAll rejects executor for execPath outside allowed roots", "[extensionlifecycle]") {
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
    mgr.ActivateAll(registry);

    auto res = registry.Execute("lobster", std::nullopt);
    REQUIRE(!res.executed);
    REQUIRE(res.status == std::string("unavailable_runtime"));
    REQUIRE(!adapterCalled);

    PluginHostAdapter::UnregisterExtensionAdapter("lobster");
    std::filesystem::remove_all(tmpRoot);
    std::filesystem::remove_all(otherDir);
}
