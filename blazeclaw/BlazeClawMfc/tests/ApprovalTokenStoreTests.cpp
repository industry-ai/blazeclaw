#include "gateway/ApprovalTokenStore.h"

#include <filesystem>
#include <iostream>
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

TEST_CASE("ApprovalTokenStore save/load/remove works", "[approvalstore]") {
    const std::string tmpDir = std::filesystem::temp_directory_path().string() + "\\blazeclaw_test";
    std::filesystem::create_directories(tmpDir);
    const std::string path = tmpDir + "\\approvals_test.json";

    blazeclaw::gateway::ApprovalTokenStore store;
    REQUIRE(store.Initialize(path));

    const std::string token = "unit-test-token";
    const std::string payload = "{\"to\":\"bob@example.com\",\"subject\":\"hi\"}";

    REQUIRE(store.SaveToken(token, payload));

    auto loaded = store.LoadToken(token);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded.value().find("bob@example.com") != std::string::npos);

    REQUIRE(store.RemoveToken(token));

    auto loaded2 = store.LoadToken(token);
    REQUIRE(!loaded2.has_value());
}
