#include "pch.h"
#include "TokenizerBridge.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <numeric>

namespace blazeclaw::core::localmodel {

namespace {

bool IsAsciiSpace(const char ch) {
  return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool IsAsciiAlpha(const char ch) {
  const unsigned char value = static_cast<unsigned char>(ch);
  return (value >= 'A' && value <= 'Z') ||
      (value >= 'a' && value <= 'z');
}

bool IsAsciiDigit(const char ch) {
  const unsigned char value = static_cast<unsigned char>(ch);
  return value >= '0' && value <= '9';
}

bool IsBoundaryToken(const char ch) {
  return ch == '\r' || ch == '\n';
}

int HexValue(const char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }

  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }

  if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }

  return -1;
}

bool TryParseHex4(
    const std::string& input,
    const std::size_t cursor,
    std::uint32_t& outCodepoint) {
  if (cursor + 4 > input.size()) {
    return false;
  }

  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    const int nibble = HexValue(input[cursor + i]);
    if (nibble < 0) {
      return false;
    }

    value = (value << 4U) | static_cast<std::uint32_t>(nibble);
  }

  outCodepoint = value;
  return true;
}

void AppendUtf8Codepoint(
    std::string& target,
    const std::uint32_t codepoint) {
  if (codepoint <= 0x7FU) {
    target.push_back(static_cast<char>(codepoint));
    return;
  }

  if (codepoint <= 0x7FFU) {
    target.push_back(
        static_cast<char>(0xC0U | ((codepoint >> 6U) & 0x1FU)));
    target.push_back(
        static_cast<char>(0x80U | (codepoint & 0x3FU)));
    return;
  }

  if (codepoint <= 0xFFFFU) {
    target.push_back(
        static_cast<char>(0xE0U | ((codepoint >> 12U) & 0x0FU)));
    target.push_back(
        static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
    target.push_back(
        static_cast<char>(0x80U | (codepoint & 0x3FU)));
    return;
  }

  target.push_back(
      static_cast<char>(0xF0U | ((codepoint >> 18U) & 0x07U)));
  target.push_back(
      static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
  target.push_back(
      static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
  target.push_back(
      static_cast<char>(0x80U | (codepoint & 0x3FU)));
}

std::string ReadAllTextFile(
    const std::filesystem::path& path,
    bool& ok) {
  ok = false;
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }

  input.seekg(0, std::ios::end);
  const auto length = input.tellg();
  if (length <= 0) {
    return {};
  }

  std::string content;
  content.resize(static_cast<std::size_t>(length));
  input.seekg(0, std::ios::beg);
  if (!input.read(content.data(), static_cast<std::streamsize>(content.size()))) {
    return {};
  }

  ok = true;
  return content;
}

} // namespace

bool TokenizerBridge::Load(
    const std::filesystem::path& tokenizerPath,
    std::string& outError) {
  outError.clear();
  m_ready = false;
  m_tokenizerPath.clear();
  m_tokenToId.clear();
  m_idToToken.clear();
  m_bpeMergeRanks.clear();
  m_eosTokenIds.clear();
  m_bosTokenId = 151643;

  std::error_code ec;
  if (!std::filesystem::exists(tokenizerPath, ec) || ec) {
    outError = "Tokenizer file was not found.";
    return false;
  }

  if (!std::filesystem::is_regular_file(tokenizerPath, ec) || ec) {
    outError = "Tokenizer path is not a regular file.";
    return false;
  }

  bool readOk = false;
  const std::string tokenizerJson = ReadAllTextFile(tokenizerPath, readOk);
  if (!readOk || tokenizerJson.empty()) {
    outError = "Tokenizer file could not be read.";
    return false;
  }

  if (!BuildByteLevelTables(outError)) {
    return false;
  }

  if (!LoadAddedTokens(tokenizerJson, outError)) {
    return false;
  }

  if (!LoadVocab(tokenizerJson, outError)) {
    return false;
  }

  if (!LoadMerges(tokenizerJson, outError)) {
    return false;
  }

  m_tokenizerPath = tokenizerPath;
  m_ready = true;
  return true;
}

bool TokenizerBridge::IsReady() const noexcept {
  return m_ready;
}

std::vector<std::int64_t> TokenizerBridge::EncodeToIds(
    const std::string& text,
    const std::uint32_t maxTokens,
    TextGenerationError& outError,
    const bool includeBos) const {
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

  std::vector<std::string> pieces;
  auto appendNormalSegment = [&](const std::string& segment) {
    if (segment.empty()) {
      return;
    }

    const auto spans = PretokenizeSpans(segment);
    for (const auto& span : spans) {
      if (span.end <= span.start || span.end > segment.size()) {
        continue;
      }

      const std::string piece = segment.substr(
          span.start,
          span.end - span.start);
      const auto bytePieces = ByteEncodePieces(piece);
      for (const auto& bytePiece : bytePieces) {
        const auto bpeTokens = ApplyBpe(bytePiece);
        pieces.insert(
            pieces.end(),
            bpeTokens.begin(),
            bpeTokens.end());
      }
    }
  };

  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const std::size_t specialStart = text.find("<|", cursor);
    if (specialStart == std::string::npos) {
      appendNormalSegment(text.substr(cursor));
      break;
    }

    if (specialStart > cursor) {
      appendNormalSegment(text.substr(cursor, specialStart - cursor));
    }

    const std::size_t specialEnd = text.find("|>", specialStart + 2);
    if (specialEnd == std::string::npos) {
      appendNormalSegment(text.substr(specialStart));
      break;
    }

    const std::string candidate = text.substr(
        specialStart,
        (specialEnd + 2) - specialStart);
    if (m_tokenToId.find(candidate) != m_tokenToId.end()) {
      pieces.push_back(candidate);
      cursor = specialEnd + 2;
      continue;
    }

    appendNormalSegment(candidate);
    cursor = specialEnd + 2;
  }

  return EncodePiecesToIds(
      pieces,
      maxTokens,
      outError,
      includeBos);
}

std::string TokenizerBridge::DecodeFromIds(
    const std::vector<std::int64_t>& tokenIds) const {
  if (!m_ready || tokenIds.empty()) {
    return {};
  }

  std::string output;
  for (const auto id : tokenIds) {
    if (id == m_bosTokenId || IsEndOfSequenceId(id)) {
      continue;
    }

    const auto it = m_idToToken.find(id);
    if (it == m_idToToken.end()) {
      continue;
    }

    output += DecodePieceToUtf8(it->second);
  }

  return output;
}

std::vector<std::string> TokenizerBridge::Tokenize(
    const std::string& text,
    const std::uint32_t maxTokens,
    TextGenerationError& outError) const {
  const auto ids = EncodeToIds(
      text,
      maxTokens,
      outError,
      false);
  if (!outError.message.empty()) {
    return {};
  }

  std::vector<std::string> tokens;
  tokens.reserve(ids.size());
  for (const auto id : ids) {
    const auto it = m_idToToken.find(id);
    if (it == m_idToToken.end()) {
      continue;
    }

    tokens.push_back(DecodePieceToUtf8(it->second));
  }

  if (tokens.empty()) {
    outError.code = TextGenerationErrorCode::TokenizationFailed;
    outError.message = "Tokenizer produced no tokens.";
  }

  return tokens;
}

std::string TokenizerBridge::Detokenize(
    const std::vector<std::string>& tokens) const {
  std::string output;
  for (const auto& token : tokens) {
    output += token;
  }

  return output;
}

bool TokenizerBridge::IsEndOfSequenceId(
    const std::int64_t tokenId) const noexcept {
  return m_eosTokenIds.find(tokenId) != m_eosTokenIds.end();
}

std::int64_t TokenizerBridge::BosTokenId() const noexcept {
  return m_bosTokenId;
}

bool TokenizerBridge::TryGetTokenId(
    const std::string& token,
    std::int64_t& outTokenId) const noexcept {
  const auto it = m_tokenToId.find(token);
  if (it == m_tokenToId.end()) {
    return false;
  }

  outTokenId = it->second;
  return true;
}

std::size_t TokenizerBridge::VocabSize() const noexcept {
  return m_idToToken.size();
}

std::string TokenizerBridge::ToLowerAscii(const std::string& value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const char ch : value) {
    lowered.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch))));
  }

  return lowered;
}

std::string TokenizerBridge::TrimAscii(const std::string& value) {
  const auto first = std::find_if_not(
      value.begin(),
      value.end(),
      IsAsciiSpace);
  const auto last = std::find_if_not(
      value.rbegin(),
      value.rend(),
      IsAsciiSpace)
                        .base();
  if (first >= last) {
    return {};
  }

  return std::string(first, last);
}

bool TokenizerBridge::SkipJsonWhitespace(
    const std::string& input,
    std::size_t& cursor) {
  while (cursor < input.size()) {
    const char ch = input[cursor];
    if (IsAsciiSpace(ch)) {
      ++cursor;
      continue;
    }

    break;
  }

  return cursor < input.size();
}

std::string TokenizerBridge::ParseJsonString(
    const std::string& input,
    std::size_t& cursor,
    bool& ok) {
  ok = false;
  if (cursor >= input.size() || input[cursor] != '"') {
    return {};
  }

  ++cursor;
  std::string value;
  while (cursor < input.size()) {
    const char ch = input[cursor++];
    if (ch == '"') {
      ok = true;
      return value;
    }

    if (ch != '\\') {
      value.push_back(ch);
      continue;
    }

    if (cursor >= input.size()) {
      return {};
    }

    const char escaped = input[cursor++];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        value.push_back(escaped);
        break;
      case 'b':
        value.push_back('\b');
        break;
      case 'f':
        value.push_back('\f');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      case 'u': {
        std::uint32_t codepoint = 0;
        if (!TryParseHex4(input, cursor, codepoint)) {
          return {};
        }

        cursor += 4;

        if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
          if (cursor + 6 <= input.size() &&
              input[cursor] == '\\' &&
              input[cursor + 1] == 'u') {
            std::uint32_t low = 0;
            if (TryParseHex4(input, cursor + 2, low) &&
                low >= 0xDC00U &&
                low <= 0xDFFFU) {
              const std::uint32_t highPart = codepoint - 0xD800U;
              const std::uint32_t lowPart = low - 0xDC00U;
              codepoint = 0x10000U +
                  ((highPart << 10U) | lowPart);
              cursor += 6;
            }
          }
        }

        AppendUtf8Codepoint(value, codepoint);
        break;
      }
      default:
        value.push_back(escaped);
        break;
    }
  }

  return {};
}

bool TokenizerBridge::ExtractJsonObjectBody(
    const std::string& input,
    const std::size_t start,
    std::size_t& bodyBegin,
    std::size_t& bodyEnd) {
  if (start >= input.size() || input[start] != '{') {
    return false;
  }

  bool inString = false;
  bool escaped = false;
  int depth = 0;
  for (std::size_t i = start; i < input.size(); ++i) {
    const char ch = input[i];
    if (inString) {
      if (escaped) {
        escaped = false;
        continue;
      }

      if (ch == '\\') {
        escaped = true;
        continue;
      }

      if (ch == '"') {
        inString = false;
      }

      continue;
    }

    if (ch == '"') {
      inString = true;
      continue;
    }

    if (ch == '{') {
      ++depth;
      if (depth == 1) {
        bodyBegin = i + 1;
      }

      continue;
    }

    if (ch == '}') {
      --depth;
      if (depth == 0) {
        bodyEnd = i;
        return true;
      }

      continue;
    }
  }

  return false;
}

bool TokenizerBridge::AdvanceToJsonKey(
    const std::string& input,
    const std::string& key,
    std::size_t& cursor) {
  const std::string token = "\"" + key + "\"";
  const auto pos = input.find(token, cursor);
  if (pos == std::string::npos) {
    return false;
  }

  cursor = pos + token.size();
  return true;
}

std::vector<TokenizerBridge::Span> TokenizerBridge::PretokenizeSpans(
    const std::string& text) {
  if (text.empty()) {
    return {};
  }

  return { Span{.start = 0, .end = text.size()} };
}

std::vector<std::string> TokenizerBridge::SplitUtf8CodePoints(
    const std::string& text) {
  std::vector<std::string> pieces;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const unsigned char byte = static_cast<unsigned char>(text[cursor]);
    std::size_t length = 1;
    if ((byte & 0x80) == 0x00) {
      length = 1;
    } else if ((byte & 0xE0) == 0xC0) {
      length = 2;
    } else if ((byte & 0xF0) == 0xE0) {
      length = 3;
    } else if ((byte & 0xF8) == 0xF0) {
      length = 4;
    }

    if (cursor + length > text.size()) {
      length = 1;
    }

    pieces.push_back(text.substr(cursor, length));
    cursor += length;
  }

  return pieces;
}

std::vector<std::string> TokenizerBridge::ByteEncodePieces(
    const std::string& text) const {
  std::string encoded;
  encoded.reserve(text.size() * 2);
  for (const unsigned char byte : text) {
    encoded += m_byteToUnicode[byte];
  }

  if (encoded.empty()) {
    return {};
  }

  return { encoded };
}

std::vector<std::string> TokenizerBridge::ApplyBpe(
    const std::string& piece) const {
  if (piece.empty()) {
    return {};
  }

  std::vector<std::string> tokens = SplitUtf8CodePoints(piece);
  if (tokens.size() <= 1) {
    return tokens;
  }

  while (tokens.size() > 1) {
    std::size_t bestPairIndex = 0;
    std::size_t bestRank = (std::numeric_limits<std::size_t>::max)();
    bool foundPair = false;

    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
      const std::string key = tokens[i] + " " + tokens[i + 1];
      const auto mergeIt = m_bpeMergeRanks.find(key);
      if (mergeIt == m_bpeMergeRanks.end()) {
        continue;
      }

      if (!foundPair || mergeIt->second < bestRank) {
        foundPair = true;
        bestRank = mergeIt->second;
        bestPairIndex = i;
      }
    }

    if (!foundPair) {
      break;
    }

    tokens[bestPairIndex] += tokens[bestPairIndex + 1];
    tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(bestPairIndex + 1));
  }

  return tokens;
}

std::vector<std::int64_t> TokenizerBridge::EncodePiecesToIds(
    const std::vector<std::string>& pieces,
    const std::uint32_t maxTokens,
    TextGenerationError& outError,
    const bool includeBos) const {
  outError = TextGenerationError{};

  std::vector<std::int64_t> ids;
  ids.reserve((std::max)(std::size_t{1}, pieces.size() + 1));
  if (includeBos) {
    ids.push_back(m_bosTokenId);
  }

  for (const auto& piece : pieces) {
    if (ids.size() >= maxTokens) {
      break;
    }

    const auto it = m_tokenToId.find(piece);
    if (it == m_tokenToId.end()) {
      outError.code = TextGenerationErrorCode::TokenizationFailed;
      outError.message = "Tokenizer vocab missing BPE piece.";
      return {};
    }

    ids.push_back(it->second);
  }

  if (ids.empty() || (includeBos && ids.size() == 1)) {
    outError.code = TextGenerationErrorCode::TokenizationFailed;
    outError.message = "Tokenizer produced no token ids.";
    return {};
  }

  return ids;
}

std::string TokenizerBridge::DecodePieceToUtf8(
    const std::string& piece) const {
  std::string output;
  const auto codePoints = SplitUtf8CodePoints(piece);
  for (const auto& cp : codePoints) {
    const auto it = m_unicodeToByte.find(cp);
    if (it != m_unicodeToByte.end()) {
      output.push_back(static_cast<char>(it->second));
      continue;
    }

    output += cp;
  }

  return output;
}

bool TokenizerBridge::LoadAddedTokens(
    const std::string& tokenizerJson,
    std::string& outError) {
  std::size_t cursor = 0;
  if (!AdvanceToJsonKey(tokenizerJson, "added_tokens", cursor)) {
    outError = "Tokenizer JSON missing added_tokens.";
    return false;
  }

  const auto arrayBegin = tokenizerJson.find('[', cursor);
  if (arrayBegin == std::string::npos) {
    outError = "Tokenizer JSON added_tokens is malformed.";
    return false;
  }

  std::size_t index = arrayBegin + 1;
  while (index < tokenizerJson.size()) {
    const bool hasNonWhitespace = SkipJsonWhitespace(tokenizerJson, index);
    if (!hasNonWhitespace) {
      break;
    }
    if (index >= tokenizerJson.size()) {
      break;
    }

    if (tokenizerJson[index] == ']') {
      break;
    }

    if (tokenizerJson[index] != '{') {
      ++index;
      continue;
    }

    std::size_t bodyBegin = 0;
    std::size_t bodyEnd = 0;
    if (!ExtractJsonObjectBody(tokenizerJson, index, bodyBegin, bodyEnd)) {
      outError = "Tokenizer JSON added_tokens object parse failed.";
      return false;
    }

    const std::string item = tokenizerJson.substr(
        bodyBegin,
        bodyEnd - bodyBegin);

    std::size_t itemCursor = 0;
    std::int64_t tokenId = -1;
    std::string tokenValue;

    if (AdvanceToJsonKey(item, "id", itemCursor)) {
      const auto colon = item.find(':', itemCursor);
      if (colon != std::string::npos) {
        auto end = colon + 1;
        while (end < item.size() &&
               (std::isdigit(static_cast<unsigned char>(item[end])) != 0 ||
                item[end] == '-')) {
          ++end;
        }

        const std::string idRaw = TrimAscii(item.substr(colon + 1, end - (colon + 1)));
        if (!idRaw.empty()) {
          tokenId = std::stoll(idRaw);
        }
      }
    }

    itemCursor = 0;
    if (AdvanceToJsonKey(item, "content", itemCursor)) {
      const auto colon = item.find(':', itemCursor);
      if (colon != std::string::npos) {
        auto valueCursor = colon + 1;
        const bool hasStringStart = SkipJsonWhitespace(item, valueCursor);
        if (!hasStringStart) {
          tokenValue.clear();
          index = bodyEnd + 1;
          continue;
        }

        bool ok = false;
        tokenValue = ParseJsonString(item, valueCursor, ok);
        if (!ok) {
          tokenValue.clear();
        }
      }
    }

    if (tokenId >= 0 && !tokenValue.empty()) {
      m_tokenToId.insert_or_assign(tokenValue, tokenId);
      m_idToToken.insert_or_assign(tokenId, tokenValue);
      if (tokenValue == "<|endoftext|>") {
        m_bosTokenId = tokenId;
      }

      if (tokenValue == "<|im_end|>" || tokenValue == "<|endoftext|>") {
        m_eosTokenIds.insert(tokenId);
      }
    }

    index = bodyEnd + 1;
  }

  return true;
}

bool TokenizerBridge::LoadVocab(
    const std::string& tokenizerJson,
    std::string& outError) {
  std::size_t cursor = 0;
  if (!AdvanceToJsonKey(tokenizerJson, "vocab", cursor)) {
    outError = "Tokenizer JSON missing model vocab.";
    return false;
  }

  const auto objectBegin = tokenizerJson.find('{', cursor);
  if (objectBegin == std::string::npos) {
    outError = "Tokenizer JSON vocab object is malformed.";
    return false;
  }

  std::size_t bodyBegin = 0;
  std::size_t bodyEnd = 0;
  if (!ExtractJsonObjectBody(tokenizerJson, objectBegin, bodyBegin, bodyEnd)) {
    outError = "Tokenizer JSON vocab body parse failed.";
    return false;
  }

  std::size_t i = bodyBegin;
  while (i < bodyEnd) {
    const bool hasEntry = SkipJsonWhitespace(tokenizerJson, i);
    if (!hasEntry) {
      break;
    }

    if (i >= bodyEnd || tokenizerJson[i] != '"') {
      ++i;
      continue;
    }

    bool keyOk = false;
    const std::string token = ParseJsonString(tokenizerJson, i, keyOk);
    if (!keyOk) {
      outError = "Tokenizer JSON vocab token parse failed.";
      return false;
    }

    const auto colon = tokenizerJson.find(':', i);
    if (colon == std::string::npos || colon >= bodyEnd) {
      outError = "Tokenizer JSON vocab entry parse failed.";
      return false;
    }

    i = colon + 1;
    const bool hasNumber = SkipJsonWhitespace(tokenizerJson, i);
    if (!hasNumber) {
      break;
    }

    std::size_t numberEnd = i;
    while (numberEnd < bodyEnd &&
           (std::isdigit(static_cast<unsigned char>(tokenizerJson[numberEnd])) != 0 ||
            tokenizerJson[numberEnd] == '-')) {
      ++numberEnd;
    }

    if (numberEnd == i) {
      outError = "Tokenizer JSON vocab id parse failed.";
      return false;
    }

    const auto idRaw = tokenizerJson.substr(i, numberEnd - i);
    const auto tokenId = static_cast<std::int64_t>(std::stoll(idRaw));
    m_tokenToId.insert_or_assign(token, tokenId);
    m_idToToken.insert_or_assign(tokenId, token);
    i = numberEnd;
  }

  return true;
}

bool TokenizerBridge::LoadMerges(
    const std::string& tokenizerJson,
    std::string& outError) {
  std::size_t cursor = 0;
  if (!AdvanceToJsonKey(tokenizerJson, "merges", cursor)) {
    outError = "Tokenizer JSON missing BPE merges.";
    return false;
  }

  const auto arrayBegin = tokenizerJson.find('[', cursor);
  if (arrayBegin == std::string::npos) {
    outError = "Tokenizer JSON merges array is malformed.";
    return false;
  }

  std::size_t i = arrayBegin + 1;
  std::size_t rank = 0;
  while (i < tokenizerJson.size()) {
    const bool hasEntry = SkipJsonWhitespace(tokenizerJson, i);
    if (!hasEntry) {
      break;
    }

    if (i >= tokenizerJson.size()) {
      break;
    }

    if (tokenizerJson[i] == ']') {
      break;
    }

    if (tokenizerJson[i] == '"') {
      bool ok = false;
      const std::string merge = ParseJsonString(tokenizerJson, i, ok);
      if (!ok || merge.empty()) {
        outError = "Tokenizer JSON merge entry parse failed.";
        return false;
      }

      m_bpeMergeRanks.insert_or_assign(merge, rank++);
      continue;
    }

    if (tokenizerJson[i] == '[') {
      std::size_t j = i + 1;
      if (!SkipJsonWhitespace(tokenizerJson, j) ||
          j >= tokenizerJson.size() ||
          tokenizerJson[j] != '"') {
        outError = "Tokenizer JSON merge pair left token parse failed.";
        return false;
      }

      bool leftOk = false;
      const std::string left = ParseJsonString(tokenizerJson, j, leftOk);
      if (!leftOk) {
        outError = "Tokenizer JSON merge pair left token parse failed.";
        return false;
      }

      if (!SkipJsonWhitespace(tokenizerJson, j) ||
          j >= tokenizerJson.size() ||
          tokenizerJson[j] != ',') {
        outError = "Tokenizer JSON merge pair separator parse failed.";
        return false;
      }

      ++j;
      if (!SkipJsonWhitespace(tokenizerJson, j) ||
          j >= tokenizerJson.size() ||
          tokenizerJson[j] != '"') {
        outError = "Tokenizer JSON merge pair right token parse failed.";
        return false;
      }

      bool rightOk = false;
      const std::string right = ParseJsonString(tokenizerJson, j, rightOk);
      if (!rightOk) {
        outError = "Tokenizer JSON merge pair right token parse failed.";
        return false;
      }

      if (!SkipJsonWhitespace(tokenizerJson, j) ||
          j >= tokenizerJson.size() ||
          tokenizerJson[j] != ']') {
        outError = "Tokenizer JSON merge pair closing bracket parse failed.";
        return false;
      }

      i = j + 1;
      m_bpeMergeRanks.insert_or_assign(left + " " + right, rank++);
      continue;
    }

    ++i;
  }

  return true;
}

bool TokenizerBridge::BuildByteLevelTables(std::string& outError) {
  outError.clear();

  std::vector<int> bytes;
  for (int i = static_cast<int>('!'); i <= static_cast<int>('~'); ++i) {
    bytes.push_back(i);
  }

  for (int i = 0xA1; i <= 0xAC; ++i) {
    bytes.push_back(i);
  }

  for (int i = 0xAE; i <= 0xFF; ++i) {
    bytes.push_back(i);
  }

  std::vector<int> chars = bytes;
  int extra = 0;
  for (int b = 0; b <= 255; ++b) {
    if (std::find(bytes.begin(), bytes.end(), b) != bytes.end()) {
      continue;
    }

    bytes.push_back(b);
    chars.push_back(256 + extra);
    ++extra;
  }

  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const int codepoint = chars[i];
    std::string utf8;
    if (codepoint <= 0x7F) {
      utf8.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
      utf8.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
      utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
      utf8.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
      utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }

    const auto byteValue = static_cast<std::uint8_t>(bytes[i]);
    m_byteToUnicode[byteValue] = utf8;
    m_unicodeToByte.insert_or_assign(utf8, byteValue);
  }

  return true;
}

} // namespace blazeclaw::core::localmodel
