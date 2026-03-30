#include "gateway/ApprovalTokenStore.h"

#include <filesystem>
#include <iostream>
// Use Catch2 test runner via explicit main below
#include <catch2/catch_all.hpp>

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

TEST_CASE("ApprovalTokenStore handles escaped strings and nested objects", "[approvalstore][edge]") {
    const std::string tmpDir = std::filesystem::temp_directory_path().string() + "\\blazeclaw_test_edge";
    std::filesystem::create_directories(tmpDir);
    const std::string path = tmpDir + "\\approvals_test_edge.json";

    blazeclaw::gateway::ApprovalTokenStore store;
    REQUIRE(store.Initialize(path));

    const std::string token = "edge-token";
    const std::string payload = "{\"message\":\"Line1\\nLine2\\\" , \"nested\": { \"a\": 1 }}";

    REQUIRE(store.SaveToken(token, payload));

    auto loaded = store.LoadToken(token);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded.value().find("Line1") != std::string::npos);
    REQUIRE(loaded.value().find("nested") != std::string::npos);

    REQUIRE(store.RemoveToken(token));
}

TEST_CASE("ApprovalTokenStore stores non-json payload as string", "[approvalstore][edge]") {
    const std::string tmpDir = std::filesystem::temp_directory_path().string() + "\\blazeclaw_test_plain";
    std::filesystem::create_directories(tmpDir);
    const std::string path = tmpDir + "\\approvals_test_plain.json";

    blazeclaw::gateway::ApprovalTokenStore store;
    REQUIRE(store.Initialize(path));

    const std::string token = "plain-token";
    const std::string payload = "just a plain string with : and , and { braces }";

    REQUIRE(store.SaveToken(token, payload));

    auto loaded = store.LoadToken(token);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded.value().find("plain string") != std::string::npos);

    REQUIRE(store.RemoveToken(token));
}

int main(int argc, char** argv) {
    // Run Catch2 tests programmatically
    return Catch::Session().run(argc, argv);
}

