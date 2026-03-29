#include "pch.h"
#include "ApprovalTokenStore.h"
#include "GatewayPersistencePaths.h"
#include "GatewayJsonUtils.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

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

static std::string EscapeJsonString(const std::string& v) {
    std::string out;
    out.reserve(v.size() + 8);
    for (char ch : v) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

// Parse a top-level JSON object into a map of key->raw value (value substring preserved)
static std::unordered_map<std::string, std::string> ParseTopLevelObject(const std::string& text) {
    std::unordered_map<std::string, std::string> out;
    const std::string trimmed = json::Trim(text);
    if (trimmed.size() < 2 || trimmed.front() != '{' || trimmed.back() != '}') return out;
    std::size_t i = 1;
    while (i + 1 < trimmed.size()) {
        while (i < trimmed.size() && (std::isspace(static_cast<unsigned char>(trimmed[i])) || trimmed[i] == ',')) ++i;
        if (i >= trimmed.size() || trimmed[i] != '"') break;
        std::string key; 
        if (!json::ParseJsonStringAt(trimmed, i, key)) break;
        while (i < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[i]))) ++i;
        if (i >= trimmed.size() || trimmed[i] != ':') break;
        ++i; while (i < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[i]))) ++i;
        if (i >= trimmed.size()) break;
        const std::size_t valBegin = i;
        char start = trimmed[i];
        if (start == '{' || start == '[') {
            int depth = 1; bool inString = false;
            ++i; // consume opening
            for (; i < trimmed.size(); ++i) {
                char ch = trimmed[i];
                if (inString) {
                    if (ch == '\\') { ++i; continue; }
                    if (ch == '"') inString = false;
                    continue;
                }
                if (ch == '"') { inString = true; continue; }
                if (ch == '{' || ch == '[') ++depth;
                else if (ch == '}' || ch == ']') { --depth; if (depth == 0) { ++i; break; } }
            }
        }
        else if (start == '"') {
            // use JSON string parser to handle escapes correctly
            std::string sval;
            std::size_t idx = i;
            if (json::ParseJsonStringAt(trimmed, idx, sval)) {
                // reconstruct as JSON string
                std::string raw = '"' + EscapeJsonString(sval) + '"';
                out.emplace(std::move(key), std::move(raw));
                i = idx;
                continue;
            }
            // fallback: naive skip
            ++i; while (i < trimmed.size()) { char ch = trimmed[i]; if (ch == '\\') { i += 2; continue; } if (ch == '"') { ++i; break; } ++i; }
        }
        else {
            while (i < trimmed.size() && trimmed[i] != ',' && trimmed[i] != '}') ++i;
        }
        const std::size_t valEnd = i;
        if (valEnd > valBegin) {
            out.emplace(key, trimmed.substr(valBegin, valEnd - valBegin)); 
        }
    }
    return out;
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

    std::string existing;
    if (!ReadFileToString(m_filePath, existing) || json::Trim(existing).empty()) {
        return WriteFileAtomic(m_filePath, "{}");
    }
    return true;
}

bool ApprovalTokenStore::SaveToken(const std::string& token, const std::string& payload) {
    if (token.empty() || m_filePath.empty()) return false;
    try {
        std::string content; ReadFileToString(m_filePath, content);
        auto map = ParseTopLevelObject(content);
        map[token] = payload;
        // Serialize
        std::ostringstream ss; ss << "{";
        bool first = true;
        for (const auto& kv : map) {
            if (!first) ss << ",";
            first = false;
            ss << '"' << EscapeJsonString(kv.first) << '"' << ':';
            // payload may already be a JSON value; write raw
            ss << kv.second;
        }
        ss << "}";
        return WriteFileAtomic(m_filePath, ss.str());
    } catch (...) { return false; }
}

std::optional<std::string> ApprovalTokenStore::LoadToken(const std::string& token) const {
    if (token.empty() || m_filePath.empty()) return std::nullopt;
    try {
        std::string content; if (!ReadFileToString(m_filePath, content)) return std::nullopt;
        auto map = ParseTopLevelObject(content);
        const auto it = map.find(token);
        if (it == map.end()) return std::nullopt;
        return it->second;
    } catch (...) { return std::nullopt; }
}

bool ApprovalTokenStore::RemoveToken(const std::string& token) {
    if (token.empty() || m_filePath.empty()) return false;
    try {
        std::string content; if (!ReadFileToString(m_filePath, content)) return false;
        auto map = ParseTopLevelObject(content);
        const auto it = map.find(token);
        if (it == map.end()) return false;
        map.erase(it);
        std::ostringstream ss; ss << "{"; bool first = true;
        for (const auto& kv : map) {
            if (!first) ss << ",";
            first = false;
            ss << '"' << EscapeJsonString(kv.first) << '"' << ':' << kv.second;
        }
        ss << "}";
        return WriteFileAtomic(m_filePath, ss.str());
    } catch (...) { return false; }
}

} // namespace blazeclaw::gateway
