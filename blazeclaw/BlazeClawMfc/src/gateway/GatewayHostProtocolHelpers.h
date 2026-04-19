#pragma once

#include <cstdint>
#include <string>

namespace blazeclaw::gateway {

[[nodiscard]] std::uint64_t GatewayEpochMilliseconds();

/// Rejects path traversal and platform-unsafe agent file paths (same rules as legacy `GatewayHost` checks).
[[nodiscard]] bool IsUnsafeGatewayAgentFilePath(const std::string& path);

} // namespace blazeclaw::gateway
