#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace blazeclaw::chat::shared {

	namespace diagnostics_keys {
		inline constexpr const char* kSnapshot = "chat.shared.diagnostics.snapshot";
		inline constexpr const char* kStreamDelta = "chat.stream.delta.count";
		inline constexpr const char* kStreamFinal = "chat.stream.final.count";
		inline constexpr const char* kStreamError = "chat.stream.error.count";
		inline constexpr const char* kConformanceFailures = "chat.stream.conformance.failures";
		inline constexpr const char* kParityViolations = "chat.stream.parity.violations";
		inline constexpr const char* kIdempotencyDuplicates = "chat.state.idempotency.duplicates";
		inline constexpr const char* kActiveRequests = "chat.state.active.count";
		inline constexpr const char* kCancelledRequests = "chat.state.cancelled.count";
		inline constexpr const char* kIdempotencyKeys = "chat.state.idempotency.keys";
	}

	struct ChatDiagnosticsSnapshot {
		std::uint64_t streamDeltaCount = 0;
		std::uint64_t streamFinalCount = 0;
		std::uint64_t streamErrorCount = 0;
		std::uint64_t conformanceFailures = 0;
		std::uint64_t parityViolations = 0;
		std::uint64_t idempotencyDuplicates = 0;
		std::size_t activeRequestCount = 0;
		std::size_t cancelledRequestCount = 0;
		std::size_t idempotencyKeyCount = 0;
	};

	class SharedChatDiagnosticsCollector {
	public:
		void BeginRequest(const std::string& requestId) {
			if (requestId.empty()) {
				return;
			}
			std::lock_guard<std::mutex> lock(m_mutex);
			m_active.insert(requestId);
			m_cancelled.erase(requestId);
		}

		void CancelRequest(const std::string& requestId) {
			if (requestId.empty()) {
				return;
			}
			std::lock_guard<std::mutex> lock(m_mutex);
			m_cancelled.insert(requestId);
		}

		void CompleteRequest(const std::string& requestId) {
			if (requestId.empty()) {
				return;
			}
			std::lock_guard<std::mutex> lock(m_mutex);
			m_active.erase(requestId);
			m_cancelled.erase(requestId);
		}

		[[nodiscard]] bool ObserveIdempotencyKey(const std::string& key) {
			if (key.empty()) {
				return true;
			}
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto inserted = m_idempotency.insert(key).second;
			if (!inserted) {
				++m_snapshot.idempotencyDuplicates;
			}
			return inserted;
		}

		void RecordStreamType(const std::string& type) {
			std::lock_guard<std::mutex> lock(m_mutex);
			if (type == "delta") {
				++m_snapshot.streamDeltaCount;
			}
			else if (type == "final") {
				++m_snapshot.streamFinalCount;
			}
			else if (type == "error") {
				++m_snapshot.streamErrorCount;
			}
		}

		void RecordConformanceFailure() {
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_snapshot.conformanceFailures;
		}

		void RecordParityViolation() {
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_snapshot.parityViolations;
		}

		[[nodiscard]] ChatDiagnosticsSnapshot Snapshot() const {
			std::lock_guard<std::mutex> lock(m_mutex);
			ChatDiagnosticsSnapshot snapshot = m_snapshot;
			snapshot.activeRequestCount = m_active.size();
			snapshot.cancelledRequestCount = m_cancelled.size();
			snapshot.idempotencyKeyCount = m_idempotency.size();
			return snapshot;
		}

		[[nodiscard]] static nlohmann::json BuildSnapshotJson(
			const std::string& mode,
			const ChatDiagnosticsSnapshot& snapshot) {
			return nlohmann::json{
				{ "key", diagnostics_keys::kSnapshot },
				{ "mode", mode },
				{ diagnostics_keys::kStreamDelta, snapshot.streamDeltaCount },
				{ diagnostics_keys::kStreamFinal, snapshot.streamFinalCount },
				{ diagnostics_keys::kStreamError, snapshot.streamErrorCount },
				{ diagnostics_keys::kConformanceFailures, snapshot.conformanceFailures },
				{ diagnostics_keys::kParityViolations, snapshot.parityViolations },
				{ diagnostics_keys::kIdempotencyDuplicates, snapshot.idempotencyDuplicates },
				{ diagnostics_keys::kActiveRequests, snapshot.activeRequestCount },
				{ diagnostics_keys::kCancelledRequests, snapshot.cancelledRequestCount },
				{ diagnostics_keys::kIdempotencyKeys, snapshot.idempotencyKeyCount },
			};
		}

	private:
		mutable std::mutex m_mutex;
		std::unordered_set<std::string> m_active;
		std::unordered_set<std::string> m_cancelled;
		std::unordered_set<std::string> m_idempotency;
		ChatDiagnosticsSnapshot m_snapshot;
	};

	class StreamParityValidator {
	public:
		[[nodiscard]] bool Observe(
			const std::string& correlationId,
			const std::string& streamType) {
			if (correlationId.empty()) {
				return true;
			}
			if (streamType != "delta" &&
				streamType != "final" &&
				streamType != "error") {
				return true;
			}

			std::lock_guard<std::mutex> lock(m_mutex);
			auto& state = m_stateByCorrelation[correlationId];
			if (streamType == "delta") {
				if (state.seenTerminal) {
					return false;
				}
				state.seenDelta = true;
				return true;
			}

			if (state.seenTerminal) {
				return false;
			}

			state.seenTerminal = true;
			state.terminalType = streamType;
			return true;
		}

	private:
		struct CorrelationState {
			bool seenDelta = false;
			bool seenTerminal = false;
			std::string terminalType;
		};

		std::mutex m_mutex;
		std::unordered_map<std::string, CorrelationState> m_stateByCorrelation;
	};

	class RollbackSafetyEvaluator {
	public:
		[[nodiscard]] static bool IsSafeModeTransition(
			const std::string& mode,
			const bool nativeRuntimeStarted,
			const bool nodeRuntimeStartedByMode) {
			if (mode == "native") {
				return nativeRuntimeStarted;
			}
			if (mode == "legacy") {
				return nodeRuntimeStartedByMode;
			}
			if (mode == "auto") {
				return nativeRuntimeStarted || !nodeRuntimeStartedByMode;
			}
			return true;
		}
	};

} // namespace blazeclaw::chat::shared
