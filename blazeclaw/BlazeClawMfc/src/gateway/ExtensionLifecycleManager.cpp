#include "pch.h"
#include "ExtensionLifecycleManager.h"
#include "GatewayJsonUtils.h"
#include "GatewayPersistencePaths.h"

#include "PluginHostAdapter.h"

#include <filesystem>
#include <windows.h>
#include <string>
#include <iostream>
#include <unordered_set>

#include <algorithm>
#include <fstream>

namespace blazeclaw::gateway {

static std::string DirectoryName(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return {};
    }

    return path.substr(0, pos);
}

static std::string JoinPath(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }

    if (right.empty()) {
        return left;
    }

    const bool leftEndsWithSeparator = left.back() == '/' || left.back() == '\\';
    if (leftEndsWithSeparator) {
        return left + right;
    }

    return left + "/" + right;
}

static std::string ReadFileUtf8(const std::string& filePath) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    std::string text;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size > 0) {
        text.resize(static_cast<std::size_t>(size));
        input.seekg(0, std::ios::beg);
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
    }

    return text;
}

static bool ExtractBoolField(const std::string& jsonText, const std::string& fieldName, const bool fallback) {
    bool value = fallback;
    if (json::FindBoolField(jsonText, fieldName, value)) {
        return value;
    }

    return fallback;
}

static std::vector<std::string> SplitTopLevelObjects(const std::string& arrayJson) {
    std::vector<std::string> objects;
    const std::string trimmed = json::Trim(arrayJson);
    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
        return objects;
    }

    std::size_t index = 1;
    while (index + 1 < trimmed.size()) {
        while (index + 1 < trimmed.size() &&
            (std::isspace(static_cast<unsigned char>(trimmed[index])) != 0 ||
                trimmed[index] == ',')) {
            ++index;
        }

        if (index + 1 >= trimmed.size() || trimmed[index] != '{') {
            break;
        }

        const std::size_t begin = index;
        int depth = 0;
        bool inString = false;
        for (; index < trimmed.size(); ++index) {
            const char ch = trimmed[index];
            if (inString) {
                if (ch == '\\') {
                    ++index;
                    continue;
                }

                if (ch == '"') {
                    inString = false;
                }
                continue;
            }

            if (ch == '"') {
                inString = true;
                continue;
            }

            if (ch == '{') {
                ++depth;
            }
            else if (ch == '}') {
                --depth;
                if (depth == 0) {
                    objects.push_back(trimmed.substr(begin, index - begin + 1));
                    ++index;
                    break;
                }
            }
        }
    }

    return objects;
}

static const char* ToStateName(const ExtensionRuntimeState state) {
    switch (state) {
    case ExtensionRuntimeState::discovered:
        return "discovered";
    case ExtensionRuntimeState::loaded:
        return "loaded";
    case ExtensionRuntimeState::active:
        return "active";
    case ExtensionRuntimeState::failed:
        return "failed";
    case ExtensionRuntimeState::deactivated:
        return "deactivated";
    default:
        return "unknown";
    }
}

static std::string BuildLifecycleLogLine(
    const std::string& extensionId,
    const ExtensionRuntimeState state,
    const std::string& code,
    const std::string& message) {
    std::string line = std::string("[ExtensionLifecycle] extension=") + extensionId +
        " state=" + ToStateName(state);
    if (!code.empty()) {
        line += " code=" + code;
    }
    if (!message.empty()) {
        line += " message=" + message;
    }
    line += "\n";
    return line;
}

static bool IsAllowedToolId(const std::string& toolId) {
    if (toolId.empty()) {
        return false;
    }

    bool previousDot = false;
    for (const char ch : toolId) {
        if (ch == '.') {
            if (previousDot) {
                return false;
            }
            previousDot = true;
            continue;
        }

        previousDot = false;
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' ||
            ch == '-') {
            continue;
        }

        return false;
    }

    return toolId.front() != '.' && toolId.back() != '.';
}

static bool ResolveExecPathForManifest(
    const ExtensionManifest& manifest,
    std::string& resolvedExecPath,
    std::string& errorCode,
    std::string& errorMessage) {
    resolvedExecPath.clear();
    errorCode.clear();
    errorMessage.clear();

    if (manifest.execPath.empty()) {
        return true;
    }

    try {
        const std::filesystem::path manifestDir =
            std::filesystem::weakly_canonical(
                std::filesystem::path(DirectoryName(manifest.path)));
        std::filesystem::path candidate = std::filesystem::path(manifest.execPath);
        if (!candidate.is_absolute()) {
            candidate = manifestDir / candidate;
        }

        std::filesystem::path canonicalExec;
        bool exists = false;
        try {
            canonicalExec = std::filesystem::canonical(candidate);
            exists = true;
        }
        catch (...) {
            canonicalExec = std::filesystem::weakly_canonical(candidate);
            exists = std::filesystem::exists(candidate);
        }

        const std::filesystem::path stateDir =
            std::filesystem::weakly_canonical(ResolveGatewayStateDirectory());

        auto startsWithRoot = [](
            const std::filesystem::path& path,
            const std::filesystem::path& root) {
                std::string pathText = path.string();
                std::string rootText = root.string();
                if (!rootText.empty() &&
                    rootText.back() != std::filesystem::path::preferred_separator) {
                    rootText.push_back(std::filesystem::path::preferred_separator);
                }
                return pathText.rfind(rootText, 0) == 0;
            };

        const bool allowed =
            startsWithRoot(canonicalExec, manifestDir) ||
            startsWithRoot(canonicalExec, stateDir);
        if (!allowed) {
            errorCode = "exec_path_outside_allowed_roots";
            errorMessage = candidate.string();
            return false;
        }

        if (!exists || !std::filesystem::is_regular_file(canonicalExec)) {
            errorCode = "exec_path_missing";
            errorMessage = candidate.string();
            return false;
        }

        resolvedExecPath = canonicalExec.string();
        return true;
    }
    catch (...) {
        errorCode = "exec_path_resolution_failed";
        errorMessage = manifest.execPath;
        return false;
    }
}

void ExtensionLifecycleManager::SetState(
    const std::string& extensionId,
    const ExtensionRuntimeState state,
    std::string code,
    std::string message) {
    if (extensionId.empty()) {
        return;
    }

    ExtensionStateSnapshot snapshot;
    snapshot.extensionId = extensionId;
    snapshot.state = state;
    snapshot.code = std::move(code);
    snapshot.message = std::move(message);
    m_states.insert_or_assign(extensionId, snapshot);

    const std::string logLine = BuildLifecycleLogLine(
        extensionId,
        state,
        snapshot.code,
        snapshot.message);
    OutputDebugStringA(logLine.c_str());
}

std::size_t ExtensionLifecycleManager::LoadCatalog(const std::string& catalogPath) {
    m_extensions.clear();
    m_states.clear();

    const std::string catalogText = ReadFileUtf8(catalogPath);
    if (catalogText.empty()) {
        return 0;
    }

    std::string extensionsRaw;
    if (!json::FindRawField(catalogText, "extensions", extensionsRaw)) {
        return 0;
    }

    const std::string catalogDirectory = DirectoryName(catalogPath);
    std::size_t registered = 0;
    for (const auto& entryJson : SplitTopLevelObjects(extensionsRaw)) {
        std::string id;
        if (!json::FindStringField(entryJson, "id", id) || id.empty()) {
            continue;
        }

        bool enabled = ExtractBoolField(entryJson, "enabled", true);

        std::string path;
        json::FindStringField(entryJson, "path", path);
        if (path.empty()) {
            continue;
        }

        ExtensionManifest manifest;
        manifest.id = id;
        manifest.path = JoinPath(catalogDirectory, path);
        manifest.enabled = enabled;
        SetState(manifest.id, ExtensionRuntimeState::discovered, "catalog_entry_loaded", {});

        // optional execPath
        std::string execPathVal;
        json::FindStringField(entryJson, "execPath", execPathVal);
        manifest.execPath = execPathVal;

        const std::string manifestText = ReadFileUtf8(manifest.path);
        if (manifestText.empty()) {
            SetState(
                manifest.id,
                ExtensionRuntimeState::failed,
                "manifest_unreadable",
                manifest.path);
            m_extensions.push_back(std::move(manifest));
            continue;
        }

        std::string toolsRaw;
        if (!json::FindRawField(manifestText, "tools", toolsRaw)) {
            m_extensions.push_back(manifest);
            continue;
        }

        for (const auto& toolJson : SplitTopLevelObjects(toolsRaw)) {
            std::string toolId;
            if (!json::FindStringField(toolJson, "id", toolId) || toolId.empty()) {
                continue;
            }

            std::string label;
            json::FindStringField(toolJson, "label", label);
            std::string category;
            json::FindStringField(toolJson, "category", category);
            bool toolEnabled = ExtractBoolField(toolJson, "enabled", true);

            manifest.tools.push_back(ExtensionToolManifest{toolId, label, category, toolEnabled});
            ++registered;
        }

        SetState(manifest.id, ExtensionRuntimeState::loaded, "manifest_loaded", {});
        m_extensions.push_back(std::move(manifest));
    }

    return registered;
}

std::vector<ExtensionLifecycleResult> ExtensionLifecycleManager::ActivateAll(
    GatewayToolRegistry& registry) {
    std::vector<ExtensionLifecycleResult> results;
    results.reserve(m_extensions.size());

    std::unordered_set<std::string> claimedToolIds;

    for (const auto& manifest : m_extensions) {
        ExtensionLifecycleResult result;
        result.extensionId = manifest.id;

        if (!manifest.enabled) {
            SetState(manifest.id, ExtensionRuntimeState::deactivated, "extension_disabled", {});
            result.success = true;
            result.code = "extension_disabled";
            results.push_back(std::move(result));
            continue;
        }

        const auto stateIt = m_states.find(manifest.id);
        if (stateIt != m_states.end() && stateIt->second.state == ExtensionRuntimeState::failed) {
            result.success = false;
            result.code = stateIt->second.code;
            result.message = stateIt->second.message;
            results.push_back(std::move(result));
            continue;
        }

        bool duplicateConflict = false;
        std::string duplicateToolId;
        for (const auto& tool : manifest.tools) {
            if (!tool.enabled) {
                continue;
            }

            if (!IsAllowedToolId(tool.id)) {
                SetState(
                    manifest.id,
                    ExtensionRuntimeState::failed,
                    "invalid_tool_id",
                    tool.id);
                duplicateConflict = true;
                break;
            }

            if (claimedToolIds.find(tool.id) != claimedToolIds.end()) {
                duplicateConflict = true;
                duplicateToolId = tool.id;
                break;
            }
        }

        if (duplicateConflict) {
            if (!duplicateToolId.empty()) {
                SetState(
                    manifest.id,
                    ExtensionRuntimeState::failed,
                    "duplicate_tool_id",
                    duplicateToolId);
            }

            result.success = false;
            result.code = m_states[manifest.id].code;
            result.message = m_states[manifest.id].message;
            results.push_back(std::move(result));
            continue;
        }

        std::string resolvedExecPath;
        std::string execErrorCode;
        std::string execErrorMessage;
        if (!ResolveExecPathForManifest(
                manifest,
                resolvedExecPath,
                execErrorCode,
                execErrorMessage)) {
            SetState(
                manifest.id,
                ExtensionRuntimeState::failed,
                execErrorCode,
                execErrorMessage);
            result.success = false;
            result.code = execErrorCode;
            result.message = execErrorMessage;
            results.push_back(std::move(result));
            continue;
        }

        const bool hasEnabledTools = std::any_of(
            manifest.tools.begin(),
            manifest.tools.end(),
            [](const ExtensionToolManifest& tool) {
                return tool.enabled;
            });

        if (hasEnabledTools) {
            const auto loadResult =
                PluginHostAdapter::LoadExtensionRuntime(manifest.id);
            if (!loadResult.ok) {
                SetState(
                    manifest.id,
                    ExtensionRuntimeState::failed,
                    loadResult.code,
                    loadResult.message);
                result.success = false;
                result.code = loadResult.code;
                result.message = loadResult.message;
                results.push_back(std::move(result));
                continue;
            }
        }

        std::vector<std::string> activatedToolIds;
        activatedToolIds.reserve(manifest.tools.size());
        bool executorResolutionFailed = false;
        std::string executorErrorCode;
        std::string executorErrorMessage;

        for (const auto& tool : manifest.tools) {
            if (!tool.enabled) {
                continue;
            }

            const auto resolveResult =
                PluginHostAdapter::ResolveExecutor(
                    manifest.id,
                    tool.id,
                    resolvedExecPath);
            if (!resolveResult.resolved || !resolveResult.executor) {
                executorResolutionFailed = true;
                executorErrorCode = resolveResult.code;
                executorErrorMessage =
                    resolveResult.message.empty()
                    ? tool.id
                    : resolveResult.message;
                break;
            }

            GatewayToolRegistry::RuntimeToolExecutor executor = resolveResult.executor;

            registry.RegisterRuntimeTool(
                ToolCatalogEntry{tool.id, tool.label, tool.category, tool.enabled},
                executor);
            claimedToolIds.insert(tool.id);
            activatedToolIds.push_back(tool.id);
            ++result.activatedTools;
        }

        if (executorResolutionFailed) {
            for (const auto& toolId : activatedToolIds) {
                registry.UnregisterRuntimeTool(toolId);
                claimedToolIds.erase(toolId);
            }

            if (hasEnabledTools) {
                PluginHostAdapter::UnloadExtensionRuntime(manifest.id);
            }

            SetState(
                manifest.id,
                ExtensionRuntimeState::failed,
                executorErrorCode.empty() ? "executor_resolution_failed" : executorErrorCode,
                executorErrorMessage);
            result.success = false;
            result.activatedTools = 0;
            result.code = executorErrorCode.empty()
                ? "executor_resolution_failed"
                : executorErrorCode;
            result.message = executorErrorMessage;
            results.push_back(std::move(result));
            continue;
        }

        SetState(manifest.id, ExtensionRuntimeState::active, "activated", {});
        result.success = true;
        result.code = "activated";
        results.push_back(std::move(result));
    }

    return results;
}

std::vector<ExtensionLifecycleResult> ExtensionLifecycleManager::DeactivateAll(
    GatewayToolRegistry& registry) {
    std::vector<ExtensionLifecycleResult> results;
    results.reserve(m_extensions.size());

    for (auto it = m_extensions.rbegin(); it != m_extensions.rend(); ++it) {
        const auto& manifest = *it;
        ExtensionLifecycleResult result;
        result.extensionId = manifest.id;

        for (const auto& tool : manifest.tools) {
            if (!tool.enabled) {
                continue;
            }
            registry.UnregisterRuntimeTool(tool.id);
            ++result.activatedTools;
        }

        const bool hasEnabledTools = std::any_of(
            manifest.tools.begin(),
            manifest.tools.end(),
            [](const ExtensionToolManifest& tool) {
                return tool.enabled;
            });

        if (hasEnabledTools) {
            const auto unloadResult =
                PluginHostAdapter::UnloadExtensionRuntime(manifest.id);
            if (!unloadResult.ok) {
                SetState(
                    manifest.id,
                    ExtensionRuntimeState::failed,
                    unloadResult.code,
                    unloadResult.message);
                result.success = false;
                result.code = unloadResult.code;
                result.message = unloadResult.message;
                results.push_back(std::move(result));
                continue;
            }
        }

        SetState(manifest.id, ExtensionRuntimeState::deactivated, "deactivated", {});
        result.success = true;
        result.code = "deactivated";
        results.push_back(std::move(result));
    }

    return results;
}

std::vector<ExtensionStateSnapshot> ExtensionLifecycleManager::GetStateSnapshots() const {
    std::vector<ExtensionStateSnapshot> snapshots;
    snapshots.reserve(m_states.size());
    for (const auto& [_, snapshot] : m_states) {
        snapshots.push_back(snapshot);
    }

    std::sort(
        snapshots.begin(),
        snapshots.end(),
        [](const ExtensionStateSnapshot& left, const ExtensionStateSnapshot& right) {
            return left.extensionId < right.extensionId;
        });

    return snapshots;
}

std::optional<ExtensionStateSnapshot> ExtensionLifecycleManager::GetStateSnapshot(
    const std::string& extensionId) const {
    const auto it = m_states.find(extensionId);
    if (it == m_states.end()) {
        return std::nullopt;
    }

    return it->second;
}

} // namespace blazeclaw::gateway
