#pragma once

#include <string>
#include <optional>
#include <mutex>
#include <cstdint>

namespace blazeclaw::gateway {

struct ApprovalSessionRecord {
    std::string token;
    std::string type;
    std::string payloadJson;
    std::uint64_t createdAtEpochMs = 0;
    std::uint64_t expiresAtEpochMs = 0;
};

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

    // Persist a typed approval session with deterministic created/expiry metadata.
    bool SaveSession(const ApprovalSessionRecord& session);

    // Load a token payload. Returns nullopt if not found. The returned string
    // is a JSON textual representation when the stored value was JSON, or a
    // JSON string when a plain string was stored.
    std::optional<std::string> LoadToken(const std::string& token) const;

    // Load full session metadata. Returns nullopt if not found.
    std::optional<ApprovalSessionRecord> LoadSession(const std::string& token) const;

    // Check if token exists and is valid for the provided timestamp.
    bool IsTokenValid(
        const std::string& token,
        std::uint64_t nowEpochMs,
        ApprovalSessionRecord* outSession = nullptr) const;

    // Remove expired sessions and return prune count.
    std::size_t PruneExpired(std::uint64_t nowEpochMs);

    // Remove token mapping
    bool RemoveToken(const std::string& token);

private:
    // Path to the approvals JSON file
    std::string m_filePath;
    // Protect concurrent access to the file
    mutable std::mutex m_mutex;
};

} // namespace blazeclaw::gateway
