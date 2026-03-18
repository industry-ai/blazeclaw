#include "pch.h"
#include "GatewayPersistencePaths.h"

#include <cstdlib>
#include <string>

namespace blazeclaw::gateway {
    namespace {
        std::filesystem::path ResolveFallbackStateDirectory() {
            return std::filesystem::temp_directory_path() /
                "BlazeClaw" /
                "state";
        }

        std::filesystem::path ResolveLocalAppDataRoot() {
            char* localAppData = nullptr;
            std::size_t requiredSize = 0;
            const errno_t err = _dupenv_s(
                &localAppData,
                &requiredSize,
                "LOCALAPPDATA");
            if (err != 0 || localAppData == nullptr || requiredSize == 0) {
                if (localAppData != nullptr) {
                    std::free(localAppData);
                }
                return {};
            }

            std::filesystem::path path(localAppData);
            std::free(localAppData);
            return path;
        }
    }

    std::filesystem::path ResolveGatewayStateDirectory() noexcept {
        try {
            const std::filesystem::path localAppData = ResolveLocalAppDataRoot();
            if (!localAppData.empty()) {
                return localAppData /
                    "BlazeClaw" /
                    "state";
            }

            return ResolveFallbackStateDirectory();
        }
        catch (...) {
            return ResolveFallbackStateDirectory();
        }
    }

    std::filesystem::path ResolveGatewayStateFilePath(
        std::string_view fileName) noexcept {
        try {
            const std::filesystem::path stateDir =
                ResolveGatewayStateDirectory();
            if (fileName.empty()) {
                return stateDir;
            }

            return stateDir / std::filesystem::path(fileName);
        }
        catch (...) {
            return ResolveFallbackStateDirectory();
        }
    }

} // namespace blazeclaw::gateway
