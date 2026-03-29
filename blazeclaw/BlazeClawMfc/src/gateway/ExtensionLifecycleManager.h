#pragma once

#include "GatewayToolRegistry.h"
#include <string>
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
    bool enabled = true;
    std::vector<ExtensionToolManifest> tools;
};

class ExtensionLifecycleManager {
public:
    ExtensionLifecycleManager() = default;

    // Load extension manifests from a catalog file. Returns number of tools discovered.
    std::size_t LoadCatalog(const std::string& catalogPath);

    // Activate extensions by registering their declared tools into the provided registry.
    void ActivateAll(GatewayToolRegistry& registry) const;

    const std::vector<ExtensionManifest>& GetExtensions() const noexcept { return m_extensions; }

private:
    std::vector<ExtensionManifest> m_extensions;
};

} // namespace blazeclaw::gateway
