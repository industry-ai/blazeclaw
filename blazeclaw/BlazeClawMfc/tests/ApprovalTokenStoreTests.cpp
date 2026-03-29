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

// LobsterExecutor unit tests
#include "gateway/executors/LobsterExecutor.h"

TEST_CASE("LobsterExecutor parse-suffix returns JSON-like suffix when present", "[lobster][parse]") {
    // replicate parsing behavior used by LobsterExecutor
    const std::string mixed = "Some logs\nMore logs\n{\"protocolVersion\":1,\"status\":\"ok\"}";

    auto tryParseSuffix = [&](const std::string& text) -> std::optional<std::string> {
        std::string trimmed = text;
        while (!trimmed.empty() && isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
        auto pos = trimmed.find_last_of("[{\n");
        if (pos == std::string::npos) return std::nullopt;
        const std::string candidate = trimmed.substr(pos);
        if (candidate.empty() || (candidate.front() != '{' && candidate.front() != '[')) return std::nullopt;
        return candidate;
    };

    auto parsed = tryParseSuffix(mixed);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value().find("protocolVersion") != std::string::npos);
}

TEST_CASE("LobsterExecutor argument validation: missing pipeline/resume token/approve", "[lobster][args]") {
    const std::string execPath = "dummy";
    auto executor = blazeclaw::gateway::executors::LobsterExecutor::Create(execPath);

    // missing args
    {
        const std::optional<std::string> noArgs = std::nullopt;
        auto res = executor("lobster", noArgs);
        REQUIRE(!res.executed);
        REQUIRE(res.status == std::string("invalid_args"));
    }

    // action run but missing pipeline
    {
        const std::string args = "{\"action\":\"run\"}";
        auto res = executor("lobster", args);
        REQUIRE(!res.executed);
        REQUIRE(res.status == std::string("invalid_args"));
        REQUIRE(res.output == std::string("pipeline_required"));
    }

    // action resume but missing token/approve
    {
        const std::string args = "{\"action\":\"resume\"}";
        auto res = executor("lobster", args);
        REQUIRE(!res.executed);
        REQUIRE(res.status == std::string("invalid_args"));
    }
}

int main(int argc, char** argv) {
    // Run Catch2 tests programmatically
    return Catch::Session().run(argc, argv);
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

