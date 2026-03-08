#include "pch.h"
#include "ConfigLoader.h"

#include <fstream>

namespace blazeclaw::config {

bool ConfigLoader::LoadFromFile(const std::wstring& path, AppConfig& outConfig) const {
  std::wifstream input(path);
  if (!input.is_open()) {
    return false;
  }

  std::wstring line;
  while (std::getline(input, line)) {
    if (line.rfind(L"channel=", 0) == 0) {
      outConfig.enabledChannels.push_back(line.substr(8));
    } else if (line.rfind(L"gateway.port=", 0) == 0) {
      outConfig.gateway.port = static_cast<std::uint16_t>(std::stoi(line.substr(13)));
    } else if (line.rfind(L"gateway.bind=", 0) == 0) {
      outConfig.gateway.bindAddress = line.substr(13);
    } else if (line.rfind(L"agent.model=", 0) == 0) {
      outConfig.agent.model = line.substr(12);
    } else if (line.rfind(L"agent.streaming=", 0) == 0) {
      outConfig.agent.enableStreaming = line.substr(16) != L"false";
    }
  }

  return true;
}

} // namespace blazeclaw::config
