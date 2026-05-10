#pragma once

#include "ITextGenerationRuntime.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace blazeclaw::core::localmodel {

	class LlamaTextGenerationRuntime final : public ITextGenerationRuntime {
	public:
		LlamaTextGenerationRuntime();
		~LlamaTextGenerationRuntime() override;

		void Configure(const blazeclaw::config::AppConfig& appConfig) override;

		[[nodiscard]] LocalModelRuntimeSnapshot Snapshot() const override;

		[[nodiscard]] bool LoadModel() override;

		[[nodiscard]] TextGenerationResult GenerateStream(
			const TextGenerationRequest& request,
			const TextDeltaCallback& onDelta) override;

		[[nodiscard]] bool Cancel(const std::string& runId) override;

		[[nodiscard]] bool VerifyDeterministicContract(
			std::string& outFailureReason) override;

	private:
		struct SessionState;

		[[nodiscard]] bool EnsureLoadedLocked(TextGenerationResult& outResult);

		void ResetSnapshotLocked();

		static void TraceRuntime(
			const char* stage,
			const std::string& runId,
			const std::string& details);

		mutable std::mutex m_mutex;
		std::mutex m_generationMutex;
		blazeclaw::config::AppConfig m_config;
		LocalModelRuntimeSnapshot m_snapshot;
		std::shared_ptr<SessionState> m_sessionState;
		std::unordered_map<std::string, bool> m_cancelFlagsByRunId;
	};

} // namespace blazeclaw::core::localmodel
