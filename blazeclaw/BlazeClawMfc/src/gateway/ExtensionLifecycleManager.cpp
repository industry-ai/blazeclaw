#include "pch.h"
#include "ExtensionLifecycleManager.h"
#include "GatewayJsonUtils.h"

#include <algorithm>

namespace blazeclaw::gateway {

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

std::size_t ExtensionLifecycleManager::LoadCatalog(const std::string& catalogPath) {
    m_extensions.clear();
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

        const std::string manifestText = ReadFileUtf8(manifest.path);
        if (manifestText.empty()) {
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

        m_extensions.push_back(std::move(manifest));
    }

    return registered;
}

void ExtensionLifecycleManager::ActivateAll(GatewayToolRegistry& registry) const {
    for (const auto& manifest : m_extensions) {
        if (!manifest.enabled) {
            continue;
        }

        for (const auto& tool : manifest.tools) {
            registry.RegisterRuntimeTool(ToolCatalogEntry{tool.id, tool.label, tool.category, tool.enabled}, nullptr);
        }
    }
}

} // namespace blazeclaw::gateway
