#pragma once

#include "ISpeechRecognitionRuntime.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace blazeclaw::core::speechrecognition {

	struct SpeechOfflineOptimizationResult {
		bool success = false;
		std::vector<std::string> optimizedRoots;
		std::vector<std::string> failedRoots;
		std::string summary;
	};

	[[nodiscard]] SpeechOfflineOptimizationResult OptimizeSpeechRecognitionModelsOffline(
		const blazeclaw::config::SpeechRecognitionConfig& config,
		const std::vector<std::wstring>& modelRoots = {});

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

		static constexpr const char* kRuntimeHotModeAlwaysOnline = "always_online";
		static constexpr const char* kRuntimeHotModeOnDemand = "on_demand";
		static constexpr const char* kRuntimeHotModeIdleTimeout = "idle_timeout";

		[[nodiscard]] bool EnsureLoadedLocked(SpeechTranscribeResult& outResult);
		void ApplyRuntimeHotPolicyToSnapshotLocked();
		void SetLifecycleStateLocked(const char* state);
		void TouchRuntimeActivityLocked();
		void MaybeUnloadForIdleLocked();
		void UnloadSessionLocked(const char* reason);
		void RunWarmupLocked();

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
		std::chrono::steady_clock::time_point m_lastRuntimeActivity{};
		bool m_runtimeWarmupCompleted = false;
		bool m_cudaCompatibilityGuardLatched = false;
		std::string m_cudaCompatibilityGuardLatchedReason;
	};

} // namespace blazeclaw::core::speechrecognition
