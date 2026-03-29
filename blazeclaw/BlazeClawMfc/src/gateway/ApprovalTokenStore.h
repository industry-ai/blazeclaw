#pragma once

#include <string>
#include <optional>
#include <mutex>

namespace blazeclaw::gateway {

class ApprovalTokenStore {
public:
    ApprovalTokenStore() = default;

    // Initialize store at given file path. If filePath is empty, a default
    // path under the gateway state directory will be used. Returns false if
    // initialization failed.
    bool Initialize(const std::string& filePath = {});

    // Persist a token -> payload mapping. Payload may be a JSON value or a
    // plain string. Returns true on success.
    bool SaveToken(const std::string& token, const std::string& payload);

    // Load a token payload. Returns nullopt if not found. The returned string
    // is a JSON textual representation when the stored value was JSON, or a
    // JSON string when a plain string was stored.
    std::optional<std::string> LoadToken(const std::string& token) const;

    // Remove token mapping
    bool RemoveToken(const std::string& token);

private:
    // Path to the approvals JSON file
    std::string m_filePath;
    // Protect concurrent access to the file
    mutable std::mutex m_mutex;
};

} // namespace blazeclaw::gateway
