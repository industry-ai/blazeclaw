#include "pch.h"
#include "SpeechTranscriptionCoordinator.h"

#include <vector>

namespace blazeclaw::core {

	namespace {

		using blazeclaw::core::speechrecognition::SpeechExecutionStage;
		using blazeclaw::core::speechrecognition::SpeechRecognitionError;
		using blazeclaw::core::speechrecognition::SpeechRecognitionErrorCode;
		using blazeclaw::core::speechrecognition::SpeechSessionStage;

		const char* ExecutionStageToString(const SpeechExecutionStage stage) {
			switch (stage) {
			case SpeechExecutionStage::Queued:
				return "queued";
			case SpeechExecutionStage::Recording:
				return "recording";
			case SpeechExecutionStage::Stopped:
				return "stopped";
			case SpeechExecutionStage::Transcribing:
				return "transcribing";
			case SpeechExecutionStage::Completed:
				return "completed";
			case SpeechExecutionStage::Failed:
				return "failed";
			case SpeechExecutionStage::Cancelled:
				return "cancelled";
			default:
				return "unknown";
			}
		}

		std::string AudioPathForLog(const std::string& audioPath) {
			if (audioPath.empty()) {
				return "<empty>";
			}
			constexpr std::size_t kMaxChars = 192;
			if (audioPath.size() <= kMaxChars) {
				return audioPath;
			}
			return audioPath.substr(0, kMaxChars) + "...";
		}

		SpeechExecutionStage ToExecutionStage(const SpeechSessionStage stage) {
			switch (stage) {
			case SpeechSessionStage::Recording:
				return SpeechExecutionStage::Recording;
			case SpeechSessionStage::Stopped:
				return SpeechExecutionStage::Stopped;
			case SpeechSessionStage::Transcribing:
				return SpeechExecutionStage::Transcribing;
			case SpeechSessionStage::Completed:
				return SpeechExecutionStage::Completed;
			case SpeechSessionStage::Failed:
				return SpeechExecutionStage::Failed;
			case SpeechSessionStage::Idle:
			case SpeechSessionStage::Paused:
			default:
				return SpeechExecutionStage::Queued;
			}
		}

		SpeechRecognitionError BuildBusySessionError(const std::string& sessionId) {
			SpeechRecognitionError error;
			error.code = SpeechRecognitionErrorCode::RuntimeUnavailable;
			error.message = sessionId.empty()
				? "speech transcription already has an active in-flight request"
				: "speech transcription already has an active in-flight request for session '" + sessionId + "'";
			return error;
		}

	} // namespace

	void SpeechTranscriptionCoordinator::SetExecutionUpdateCallback(ExecutionUpdateCallback callback) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_executionUpdateCallback = std::move(callback);
	}

	SpeechTranscriptionCoordinator::ExecutionAccepted SpeechTranscriptionCoordinator::Accept(
		const ExecutionRequest& request) {
		ExecutionAccepted accepted;
		const std::string trackingRunId = BuildTrackingRunId(request);
		ExecutionState state = BuildInitialState(request);
		ExecutionUpdateCallback callback;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!request.sessionId.empty()) {
				const auto sessionRunIt = m_sessionRunBySessionId.find(request.sessionId);
				if (sessionRunIt != m_sessionRunBySessionId.end()) {
					const auto existingIt = m_executionByRunId.find(sessionRunIt->second);
					if (existingIt != m_executionByRunId.end() && IsSessionInFlight(existingIt->second.stage)) {
						accepted.accepted = false;
						accepted.executionState = existingIt->second;
						accepted.error = BuildBusySessionError(request.sessionId);
						TRACE(
							"[SpeechTranscriptionCoordinator][accept.rejected_busy_session] sessionId=%S runId=%S stage=%S\n",
							request.sessionId.c_str(),
							existingIt->second.runId.c_str(),
							ExecutionStageToString(existingIt->second.stage));
						return accepted;
					}
				}
			}

			const auto runIt = m_executionByRunId.find(trackingRunId);
			if (runIt != m_executionByRunId.end() && !IsTerminal(runIt->second.stage)) {
				accepted.accepted = false;
				accepted.executionState = runIt->second;
				accepted.error = SpeechRecognitionError{
					.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
					.message = "speech transcription run is already active",
				};
				TRACE(
					"[SpeechTranscriptionCoordinator][accept.rejected_busy_run] runId=%S stage=%S\n",
					trackingRunId.c_str(),
					ExecutionStageToString(runIt->second.stage));
				return accepted;
			}

			state.runId = trackingRunId;
			m_executionByRunId[trackingRunId] = state;
			if (!state.sessionId.empty()) {
				m_sessionRunBySessionId[state.sessionId] = trackingRunId;
			}
			callback = m_executionUpdateCallback;
			accepted.accepted = true;
			accepted.executionState = state;
		}

		if (callback) {
			callback(state);
		}
		TRACE(
			"[SpeechTranscriptionCoordinator][accept.accepted] sessionId=%S runId=%S audioPath=%S stage=%S\n",
			state.sessionId.c_str(),
			state.runId.c_str(),
			AudioPathForLog(state.audioPath).c_str(),
			ExecutionStageToString(state.stage));
		return accepted;
	}

	SpeechTranscriptionCoordinator::ExecutionStatus SpeechTranscriptionCoordinator::GetStatus(
		const std::string& runId) const {
		ExecutionStatus status;
		const std::string trackingRunId = runId;
		if (trackingRunId.empty()) {
			return status;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_executionByRunId.find(trackingRunId);
		if (it == m_executionByRunId.end()) {
			return status;
		}

		status.found = true;
		status.executionState = it->second;
		return status;
	}

	SpeechTranscriptionCoordinator::TranscribeResult SpeechTranscriptionCoordinator::Execute(
		RuntimeInterface& runtime,
		const ExecutionRequest& request) {
		const auto accepted = Accept(request);
		if (!accepted.accepted) {
			TranscribeResult rejected;
			rejected.ok = false;
			rejected.cancelled = false;
			rejected.sessionState.sessionId = accepted.executionState.sessionId;
			rejected.sessionState.runId = accepted.executionState.runId;
			rejected.sessionState.audioPath = accepted.executionState.audioPath;
			rejected.sessionState.audioArtifact = accepted.executionState.audioArtifact;
			rejected.sessionState.language = accepted.executionState.language;
			rejected.sessionState.stage = speechrecognition::SpeechSessionStage::Failed;
			rejected.error = accepted.error;
			rejected.sessionState.error = accepted.error;
			return rejected;
		}

		ExecutionState transcribingState;
		ExecutionUpdateCallback callback;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto current = m_executionByRunId.find(accepted.executionState.runId);
			if (current != m_executionByRunId.end()) {
				current->second.stage = SpeechExecutionStage::Transcribing;
				transcribingState = current->second;
				callback = m_executionUpdateCallback;
			}
		}
		if (callback) {
			callback(transcribingState);
		}
		TRACE(
			"[SpeechTranscriptionCoordinator][execute.start] sessionId=%S runId=%S audioPath=%S\n",
			accepted.executionState.sessionId.c_str(),
			accepted.executionState.runId.c_str(),
			AudioPathForLog(accepted.executionState.audioPath).c_str());

		const auto result = runtime.Transcribe(speechrecognition::SpeechTranscribeRequest{
			.runId = accepted.executionState.runId,
			.sessionId = request.sessionId,
			.audioPath = request.audioPath,
			.audioArtifact = request.audioArtifact,
			.language = request.language,
			.prompt = request.prompt,
		});

		ExecutionState completedState = BuildState(
			accepted.executionState,
			ToExecutionStage(result.sessionState.stage),
			&result);
		ExecutionUpdateCallback completedCallback;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_executionByRunId.find(accepted.executionState.runId);
			if (it != m_executionByRunId.end()) {
				it->second = completedState;
				completedState = it->second;
				completedCallback = m_executionUpdateCallback;
				if (IsTerminal(it->second.stage) &&
					!it->second.sessionId.empty()) {
					const auto sessionIt = m_sessionRunBySessionId.find(it->second.sessionId);
					if (sessionIt != m_sessionRunBySessionId.end() && sessionIt->second == it->second.runId) {
						m_sessionRunBySessionId.erase(sessionIt);
					}
				}
			}
		}
		if (completedCallback) {
			completedCallback(completedState);
		}
		TRACE(
			"[SpeechTranscriptionCoordinator][execute.completed] sessionId=%S runId=%S stage=%S cancelled=%d latencyMs=%u\n",
			completedState.sessionId.c_str(),
			completedState.runId.c_str(),
			ExecutionStageToString(completedState.stage),
			completedState.cancelRequested ? 1 : 0,
			completedState.latencyMs);

		return result;
	}

	bool SpeechTranscriptionCoordinator::Cancel(
		RuntimeInterface& runtime,
		const std::string& runId) {
		if (runId.empty()) {
			return false;
		}

		ExecutionState cancelledState;
		ExecutionUpdateCallback callback;
		bool found = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_executionByRunId.find(runId);
			if (it != m_executionByRunId.end()) {
				it->second.cancelRequested = true;
				cancelledState = it->second;
				callback = m_executionUpdateCallback;
				found = true;
			}
		}
		if (found && callback) {
			callback(cancelledState);
		}

		const bool cancelled = runtime.Cancel(runId);
		TRACE(
			"[SpeechTranscriptionCoordinator][cancel.requested] runId=%S source=api accepted=%d\n",
			runId.c_str(),
			cancelled ? 1 : 0);
		return cancelled;
	}

	void SpeechTranscriptionCoordinator::Shutdown(RuntimeInterface& runtime) {
		ExecutionUpdateCallback callback;
		std::vector<ExecutionState> cancelledStates;
		std::vector<std::string> activeRunIds;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			callback = m_executionUpdateCallback;
			m_executionUpdateCallback = {};
			for (auto& entry : m_executionByRunId) {
				if (IsTerminal(entry.second.stage)) {
					continue;
				}
				entry.second.cancelRequested = true;
				entry.second.stage = SpeechExecutionStage::Cancelled;
				cancelledStates.push_back(entry.second);
				activeRunIds.push_back(entry.first);
			}
			m_executionByRunId.clear();
			m_sessionRunBySessionId.clear();
		}

		for (const auto& runId : activeRunIds) {
			(void)runtime.Cancel(runId);
			TRACE(
				"[SpeechTranscriptionCoordinator][cancel.requested] runId=%S source=shutdown accepted=1\n",
				runId.c_str());
		}

		if (callback) {
			for (const auto& state : cancelledStates) {
				callback(state);
			}
		}
		TRACE(
			"[SpeechTranscriptionCoordinator][shutdown.summary] cancelledRuns=%zu clearedSessions=%zu\n",
			activeRunIds.size(),
			static_cast<std::size_t>(cancelledStates.size()));
	}

	bool SpeechTranscriptionCoordinator::IsTerminal(
		speechrecognition::SpeechExecutionStage stage) {
		switch (stage) {
		case speechrecognition::SpeechExecutionStage::Completed:
		case speechrecognition::SpeechExecutionStage::Failed:
		case speechrecognition::SpeechExecutionStage::Cancelled:
			return true;
		default:
			return false;
		}
	}

	bool SpeechTranscriptionCoordinator::IsSessionInFlight(
		speechrecognition::SpeechExecutionStage stage) {
		switch (stage) {
		case speechrecognition::SpeechExecutionStage::Queued:
		case speechrecognition::SpeechExecutionStage::Recording:
		case speechrecognition::SpeechExecutionStage::Stopped:
		case speechrecognition::SpeechExecutionStage::Transcribing:
			return true;
		default:
			return false;
		}
	}

	std::string SpeechTranscriptionCoordinator::BuildTrackingRunId(
		const ExecutionRequest& request) {
		if (!request.runId.empty()) {
			return request.runId;
		}
		if (!request.sessionId.empty()) {
			return request.sessionId + ":speech";
		}
		return "speech:anonymous";
	}

	SpeechTranscriptionCoordinator::ExecutionState SpeechTranscriptionCoordinator::BuildInitialState(
		const ExecutionRequest& request) {
		ExecutionState state;
		state.runId = BuildTrackingRunId(request);
		state.sessionId = request.sessionId;
		state.stage = SpeechExecutionStage::Queued;
		state.audioPath = request.audioPath;
		state.audioArtifact = request.audioArtifact;
		state.language = request.language;
		state.prompt = request.prompt;
		return state;
	}

	SpeechTranscriptionCoordinator::ExecutionState SpeechTranscriptionCoordinator::BuildState(
		const ExecutionState& baseline,
		speechrecognition::SpeechExecutionStage stage,
		const TranscribeResult* result,
		bool cancelRequested) {
		ExecutionState state = baseline;
		state.stage = stage;
		state.cancelRequested = baseline.cancelRequested || cancelRequested;
		if (result == nullptr) {
			return state;
		}

		state.runId = !result->sessionState.runId.empty() ? result->sessionState.runId : baseline.runId;
		state.sessionId = !result->sessionState.sessionId.empty() ? result->sessionState.sessionId : baseline.sessionId;
		state.audioPath = !result->sessionState.audioPath.empty() ? result->sessionState.audioPath : baseline.audioPath;
		state.audioArtifact = result->sessionState.audioArtifact.has_value()
			? result->sessionState.audioArtifact
			: baseline.audioArtifact;
		state.transcriptText = !result->sessionState.transcriptText.empty()
			? result->sessionState.transcriptText
			: result->text;
		state.language = !result->sessionState.language.empty()
			? result->sessionState.language
			: result->language;
		state.latencyMs = result->sessionState.latencyMs == 0
			? result->latencyMs
			: result->sessionState.latencyMs;
		state.segment = result->sessionState.segment;
		state.error = result->sessionState.error.has_value()
			? result->sessionState.error
			: result->error;
		if (result->cancelled) {
			state.cancelRequested = true;
			state.stage = SpeechExecutionStage::Cancelled;
		}
		return state;
	}

} // namespace blazeclaw::core
