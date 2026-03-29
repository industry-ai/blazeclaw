#pragma once

#include <string>
#include <optional>

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
