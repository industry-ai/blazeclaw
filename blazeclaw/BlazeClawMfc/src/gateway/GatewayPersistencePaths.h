#pragma once

#include <filesystem>
#include <string_view>

namespace blazeclaw::gateway {

std::filesystem::path ResolveGatewayStateDirectory() noexcept;
std::filesystem::path ResolveGatewayStateFilePath(
    std::string_view fileName) noexcept;

} // namespace blazeclaw::gateway
