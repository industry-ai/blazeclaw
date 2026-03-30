#include "gateway/PluginHostAdapter.h"
#include "gateway/GatewayToolRegistry.h"

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using blazeclaw::gateway::PluginHostAdapter;

TEST_CASE("Weather lookup tool adapter resolves and returns forecast", "[ops-tools][weather]") {
    const auto load = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
    REQUIRE(load.ok);

    const auto resolved =
        PluginHostAdapter::ResolveExecutor("ops-tools", "weather.lookup", "");
    REQUIRE(resolved.resolved);
    REQUIRE(resolved.executor);

    const std::string args = "{\"city\":\"Wuhan\",\"date\":\"tomorrow\"}";
    const auto result = resolved.executor("weather.lookup", args);

    REQUIRE(result.executed);
    REQUIRE(result.status == "ok");

    const auto payload = nlohmann::json::parse(result.output);
    REQUIRE(payload.value("ok", false));
    REQUIRE(payload.at("forecast").at("city").get<std::string>() == "Wuhan");

    const auto unload = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
    REQUIRE(unload.ok);
}

TEST_CASE("Email schedule tool adapter prepare and approve flow", "[ops-tools][email]") {
    const auto load = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
    REQUIRE(load.ok);

    const auto resolved =
        PluginHostAdapter::ResolveExecutor("ops-tools", "email.schedule", "");
    REQUIRE(resolved.resolved);
    REQUIRE(resolved.executor);

    const std::string prepareArgs =
        "{\"action\":\"prepare\",\"to\":\"jicheng@whu.edu.cn\",\"subject\":\"Wuhan weather report\",\"body\":\"Tomorrow cloudy around 20C.\",\"sendAt\":\"13:00\",\"ttlMinutes\":60}";
    const auto prepareResult = resolved.executor("email.schedule", prepareArgs);

    REQUIRE(prepareResult.executed);
    REQUIRE(prepareResult.status == "needs_approval");

    const auto preparePayload = nlohmann::json::parse(prepareResult.output);
    const auto token = preparePayload.at("requiresApproval").at("approvalToken").get<std::string>();
    REQUIRE_FALSE(token.empty());

    const std::string approveArgs =
        std::string("{\"action\":\"approve\",\"approvalToken\":\"") + token + "\",\"approve\":true}";
    const auto approveResult = resolved.executor("email.schedule", approveArgs);

    REQUIRE(approveResult.executed);
    REQUIRE(approveResult.status == "ok");

    const auto approvePayload = nlohmann::json::parse(approveResult.output);
    REQUIRE(approvePayload.at("status").get<std::string>() == "ok");

    const auto invalidReuseResult = resolved.executor("email.schedule", approveArgs);
    REQUIRE_FALSE(invalidReuseResult.executed);
    REQUIRE(invalidReuseResult.status == "invalid_args");

    const auto unload = PluginHostAdapter::UnloadExtensionRuntime("ops-tools");
    REQUIRE(unload.ok);
}
