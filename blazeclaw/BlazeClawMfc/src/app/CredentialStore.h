#pragma once

#include <optional>
#include <string>

namespace blazeclaw::app {

class CredentialStore {
public:
  // targetName e.g. L"blazeclaw.deepseek"
  static bool SaveCredential(const std::wstring& target, const std::string& secretUtf8);
  static std::optional<std::string> LoadCredential(const std::wstring& target);
  static bool DeleteCredential(const std::wstring& target);
};

} // namespace blazeclaw::app
