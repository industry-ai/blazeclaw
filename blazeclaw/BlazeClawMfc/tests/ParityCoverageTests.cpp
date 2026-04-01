#include "gateway/ExtensionLifecycleManager.h"
#include "gateway/GatewayJsonUtils.h"
#include "gateway/GatewayToolRegistry.h"
#include "gateway/PluginHostAdapter.h"
#include "gateway/executors/EmailScheduleExecutor.h"

#include <catch2/catch_all.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace blazeclaw::gateway;

TEST_CASE("Parity coverage: lifecycle activation/deactivation updates tool catalog", "[parity][lifecycle]") {
    PluginHostAdapter::RegisterExtensionAdapter(
        "ops-tools",
        [](const std::string&, const std::string&, const std::string&) {
            return GatewayToolRegistry::RuntimeToolExecutor{};
        });

    const auto tmpRoot = std::filesystem::temp_directory_path() /
        ("blazeclaw_parity_" + std::to_string(std::rand()));
    std::filesystem::create_directories(tmpRoot);

    const auto extDir = tmpRoot / "ops";
    std::filesystem::create_directories(extDir);

    {
        std::ofstream out((extDir / "blazeclaw.extension.json").string());
        out << "{\"tools\":[{\"id\":\"weather.lookup\",\"label\":\"Weather Lookup\",\"category\":\"ops\",\"enabled\":true}]}";
    }

    {
        std::ofstream out((tmpRoot / "extensions.catalog.json").string());
        out << "{\"version\":1,\"extensions\":[{\"id\":\"ops-tools\",\"path\":\"ops/blazeclaw.extension.json\",\"enabled\":true}]}";
    }

    ExtensionLifecycleManager lifecycle;
    GatewayToolRegistry registry;

    REQUIRE(lifecycle.LoadCatalog((tmpRoot / "extensions.catalog.json").string()) == 1);

    const auto activated = lifecycle.ActivateAll(registry);
    REQUIRE(activated.size() == 1);
    REQUIRE(activated.front().success);

    const auto afterActivate = registry.List();
    REQUIRE_FALSE(afterActivate.empty());
    REQUIRE(std::any_of(afterActivate.begin(), afterActivate.end(), [](const ToolCatalogEntry& t) {
        return t.id == "weather.lookup";
    }));

    const auto deactivated = lifecycle.DeactivateAll(registry);
    REQUIRE(deactivated.size() == 1);
    REQUIRE(deactivated.front().success);

    const auto afterDeactivate = registry.List();
    REQUIRE(std::none_of(afterDeactivate.begin(), afterDeactivate.end(), [](const ToolCatalogEntry& t) {
        return t.id == "weather.lookup";
    }));

    std::filesystem::remove_all(tmpRoot);
}

TEST_CASE("Parity coverage: tool call sequence supports approval prepare and approve", "[parity][approval]") {
    GatewayToolRegistry registry;

    PluginHostAdapter::RegisterExtensionAdapter(
        "ops-tools",
        [](const std::string&, const std::string&, const std::string&) {
            return GatewayToolRegistry::RuntimeToolExecutor{};
        });

    const auto loaded = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
    REQUIRE(loaded.ok);

    const auto resolved =
        PluginHostAdapter::ResolveExecutor("ops-tools", "email.schedule", "");

    REQUIRE(resolved.resolved);
    REQUIRE(resolved.executor);

    registry.RegisterRuntimeTool(
        ToolCatalogEntry{
            .id = "email.schedule",
            .label = "Email Schedule",
            .category = "ops",
            .enabled = true,
        },
        resolved.executor);

    const auto prepare = registry.Execute(
        "email.schedule",
        std::string("{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Report\",\"body\":\"Tomorrow cloudy\",\"sendAt\":\"13:00\"}"));

    REQUIRE(prepare.executed);
    REQUIRE(prepare.status == "needs_approval");
    REQUIRE(prepare.output.find("approvalToken") != std::string::npos);

    const auto tokenStart = prepare.output.find("\"approvalToken\":\"");
    REQUIRE(tokenStart != std::string::npos);
    const auto tokenValueStart = tokenStart + std::string("\"approvalToken\":\"").size();
    const auto tokenValueEnd = prepare.output.find('"', tokenValueStart);
    REQUIRE(tokenValueEnd != std::string::npos);
    const std::string token = prepare.output.substr(tokenValueStart, tokenValueEnd - tokenValueStart);
    REQUIRE_FALSE(token.empty());

    const auto approve = registry.Execute(
        "email.schedule",
        std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token + "\",\"approve\":true}");

    REQUIRE(approve.executed);
    REQUIRE(approve.status == "ok");

    const auto unloaded = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
    REQUIRE(unloaded.ok);
}

TEST_CASE("Parity coverage: prompt intent parser handles now and today", "[parity][chat][orchestration]") {
    const auto intent = prompt::AnalyzeWeatherEmailPromptIntent(
        "Check today's weather in Wuhan, write a short report, and email it to jichengwhu@163.com now.");

    REQUIRE(intent.matched);
    REQUIRE(intent.hasWeather);
    REQUIRE(intent.hasEmail);
    REQUIRE(intent.hasReport);
    REQUIRE(intent.hasRecipient);
    REQUIRE(intent.hasSchedule);
    REQUIRE(intent.date == "today");
    REQUIRE(intent.scheduleKind == "immediate_keyword");
    REQUIRE(intent.sendAt.size() == 5);
    REQUIRE(intent.decompositionSteps == 3);
}

TEST_CASE("Parity coverage: prompt intent parser handles explicit time schedule", "[parity][chat][orchestration]") {
    const auto intent = prompt::AnalyzeWeatherEmailPromptIntent(
        "Check tomorrow weather in Wuhan and email it to jichengwhu@163.com at 3pm.");

    REQUIRE(intent.matched);
    REQUIRE(intent.hasWeather);
    REQUIRE(intent.hasEmail);
    REQUIRE(intent.hasRecipient);
    REQUIRE(intent.date == "tomorrow");
    REQUIRE(intent.hasSchedule);
    REQUIRE(intent.scheduleKind == "clock_time");
    REQUIRE(intent.sendAt == "15:00");
}

TEST_CASE("Parity coverage: prompt intent parser reports miss reasons", "[parity][chat][orchestration]") {
    const auto intent = prompt::AnalyzeWeatherEmailPromptIntent(
        "Please summarize today's traffic updates.");

    REQUIRE_FALSE(intent.matched);
    REQUIRE(std::find(
        intent.missReasons.begin(),
        intent.missReasons.end(),
        "missing_weather") != intent.missReasons.end());
    REQUIRE(std::find(
        intent.missReasons.begin(),
        intent.missReasons.end(),
        "missing_email_action") != intent.missReasons.end());
    REQUIRE(std::find(
        intent.missReasons.begin(),
        intent.missReasons.end(),
        "missing_recipient_email") != intent.missReasons.end());
}
