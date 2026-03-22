#pragma once

#include "ITextGenerationRuntime.h"
#include "TokenizerBridge.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace blazeclaw::core::localmodel {

class OnnxTextGenerationRuntime final : public ITextGenerationRuntime {
public:
  OnnxTextGenerationRuntime();
  ~OnnxTextGenerationRuntime() override;

  void Configure(const blazeclaw::config::AppConfig& appConfig) override;

  [[nodiscard]] LocalModelRuntimeSnapshot Snapshot() const override;

  [[nodiscard]] bool LoadModel() override;

  [[nodiscard]] TextGenerationResult GenerateStream(
      const TextGenerationRequest& request,
      const TextDeltaCallback& onDelta) override;

  [[nodiscard]] bool Cancel(const std::string& runId) override;

private:
  struct SessionState;

  [[nodiscard]] bool EnsureLoadedLocked(TextGenerationResult& outResult);

  [[nodiscard]] static std::string ToNarrow(const std::wstring& value);

  void ResetSnapshotLocked();

  mutable std::mutex m_mutex;
  blazeclaw::config::AppConfig m_config;
  LocalModelRuntimeSnapshot m_snapshot;
  TokenizerBridge m_tokenizer;
  std::unique_ptr<SessionState> m_sessionState;
  std::unordered_map<std::string, bool> m_cancelFlagsByRunId;
};

} // namespace blazeclaw::core::localmodel
