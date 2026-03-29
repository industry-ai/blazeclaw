#pragma once

#include <string>
#include <optional>
#include <filesystem>
#include <fstream>
#include <vector>

#include "GatewayPersistencePaths.h"

namespace blazeclaw::gateway {

class ApprovalTokenStore {
public:
    ApprovalTokenStore() = default;

    // Initialize store at given file path. Returns false if initialization failed.
    bool Initialize(const std::string& filePath);

    // Persist a token -> payload mapping
    bool SaveToken(const std::string& token, const std::string& payload);

    // Load a token payload. Returns nullopt if not found.
    std::optional<std::string> LoadToken(const std::string& token) const;

    // Remove token mapping
    bool RemoveToken(const std::string& token);

private:
    std::string m_filePath;
};

} // namespace blazeclaw::gateway

// Inline implementations to ensure symbols are available without a separate .cpp file.
namespace blazeclaw::gateway {

inline bool ApprovalTokenStore::Initialize(const std::string& filePath) {
    if (filePath.empty()) {
        try {
            m_filePath = ResolveGatewayStateFilePath("approvals.txt").string();
        }
        catch (...) {
            m_filePath.clear();
            return false;
        }
    }
    else {
        m_filePath = filePath;
    }

    try {
        const std::filesystem::path dir = std::filesystem::path(m_filePath).parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
    }
    catch (...) {
        return false;
    }

    return true;
}

inline bool ApprovalTokenStore::SaveToken(const std::string& token, const std::string& payload) {
    if (token.empty() || m_filePath.empty()) return false;
    try {
        std::ofstream out(m_filePath, std::ios::app | std::ios::binary);
        if (!out.is_open()) return false;
        out << token << "\t" << payload << "\n";
        out.close();
        return true;
    }
    catch (...) {
        return false;
    }
}

inline std::optional<std::string> ApprovalTokenStore::LoadToken(const std::string& token) const {
    if (token.empty() || m_filePath.empty()) return std::nullopt;
    try {
        if (!std::filesystem::exists(m_filePath)) return std::nullopt;
        std::ifstream in(m_filePath, std::ios::binary);
        if (!in.is_open()) return std::nullopt;
        std::string line;
        while (std::getline(in, line)) {
            const auto tabPos = line.find('\t');
            if (tabPos == std::string::npos) continue;
            const std::string key = line.substr(0, tabPos);
            if (key == token) return line.substr(tabPos + 1);
        }
        return std::nullopt;
    }
    catch (...) {
        return std::nullopt;
    }
}

inline bool ApprovalTokenStore::RemoveToken(const std::string& token) {
    if (token.empty() || m_filePath.empty()) return false;
    try {
        if (!std::filesystem::exists(m_filePath)) return false;
        std::ifstream in(m_filePath, std::ios::binary);
        if (!in.is_open()) return false;
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            const auto tabPos = line.find('\t');
            if (tabPos == std::string::npos) continue;
            const std::string key = line.substr(0, tabPos);
            if (key == token) continue;
            lines.push_back(line);
        }
        in.close();
        std::ofstream out(m_filePath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        for (const auto& l : lines) out << l << "\n";
        out.close();
        return true;
    }
    catch (...) {
        return false;
    }
}

} // namespace blazeclaw::gateway
