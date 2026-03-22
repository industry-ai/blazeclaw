#pragma once

#include "ITextGenerationRuntime.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core::localmodel {

class TokenizerBridge {
public:
  [[nodiscard]] bool Load(
      const std::filesystem::path& tokenizerPath,
      std::string& outError);

  [[nodiscard]] bool IsReady() const noexcept;

  [[nodiscard]] std::vector<std::string> Tokenize(
      const std::string& text,
      std::uint32_t maxTokens,
      TextGenerationError& outError) const;

  [[nodiscard]] std::string Detokenize(
      const std::vector<std::string>& tokens) const;

private:
  [[nodiscard]] static bool IsTokenSeparator(char ch) noexcept;

  std::filesystem::path m_tokenizerPath;
  bool m_ready = false;
};

} // namespace blazeclaw::core::localmodel
