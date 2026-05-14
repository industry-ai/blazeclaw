#pragma once

#include "ISpeechRecognitionRuntime.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace blazeclaw::core::speechrecognition {

	class SpeechRecognitionRuntime final : public ISpeechRecognitionRuntime {
	public:
		SpeechRecognitionRuntime();
		~SpeechRecognitionRuntime() override;

		void Configure(const blazeclaw::config::AppConfig& appConfig) override;

		[[nodiscard]] SpeechRecognitionRuntimeSnapshot Snapshot() const override;

		[[nodiscard]] bool LoadModel() override;

		[[nodiscard]] SpeechTranscribeResult Transcribe(
			const SpeechTranscribeRequest& request) override;

		[[nodiscard]] bool Cancel(const std::string& runId) override;

	private:
		struct SessionState;

		[[nodiscard]] bool EnsureLoadedLocked(SpeechTranscribeResult& outResult);

		void ResetSnapshotLocked();

		static std::string ToNarrow(const std::wstring& value);
		static void TraceRuntime(
			const char* stage,
			const std::string& runId,
			const std::string& details);

		mutable std::mutex m_mutex;
		mutable std::mutex m_cancelMutex;
		blazeclaw::config::AppConfig m_config;
		SpeechRecognitionRuntimeSnapshot m_snapshot;
		std::unique_ptr<SessionState> m_sessionState;
		std::unordered_map<std::string, bool> m_cancelFlagsByRunId;
		bool m_cudaCompatibilityGuardLatched = false;
		std::string m_cudaCompatibilityGuardLatchedReason;
	};

} // namespace blazeclaw::core::speechrecognition
