#include "pch.h"
#include "TokenizerBridge.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::core::localmodel {

bool TokenizerBridge::Load(
    const std::filesystem::path& tokenizerPath,
    std::string& outError) {
  outError.clear();
  m_ready = false;
  m_tokenizerPath.clear();

  std::error_code ec;
  if (!std::filesystem::exists(tokenizerPath, ec) || ec) {
    outError = "Tokenizer file was not found.";
    return false;
  }

  if (!std::filesystem::is_regular_file(tokenizerPath, ec) || ec) {
    outError = "Tokenizer path is not a regular file.";
    return false;
  }

  m_tokenizerPath = tokenizerPath;
  m_ready = true;
  return true;
}

bool TokenizerBridge::IsReady() const noexcept {
  return m_ready;
}

std::vector<std::string> TokenizerBridge::Tokenize(
    const std::string& text,
    const std::uint32_t maxTokens,
    TextGenerationError& outError) const {
  outError = TextGenerationError{};

  if (!m_ready) {
    outError.code = TextGenerationErrorCode::TokenizerNotFound;
    outError.message = "Tokenizer is not loaded.";
    return {};
  }

  if (text.empty()) {
    outError.code = TextGenerationErrorCode::InvalidInput;
    outError.message = "Prompt must not be empty.";
    return {};
  }

  std::vector<std::string> tokens;
  tokens.reserve((std::min)(text.size(), static_cast<std::size_t>(maxTokens)));

  std::string current;
  for (const char ch : text) {
    if (IsTokenSeparator(ch)) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
        if (tokens.size() >= maxTokens) {
          break;
        }
      }

      continue;
    }

    current.push_back(ch);
  }

  if (!current.empty() && tokens.size() < maxTokens) {
    tokens.push_back(current);
  }

  if (tokens.empty()) {
    outError.code = TextGenerationErrorCode::TokenizationFailed;
    outError.message = "Tokenizer produced no tokens.";
    return {};
  }

  return tokens;
}

std::string TokenizerBridge::Detokenize(
    const std::vector<std::string>& tokens) const {
  std::string output;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i > 0) {
      output.push_back(' ');
    }

    output += tokens[i];
  }

  return output;
}

bool TokenizerBridge::IsTokenSeparator(const char ch) noexcept {
  const unsigned char value = static_cast<unsigned char>(ch);
  return std::isspace(value) != 0 ||
      ch == ',' ||
      ch == '.' ||
      ch == ';' ||
      ch == ':' ||
      ch == '!' ||
      ch == '?' ||
      ch == '(' ||
      ch == ')' ||
      ch == '[' ||
      ch == ']' ||
      ch == '{' ||
      ch == '}' ||
      ch == '"';
}

} // namespace blazeclaw::core::localmodel
