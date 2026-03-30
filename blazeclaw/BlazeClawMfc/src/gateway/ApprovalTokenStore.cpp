#include "pch.h"
#include "ApprovalTokenStore.h"
#include "GatewayPersistencePaths.h"
#include "GatewayJsonUtils.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

static bool ReadFileToString(const std::string& path, std::string& out) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return false;
        out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return true;
    }
    catch (...) { return false; }
}

static bool WriteFileAtomic(const std::string& path, const std::string& content) {
    try {
        const std::filesystem::path p(path);
        const auto dir = p.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) std::filesystem::create_directories(dir);
        const std::string tmp = path + ".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        std::filesystem::rename(tmp, path);
        return true;
    }
    catch (...) { return false; }
}

static nlohmann::json ReadStoreJson(const std::string& path) {
    std::string content;
    if (!ReadFileToString(path, content) || gateway::json::Trim(content).empty()) {
        return nlohmann::json::object();
    }

    try {
        auto parsed = nlohmann::json::parse(content);
        if (!parsed.is_object()) {
            return nlohmann::json::object();
        }

        return parsed;
    }
    catch (...) {
        return nlohmann::json::object();
    }
}

static bool WriteStoreJson(const std::string& path, const nlohmann::json& root) {
    try {
        return WriteFileAtomic(path, root.dump(2));
    }
    catch (...) {
        return false;
    }
}

static std::optional<ApprovalSessionRecord> ParseSessionRecord(
    const std::string& token,
    const nlohmann::json& value) {
    if (token.empty()) {
        return std::nullopt;
    }

    ApprovalSessionRecord session;
    session.token = token;

    if (value.is_object() && value.contains("payload")) {
        const auto& payload = value["payload"];
        session.payloadJson = payload.dump();

        if (value.contains("type") && value["type"].is_string()) {
            session.type = value["type"].get<std::string>();
        }

        if (value.contains("createdAtEpochMs") &&
            value["createdAtEpochMs"].is_number_unsigned()) {
            session.createdAtEpochMs = value["createdAtEpochMs"].get<std::uint64_t>();
        }

        if (value.contains("expiresAtEpochMs") &&
            value["expiresAtEpochMs"].is_number_unsigned()) {
            session.expiresAtEpochMs = value["expiresAtEpochMs"].get<std::uint64_t>();
        }

        return session;
    }

    session.payloadJson = value.dump();
    return session;
}

bool ApprovalTokenStore::Initialize(const std::string& filePath) {
    if (filePath.empty()) {
        try { m_filePath = ResolveGatewayStateFilePath("approvals.json").string(); }
        catch (...) { m_filePath.clear(); return false; }
    } else m_filePath = filePath;

    try {
        const auto dir = std::filesystem::path(m_filePath).parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) std::filesystem::create_directories(dir);
    } catch (...) { return false; }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::string existing;
    if (!ReadFileToString(m_filePath, existing) || gateway::json::Trim(existing).empty()) {
        nlohmann::json j = nlohmann::json::object();
        return WriteStoreJson(m_filePath, j);
    }

    auto parsed = ReadStoreJson(m_filePath);
    return WriteStoreJson(m_filePath, parsed);
}

bool ApprovalTokenStore::SaveSession(const ApprovalSessionRecord& session) {
    if (session.token.empty() || m_filePath.empty()) {
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto root = ReadStoreJson(m_filePath);

        nlohmann::json payload = nullptr;
        try {
            payload = nlohmann::json::parse(session.payloadJson);
        }
        catch (...) {
            payload = session.payloadJson;
        }

        root[session.token] = nlohmann::json::object({
            { "payload", payload },
            { "type", session.type },
            { "createdAtEpochMs", session.createdAtEpochMs },
            { "expiresAtEpochMs", session.expiresAtEpochMs },
        });

        return WriteStoreJson(m_filePath, root);
    }
    catch (...) {
        return false;
    }
}

std::optional<ApprovalSessionRecord> ApprovalTokenStore::LoadSession(
    const std::string& token) const {
    if (token.empty() || m_filePath.empty()) {
        return std::nullopt;
    }

    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto root = ReadStoreJson(m_filePath);
        if (!root.contains(token)) {
            return std::nullopt;
        }

        return ParseSessionRecord(token, root[token]);
    }
    catch (...) {
        return std::nullopt;
    }
}

bool ApprovalTokenStore::IsTokenValid(
    const std::string& token,
    const std::uint64_t nowEpochMs,
    ApprovalSessionRecord* outSession) const {
    const auto session = LoadSession(token);
    if (!session.has_value()) {
        return false;
    }

    if (session->expiresAtEpochMs > 0 && nowEpochMs >= session->expiresAtEpochMs) {
        return false;
    }

    if (outSession != nullptr) {
        *outSession = session.value();
    }

    return true;
}

bool ApprovalTokenStore::SaveToken(const std::string& token, const std::string& payload) {
    if (token.empty() || m_filePath.empty()) {
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto root = ReadStoreJson(m_filePath);

        try {
            nlohmann::json pv = nlohmann::json::parse(payload);
            root[token] = pv;
        } catch (...) {
            root[token] = payload;
        }

        return WriteStoreJson(m_filePath, root);
    }
    catch (...) {
        return false;
    }
}

std::optional<std::string> ApprovalTokenStore::LoadToken(const std::string& token) const {
    if (token.empty() || m_filePath.empty()) {
        return std::nullopt;
    }

    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto root = ReadStoreJson(m_filePath);

        if (!root.contains(token)) {
            return std::nullopt;
        }

        if (root[token].is_object() && root[token].contains("payload")) {
            return root[token]["payload"].dump();
        }

        return root[token].dump();
    }
    catch (...) {
        return std::nullopt;
    }
}

std::size_t ApprovalTokenStore::PruneExpired(const std::uint64_t nowEpochMs) {
    if (m_filePath.empty()) {
        return 0;
    }

    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto root = ReadStoreJson(m_filePath);

        std::vector<std::string> expiredTokens;
        for (auto it = root.begin(); it != root.end(); ++it) {
            const auto parsed = ParseSessionRecord(it.key(), it.value());
            if (!parsed.has_value()) {
                continue;
            }

            if (parsed->expiresAtEpochMs > 0 && nowEpochMs >= parsed->expiresAtEpochMs) {
                expiredTokens.push_back(it.key());
            }
        }

        for (const auto& token : expiredTokens) {
            root.erase(token);
        }

        if (!expiredTokens.empty()) {
            WriteStoreJson(m_filePath, root);
        }

        return expiredTokens.size();
    }
    catch (...) {
        return 0;
    }
}

bool ApprovalTokenStore::RemoveToken(const std::string& token) {
    if (token.empty() || m_filePath.empty()) {
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto root = ReadStoreJson(m_filePath);
        if (!root.contains(token)) {
            return false;
        }

        root.erase(token);
        return WriteStoreJson(m_filePath, root);
    }
    catch (...) {
        return false;
    }
}

} // namespace blazeclaw::gateway
