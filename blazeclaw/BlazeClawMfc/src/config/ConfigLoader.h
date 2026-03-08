#pragma once

#include "ConfigModels.h"

namespace blazeclaw::config {

class ConfigLoader {
public:
  bool LoadFromFile(const std::wstring& path, AppConfig& outConfig) const;
};

} // namespace blazeclaw::config
