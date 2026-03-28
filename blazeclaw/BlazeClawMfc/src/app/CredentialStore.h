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

  // DPAPI helpers: encrypt/decrypt to a per-user file (path is application-controlled)
  static bool SaveCredentialDPAPI(const std::wstring& filePath, const std::string& secretUtf8);
  static std::optional<std::string> LoadCredentialDPAPI(const std::wstring& filePath);
};

} // namespace blazeclaw::app
