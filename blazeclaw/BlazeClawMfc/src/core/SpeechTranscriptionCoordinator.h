#pragma once

#include "runtime/SpeechRecognition/ISpeechRecognitionRuntime.h"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace blazeclaw::core {

	class SpeechTranscriptionCoordinator {
	public:
		using RuntimeInterface = speechrecognition::ISpeechRecognitionRuntime;
		using ExecutionRequest = speechrecognition::SpeechExecutionRequest;
		using ExecutionAccepted = speechrecognition::SpeechExecutionAccepted;
		using ExecutionStatus = speechrecognition::SpeechExecutionStatus;
		using ExecutionState = speechrecognition::SpeechExecutionState;
		using ExecutionUpdateCallback = speechrecognition::SpeechExecutionUpdateCallback;
		using TranscribeResult = speechrecognition::SpeechTranscribeResult;

		SpeechTranscriptionCoordinator() = default;

		void SetExecutionUpdateCallback(ExecutionUpdateCallback callback);

		[[nodiscard]] ExecutionAccepted Accept(
			const ExecutionRequest& request);

		[[nodiscard]] ExecutionStatus GetStatus(
			const std::string& runId) const;

		[[nodiscard]] TranscribeResult Execute(
			RuntimeInterface& runtime,
			const ExecutionRequest& request);

		[[nodiscard]] bool Cancel(
			RuntimeInterface& runtime,
			const std::string& runId);

		void Shutdown(RuntimeInterface& runtime);

	private:
		[[nodiscard]] static bool IsTerminal(
			speechrecognition::SpeechExecutionStage stage);

		[[nodiscard]] static bool IsSessionInFlight(
			speechrecognition::SpeechExecutionStage stage);

		[[nodiscard]] static std::string BuildTrackingRunId(
			const ExecutionRequest& request);

		[[nodiscard]] static ExecutionState BuildInitialState(
			const ExecutionRequest& request);

		[[nodiscard]] static ExecutionState BuildState(
			const ExecutionState& baseline,
			speechrecognition::SpeechExecutionStage stage,
			const TranscribeResult* result = nullptr,
			bool cancelRequested = false);

		mutable std::mutex m_mutex;
		std::unordered_map<std::string, ExecutionState> m_executionByRunId;
		std::unordered_map<std::string, std::string> m_sessionRunBySessionId;
		ExecutionUpdateCallback m_executionUpdateCallback;
	};

} // namespace blazeclaw::core
