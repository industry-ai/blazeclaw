#pragma once

#include "ConfigModels.h"

namespace blazeclaw::config {

class ConfigLoader {
public:
  bool LoadFromFile(const std::wstring& path, AppConfig& outConfig) const;
  bool LoadFromFile(
    const std::wstring& path,
    AppConfig& outConfig,
    GatewayStartupConfigFileSnapshot* outFileSnapshot) const;
};

void BuildGatewayStartupConfigFileSnapshot(
  const std::wstring& path,
  const std::vector<std::uint64_t>& internalWriteHashes,
  GatewayStartupConfigFileSnapshot& out);

} // namespace blazeclaw::config
