#pragma once

#include "ITextGenerationRuntime.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
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

  [[nodiscard]] std::vector<std::int64_t> EncodeToIds(
      const std::string& text,
      std::uint32_t maxTokens,
      TextGenerationError& outError,
      bool includeBos = true) const;

  [[nodiscard]] std::string DecodeFromIds(
      const std::vector<std::int64_t>& tokenIds) const;

  [[nodiscard]] bool IsEndOfSequenceId(
      std::int64_t tokenId) const noexcept;

  [[nodiscard]] std::int64_t BosTokenId() const noexcept;

  [[nodiscard]] bool TryGetTokenId(
      const std::string& token,
      std::int64_t& outTokenId) const noexcept;

  [[nodiscard]] std::string Detokenize(
      const std::vector<std::string>& tokens) const;

private:
  struct BpeMergeEntry {
    std::string left;
    std::string right;
    std::size_t rank = 0;
  };

  struct Span {
    std::size_t start = 0;
    std::size_t end = 0;
  };

  [[nodiscard]] static std::string ToLowerAscii(
      const std::string& value);

  [[nodiscard]] static std::string TrimAscii(
      const std::string& value);

  [[nodiscard]] static std::string ParseJsonString(
      const std::string& input,
      std::size_t& cursor,
      bool& ok);

  [[nodiscard]] static bool SkipJsonWhitespace(
      const std::string& input,
      std::size_t& cursor);

  [[nodiscard]] static bool AdvanceToJsonKey(
      const std::string& input,
      const std::string& key,
      std::size_t& cursor);

  [[nodiscard]] static bool ExtractJsonObjectBody(
      const std::string& input,
      std::size_t start,
      std::size_t& bodyBegin,
      std::size_t& bodyEnd);

  [[nodiscard]] static std::vector<Span> PretokenizeSpans(
      const std::string& text);

  [[nodiscard]] static std::vector<std::string> SplitUtf8CodePoints(
      const std::string& text);

  [[nodiscard]] std::vector<std::string> ByteEncodePieces(
      const std::string& text) const;

  [[nodiscard]] std::vector<std::string> ApplyBpe(
      const std::string& piece) const;

  [[nodiscard]] std::vector<std::int64_t> EncodePiecesToIds(
      const std::vector<std::string>& pieces,
      std::uint32_t maxTokens,
      TextGenerationError& outError,
      bool includeBos) const;

  [[nodiscard]] std::string DecodePieceToUtf8(
      const std::string& piece) const;

  [[nodiscard]] bool LoadAddedTokens(
      const std::string& tokenizerJson,
      std::string& outError);

  [[nodiscard]] bool LoadVocab(
      const std::string& tokenizerJson,
      std::string& outError);

  [[nodiscard]] bool LoadMerges(
      const std::string& tokenizerJson,
      std::string& outError);

  [[nodiscard]] bool BuildByteLevelTables(
      std::string& outError);

  std::filesystem::path m_tokenizerPath;
  bool m_ready = false;
  std::int64_t m_bosTokenId = 151643;
  std::set<std::int64_t> m_eosTokenIds;
  std::unordered_map<std::string, std::int64_t> m_tokenToId;
  std::unordered_map<std::int64_t, std::string> m_idToToken;
  std::unordered_map<std::string, std::size_t> m_bpeMergeRanks;
  std::array<std::string, 256> m_byteToUnicode;
  std::unordered_map<std::string, std::uint8_t> m_unicodeToByte;
};

} // namespace blazeclaw::core::localmodel
