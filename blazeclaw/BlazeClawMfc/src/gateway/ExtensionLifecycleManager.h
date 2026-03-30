#pragma once

#include "GatewayToolRegistry.h"
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

namespace blazeclaw::gateway {

struct ExtensionToolManifest {
    std::string id;
    std::string label;
    std::string category;
    bool enabled = true;
};

struct ExtensionManifest {
    std::string id;
    std::string path;
    std::string kind;
    std::string execPath;
    bool enabled = true;
    std::vector<ExtensionToolManifest> tools;
};

enum class ExtensionRuntimeState {
    discovered,
    loaded,
    active,
    failed,
    deactivated,
};

struct ExtensionStateSnapshot {
    std::string extensionId;
    ExtensionRuntimeState state = ExtensionRuntimeState::discovered;
    std::string code;
    std::string message;
};

struct ExtensionLifecycleResult {
    std::string extensionId;
    bool success = false;
    std::size_t activatedTools = 0;
    std::string code;
    std::string message;
};

class ExtensionLifecycleManager {
public:
    ExtensionLifecycleManager() = default;

    // Load extension manifests from a catalog file. Returns number of tools discovered.
    std::size_t LoadCatalog(const std::string& catalogPath);

    // Activate extensions by registering their declared tools into the provided registry.
    std::vector<ExtensionLifecycleResult> ActivateAll(GatewayToolRegistry& registry);

    // Deactivate all previously activated tools from the registry.
    std::vector<ExtensionLifecycleResult> DeactivateAll(GatewayToolRegistry& registry);

    const std::vector<ExtensionManifest>& GetExtensions() const noexcept { return m_extensions; }
    std::vector<ExtensionStateSnapshot> GetStateSnapshots() const;
    std::optional<ExtensionStateSnapshot> GetStateSnapshot(const std::string& extensionId) const;

private:
    void SetState(
        const std::string& extensionId,
        ExtensionRuntimeState state,
        std::string code = {},
        std::string message = {});

    std::vector<ExtensionManifest> m_extensions;
    std::unordered_map<std::string, ExtensionStateSnapshot> m_states;
};

} // namespace blazeclaw::gateway
