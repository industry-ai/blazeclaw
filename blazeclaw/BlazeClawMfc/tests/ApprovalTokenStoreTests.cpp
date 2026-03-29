#include "pch.h"
#include "gateway/ApprovalTokenStore.h"

#include <cassert>
#include <filesystem>
#include <iostream>

int main() {
    const std::string tmpDir = std::filesystem::temp_directory_path().string() + "\\blazeclaw_test";
    std::filesystem::create_directories(tmpDir);
    const std::string path = tmpDir + "\\approvals_test.json";

    blazeclaw::gateway::ApprovalTokenStore store;
    if (!store.Initialize(path)) {
        std::cerr << "Initialize failed" << std::endl;
        return 2;
    }

    const std::string token = "unit-test-token";
    const std::string payload = "{\"to\":\"bob@example.com\",\"subject\":\"hi\"}";

    if (!store.SaveToken(token, payload)) {
        std::cerr << "SaveToken failed" << std::endl;
        return 3;
    }

    auto loaded = store.LoadToken(token);
    if (!loaded.has_value()) {
        std::cerr << "LoadToken returned nullopt" << std::endl;
        return 4;
    }

    if (loaded.value().find("bob@example.com") == std::string::npos) {
        std::cerr << "Loaded payload mismatch" << std::endl;
        return 5;
    }

    if (!store.RemoveToken(token)) {
        std::cerr << "RemoveToken failed" << std::endl;
        return 6;
    }

    auto loaded2 = store.LoadToken(token);
    if (loaded2.has_value()) {
        std::cerr << "Token still present after remove" << std::endl;
        return 7;
    }

    std::cout << "ApprovalTokenStoreTests: PASS" << std::endl;
    return 0;
}
