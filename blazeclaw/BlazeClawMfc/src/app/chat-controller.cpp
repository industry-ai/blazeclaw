#include "pch.h"
#include "chat-controller.h"
#include "../gateway/GatewayProtocolModels.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <cmath>
#include <memory>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace blazeclaw::app::chatcontroller {

	namespace {

		std::string TrimCopy(const std::string& value);
		uint64_t CurrentSteadyClockMs();
		class NativeSpeechState;
		NativeSpeechState& SpeechStateInstance();

		NativeChatControllerLifecycle& LifecycleInstance()
		{
			static NativeChatControllerLifecycle instance;
			return instance;
		}

		struct NativeSendParams {
			std::string sessionKey;
			std::string message;
			std::string idempotencyKey;
			std::string responseMode = "single";
			std::vector<std::string> requestedResponders;
			bool detached = false;
			bool forceError = false;
			std::size_t attachmentCount = 0;
		};

		struct NativeSendCorrelationSnapshot {
			std::string requestCorrelationId;
			std::string status;
			std::string activeRunId;
			std::string promptRunId;
			std::string activePromptRunId;
			std::string activeResponderRunId;
			std::string responseMode = "single";
			std::string promptTerminalState;
			bool multiActive = false;
			bool promptGroupCompleted = false;
			std::size_t activeResponderCount = 0;
			std::size_t queueDepth = 0;
			std::size_t pendingCorrelationCount = 0;
			bool queued = false;
		};

		struct NativeStreamStateSnapshot {
			std::string activeRunId;
			std::string activePromptRunId;
			std::string activeResponderRunId;
			std::string streamText;
			std::string terminalState;
			bool hasStreamDraft = false;
			bool promptCompleted = false;
			std::size_t activeResponderCount = 0;
			std::size_t completedResponderCount = 0;
			std::size_t deltaCount = 0;
			std::size_t terminalCount = 0;
			nlohmann::json responderStreams = nlohmann::json::array();
			nlohmann::json promptGroups = nlohmann::json::array();
		};

		struct NativeProcessEventsParams {
			std::string sessionKey;
			nlohmann::json events = nlohmann::json::array();
		};

		struct NativeProcessEventsResult {
			NativeStreamStateSnapshot streamSnapshot;
			nlohmann::json uiOps = nlohmann::json::array();
			std::size_t processedEventCount = 0;
		};

		struct NativeSessionOption {
			std::string id;
			std::string scope;
			bool active = false;
		};

		struct NativeSessionSettingsSnapshot {
			std::string activeSessionKey = "main";
			std::vector<NativeSessionOption> options;
			uint64_t switchGeneration = 0;
			bool switched = false;
		};

		struct NativeLoadSessionOptionsParams {
			std::string sessionKey;
			nlohmann::json sessions = nlohmann::json::array();
		};

		struct NativeModelOption {
			std::string id;
			std::string label;
		};

		struct NativeModelSettingsSnapshot {
			std::vector<NativeModelOption> modelOptions;
			std::vector<std::string> thinkingOptions;
			std::string selectedModel = "default";
			std::string thinkingLevel = "normal";
			uint64_t modelSelectionGeneration = 0;
			uint64_t thinkingLevelGeneration = 0;
			bool modelSelectionChanged = false;
			bool thinkingLevelChanged = false;
		};

		struct NativeApprovalValidationSnapshot {
			std::string approvalToken;
			bool valid = false;
			bool tokenPresent = false;
			uint64_t expiresAtEpochMs = 0;
			std::string errorCode;
			std::string source;
			bool approve = false;
			bool ok = false;
			std::string status;
			std::string message;
			std::string output;
			std::string remediation;
			std::string missingDependency;
			std::string installHint;
			std::string configHint;
			std::string failureBucket;
			std::string errorMessage;
			bool readinessKnown = false;
			bool readinessReady = false;
			std::string readinessCode;
			std::string readinessMessage;
			std::string readinessRemediation;
			std::string readinessMissingDependency;
			std::string readinessInstallHint;
			std::string readinessConfigHint;
			std::string readinessBucket;
			uint64_t validationGeneration = 0;
			uint64_t executionGeneration = 0;
		};

		struct NativeSpeechCapabilitiesSnapshot {
			bool sttSupported = false;
			bool sttReady = false;
			std::string audioHandoffMode = "dual";
			bool streamingSupported = false;
			bool streamingPreviewEnabled = false;
			bool livePreviewToggleEnabled = false;
			std::string livePreviewToggleSource;
			bool streamingConfigured = false;
			bool modelNativeVad = false;
			int64_t streamingChunkMs = 0;
			int64_t streamingLookbackMs = 0;
			std::string provider;
			std::string effectiveExecutionProvider;
			bool cudaExecutionProviderAvailable = false;
			bool cudaExecutionProviderEnabled = false;
			std::string cudaExecutionProviderReason;
			bool transcriptSupportsSegments = false;
			bool transcriptSupportsInterim = false;
			bool transcriptSupportsFinal = true;
			bool ttsSupported = false;
			bool ttsReady = false;
			std::vector<std::string> lifecycle;
			bool loaded = false;
			std::string error;
		};

		struct NativeSpeechErrorPolicySnapshot {
			bool loaded = false;
			std::string defaultClass = "status";
			nlohmann::json map = nlohmann::json::object();
			nlohmann::json retry = nlohmann::json::object();
		};

		struct NativeSpeechSessionSnapshot {
			std::string stage = "idle";
			std::string text;
			std::string segmentText;
			bool segmentFinal = false;
			int64_t segmentSequence = 0;
			std::string runId;
			std::string finalRunId;
			std::string sessionId;
			std::string audioPath;
			nlohmann::json audioArtifact = nullptr;
			std::string language;
			int64_t latencyMs = 0;
			bool cancelled = false;
			std::string errorCode;
			std::string errorMessage;
			std::string errorClass = "status";
			bool retryable = false;
			std::string retryStrategy = "immediate";
			std::string retryGuidance;
			nlohmann::json debugInfo = nullptr;
			nlohmann::json preflight = nullptr;
			nlohmann::json noSpeechTriage = nullptr;
			uint64_t updatedAtMs = 0;
			uint64_t generation = 0;
		};

		struct NativeSpeechStateSnapshot {
			NativeSpeechCapabilitiesSnapshot capabilities;
			NativeSpeechErrorPolicySnapshot errorPolicy;
			NativeSpeechSessionSnapshot session;
		};

		class NativeChatSendState final {
		public:
			NativeSendCorrelationSnapshot RegisterSend(
				const blazeclaw::gateway::protocol::RequestFrame& request,
				const NativeSendParams& params)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_responseMode = NormalizeResponseMode(params.responseMode);
				m_lastPromptCompleted = false;
				m_lastPromptTerminalState.clear();

				const std::string correlationId = TrimCopy(request.id);
				const bool hasActiveRun = !m_activeRunId.empty();
				if (hasActiveRun)
				{
					m_sendQueue.push_back(correlationId);
				}
				else
				{
					const std::string provisionalRunId = TrimCopy(params.idempotencyKey);
					m_activeRunId = provisionalRunId.empty()
						? std::string("native-") + correlationId
						: provisionalRunId;
					m_activeResponderRunId = m_activeRunId;
					m_activePromptRunId = m_activeRunId + ".prompt";
				}

				m_pendingCorrelations[correlationId] = CorrelationEntry{
					.method = "chat.send",
					.sessionKey = [&params]() {
						const std::string key = TrimCopy(params.sessionKey);
						return key.empty() ? std::string("main") : key;
					}(),
					.status = hasActiveRun ? "queued" : "dispatched",
					.responseMode = m_responseMode,
					.requestedResponders = params.requestedResponders,
				};

				return BuildSnapshot(correlationId, hasActiveRun);
			}

			NativeSendCorrelationSnapshot HandleRpcResult(
				const std::string& correlationId,
				bool ok,
				const std::string& runId,
				bool terminal,
				const std::string& terminalState,
				const std::string& promptRunId,
				const std::string& responderRunId,
				const std::vector<std::string>& responderRunIds,
				const std::string& responseMode)
			{
				std::lock_guard<std::mutex> lock(m_mutex);

				const std::string normalizedCorrelationId = TrimCopy(correlationId);
				auto it = m_pendingCorrelations.find(normalizedCorrelationId);
				if (it != m_pendingCorrelations.end())
				{
					it->second.status = ok ? "acknowledged" : "failed";
					if (!responseMode.empty())
					{
						it->second.responseMode = NormalizeResponseMode(responseMode);
					}
					m_responseMode = it->second.responseMode;
					m_pendingCorrelations.erase(it);
				}
				else if (!responseMode.empty())
				{
					m_responseMode = NormalizeResponseMode(responseMode);
				}

				const std::string normalizedRunId = TrimCopy(runId);
				if (!normalizedRunId.empty())
				{
					m_activeRunId = normalizedRunId;
				}

				const std::string normalizedPromptRunId = ResolvePromptRunId(promptRunId, normalizedRunId);
				if (!normalizedPromptRunId.empty())
				{
					m_activePromptRunId = normalizedPromptRunId;
				}

				const std::string normalizedResponderRunId = ResolveResponderRunId(responderRunId, normalizedRunId);
				if (!normalizedResponderRunId.empty())
				{
					m_activeResponderRunId = normalizedResponderRunId;
				}

				RegisterPromptRespondersUnsafe(normalizedPromptRunId, responderRunIds);

				if (terminal)
				{
					const auto completion = HandleTerminalTransitionUnsafe(
						normalizedPromptRunId,
						normalizedResponderRunId,
						terminalState);
					if (completion.completed)
					{
						FinishPromptGroupUnsafe(completion.promptRunId, completion.terminalState);
					}
				}

				return BuildSnapshot(normalizedCorrelationId, false);
			}

			NativeSendCorrelationSnapshot HandleRpcResult(
				const std::string& correlationId,
				bool ok,
				const std::string& runId,
				bool terminal)
			{
				return HandleRpcResult(
					correlationId,
					ok,
					runId,
					terminal,
					"",
					"",
					"",
					{},
					"");
			}

			NativeSendCorrelationSnapshot NoteEventTransition(
				const std::string& promptRunId,
				const std::string& responderRunId,
				const std::string& state)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				const std::string normalizedPrompt = ResolvePromptRunId(promptRunId, responderRunId);
				const std::string normalizedResponder = ResolveResponderRunId(responderRunId, responderRunId);
				const auto completion = HandleTerminalTransitionUnsafe(
					normalizedPrompt,
					normalizedResponder,
					state);
				if (completion.completed)
				{
					FinishPromptGroupUnsafe(completion.promptRunId, completion.terminalState);
				}

				NativeSendCorrelationSnapshot snapshot = BuildSnapshot("", false);
				snapshot.promptGroupCompleted = completion.completed;
				snapshot.promptRunId = completion.promptRunId;
				snapshot.promptTerminalState = completion.terminalState;
				return snapshot;
			}

			NativeSendCorrelationSnapshot NotePromptAbort(
				const std::string& promptRunId,
				const std::string& state)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				NativeSendCorrelationSnapshot snapshot = BuildSnapshot("", false);
				const std::string normalizedPromptRunId = TrimCopy(promptRunId);
				if (normalizedPromptRunId.empty())
				{
					return snapshot;
				}

				auto& group = m_promptGroups[normalizedPromptRunId];
				for (const auto& runId : group.expectedResponderRunIds)
				{
					if (!runId.empty())
					{
						group.terminalResponderRunIds.insert(runId);
					}
				}
				for (const auto& runId : group.observedResponderRunIds)
				{
					if (!runId.empty())
					{
						group.terminalResponderRunIds.insert(runId);
					}
				}

				std::string normalizedState = NormalizeState(state);
				if (!IsTerminalState(normalizedState))
				{
					normalizedState = "aborted";
				}

				group.completed = true;
				group.terminalState = normalizedState;
				FinishPromptGroupUnsafe(normalizedPromptRunId, group.terminalState);

				snapshot = BuildSnapshot("", false);
				snapshot.promptGroupCompleted = true;
				snapshot.promptRunId = normalizedPromptRunId;
				snapshot.promptTerminalState = group.terminalState;
				return snapshot;
			}

			NativeSendCorrelationSnapshot Snapshot() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return NativeSendCorrelationSnapshot{
					.requestCorrelationId = "",
					.status = "snapshot",
					.activeRunId = m_activeRunId,
					.promptRunId = m_activePromptRunId,
					.activePromptRunId = m_activePromptRunId,
					.activeResponderRunId = m_activeResponderRunId,
					.responseMode = m_responseMode,
					.promptTerminalState = m_lastPromptTerminalState,
					.multiActive = m_responseMode == "multi_active",
					.promptGroupCompleted = m_lastPromptCompleted,
					.activeResponderCount = ActiveResponderCountUnsafe(),
					.queueDepth = m_sendQueue.size(),
					.pendingCorrelationCount = m_pendingCorrelations.size(),
					.queued = false,
				};
			}

			void Reset()
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_sendQueue.clear();
				m_pendingCorrelations.clear();
				m_activeRunId.clear();
				m_activePromptRunId.clear();
				m_activeResponderRunId.clear();
				m_responseMode = "single";
				m_lastPromptTerminalState.clear();
				m_lastPromptCompleted = false;
				m_promptGroups.clear();
			}

		private:
			struct CorrelationEntry {
				std::string method;
				std::string sessionKey;
				std::string status;
				std::string responseMode = "single";
				std::vector<std::string> requestedResponders;
			};

			struct PromptGroupEntry {
				std::unordered_set<std::string> expectedResponderRunIds;
				std::unordered_set<std::string> observedResponderRunIds;
				std::unordered_set<std::string> terminalResponderRunIds;
				std::string terminalState;
				bool completed = false;
			};

			struct PromptCompletion {
				bool completed = false;
				std::string promptRunId;
				std::string terminalState;
			};

			static std::string NormalizeState(const std::string& value)
			{
				std::string normalized = TrimCopy(value);
				std::transform(
					normalized.begin(),
					normalized.end(),
					normalized.begin(),
					[](const unsigned char c) {
						return static_cast<char>(std::tolower(c));
					});
				return normalized;
			}

			static bool IsTerminalState(const std::string& normalizedState)
			{
				return normalizedState == "final" ||
					normalizedState == "completed" ||
					normalizedState == "error" ||
					normalizedState == "aborted" ||
					normalizedState == "needs_approval";
			}

			static std::string NormalizeResponseMode(const std::string& value)
			{
				const std::string normalized = NormalizeState(value);
				return normalized == "multi_active"
					? normalized
					: "single";
			}

			static std::string ResolvePromptRunId(
				const std::string& promptRunId,
				const std::string& runId)
			{
				const std::string normalizedPrompt = TrimCopy(promptRunId);
				if (!normalizedPrompt.empty())
				{
					return normalizedPrompt;
				}

				const std::string normalizedRun = TrimCopy(runId);
				if (normalizedRun.empty())
				{
					return "";
				}

				return normalizedRun + ".prompt";
			}

			static std::string ResolveResponderRunId(
				const std::string& responderRunId,
				const std::string& runId)
			{
				const std::string normalizedResponder = TrimCopy(responderRunId);
				if (!normalizedResponder.empty())
				{
					return normalizedResponder;
				}

				return TrimCopy(runId);
			}

			void RegisterPromptRespondersUnsafe(
				const std::string& promptRunId,
				const std::vector<std::string>& responderRunIds)
			{
				if (promptRunId.empty())
				{
					return;
				}

				auto& group = m_promptGroups[promptRunId];
				for (const auto& candidate : responderRunIds)
				{
					const std::string normalized = TrimCopy(candidate);
					if (normalized.empty())
					{
						continue;
					}
					group.expectedResponderRunIds.insert(normalized);
				}
			}

			PromptCompletion HandleTerminalTransitionUnsafe(
				const std::string& promptRunId,
				const std::string& responderRunId,
				const std::string& state)
			{
				PromptCompletion completion;
				completion.promptRunId = promptRunId;
				if (promptRunId.empty() || responderRunId.empty())
				{
					return completion;
				}

				auto& group = m_promptGroups[promptRunId];
				group.observedResponderRunIds.insert(responderRunId);

				const std::string normalizedState = NormalizeState(state);
				if (IsTerminalState(normalizedState))
				{
					group.terminalResponderRunIds.insert(responderRunId);
					if (group.terminalState.empty())
					{
						group.terminalState = normalizedState;
					}
					else if (normalizedState == "error")
					{
						group.terminalState = "error";
					}
					else if (group.terminalState != "error" && normalizedState == "needs_approval")
					{
						group.terminalState = "needs_approval";
					}
					else if (group.terminalState != "error" &&
						group.terminalState != "needs_approval" &&
						normalizedState == "aborted")
					{
						group.terminalState = "aborted";
					}
				}

				if (!group.expectedResponderRunIds.empty())
				{
					group.completed =
						group.terminalResponderRunIds.size() >= group.expectedResponderRunIds.size();
				}
				else
				{
					group.completed =
						!group.observedResponderRunIds.empty() &&
						group.terminalResponderRunIds.size() >= group.observedResponderRunIds.size();
				}

				completion.completed = group.completed;
				completion.terminalState = group.terminalState.empty()
					? std::string("final")
					: group.terminalState;
				return completion;
			}

			void FinishPromptGroupUnsafe(
				const std::string& promptRunId,
				const std::string& terminalState)
			{
				m_lastPromptCompleted = true;
				m_lastPromptTerminalState = terminalState;
				m_activePromptRunId = promptRunId;
				m_activeResponderRunId.clear();
				m_activeRunId.clear();
				if (!m_sendQueue.empty())
				{
					m_sendQueue.pop_front();
				}
			}

			std::size_t ActiveResponderCountUnsafe() const
			{
				if (m_activePromptRunId.empty())
				{
					return 0;
				}
				const auto it = m_promptGroups.find(m_activePromptRunId);
				if (it == m_promptGroups.end())
				{
					return 0;
				}
				if (!it->second.expectedResponderRunIds.empty())
				{
					return it->second.expectedResponderRunIds.size();
				}
				return it->second.observedResponderRunIds.size();
			}

			NativeSendCorrelationSnapshot BuildSnapshot(
				const std::string& correlationId,
				bool queued) const
			{
				return NativeSendCorrelationSnapshot{
					.requestCorrelationId = correlationId,
					.status = queued ? "queued" : "dispatched",
					.activeRunId = m_activeRunId,
					.promptRunId = m_activePromptRunId,
					.activePromptRunId = m_activePromptRunId,
					.activeResponderRunId = m_activeResponderRunId,
					.responseMode = m_responseMode,
					.promptTerminalState = m_lastPromptTerminalState,
					.multiActive = m_responseMode == "multi_active",
					.promptGroupCompleted = m_lastPromptCompleted,
					.activeResponderCount = ActiveResponderCountUnsafe(),
					.queueDepth = m_sendQueue.size(),
					.pendingCorrelationCount = m_pendingCorrelations.size(),
					.queued = queued,
				};
			}

			mutable std::mutex m_mutex;
			std::deque<std::string> m_sendQueue;
			std::unordered_map<std::string, CorrelationEntry> m_pendingCorrelations;
			std::unordered_map<std::string, PromptGroupEntry> m_promptGroups;
			std::string m_activeRunId;
			std::string m_activePromptRunId;
			std::string m_activeResponderRunId;
			std::string m_responseMode = "single";
			std::string m_lastPromptTerminalState;
			bool m_lastPromptCompleted = false;
		};

		NativeChatSendState& SendStateInstance()
		{
			static NativeChatSendState instance;
			return instance;
		}

		class NativeSessionSettingsState final {
		public:
			NativeSessionSettingsSnapshot LoadOptions(const NativeLoadSessionOptionsParams& params)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot.switched = false;

				std::vector<NativeSessionOption> normalized;
				if (params.sessions.is_array())
				{
					normalized.reserve(params.sessions.size());
					for (const auto& session : params.sessions)
					{
						if (!session.is_object())
						{
							continue;
						}

						NativeSessionOption row;
						row.id = NormalizeSessionKey(session.value("id", std::string{}));
						row.scope = NormalizeScope(session.value("scope", std::string{}));
						row.active = session.value("active", false);
						normalized.push_back(std::move(row));
					}
				}

				if (normalized.empty())
				{
					normalized.push_back(NativeSessionOption{
						.id = "main",
						.scope = "chat",
						.active = true,
					});
				}

				std::unordered_set<std::string> seen;
				std::vector<NativeSessionOption> deduped;
				deduped.reserve(normalized.size());
				for (const auto& row : normalized)
				{
					if (!seen.insert(row.id).second)
					{
						continue;
					}
					deduped.push_back(row);
				}

				m_snapshot.options = std::move(deduped);

				const std::string requested = NormalizeSessionKey(params.sessionKey);
				if (!requested.empty())
				{
					m_snapshot.activeSessionKey = requested;
				}

				if (!ContainsSessionUnsafe(m_snapshot.activeSessionKey))
				{
					m_snapshot.activeSessionKey = m_snapshot.options.front().id;
				}

				for (auto& row : m_snapshot.options)
				{
					row.active = row.id == m_snapshot.activeSessionKey;
				}

				return m_snapshot;
			}

			NativeSessionSettingsSnapshot SwitchSession(const std::string& sessionKey)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				const std::string nextSession = NormalizeSessionKey(sessionKey);
				if (nextSession == m_snapshot.activeSessionKey)
				{
					m_snapshot.switched = false;
					return m_snapshot;
				}

				if (!ContainsSessionUnsafe(nextSession))
				{
					m_snapshot.options.push_back(NativeSessionOption{
						.id = nextSession,
						.scope = "chat",
						.active = true,
					});
				}

				m_snapshot.activeSessionKey = nextSession;
				for (auto& row : m_snapshot.options)
				{
					row.active = row.id == m_snapshot.activeSessionKey;
				}
				m_snapshot.switchGeneration += 1;
				m_snapshot.switched = true;
				return m_snapshot;
			}

			NativeSessionSettingsSnapshot Snapshot() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_snapshot;
			}

			void Reset()
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot = NativeSessionSettingsSnapshot{};
				m_snapshot.options = {
					NativeSessionOption{
						.id = "main",
						.scope = "chat",
						.active = true,
					}
				};
			}

		private:
			static std::string NormalizeSessionKey(const std::string& value)
			{
				const std::string normalized = TrimCopy(value);
				return normalized.empty() ? std::string("main") : normalized;
			}

			static std::string NormalizeScope(const std::string& value)
			{
				const std::string normalized = TrimCopy(value);
				return normalized.empty() ? std::string("chat") : normalized;
			}

			bool ContainsSessionUnsafe(const std::string& key) const
			{
				for (const auto& row : m_snapshot.options)
				{
					if (row.id == key)
					{
						return true;
					}
				}
				return false;
			}

			mutable std::mutex m_mutex;
			NativeSessionSettingsSnapshot m_snapshot{
				.activeSessionKey = "main",
				.options = { NativeSessionOption{ .id = "main", .scope = "chat", .active = true } },
				.switchGeneration = 0,
				.switched = false,
			};
		};

		NativeSessionSettingsState& SessionSettingsInstance()
		{
			static NativeSessionSettingsState instance;
			return instance;
		}

		class NativeModelSettingsState final {
		public:
			NativeModelSettingsSnapshot LoadModelOptions(const nlohmann::json& params)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot.modelSelectionChanged = false;
				m_snapshot.thinkingLevelChanged = false;

				std::vector<NativeModelOption> normalized;
				const auto models = params.contains("models") && params["models"].is_array()
					? params["models"]
					: nlohmann::json::array();
				normalized.reserve(models.size());
				for (const auto& item : models)
				{
					if (!item.is_object())
					{
						continue;
					}
					const std::string id = NormalizeModelId(item.value("id", std::string{}));
					if (id.empty())
					{
						continue;
					}
					NativeModelOption row;
					row.id = id;
					row.label = NormalizeModelLabel(item.value("label", std::string{}), id);
					normalized.push_back(std::move(row));
				}

				if (normalized.empty())
				{
					normalized.push_back(NativeModelOption{
						.id = "default",
						.label = "default",
					});
				}

				std::unordered_set<std::string> seen;
				std::vector<NativeModelOption> deduped;
				deduped.reserve(normalized.size());
				for (const auto& row : normalized)
				{
					if (!seen.insert(row.id).second)
					{
						continue;
					}
					deduped.push_back(row);
				}
				m_snapshot.modelOptions = std::move(deduped);

				const std::string incomingSelectedModel =
					params.contains("selectedModel") && params["selectedModel"].is_string()
					? NormalizeModelId(params["selectedModel"].get<std::string>())
					: std::string{};
				if (!incomingSelectedModel.empty())
				{
					m_snapshot.selectedModel = incomingSelectedModel;
				}

				if (!ContainsModelUnsafe(m_snapshot.selectedModel))
				{
					m_snapshot.selectedModel = m_snapshot.modelOptions.front().id;
				}

				std::vector<std::string> thinking = {
					"low",
					"normal",
					"high",
				};
				if (params.contains("thinkingOptions") && params["thinkingOptions"].is_array())
				{
					std::vector<std::string> custom;
					for (const auto& item : params["thinkingOptions"])
					{
						if (!item.is_string())
						{
							continue;
						}
						const std::string level = NormalizeThinkingLevel(item.get<std::string>());
						if (level.empty())
						{
							continue;
						}
						custom.push_back(level);
					}
					if (!custom.empty())
					{
						thinking = std::move(custom);
					}
				}
				m_snapshot.thinkingOptions = std::move(thinking);

				const std::string incomingThinking =
					params.contains("thinkingLevel") && params["thinkingLevel"].is_string()
					? NormalizeThinkingLevel(params["thinkingLevel"].get<std::string>())
					: std::string{};
				if (!incomingThinking.empty())
				{
					m_snapshot.thinkingLevel = incomingThinking;
				}

				if (!ContainsThinkingLevelUnsafe(m_snapshot.thinkingLevel))
				{
					m_snapshot.thinkingLevel = m_snapshot.thinkingOptions.front();
				}

				return m_snapshot;
			}

			NativeModelSettingsSnapshot ApplyModelSelection(const std::string& modelId)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				const std::string nextModel = NormalizeModelId(modelId);
				if (nextModel.empty())
				{
					m_snapshot.modelSelectionChanged = false;
					return m_snapshot;
				}

				if (!ContainsModelUnsafe(nextModel))
				{
					m_snapshot.modelOptions.push_back(NativeModelOption{
						.id = nextModel,
						.label = nextModel,
					});
				}

				if (nextModel == m_snapshot.selectedModel)
				{
					m_snapshot.modelSelectionChanged = false;
					return m_snapshot;
				}

				m_snapshot.selectedModel = nextModel;
				m_snapshot.modelSelectionGeneration += 1;
				m_snapshot.modelSelectionChanged = true;
				return m_snapshot;
			}

			NativeModelSettingsSnapshot ApplyThinkingLevel(const std::string& level)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				const std::string nextLevel = NormalizeThinkingLevel(level);
				if (nextLevel.empty())
				{
					m_snapshot.thinkingLevelChanged = false;
					return m_snapshot;
				}

				if (!ContainsThinkingLevelUnsafe(nextLevel))
				{
					m_snapshot.thinkingOptions.push_back(nextLevel);
				}

				if (nextLevel == m_snapshot.thinkingLevel)
				{
					m_snapshot.thinkingLevelChanged = false;
					return m_snapshot;
				}

				m_snapshot.thinkingLevel = nextLevel;
				m_snapshot.thinkingLevelGeneration += 1;
				m_snapshot.thinkingLevelChanged = true;
				return m_snapshot;
			}

			NativeModelSettingsSnapshot Snapshot() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_snapshot;
			}

			void Reset()
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot = NativeModelSettingsSnapshot{};
				m_snapshot.modelOptions = {
					NativeModelOption{ .id = "default", .label = "default" }
				};
				m_snapshot.thinkingOptions = {
					"low",
					"normal",
					"high",
				};
			}

		private:
			static std::string NormalizeModelId(const std::string& value)
			{
				return TrimCopy(value);
			}

			static std::string NormalizeModelLabel(const std::string& value, const std::string& fallback)
			{
				const std::string label = TrimCopy(value);
				return label.empty() ? fallback : label;
			}

			static std::string NormalizeThinkingLevel(const std::string& value)
			{
				std::string normalized = TrimCopy(value);
				std::transform(
					normalized.begin(),
					normalized.end(),
					normalized.begin(),
					[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return normalized;
			}

			bool ContainsModelUnsafe(const std::string& id) const
			{
				for (const auto& row : m_snapshot.modelOptions)
				{
					if (row.id == id)
					{
						return true;
					}
				}
				return false;
			}

			bool ContainsThinkingLevelUnsafe(const std::string& level) const
			{
				for (const auto& row : m_snapshot.thinkingOptions)
				{
					if (row == level)
					{
						return true;
					}
				}
				return false;
			}

			mutable std::mutex m_mutex;
			NativeModelSettingsSnapshot m_snapshot{
				.modelOptions = { NativeModelOption{ .id = "default", .label = "default" } },
				.thinkingOptions = { "low", "normal", "high" },
				.selectedModel = "default",
				.thinkingLevel = "normal",
				.modelSelectionGeneration = 0,
				.thinkingLevelGeneration = 0,
				.modelSelectionChanged = false,
				.thinkingLevelChanged = false,
			};
		};

		NativeModelSettingsState& ModelSettingsInstance()
		{
			static NativeModelSettingsState instance;
			return instance;
		}

		class NativeApprovalValidationState final {
		public:
			struct NativeApprovalExecutionInput {
				std::string approvalToken;
				bool approve = false;
				bool readinessKnown = false;
				bool readinessReady = false;
				std::string readinessCode;
				std::string readinessMessage;
				std::string readinessRemediation;
				std::string readinessMissingDependency;
				std::string readinessInstallHint;
				std::string readinessConfigHint;
				std::string readinessBucket;
				nlohmann::json executePayload = nlohmann::json::object();
			};

			NativeApprovalValidationSnapshot ParseTokenFromText(const std::string& text)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot.validationGeneration += 1;
				m_snapshot.source = "text";
				m_snapshot.approvalToken.clear();
				m_snapshot.expiresAtEpochMs = 0;

				const std::string raw = text;
				if (raw.empty())
				{
					m_snapshot.valid = false;
					m_snapshot.tokenPresent = false;
					m_snapshot.errorCode = "approval_token_missing";
					return m_snapshot;
				}

				std::smatch match;
				const std::regex preferred(
					R"(approvalToken=([A-Za-z0-9:_\-]+)(?=\s|[，。！？；,.!?;]|$))");
				const std::regex fallback(R"(approvalToken=([A-Za-z0-9:_\-]+))");
				bool found = std::regex_search(raw, match, preferred);
				if (!found)
				{
					found = std::regex_search(raw, match, fallback);
				}

				if (!found || match.size() < 2)
				{
					m_snapshot.valid = false;
					m_snapshot.tokenPresent = false;
					m_snapshot.errorCode = "approval_token_missing";
					return m_snapshot;
				}

				const std::string token = TrimCopy(match[1].str());
				m_snapshot.approvalToken = token;
				m_snapshot.tokenPresent = !token.empty();
				m_snapshot.valid = IsValidToken(token);
				m_snapshot.errorCode = m_snapshot.valid ? "" : "approval_token_invalid";

				std::smatch expiresMatch;
				const std::regex expiresPattern(R"(expiresAtEpochMs=(\d{8,}))");
				if (std::regex_search(raw, expiresMatch, expiresPattern) && expiresMatch.size() >= 2)
				{
					try
					{
						m_snapshot.expiresAtEpochMs = static_cast<uint64_t>(
							std::stoull(expiresMatch[1].str()));
					}
					catch (...)
					{
						m_snapshot.expiresAtEpochMs = 0;
					}
				}

				return m_snapshot;
			}

			NativeApprovalValidationSnapshot ValidateToken(const std::string& token)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot.validationGeneration += 1;
				m_snapshot.source = "token";
				m_snapshot.approvalToken = TrimCopy(token);
				m_snapshot.expiresAtEpochMs = 0;

				if (m_snapshot.approvalToken.empty())
				{
					m_snapshot.valid = false;
					m_snapshot.tokenPresent = false;
					m_snapshot.errorCode = "approval_token_missing";
					return m_snapshot;
				}

				m_snapshot.tokenPresent = true;
				m_snapshot.valid = IsValidToken(m_snapshot.approvalToken);
				m_snapshot.errorCode = m_snapshot.valid ? "" : "approval_token_invalid";
				return m_snapshot;
			}

			NativeApprovalValidationSnapshot ApplyExecutionResult(const NativeApprovalExecutionInput& input)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot.executionGeneration += 1;
				m_snapshot.source = "execute";
				m_snapshot.approve = input.approve;
				m_snapshot.approvalToken = TrimCopy(input.approvalToken);
				m_snapshot.tokenPresent = !m_snapshot.approvalToken.empty();
				m_snapshot.valid = IsValidToken(m_snapshot.approvalToken);
				m_snapshot.status.clear();
				m_snapshot.message.clear();
				m_snapshot.output.clear();
				m_snapshot.errorCode.clear();
				m_snapshot.remediation.clear();
				m_snapshot.missingDependency.clear();
				m_snapshot.installHint.clear();
				m_snapshot.configHint.clear();
				m_snapshot.failureBucket.clear();
				m_snapshot.errorMessage.clear();
				m_snapshot.readinessKnown = input.readinessKnown;
				m_snapshot.readinessReady = input.readinessReady;
				m_snapshot.readinessCode = TrimCopy(input.readinessCode);
				m_snapshot.readinessMessage = TrimCopy(input.readinessMessage);
				m_snapshot.readinessRemediation = TrimCopy(input.readinessRemediation);
				m_snapshot.readinessMissingDependency = TrimCopy(input.readinessMissingDependency);
				m_snapshot.readinessInstallHint = TrimCopy(input.readinessInstallHint);
				m_snapshot.readinessConfigHint = TrimCopy(input.readinessConfigHint);
				m_snapshot.readinessBucket = TrimCopy(input.readinessBucket);

				if (!m_snapshot.valid)
				{
					m_snapshot.ok = false;
					m_snapshot.status = "invalid";
					m_snapshot.message = "approval token is invalid";
					m_snapshot.errorCode = "approval_token_invalid";
					return m_snapshot;
				}

				const auto payload = input.executePayload;
				const std::string rawStatus = payload.contains("status") && payload["status"].is_string()
					? NormalizeLower(payload["status"].get<std::string>())
					: std::string{};
				const std::string output = payload.contains("output") && payload["output"].is_string()
					? payload["output"].get<std::string>()
					: std::string{};
				m_snapshot.output = output;

				nlohmann::json parsedHints = ParseExecutionHints(output);
				m_snapshot.errorCode = ResolveNormalizedErrorCode(payload, parsedHints, output);
				const bool expired = ContainsInsensitive(output, "expired") ||
					m_snapshot.errorCode == "approval_token_expired";

				bool resolvedOk = rawStatus == "cancelled" || rawStatus == "ok";
				if (input.approve)
				{
					resolvedOk = rawStatus == "ok";
				}

				m_snapshot.status = rawStatus.empty() ? "unknown" : rawStatus;
				if (expired)
				{
					m_snapshot.status = "expired";
				}
				m_snapshot.ok = resolvedOk && !expired;

				if (parsedHints.is_object())
				{
					m_snapshot.remediation = ReadStringField(parsedHints, "remediation");
					m_snapshot.missingDependency = ReadStringField(parsedHints, "missingDependency");
					m_snapshot.installHint = ReadStringField(parsedHints, "installHint");
					m_snapshot.configHint = ReadStringField(parsedHints, "configHint");
					m_snapshot.failureBucket = ReadStringField(parsedHints, "bucket");
					m_snapshot.errorMessage = ReadStringField(parsedHints, "message");
				}

				if (m_snapshot.status == "invalid")
				{
					m_snapshot.message = "approval token is invalid";
				}

				return m_snapshot;
			}

			NativeApprovalValidationSnapshot Snapshot() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_snapshot;
			}

			void Reset()
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot = NativeApprovalValidationSnapshot{};
			}

		private:
			static std::string NormalizeLower(const std::string& value)
			{
				std::string normalized = TrimCopy(value);
				std::transform(
					normalized.begin(),
					normalized.end(),
					normalized.begin(),
					[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return normalized;
			}

			static bool ContainsInsensitive(const std::string& source, const std::string& needle)
			{
				if (needle.empty())
				{
					return false;
				}
				const std::string lhs = NormalizeLower(source);
				const std::string rhs = NormalizeLower(needle);
				return lhs.find(rhs) != std::string::npos;
			}

			static nlohmann::json ParseExecutionHints(const std::string& output)
			{
				if (output.empty())
				{
					return nlohmann::json::object();
				}
				const auto parsed = nlohmann::json::parse(output, nullptr, false);
				if (parsed.is_discarded() || !parsed.is_object())
				{
					return nlohmann::json::object();
				}
				if (!parsed.contains("error") || !parsed["error"].is_object())
				{
					return nlohmann::json::object();
				}
				return parsed["error"];
			}

			static std::string ReadStringField(const nlohmann::json& obj, const char* key)
			{
				if (key == nullptr || !obj.is_object() || !obj.contains(key) || !obj[key].is_string())
				{
					return "";
				}
				return TrimCopy(obj[key].get<std::string>());
			}

			static std::string ResolveNormalizedErrorCode(
				const nlohmann::json& executePayload,
				const nlohmann::json& parsedHints,
				const std::string& output)
			{
				const std::string topLevel = executePayload.contains("errorCode") && executePayload["errorCode"].is_string()
					? TrimCopy(executePayload["errorCode"].get<std::string>())
					: std::string{};
				if (!topLevel.empty() && topLevel != "legacy_execution_failed")
				{
					return topLevel;
				}

				const std::string nested = ReadStringField(parsedHints, "code");
				if (!nested.empty())
				{
					return nested;
				}

				if (!topLevel.empty())
				{
					return topLevel;
				}

				std::smatch match;
				const std::regex codePattern(
					R"regex("code"\s*:\s*"([^"]+)")regex");
				if (std::regex_search(output, match, codePattern) && match.size() >= 2)
				{
					return TrimCopy(match[1].str());
				}

				return "";
			}

			static bool IsValidToken(const std::string& token)
			{
				if (token.empty())
				{
					return false;
				}

				const std::regex allowedPattern(R"(^[A-Za-z0-9:_\-]+$)");
				if (!std::regex_match(token, allowedPattern))
				{
					return false;
				}

				const std::regex emailApprovalPattern(R"(^email-approval-\d{10,}-\d+$)");
				if (std::regex_match(token, emailApprovalPattern))
				{
					return true;
				}

				return token.size() >= 24;
			}

			mutable std::mutex m_mutex;
			NativeApprovalValidationSnapshot m_snapshot;
		};

		NativeApprovalValidationState& ApprovalValidationInstance()
		{
			static NativeApprovalValidationState instance;
			return instance;
		}

		class NativeSpeechState final {
		public:
			NativeSpeechStateSnapshot LoadCapabilities(const nlohmann::json& payload)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot.capabilities = NormalizeCapabilities(payload);
				return m_snapshot;
			}

			NativeSpeechStateSnapshot LoadErrorPolicy(const nlohmann::json& payload)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot.errorPolicy = NormalizeErrorPolicy(payload);
				return m_snapshot;
			}

			NativeSpeechStateSnapshot ApplyLifecycleUpdate(const nlohmann::json& payload)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				NativeSpeechSessionSnapshot next = NormalizeSession(payload);
				const auto& previous = m_snapshot.session;

				if (IsPreviewUpdateBlockedByFinalAuthority(next, previous))
				{
					return m_snapshot;
				}
				if (IsEmptyTerminalPreviewUpdate(next, previous))
				{
					return m_snapshot;
				}
				if (IsStaleSpeechPreviewUpdate(next, previous))
				{
					return m_snapshot;
				}

				if (IsPreviewTerminalUpdateWhileRecording(next, previous))
				{
					next.stage = "streaming";
					next.segmentFinal = false;
				}

				const bool clearsPreviousTranscript =
					next.stage == "failed" && next.errorCode == "missing_final_transcript";

				if (next.segmentText.empty() && next.stage == "segment_finalized")
				{
					next.segmentText = next.text;
				}

				if (!clearsPreviousTranscript &&
					next.segmentText.empty() &&
					(next.stage == "queued" ||
						next.stage == "stopped" ||
						next.stage == "transcribing" ||
						next.stage == "failed"))
				{
					next.segmentText = previous.segmentText;
				}

				if (!clearsPreviousTranscript &&
					next.text.empty() &&
					(next.stage == "streaming" ||
						next.stage == "queued" ||
						next.stage == "stopped" ||
						next.stage == "transcribing" ||
						next.stage == "failed"))
				{
					next.text = previous.text;
				}

				if (InferNoSpeechSignal(next))
				{
					next.errorCode = "no_speech_detected";
					if (next.errorMessage.empty() ||
						ContainsInsensitive(next.errorMessage, "inference_failed"))
					{
						next.errorMessage = "speech transcribe no_speech_detected: check selected recording device and retry";
					}
					next.errorClass = "status";
					next.retryable = true;
					if (next.retryStrategy.empty())
					{
						next.retryStrategy = "immediate";
					}
					if (next.retryGuidance.empty())
					{
						next.retryGuidance =
							"No speech detected. Check microphone level/input channel and retry.";
					}
					next.stage = "failed";
				}

				next.generation = previous.generation + 1;
				next.updatedAtMs = CurrentSteadyClockMs();
				m_snapshot.session = next;
				return m_snapshot;
			}

			NativeSpeechStateSnapshot Snapshot() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_snapshot;
			}

			void Reset()
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot = NativeSpeechStateSnapshot{};
			}

		private:
			static bool ContainsInsensitive(const std::string& source, const std::string& needle)
			{
				if (needle.empty())
				{
					return false;
				}
				std::string lhs = source;
				std::string rhs = needle;
				std::transform(lhs.begin(), lhs.end(), lhs.begin(),
					[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::transform(rhs.begin(), rhs.end(), rhs.begin(),
					[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return lhs.find(rhs) != std::string::npos;
			}

			static std::string NormalizeSpeechErrorCode(const std::string& rawCode)
			{
				std::string code = TrimCopy(rawCode);
				std::transform(code.begin(), code.end(), code.begin(),
					[](const unsigned char c) {
						if (std::isspace(c) != 0)
						{
							return static_cast<char>('_');
						}
						return static_cast<char>(std::tolower(c));
					});
				return code;
			}

			static bool IsSpeechPreviewRunId(const std::string& runId)
			{
				return TrimCopy(runId).rfind("speech-preview-", 0) == 0;
			}

			static bool IsSpeechFinalRunId(const std::string& runId)
			{
				return TrimCopy(runId).rfind("speech-final-", 0) == 0;
			}

			static bool IsActiveSpeechPreviewState(const NativeSpeechSessionSnapshot& session)
			{
				if (session.stage == "recording" ||
					session.stage == "start_stream" ||
					session.stage == "streaming")
				{
					return true;
				}
				return session.stage == "queued" && IsSpeechPreviewRunId(session.runId);
			}

			static bool IsSpeechPreviewUpdate(const NativeSpeechSessionSnapshot& session)
			{
				return IsSpeechPreviewRunId(session.runId);
			}

			static bool IsPreviewUpdateBlockedByFinalAuthority(
				const NativeSpeechSessionSnapshot& next,
				const NativeSpeechSessionSnapshot& previous)
			{
				if (!IsSpeechPreviewUpdate(next))
				{
					return false;
				}
				if (IsSpeechFinalRunId(previous.finalRunId) || IsSpeechFinalRunId(previous.runId))
				{
					return true;
				}
				return !IsActiveSpeechPreviewState(previous);
			}

			static bool IsStaleSpeechPreviewUpdate(
				const NativeSpeechSessionSnapshot& next,
				const NativeSpeechSessionSnapshot& previous)
			{
				if (next.stage != "streaming" || next.segmentFinal)
				{
					return false;
				}
				if (!previous.stage.empty() && !IsActiveSpeechPreviewState(previous))
				{
					return true;
				}
				if (IsSpeechPreviewRunId(next.runId) &&
					IsSpeechPreviewRunId(previous.runId) &&
					next.runId != previous.runId)
				{
					return true;
				}
				if (!next.sessionId.empty() &&
					!previous.sessionId.empty() &&
					next.sessionId != previous.sessionId)
				{
					return true;
				}
				if (next.segmentSequence > 0 &&
					previous.segmentSequence > 0 &&
					next.segmentSequence < previous.segmentSequence)
				{
					return true;
				}
				const std::string nextText = !next.segmentText.empty()
					? next.segmentText
					: next.text;
				const std::string previousText = !previous.segmentText.empty()
					? previous.segmentText
					: previous.text;
				return next.segmentSequence > 0 &&
					previous.segmentSequence > 0 &&
					next.segmentSequence == previous.segmentSequence &&
					nextText.size() < previousText.size();
			}

			static bool IsEmptyTerminalPreviewUpdate(
				const NativeSpeechSessionSnapshot& next,
				const NativeSpeechSessionSnapshot& previous)
			{
				if (next.stage != "completed" && next.stage != "segment_finalized")
				{
					return false;
				}
				if (!IsSpeechPreviewRunId(next.runId))
				{
					return false;
				}
				if (!IsActiveSpeechPreviewState(previous))
				{
					return false;
				}
				return next.segmentText.empty() && next.text.empty() && next.errorCode.empty();
			}

			static bool IsPreviewTerminalUpdateWhileRecording(
				const NativeSpeechSessionSnapshot& next,
				const NativeSpeechSessionSnapshot& previous)
			{
				if (next.stage != "completed" && next.stage != "segment_finalized")
				{
					return false;
				}
				if (!IsSpeechPreviewRunId(next.runId))
				{
					return false;
				}
				return IsActiveSpeechPreviewState(previous);
			}

			static bool InferNoSpeechSignal(const NativeSpeechSessionSnapshot& session)
			{
				const std::string normalizedCode = NormalizeSpeechErrorCode(session.errorCode);
				if (normalizedCode == "no_speech_detected")
				{
					return true;
				}

				if (ContainsInsensitive(session.errorMessage, "no_speech_detected"))
				{
					return true;
				}

				if (session.debugInfo.is_object())
				{
					const std::string sherpaFinalOutcome =
						ReadString(session.debugInfo, "sherpaFinalOutcome");
					if (TrimCopy(sherpaFinalOutcome) == "no_speech_detected")
					{
						return true;
					}
				}

				if (session.preflight.is_object())
				{
					const double healthIndex = ReadNumber(session.preflight, "healthIndex");
					if (std::isfinite(healthIndex) && healthIndex > 0 && healthIndex < 55)
					{
						if (normalizedCode == "inference_failed" ||
							ContainsInsensitive(session.errorMessage, "final transcript unavailable"))
						{
							return true;
						}
					}
				}

				return false;
			}

			static std::string ReadString(const nlohmann::json& object, const char* key)
			{
				if (!object.is_object() || key == nullptr ||
					!object.contains(key) || !object[key].is_string())
				{
					return "";
				}
				return TrimCopy(object[key].get<std::string>());
			}

			static bool ReadBool(const nlohmann::json& object, const char* key, bool fallback = false)
			{
				if (!object.is_object() || key == nullptr || !object.contains(key))
				{
					return fallback;
				}
				return object[key].is_boolean() ? object[key].get<bool>() : fallback;
			}

			static int64_t ReadInteger(const nlohmann::json& object, const char* key, int64_t fallback = 0)
			{
				if (!object.is_object() || key == nullptr || !object.contains(key))
				{
					return fallback;
				}
				if (object[key].is_number_integer())
				{
					return object[key].get<int64_t>();
				}
				if (object[key].is_number_unsigned())
				{
					return static_cast<int64_t>(object[key].get<uint64_t>());
				}
				return fallback;
			}

			static double ReadNumber(const nlohmann::json& object, const char* key, double fallback = 0.0)
			{
				if (!object.is_object() || key == nullptr || !object.contains(key) ||
					!object[key].is_number())
				{
					return fallback;
				}
				return object[key].get<double>();
			}

			static nlohmann::json ReadObjectCopy(const nlohmann::json& object, const char* key)
			{
				if (!object.is_object() || key == nullptr || !object.contains(key) ||
					!object[key].is_object())
				{
					return nullptr;
				}
				return object[key];
			}

			static NativeSpeechCapabilitiesSnapshot NormalizeCapabilities(const nlohmann::json& payload)
			{
				NativeSpeechCapabilitiesSnapshot snapshot;
				const nlohmann::json source = payload.is_object() ? payload : nlohmann::json::object();
				const nlohmann::json stt = ReadObjectCopy(source, "stt").is_object()
					? ReadObjectCopy(source, "stt")
					: nlohmann::json::object();
				const nlohmann::json transcript = ReadObjectCopy(source, "transcript").is_object()
					? ReadObjectCopy(source, "transcript")
					: nlohmann::json::object();
				const nlohmann::json tts = ReadObjectCopy(source, "tts").is_object()
					? ReadObjectCopy(source, "tts")
					: nlohmann::json::object();

				snapshot.sttSupported = ReadBool(stt, "supported");
				snapshot.sttReady = ReadBool(stt, "ready");
				snapshot.audioHandoffMode = ReadString(stt, "audioHandoffMode");
				if (snapshot.audioHandoffMode.empty())
				{
					snapshot.audioHandoffMode = "dual";
				}
				snapshot.streamingSupported = ReadBool(stt, "streamingSupported");
				snapshot.streamingPreviewEnabled = ReadBool(stt, "streamingPreviewEnabled");
				snapshot.livePreviewToggleEnabled = ReadBool(stt, "livePreviewToggleEnabled");
				snapshot.livePreviewToggleSource = ReadString(stt, "livePreviewToggleSource");
				snapshot.streamingConfigured = ReadBool(stt, "streamingConfigured");
				snapshot.modelNativeVad = ReadBool(stt, "modelNativeVad");
				snapshot.streamingChunkMs = ReadInteger(stt, "streamingChunkMs", 0);
				snapshot.streamingLookbackMs = ReadInteger(stt, "streamingLookbackMs", 0);
				snapshot.provider = ReadString(stt, "provider");
				snapshot.effectiveExecutionProvider = ReadString(stt, "effectiveExecutionProvider");
				snapshot.cudaExecutionProviderAvailable = ReadBool(stt, "cudaExecutionProviderAvailable");
				snapshot.cudaExecutionProviderEnabled = ReadBool(stt, "cudaExecutionProviderEnabled");
				snapshot.cudaExecutionProviderReason = ReadString(stt, "cudaExecutionProviderReason");
				snapshot.transcriptSupportsSegments = ReadBool(transcript, "supportsSegments");
				snapshot.transcriptSupportsInterim = ReadBool(transcript, "supportsInterim");
				snapshot.transcriptSupportsFinal = !transcript.is_object() ||
					!transcript.contains("supportsFinal")
					? true
					: ReadBool(transcript, "supportsFinal", true);
				snapshot.ttsSupported = ReadBool(tts, "supported");
				snapshot.ttsReady = ReadBool(tts, "ready");
				snapshot.loaded = true;

				if (source.contains("lifecycle") && source["lifecycle"].is_array())
				{
					for (const auto& row : source["lifecycle"])
					{
						if (!row.is_string())
						{
							continue;
						}
						const std::string value = TrimCopy(row.get<std::string>());
						if (!value.empty())
						{
							snapshot.lifecycle.push_back(value);
						}
					}
				}

				return snapshot;
			}

			static NativeSpeechErrorPolicySnapshot NormalizeErrorPolicy(const nlohmann::json& payload)
			{
				NativeSpeechErrorPolicySnapshot snapshot;
				const nlohmann::json source = payload.is_object() ? payload : nlohmann::json::object();
				snapshot.loaded = true;
				snapshot.defaultClass = ReadString(source, "defaultClass");
				if (snapshot.defaultClass.empty())
				{
					snapshot.defaultClass = "status";
				}
				snapshot.map = ReadObjectCopy(source, "map").is_object()
					? ReadObjectCopy(source, "map")
					: nlohmann::json::object();
				snapshot.retry = ReadObjectCopy(source, "retry").is_object()
					? ReadObjectCopy(source, "retry")
					: nlohmann::json::object();
				return snapshot;
			}

			static NativeSpeechSessionSnapshot NormalizeSession(const nlohmann::json& payload)
			{
				NativeSpeechSessionSnapshot session;
				const nlohmann::json source = payload.is_object() ? payload : nlohmann::json::object();
				const nlohmann::json speechSession =
					ReadObjectCopy(source, "speechSession").is_object()
					? ReadObjectCopy(source, "speechSession")
					: source;

				const nlohmann::json segment = ReadObjectCopy(speechSession, "segment").is_object()
					? ReadObjectCopy(speechSession, "segment")
					: nlohmann::json::object();

				session.stage = ReadString(speechSession, "stage");
				if (session.stage.empty())
				{
					session.stage = "idle";
				}
				session.text = ReadString(speechSession, "text");
				if (session.text.empty())
				{
					session.text = ReadString(source, "text");
				}
				if (session.text.empty())
				{
					session.text = ReadString(source, "transcript");
				}
				session.segmentText = segment.is_object()
					? ReadString(segment, "text")
					: "";
				session.segmentSequence = segment.is_object()
					? ReadInteger(segment, "sequence", 0)
					: 0;

				session.runId = ReadString(speechSession, "runId");
				if (session.runId.empty())
				{
					session.runId = ReadString(source, "runId");
				}
				session.finalRunId = IsSpeechFinalRunId(session.runId)
					? session.runId
					: "";

				session.sessionId = ReadString(speechSession, "sessionId");
				if (session.sessionId.empty())
				{
					session.sessionId = ReadString(source, "sessionId");
				}
				session.audioPath = ReadString(speechSession, "audioPath");
				if (session.audioPath.empty())
				{
					session.audioPath = ReadString(source, "audioPath");
				}

				session.audioArtifact = ReadObjectCopy(speechSession, "audioArtifact").is_object()
					? ReadObjectCopy(speechSession, "audioArtifact")
					: ReadObjectCopy(source, "audioArtifact");
				session.language = ReadString(speechSession, "language");
				if (session.language.empty())
				{
					session.language = ReadString(source, "language");
				}
				session.latencyMs = ReadInteger(speechSession, "latencyMs", 0);
				if (session.latencyMs <= 0)
				{
					session.latencyMs = ReadInteger(source, "latencyMs", 0);
				}
				session.cancelled = ReadBool(speechSession, "cancelled") || ReadBool(source, "cancelled");

				session.errorCode = ReadString(source, "errorCode");
				session.errorMessage = ReadString(source, "errorMessage");
				session.errorClass = ReadString(source, "errorClass");
				if (session.errorClass.empty())
				{
					session.errorClass = "status";
				}
				session.retryable = ReadBool(source, "retryable");
				session.retryStrategy = ReadString(source, "retryStrategy");
				if (session.retryStrategy.empty())
				{
					session.retryStrategy = "immediate";
				}
				session.retryGuidance = ReadString(source, "retryGuidance");
				session.debugInfo = ReadObjectCopy(source, "debugInfo");
				session.preflight = ReadObjectCopy(source, "preflight");
				session.noSpeechTriage = ReadObjectCopy(source, "noSpeechTriage");

				const bool previewRunActive =
					IsSpeechPreviewRunId(session.runId) && session.stage == "streaming";
				const bool stageImpliesSegmentFinal =
					!previewRunActive &&
					(session.stage == "segment_finalized" ||
						session.stage == "stopped" ||
						session.stage == "completed" ||
						session.stage == "failed" ||
						session.stage == "cancelled");
				session.segmentFinal = segment.is_object()
					? ReadBool(segment, "final", stageImpliesSegmentFinal)
					: stageImpliesSegmentFinal;

				return session;
			}

			mutable std::mutex m_mutex;
			NativeSpeechStateSnapshot m_snapshot;
		};

		NativeSpeechState& SpeechStateInstance()
		{
			static NativeSpeechState instance;
			return instance;
		}

		class NativeChatStreamState final {
		public:
			NativeProcessEventsResult ApplyEvents(
				const NativeProcessEventsParams& params,
				NativeChatSendState& sendState)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				NativeProcessEventsResult result;
				result.uiOps = nlohmann::json::array();

				if (!params.events.is_array())
				{
					result.streamSnapshot = m_snapshot;
					return result;
				}

				const std::string normalizedSession = NormalizeSessionKey(params.sessionKey);
				for (const auto& event : params.events)
				{
					if (!event.is_object())
					{
						continue;
					}

					const std::string eventSession = NormalizeSessionKey(
						event.value("sessionKey", std::string{}));
					if (!normalizedSession.empty() && eventSession != normalizedSession)
					{
						continue;
					}

					const std::string runId = TrimCopy(event.value("runId", std::string{}));
					const std::string state = NormalizeState(event.value("state", std::string{}));
					const std::string text = ParseTextFromMessageField(event);	// now, it may return error messages
					const std::string promptRunId = ResolvePromptRunId(event, runId);
					const std::string responderRunId = ResolveResponderRunId(event, runId);
					const std::string responderId = ReadStringByAlias(
						event,
						{"responderId", "responder"});
					const std::string responderLabel = ReadStringByAlias(
						event,
						{"responderLabel", "label", "responderName"});
					const std::int64_t responderOrder = ReadIntegerByAlias(
						event,
						{"responderOrder", "order"},
						-1);
					const std::string responseMode = NormalizeResponseMode(ReadStringByAlias(
						event,
						{"responseMode"}));

					EnsurePromptGroup(
						promptRunId,
						responseMode,
						responderRunId);

					ResponderStreamEntry* responderEntry = nullptr;
					if (!responderRunId.empty())
					{
						responderEntry = &EnsureResponderStream(
							responderRunId,
							promptRunId,
							responderId,
							responderLabel,
							responderOrder);
					}

					if (state == "delta")
					{
						if (!runId.empty())
						{
							m_snapshot.activeRunId = !responderRunId.empty()
								? responderRunId
								: runId;
							m_snapshot.activeResponderRunId = responderRunId;
							m_snapshot.activePromptRunId = promptRunId;
						}
						if (!text.empty() &&
							!IsSilentReplyText(text) &&
							text.size() >= m_snapshot.streamText.size())
						{
							m_snapshot.streamText = text;
							m_snapshot.hasStreamDraft = true;
							m_snapshot.terminalState = "delta";
							if (responderEntry != nullptr)
							{
								if (text.size() >= responderEntry->streamText.size())
								{
									responderEntry->streamText = text;
								}
								responderEntry->hasStreamDraft = true;
							}
							result.uiOps.push_back({
								{"op", "chat.update_stream"},
								{"target", "messages"},
								{"data", {
									{"runId", m_snapshot.activeRunId},
									{"promptRunId", promptRunId},
									{"responderRunId", responderRunId},
									{"responderId", responderId},
									{"responderLabel", responderLabel},
									{"responderOrder", responderOrder},
									{"text", m_snapshot.streamText},
								}},
							});
						}
						if (responderEntry != nullptr)
						{
							responderEntry->deltaCount += 1;
						}
						m_snapshot.deltaCount += 1;
						result.processedEventCount += 1;
						continue;
					}

					if (IsTerminalState(state))
					{
						const std::string terminalText = !text.empty()
							? text	// this will already prefer errorMessage when message.empty()
							: (responderEntry != nullptr ? responderEntry->streamText : m_snapshot.streamText);
						const std::string effectiveRunId = !responderRunId.empty()
							? responderRunId
							: (!runId.empty() ? runId : m_snapshot.activeRunId);

						if (responderEntry != nullptr)
						{
							responderEntry->terminalState = state;
							responderEntry->completed = true;
							responderEntry->hasStreamDraft = false;
							responderEntry->terminalCount += 1;
						}

						if (!promptRunId.empty())
						{
							auto promptIt = m_promptGroups.find(promptRunId);
							if (promptIt != m_promptGroups.end())
							{
								if (!responderRunId.empty())
								{
									promptIt->second.completedResponderRunIds.insert(responderRunId);
								}
								promptIt->second.completed =
									!promptIt->second.responderRunIds.empty() &&
									promptIt->second.completedResponderRunIds.size() >=
										promptIt->second.responderRunIds.size();
								promptIt->second.terminalState = state;
							}
						}

						if (!terminalText.empty() && !IsSilentReplyText(terminalText))
						{
							result.uiOps.push_back({
								{"op", "chat.finalize_stream"},
								{"target", "messages"},
								{"data", {
									{"runId", effectiveRunId},
									{"promptRunId", promptRunId},
									{"responderRunId", responderRunId},
									{"responderId", responderId},
									{"responderLabel", responderLabel},
									{"responderOrder", responderOrder},
									{"text", terminalText},
									{"terminalState", state},
								}},
							});
						}

						auto sendSnapshot = sendState.NoteEventTransition(
							promptRunId,
							responderRunId,
							state);
						if (responderRunId.empty() && !promptRunId.empty() && state == "aborted")
						{
							sendSnapshot = sendState.NotePromptAbort(promptRunId, state);
						}

						if (sendSnapshot.promptGroupCompleted && !sendSnapshot.promptRunId.empty())
						{
							result.uiOps.push_back({
								{"op", "chat.complete_prompt_group"},
								{"target", "messages"},
								{"data", {
									{"promptRunId", sendSnapshot.promptRunId},
									{"terminalState", sendSnapshot.promptTerminalState},
									{"responseMode", sendSnapshot.responseMode},
								}},
							});
						}

						m_snapshot.activeRunId.clear();
						m_snapshot.activeResponderRunId.clear();
						m_snapshot.activePromptRunId = promptRunId;
						m_snapshot.streamText.clear();
						m_snapshot.hasStreamDraft = false;
						m_snapshot.terminalState = state;
						m_snapshot.promptCompleted = sendSnapshot.promptGroupCompleted;
						m_snapshot.terminalCount += 1;
						result.processedEventCount += 1;
					}
				}

				m_snapshot.responderStreams = BuildResponderStreamsJson();
				m_snapshot.promptGroups = BuildPromptGroupsJson();
				m_snapshot.completedResponderCount = CountCompletedResponders();
				m_snapshot.activeResponderCount = CountTrackedResponders();

				result.streamSnapshot = m_snapshot;
				return result;
			}

			NativeStreamStateSnapshot Snapshot() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_snapshot;
			}

			void Reset()
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot = NativeStreamStateSnapshot{};
				m_responderStreams.clear();
				m_promptGroups.clear();
			}

		private:
			struct ResponderStreamEntry {
				std::string runId;
				std::string promptRunId;
				std::string responderRunId;
				std::string responderId;
				std::string responderLabel;
				std::int64_t responderOrder = -1;
				std::string streamText;
				std::string terminalState;
				bool hasStreamDraft = false;
				bool completed = false;
				std::size_t deltaCount = 0;
				std::size_t terminalCount = 0;
			};

			struct PromptGroupEntry {
				std::string promptRunId;
				std::string responseMode = "single";
				std::string terminalState;
				bool completed = false;
				std::unordered_set<std::string> responderRunIds;
				std::unordered_set<std::string> completedResponderRunIds;
			};

			static std::string NormalizeSessionKey(const std::string& value)
			{
				const std::string key = TrimCopy(value);
				return key.empty() ? std::string("main") : key;
			}

			static std::string NormalizeState(const std::string& value)
			{
				std::string normalized = TrimCopy(value);
				std::transform(
					normalized.begin(),
					normalized.end(),
					normalized.begin(),
					[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return normalized;
			}

			static bool IsSilentReplyText(const std::string& value)
			{
				std::string normalized = NormalizeState(value);
				return normalized == "no_reply";
			}

			static bool IsTerminalState(const std::string& value)
			{
				return value == "final" ||
					value == "completed" ||
					value == "aborted" ||
					value == "error" ||
					value == "needs_approval";
			}

			static std::string NormalizeResponseMode(const std::string& value)
			{
				const std::string normalized = NormalizeState(value);
				return normalized == "multi_active"
					? normalized
					: "single";
			}

			static std::string ReadStringByAlias(
				const nlohmann::json& event,
				std::initializer_list<const char*> keys)
			{
				if (!event.is_object())
				{
					return "";
				}
				for (const char* key : keys)
				{
					if (key == nullptr)
					{
						continue;
					}
					auto it = event.find(key);
					if (it != event.end() && it->is_string())
					{
						const std::string value = TrimCopy(it->get<std::string>());
						if (!value.empty())
						{
							return value;
						}
					}
				}
				return "";
			}

			static std::int64_t ReadIntegerByAlias(
				const nlohmann::json& event,
				std::initializer_list<const char*> keys,
				std::int64_t fallback)
			{
				if (!event.is_object())
				{
					return fallback;
				}
				for (const char* key : keys)
				{
					if (key == nullptr)
					{
						continue;
					}
					auto it = event.find(key);
					if (it == event.end() || !it->is_number_integer())
					{
						continue;
					}
					return it->get<std::int64_t>();
				}
				return fallback;
			}

			static std::string ResolvePromptRunId(
				const nlohmann::json& event,
				const std::string& runId)
			{
				const std::string promptRunId = ReadStringByAlias(
					event,
					{"promptRunId", "parentRunId"});
				if (!promptRunId.empty())
				{
					return promptRunId;
				}

				if (runId.empty())
				{
					return "";
				}
				return runId + ".prompt";
			}

			static std::string ResolveResponderRunId(
				const nlohmann::json& event,
				const std::string& runId)
			{
				const std::string responderRunId = ReadStringByAlias(
					event,
					{"responderRunId"});
				if (!responderRunId.empty())
				{
					return responderRunId;
				}

				return TrimCopy(runId);
			}

			ResponderStreamEntry& EnsureResponderStream(
				const std::string& responderRunId,
				const std::string& promptRunId,
				const std::string& responderId,
				const std::string& responderLabel,
				std::int64_t responderOrder)
			{
				auto [it, inserted] = m_responderStreams.emplace(
					responderRunId,
					ResponderStreamEntry{});
				ResponderStreamEntry& entry = it->second;
				if (inserted)
				{
					entry.runId = responderRunId;
					entry.responderRunId = responderRunId;
				}
				if (!promptRunId.empty())
				{
					entry.promptRunId = promptRunId;
				}
				if (!responderId.empty())
				{
					entry.responderId = responderId;
				}
				if (!responderLabel.empty())
				{
					entry.responderLabel = responderLabel;
				}
				if (responderOrder >= 0)
				{
					entry.responderOrder = responderOrder;
				}
				return entry;
			}

			void EnsurePromptGroup(
				const std::string& promptRunId,
				const std::string& responseMode,
				const std::string& responderRunId)
			{
				if (promptRunId.empty())
				{
					return;
				}

				auto [it, inserted] = m_promptGroups.emplace(promptRunId, PromptGroupEntry{});
				PromptGroupEntry& group = it->second;
				if (inserted)
				{
					group.promptRunId = promptRunId;
				}
				if (!responseMode.empty())
				{
					group.responseMode = responseMode;
				}
				if (!responderRunId.empty())
				{
					group.responderRunIds.insert(responderRunId);
				}
			}

			nlohmann::json BuildResponderStreamsJson() const
			{
				nlohmann::json rows = nlohmann::json::array();
				for (const auto& [runId, entry] : m_responderStreams)
				{
					rows.push_back({
						{"runId", runId},
						{"promptRunId", entry.promptRunId},
						{"responderRunId", entry.responderRunId},
						{"responderId", entry.responderId},
						{"responderLabel", entry.responderLabel},
						{"responderOrder", entry.responderOrder},
						{"streamText", entry.streamText},
						{"terminalState", entry.terminalState},
						{"hasStreamDraft", entry.hasStreamDraft},
						{"completed", entry.completed},
						{"deltaCount", entry.deltaCount},
						{"terminalCount", entry.terminalCount},
					});
				}
				return rows;
			}

			nlohmann::json BuildPromptGroupsJson() const
			{
				nlohmann::json rows = nlohmann::json::array();
				for (const auto& [promptRunId, group] : m_promptGroups)
				{
					rows.push_back({
						{"promptRunId", promptRunId},
						{"responseMode", group.responseMode},
						{"terminalState", group.terminalState},
						{"completed", group.completed},
						{"totalResponders", group.responderRunIds.size()},
						{"completedResponders", group.completedResponderRunIds.size()},
					});
				}
				return rows;
			}

			std::size_t CountTrackedResponders() const
			{
				return m_responderStreams.size();
			}

			std::size_t CountCompletedResponders() const
			{
				std::size_t count = 0;
				for (const auto& [runId, entry] : m_responderStreams)
				{
					if (entry.completed)
					{
						count += 1;
					}
				}
				return count;
			}

			static std::string ParseTextFromMessageObject(const nlohmann::json& message)
			{
				if (!message.is_object())
				{
					return "";
				}

				if (message.contains("text") && message["text"].is_string())
				{
					return TrimCopy(message["text"].get<std::string>());
				}

				if (message.contains("content") && message["content"].is_array())
				{
					std::string joined;
					for (const auto& item : message["content"])
					{
						if (!item.is_object())
						{
							continue;
						}
						const std::string type = NormalizeState(item.value("type", std::string{}));
						if (type != "text" || !item.contains("text") || !item["text"].is_string())
						{
							continue;
						}
						if (!joined.empty())
						{
							joined += "\n";
						}
						joined += item["text"].get<std::string>();
					}
					return TrimCopy(joined);
				}

				return "";
			}

			// existing helper already parses `message`. 
			// Add fallback to return `errorMessage` when message absent.
			static std::string ParseTextFromMessageField(const nlohmann::json& event)
			{
				//if (!event.is_object() || !event.contains("message"))
				if (!event.is_object())
				{
					return "";
				}

				// If there's a message (string or structured), return it
				const auto& message = event["message"];
				if (message.is_string())
				{
					//return TrimCopy(message.get<std::string>());
					const std::string msg = TrimCopy(message.get<std::string>());
					if (!msg.empty()) return msg;
				}
				//return ParseTextFromMessageObject(message);
				// handle object message form (existing ParseTextFromMessageObject)
				const std::string parsed = ParseTextFromMessageObject(message);
				if (!parsed.empty()) return parsed;

				// NEW: fallback to optional errorMessage (server-provided)
				if (event.contains("errorMessage") && event["errorMessage"].is_string()) {
					const std::string err = TrimCopy(event["errorMessage"].get<std::string>());
					if (!err.empty()) return err;
				}

				return "";	// NEW: no text found
			}

			mutable std::mutex m_mutex;
			NativeStreamStateSnapshot m_snapshot;
			std::unordered_map<std::string, ResponderStreamEntry> m_responderStreams;
			std::unordered_map<std::string, PromptGroupEntry> m_promptGroups;
		};

		NativeChatStreamState& StreamStateInstance()
		{
			static NativeChatStreamState instance;
			return instance;
		}

		uint64_t CurrentSteadyClockMs()
		{
			return static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
		}

		struct NativeReconcileWatchdogSnapshot {
			bool active = false;
			std::string runId;
			uint64_t startedAtMs = 0;
			uint64_t lastInboundEventMs = 0;
			uint64_t lastReconcileMs = 0;
			uint64_t lastWarningMs = 0;
			uint64_t staleThresholdMs = 4000;
			uint64_t reconcileCooldownMs = 2000;
			uint64_t warningCooldownMs = 10000;
			uint64_t tickMs = 1000;
		};

		struct NativeReconcileWatchdogTickParams {
			std::string sessionKey;
			std::string runId;
			bool bridgeAvailable = true;
			uint64_t nowMs = 0;
			std::size_t queuedMessages = 0;
		};

		struct NativeReconcileWatchdogTickResult {
			NativeReconcileWatchdogSnapshot snapshot;
			nlohmann::json uiOps = nlohmann::json::array();
			uint64_t staleForMs = 0;
		};

		struct NativeReconcileWatchdogParams {
			std::string sessionKey;
			std::string runId;
			bool bridgeAvailable = true;
			std::size_t queuedMessages = 0;
			uint64_t nowMs = 0;
		};

		class NativeReconcileWatchdogState final {
		public:
			void Start(const std::string& runId, uint64_t nowMs)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				const std::string normalizedRunId = TrimCopy(runId);
				if (normalizedRunId.empty())
				{
					StopUnsafe();
					return;
				}

				m_snapshot.active = true;
				m_snapshot.runId = normalizedRunId;
				m_snapshot.startedAtMs = nowMs;
				m_snapshot.lastInboundEventMs = nowMs;
			}

			void Stop()
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				StopUnsafe();
			}

			void NoteInboundEvent(const std::string& eventState, uint64_t nowMs)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_snapshot.active)
				{
					return;
				}

				m_snapshot.lastInboundEventMs = nowMs;
				const std::string normalized = NormalizeState(eventState);
				if (normalized == "delta" || normalized == "queued" || normalized == "started")
				{
					m_snapshot.lastWarningMs = 0;
				}
			}

			NativeReconcileWatchdogTickResult EvaluateTick(const NativeReconcileWatchdogTickParams& params)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				NativeReconcileWatchdogTickResult result;

				const std::string normalizedRunId = TrimCopy(params.runId);
				if (normalizedRunId.empty() || !params.bridgeAvailable)
				{
					StopUnsafe();
					result.snapshot = m_snapshot;
					return result;
				}

				if (!m_snapshot.active || m_snapshot.runId != normalizedRunId)
				{
					m_snapshot.active = true;
					m_snapshot.runId = normalizedRunId;
					m_snapshot.startedAtMs = params.nowMs;
					m_snapshot.lastInboundEventMs = params.nowMs;
				}

				const uint64_t lastInbound = m_snapshot.lastInboundEventMs > 0
					? m_snapshot.lastInboundEventMs
					: m_snapshot.startedAtMs;
				if (params.nowMs <= lastInbound)
				{
					result.snapshot = m_snapshot;
					return result;
				}

				const uint64_t staleForMs = params.nowMs - lastInbound;
				result.staleForMs = staleForMs;
				if (staleForMs >= m_snapshot.staleThresholdMs)
				{
					if (params.nowMs - m_snapshot.lastReconcileMs >= m_snapshot.reconcileCooldownMs)
					{
						m_snapshot.lastReconcileMs = params.nowMs;
						result.uiOps.push_back({
							{"op", "chat.request_poll"},
							{"target", "chat.events"},
							{"data", {
								{"sessionKey", NormalizeSessionKey(params.sessionKey)},
								{"limit", 50},
								{"reason", "stale_run_watchdog"},
								{"staleForMs", staleForMs},
							}},
						});
					}

					if (params.queuedMessages > 0 &&
						params.nowMs - m_snapshot.lastWarningMs >= m_snapshot.warningCooldownMs)
					{
						m_snapshot.lastWarningMs = params.nowMs;
						result.uiOps.push_back({
							{"op", "chat.set_status"},
							{"target", "chat"},
							{"data", {
								{"message", "waiting for terminal event; reconciling stalled run"},
								{"queuedMessages", params.queuedMessages},
								{"reason", "stale_run_queue_warning"},
							}},
						});
					}
				}

				result.snapshot = m_snapshot;
				return result;
			}

			NativeReconcileWatchdogSnapshot Snapshot() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_snapshot;
			}

			void Reset()
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_snapshot = NativeReconcileWatchdogSnapshot{};
			}

		private:
			static std::string NormalizeState(const std::string& value)
			{
				std::string normalized = TrimCopy(value);
				std::transform(
					normalized.begin(),
					normalized.end(),
					normalized.begin(),
					[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return normalized;
			}

			static std::string NormalizeSessionKey(const std::string& value)
			{
				const std::string normalized = TrimCopy(value);
				return normalized.empty() ? std::string("main") : normalized;
			}

			void StopUnsafe()
			{
				m_snapshot.active = false;
				m_snapshot.runId.clear();
				m_snapshot.startedAtMs = 0;
				m_snapshot.lastInboundEventMs = 0;
				m_snapshot.lastReconcileMs = 0;
				m_snapshot.lastWarningMs = 0;
			}

			mutable std::mutex m_mutex;
			NativeReconcileWatchdogSnapshot m_snapshot;
		};

		NativeReconcileWatchdogState& ReconcileWatchdogInstance()
		{
			static NativeReconcileWatchdogState instance;
			return instance;
		}


		std::string TrimCopy(const std::string& value)
		{
			std::size_t start = 0;
			while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
			{
				++start;
			}

			std::size_t end = value.size();
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
			{
				--end;
			}

			return value.substr(start, end - start);
		}

		std::string NormalizeVersionOrDefault(const std::string& value, const char* fallback)
		{
			const std::string normalized = TrimCopy(value);
			if (!normalized.empty())
			{
				return normalized;
			}

			return std::string(fallback != nullptr ? fallback : "");
		}

		nlohmann::json BuildLifecyclePayload(
			const NativeControllerLifecycleSnapshot& snapshot,
			const NativeChatControllerLifecycle& lifecycle,
			const NativeSendCorrelationSnapshot& sendState,
			const NativeStreamStateSnapshot& streamState,
			const NativeReconcileWatchdogSnapshot& reconcileWatchdog,
			const NativeSessionSettingsSnapshot& sessionSettings,
			const NativeModelSettingsSnapshot& modelSettings,
			const NativeApprovalValidationSnapshot& approvalValidation,
			const nlohmann::json& uiOps,
			const char* operation)
		{
			const auto speechState = SpeechStateInstance().Snapshot();
			nlohmann::json sessionOptions = nlohmann::json::array();
			for (const auto& row : sessionSettings.options)
			{
				sessionOptions.push_back({
					{"id", row.id},
					{"scope", row.scope},
					{"active", row.active},
				});
			}

			nlohmann::json payload = {
				{"statePatch", {
					{"controllerLifecycle", {
						{"initialized", snapshot.initialized},
						{"lifecycleGeneration", snapshot.lifecycleGeneration},
						{"initializedAtMs", lifecycle.GetInitializedAtMs()},
						{"resetAtMs", lifecycle.GetResetAtMs()},
						{"sessionKey", snapshot.sessionKey},
						{"contractName", snapshot.contractName},
						{"contractVersion", snapshot.contractVersion},
						{"schemaName", snapshot.schemaName},
						{"schemaVersion", snapshot.schemaVersion},
					}},
					{"chatSend", {
						{"requestCorrelationId", sendState.requestCorrelationId},
						{"status", sendState.status},
						{"activeRunId", sendState.activeRunId},
						{"promptRunId", sendState.promptRunId},
						{"activePromptRunId", sendState.activePromptRunId},
						{"activeResponderRunId", sendState.activeResponderRunId},
						{"responseMode", sendState.responseMode},
						{"promptTerminalState", sendState.promptTerminalState},
						{"multiActive", sendState.multiActive},
						{"promptGroupCompleted", sendState.promptGroupCompleted},
						{"activeResponderCount", sendState.activeResponderCount},
						{"queueDepth", sendState.queueDepth},
						{"pendingCorrelationCount", sendState.pendingCorrelationCount},
						{"queued", sendState.queued},
					}},
					{"chatStream", {
						{"activeRunId", streamState.activeRunId},
						{"activePromptRunId", streamState.activePromptRunId},
						{"activeResponderRunId", streamState.activeResponderRunId},
						{"streamText", streamState.streamText},
						{"terminalState", streamState.terminalState},
						{"hasStreamDraft", streamState.hasStreamDraft},
						{"promptCompleted", streamState.promptCompleted},
						{"activeResponderCount", streamState.activeResponderCount},
						{"completedResponderCount", streamState.completedResponderCount},
						{"deltaCount", streamState.deltaCount},
						{"terminalCount", streamState.terminalCount},
						{"responderStreams", streamState.responderStreams.is_array()
							? streamState.responderStreams
							: nlohmann::json::array()},
						{"promptGroups", streamState.promptGroups.is_array()
							? streamState.promptGroups
							: nlohmann::json::array()},
					}},
					{"chatReconcile", {
						{"watchdogActive", reconcileWatchdog.active},
						{"runId", reconcileWatchdog.runId},
						{"startedAtMs", reconcileWatchdog.startedAtMs},
						{"lastInboundEventMs", reconcileWatchdog.lastInboundEventMs},
						{"lastReconcileMs", reconcileWatchdog.lastReconcileMs},
						{"lastWarningMs", reconcileWatchdog.lastWarningMs},
						{"staleThresholdMs", reconcileWatchdog.staleThresholdMs},
						{"reconcileCooldownMs", reconcileWatchdog.reconcileCooldownMs},
						{"warningCooldownMs", reconcileWatchdog.warningCooldownMs},
						{"tickMs", reconcileWatchdog.tickMs},
					}},
					{"session", {
						{"sessionKey", sessionSettings.activeSessionKey},
						{"activeSessionKey", sessionSettings.activeSessionKey},
						{"options", sessionOptions},
						{"sessionOptions", sessionOptions},
						{"switchGeneration", sessionSettings.switchGeneration},
						{"switched", sessionSettings.switched},
					}},
					{"models", {
						{"selectedModel", modelSettings.selectedModel},
						{"activeModel", modelSettings.selectedModel},
						{"options", [&modelSettings]() {
							nlohmann::json rows = nlohmann::json::array();
							for (const auto& row : modelSettings.modelOptions)
							{
								rows.push_back({
									{"id", row.id},
									{"label", row.label},
								});
							}
							return rows;
						}()},
						{"modelOptions", [&modelSettings]() {
							nlohmann::json rows = nlohmann::json::array();
							for (const auto& row : modelSettings.modelOptions)
							{
								rows.push_back({
									{"id", row.id},
									{"label", row.label},
								});
							}
							return rows;
						}()},
						{"modelSelectionGeneration", modelSettings.modelSelectionGeneration},
						{"modelSelectionChanged", modelSettings.modelSelectionChanged},
						{"thinkingLevel", modelSettings.thinkingLevel},
						{"thinking", modelSettings.thinkingLevel},
						{"thinkingOptions", modelSettings.thinkingOptions},
						{"thinkingLevelGeneration", modelSettings.thinkingLevelGeneration},
						{"thinkingLevelChanged", modelSettings.thinkingLevelChanged},
					}},
					{"approval", {
						{"approvalToken", approvalValidation.approvalToken},
						{"valid", approvalValidation.valid},
						{"tokenPresent", approvalValidation.tokenPresent},
						{"expiresAtEpochMs", approvalValidation.expiresAtEpochMs},
						{"approve", approvalValidation.approve},
						{"ok", approvalValidation.ok},
						{"status", approvalValidation.status},
						{"message", approvalValidation.message},
						{"output", approvalValidation.output},
						{"errorCode", approvalValidation.errorCode},
						{"remediation", approvalValidation.remediation},
						{"missingDependency", approvalValidation.missingDependency},
						{"installHint", approvalValidation.installHint},
						{"configHint", approvalValidation.configHint},
						{"failureBucket", approvalValidation.failureBucket},
						{"errorMessage", approvalValidation.errorMessage},
						{"readinessKnown", approvalValidation.readinessKnown},
						{"readinessReady", approvalValidation.readinessReady},
						{"readinessCode", approvalValidation.readinessCode},
						{"readinessMessage", approvalValidation.readinessMessage},
						{"readinessRemediation", approvalValidation.readinessRemediation},
						{"readinessMissingDependency", approvalValidation.readinessMissingDependency},
						{"readinessInstallHint", approvalValidation.readinessInstallHint},
						{"readinessConfigHint", approvalValidation.readinessConfigHint},
						{"readinessBucket", approvalValidation.readinessBucket},
						{"source", approvalValidation.source},
						{"validationGeneration", approvalValidation.validationGeneration},
						{"executionGeneration", approvalValidation.executionGeneration},
					}},
					{"speech", {
						{"capabilities", {
							{"sttSupported", speechState.capabilities.sttSupported},
							{"sttReady", speechState.capabilities.sttReady},
							{"audioHandoffMode", speechState.capabilities.audioHandoffMode},
							{"streamingSupported", speechState.capabilities.streamingSupported},
							{"streamingPreviewEnabled", speechState.capabilities.streamingPreviewEnabled},
							{"livePreviewToggleEnabled", speechState.capabilities.livePreviewToggleEnabled},
							{"livePreviewToggleSource", speechState.capabilities.livePreviewToggleSource},
							{"streamingConfigured", speechState.capabilities.streamingConfigured},
							{"modelNativeVad", speechState.capabilities.modelNativeVad},
							{"streamingChunkMs", speechState.capabilities.streamingChunkMs},
							{"streamingLookbackMs", speechState.capabilities.streamingLookbackMs},
							{"provider", speechState.capabilities.provider},
							{"effectiveExecutionProvider", speechState.capabilities.effectiveExecutionProvider},
							{"cudaExecutionProviderAvailable", speechState.capabilities.cudaExecutionProviderAvailable},
							{"cudaExecutionProviderEnabled", speechState.capabilities.cudaExecutionProviderEnabled},
							{"cudaExecutionProviderReason", speechState.capabilities.cudaExecutionProviderReason},
							{"transcriptSupportsSegments", speechState.capabilities.transcriptSupportsSegments},
							{"transcriptSupportsInterim", speechState.capabilities.transcriptSupportsInterim},
							{"transcriptSupportsFinal", speechState.capabilities.transcriptSupportsFinal},
							{"ttsSupported", speechState.capabilities.ttsSupported},
							{"ttsReady", speechState.capabilities.ttsReady},
							{"lifecycle", speechState.capabilities.lifecycle},
							{"loaded", speechState.capabilities.loaded},
							{"error", speechState.capabilities.error},
						}},
						{"errorPolicy", {
							{"loaded", speechState.errorPolicy.loaded},
							{"defaultClass", speechState.errorPolicy.defaultClass},
							{"map", speechState.errorPolicy.map.is_object() ? speechState.errorPolicy.map : nlohmann::json::object()},
							{"retry", speechState.errorPolicy.retry.is_object() ? speechState.errorPolicy.retry : nlohmann::json::object()},
						}},
						{"speechSession", {
							{"stage", speechState.session.stage},
							{"text", speechState.session.text},
							{"segmentText", speechState.session.segmentText},
							{"segmentFinal", speechState.session.segmentFinal},
							{"segmentSequence", speechState.session.segmentSequence},
							{"runId", speechState.session.runId},
							{"finalRunId", speechState.session.finalRunId},
							{"sessionId", speechState.session.sessionId},
							{"audioPath", speechState.session.audioPath},
							{"audioArtifact", speechState.session.audioArtifact.is_object() ? speechState.session.audioArtifact : nullptr},
							{"language", speechState.session.language},
							{"latencyMs", speechState.session.latencyMs},
							{"cancelled", speechState.session.cancelled},
							{"errorCode", speechState.session.errorCode},
							{"errorMessage", speechState.session.errorMessage},
							{"errorClass", speechState.session.errorClass},
							{"retryable", speechState.session.retryable},
							{"retryStrategy", speechState.session.retryStrategy},
							{"retryGuidance", speechState.session.retryGuidance},
							{"debugInfo", speechState.session.debugInfo.is_object() ? speechState.session.debugInfo : nullptr},
							{"preflight", speechState.session.preflight.is_object() ? speechState.session.preflight : nullptr},
							{"noSpeechTriage", speechState.session.noSpeechTriage.is_object() ? speechState.session.noSpeechTriage : nullptr},
							{"updatedAtMs", speechState.session.updatedAtMs},
							{"generation", speechState.session.generation},
						}},
					}},
				}},
				{"uiOps", uiOps.is_array() ? uiOps : nlohmann::json::array()},
				{"diagnostics", {
					{"counters", {
						{"chatSend.queueDepth", sendState.queueDepth},
						{"chatSend.pendingCorrelations", sendState.pendingCorrelationCount},
						{"chatSend.activeResponderCount", sendState.activeResponderCount},
						{"chatStream.deltaCount", streamState.deltaCount},
						{"chatStream.terminalCount", streamState.terminalCount},
						{"chatStream.activeResponderCount", streamState.activeResponderCount},
						{"chatStream.completedResponderCount", streamState.completedResponderCount},
						{"session.optionsCount", sessionSettings.options.size()},
						{"session.switchGeneration", sessionSettings.switchGeneration},
						{"models.optionsCount", modelSettings.modelOptions.size()},
						{"models.modelSelectionGeneration", modelSettings.modelSelectionGeneration},
						{"models.thinkingLevelGeneration", modelSettings.thinkingLevelGeneration},
						{"approval.validationGeneration", approvalValidation.validationGeneration},
						{"approval.executionGeneration", approvalValidation.executionGeneration},
						{"speech.sessionGeneration", speechState.session.generation},
					}},
					{"events", nlohmann::json::array()},
				}},
				{"warnings", nlohmann::json::array()},
			};

			if (operation != nullptr && operation[0] != '\0')
			{
				payload["operation"] = operation;
			}

			return payload;
		}

		NativeReconcileWatchdogParams ParseWatchdogParams(
			const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			NativeReconcileWatchdogParams params;

			const auto parsed = nlohmann::json::parse(
				request.paramsJson.value_or("{}"),
				nullptr,
				false);
			if (parsed.is_discarded() || !parsed.is_object())
			{
				return params;
			}

			if (parsed.contains("sessionKey") && parsed["sessionKey"].is_string())
			{
				params.sessionKey = parsed["sessionKey"].get<std::string>();
			}
			if (parsed.contains("runId") && parsed["runId"].is_string())
			{
				params.runId = parsed["runId"].get<std::string>();
			}
			if (parsed.contains("bridgeAvailable") && parsed["bridgeAvailable"].is_boolean())
			{
				params.bridgeAvailable = parsed["bridgeAvailable"].get<bool>();
			}
			if (parsed.contains("queuedMessages") && parsed["queuedMessages"].is_number_integer())
			{
				params.queuedMessages = static_cast<std::size_t>(parsed["queuedMessages"].get<std::int64_t>());
			}
			if (parsed.contains("nowMs") && parsed["nowMs"].is_number_unsigned())
			{
				params.nowMs = parsed["nowMs"].get<uint64_t>();
			}

			return params;
		}

		NativeLoadSessionOptionsParams ParseLoadSessionOptionsParams(
			const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			NativeLoadSessionOptionsParams params;

			const auto parsed = nlohmann::json::parse(
				request.paramsJson.value_or("{}"),
				nullptr,
				false);
			if (parsed.is_discarded() || !parsed.is_object())
			{
				return params;
			}

			if (parsed.contains("sessionKey") && parsed["sessionKey"].is_string())
			{
				params.sessionKey = parsed["sessionKey"].get<std::string>();
			}

			if (parsed.contains("sessions") && parsed["sessions"].is_array())
			{
				params.sessions = parsed["sessions"];
			}

			return params;
		}

		NativeControllerInitializeParams ParseInitializeParams(
			const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			NativeControllerInitializeParams params;

			const auto parsed = nlohmann::json::parse(
				request.paramsJson.value_or("{}"),
				nullptr,
				false);
			if (parsed.is_discarded() || !parsed.is_object())
			{
				return params;
			}

			auto applyIfString = [&parsed](const char* key, std::string& target)
				{
					if (key == nullptr)
					{
						return;
					}
					const auto it = parsed.find(key);
					if (it == parsed.end() || !it->is_string())
					{
						return;
					}
					target = it->get<std::string>();
				};

			applyIfString("sessionKey", params.sessionKey);
			applyIfString("contractName", params.contractName);
			applyIfString("contractVersion", params.contractVersion);
			applyIfString("schemaName", params.schemaName);
			applyIfString("schemaVersion", params.schemaVersion);

			return params;
		}

		NativeSendParams ParseSendParams(
			const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			NativeSendParams params;

			const auto parsed = nlohmann::json::parse(
				request.paramsJson.value_or("{}"),
				nullptr,
				false);
			if (parsed.is_discarded() || !parsed.is_object())
			{
				return params;
			}

			auto readString = [&parsed](const char* key) -> std::string
				{
					if (key == nullptr)
					{
						return "";
					}
					const auto it = parsed.find(key);
					if (it == parsed.end() || !it->is_string())
					{
						return "";
					}
					return it->get<std::string>();
				};

			params.sessionKey = readString("sessionKey");
			params.message = readString("message");
			params.idempotencyKey = readString("idempotencyKey");
			params.responseMode = readString("responseMode");

			const auto respondersIt = parsed.find("responders");
			if (respondersIt != parsed.end() && respondersIt->is_array())
			{
				for (const auto& row : *respondersIt)
				{
					if (!row.is_string())
					{
						continue;
					}
					const std::string responder = TrimCopy(row.get<std::string>());
					if (!responder.empty())
					{
						params.requestedResponders.push_back(responder);
					}
				}
			}

			const auto detachedIt = parsed.find("detached");
			if (detachedIt != parsed.end() && detachedIt->is_boolean())
			{
				params.detached = detachedIt->get<bool>();
			}

			const auto forceErrorIt = parsed.find("forceError");
			if (forceErrorIt != parsed.end() && forceErrorIt->is_boolean())
			{
				params.forceError = forceErrorIt->get<bool>();
			}

			const auto attachmentsIt = parsed.find("attachments");
			if (attachmentsIt != parsed.end() && attachmentsIt->is_array())
			{
				params.attachmentCount = attachmentsIt->size();
			}

			return params;
		}

		nlohmann::json ParseObjectOrDefault(const nlohmann::json& root, const char* key)
		{
			if (!root.is_object() || key == nullptr)
			{
				return nlohmann::json::object();
			}
			const auto it = root.find(key);
			if (it == root.end() || !it->is_object())
			{
				return nlohmann::json::object();
			}
			return *it;
		}

		NativeSendCorrelationSnapshot HandleRpcCorrelationResult(
			const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			const auto parsed = nlohmann::json::parse(
				request.paramsJson.value_or("{}"),
				nullptr,
				false);
			if (parsed.is_discarded() || !parsed.is_object())
			{
				return SendStateInstance().HandleRpcResult("", false, "", false);
			}

			const std::string correlationId = parsed.value("id", std::string{});
			const bool ok = parsed.value("ok", false);
			const auto payload = ParseObjectOrDefault(parsed, "payload");
			const std::string runId = payload.value("runId", std::string{});
			const std::string state = payload.value("state", std::string{});
			std::string normalizedState = TrimCopy(state);
			std::transform(
				normalizedState.begin(),
				normalizedState.end(),
				normalizedState.begin(),
				[](const unsigned char c) {
					return static_cast<char>(std::tolower(c));
				});
			if (normalizedState == "failed")
			{
				normalizedState = "error";
			}
			const std::string promptRunId = payload.value("promptRunId", std::string{});
			const std::string responderRunId = payload.value("responderRunId", std::string{});
			const std::string responseMode = payload.value("responseMode", std::string{});
			std::vector<std::string> responderRunIds;
			if (payload.contains("responderRunIds") && payload["responderRunIds"].is_array())
			{
				for (const auto& row : payload["responderRunIds"])
				{
					if (!row.is_string())
					{
						continue;
					}
					const std::string normalized = TrimCopy(row.get<std::string>());
					if (!normalized.empty())
					{
						responderRunIds.push_back(normalized);
					}
				}
			}
			const bool terminal = normalizedState == "completed" ||
				normalizedState == "error" ||
				normalizedState == "aborted" ||
				normalizedState == "final" ||
				normalizedState == "needs_approval";

			return SendStateInstance().HandleRpcResult(
				correlationId,
				ok,
				runId,
				terminal,
				normalizedState,
				promptRunId,
				responderRunId,
				responderRunIds,
				responseMode);
		}

		NativeProcessEventsParams ParseProcessEventsParams(
			const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			NativeProcessEventsParams params;

			const auto parsed = nlohmann::json::parse(
				request.paramsJson.value_or("{}"),
				nullptr,
				false);
			if (parsed.is_discarded() || !parsed.is_object())
			{
				return params;
			}

			if (parsed.contains("sessionKey") && parsed["sessionKey"].is_string())
			{
				params.sessionKey = parsed["sessionKey"].get<std::string>();
			}

			if (parsed.contains("events") && parsed["events"].is_array())
			{
				params.events = parsed["events"];
			}

			return params;
		}

		nlohmann::json ParseJsonObjectParams(
			const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			const auto parsed = nlohmann::json::parse(
				request.paramsJson.value_or("{}"),
				nullptr,
				false);
			if (parsed.is_discarded() || !parsed.is_object())
			{
				return nlohmann::json::object();
			}

			return parsed;
		}

	} // namespace

	NativeControllerBuildMarker CreateNativeControllerBuildMarker()
	{
		return NativeControllerBuildMarker{};
	}

	NativeChatControllerLifecycle::NativeChatControllerLifecycle(std::shared_ptr<const ITimeProvider> timeProvider)
		: m_timeProvider(std::move(timeProvider))
	{
		if (!m_timeProvider)
		{
			m_timeProvider = std::make_shared<SteadyTimeProvider>();
		}
	}

	void NativeChatControllerLifecycle::SetTimeProvider(std::shared_ptr<const ITimeProvider> timeProvider)
	{
		// Not synchronized; intended for test setup before concurrent use.
		if (timeProvider)
		{
			m_timeProvider = std::move(timeProvider);
		}
	}

	void NativeChatControllerLifecycle::Initialize(const NativeControllerInitializeParams& params)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_snapshot.initialized = true;
		m_snapshot.lifecycleGeneration += 1;
		m_snapshot.initializedAt = m_timeProvider->Now();
		m_snapshot.sessionKey = NormalizeSessionKey(params.sessionKey);
		m_snapshot.contractName = NormalizeVersionOrDefault(
			params.contractName,
			"blazeclaw.chat.controller.bridge");
		m_snapshot.contractVersion = NormalizeVersionOrDefault(
			params.contractVersion,
			"1.0.0");
		m_snapshot.schemaName = NormalizeVersionOrDefault(
			params.schemaName,
			"chat-controller-bridge-envelope");
		m_snapshot.schemaVersion = NormalizeVersionOrDefault(
			params.schemaVersion,
			"1.0.0");
	}

	NativeControllerLifecycleSnapshot NativeChatControllerLifecycle::GetSnapshot() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot;
	}

	void NativeChatControllerLifecycle::Reset()
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_snapshot.initialized = false;
		m_snapshot.lifecycleGeneration += 1;
		m_snapshot.resetAt = m_timeProvider->Now();
		m_snapshot.initializedAt = std::chrono::steady_clock::time_point{};
		m_snapshot.sessionKey = "main";
		m_snapshot.contractName = "blazeclaw.chat.controller.bridge";
		m_snapshot.contractVersion = "1.0.0";
		m_snapshot.schemaName = "chat-controller-bridge-envelope";
		m_snapshot.schemaVersion = "1.0.0";
	}

	bool NativeChatControllerLifecycle::IsInitialized() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot.initialized;
	}

	std::string NativeChatControllerLifecycle::NormalizeSessionKey(const std::string& value)
	{
		const std::string trimmed = TrimCopy(value);
		if (!trimmed.empty())
		{
			return trimmed;
		}

		return "main";
	}

	uint64_t NativeChatControllerLifecycle::TimePointToMs(std::chrono::steady_clock::time_point tp)
	{
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
	}

	std::chrono::milliseconds NativeChatControllerLifecycle::TimePointToDuration(std::chrono::steady_clock::time_point tp)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
	}

	uint64_t NativeChatControllerLifecycle::GetInitializedAtMs() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return TimePointToMs(m_snapshot.initializedAt);
	}

	uint64_t NativeChatControllerLifecycle::GetResetAtMs() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return TimePointToMs(m_snapshot.resetAt);
	}

	std::chrono::milliseconds NativeChatControllerLifecycle::GetInitializedAtDuration() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return TimePointToDuration(m_snapshot.initializedAt);
	}

	std::chrono::milliseconds NativeChatControllerLifecycle::GetResetAtDuration() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return TimePointToDuration(m_snapshot.resetAt);
	}

	uint64_t NativeChatControllerLifecycle::GetTimeSinceInitializedMs() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto tp = m_snapshot.initializedAt;
		if (tp == std::chrono::steady_clock::time_point{})
		{
			return 0;
		}
		const auto now = m_timeProvider->Now();
		if (now <= tp)
		{
			return 0;
		}
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now - tp).count());
	}

	uint64_t NativeChatControllerLifecycle::GetTimeSinceResetMs() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto tp = m_snapshot.resetAt;
		if (tp == std::chrono::steady_clock::time_point{})
		{
			return 0;
		}
		const auto now = m_timeProvider->Now();
		if (now <= tp)
		{
			return 0;
		}
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now - tp).count());
	}

	bool IsNativeChatControllerBridgeMethod(const std::string& method)
	{
		return method == "chat.controller.initialize" ||
			method == "chat.controller.send" ||
			method == "chat.controller.processEvents" ||
			method == "chat.controller.loadSpeechCapabilities" ||
			method == "chat.controller.loadSpeechErrorPolicy" ||
			method == "chat.controller.transcribeSpeech" ||
			method == "chat.controller.applySpeechLifecycleUpdate" ||
			method == "chat.controller.loadSessionOptions" ||
			method == "chat.controller.switchSession" ||
			method == "chat.controller.loadModelOptions" ||
			method == "chat.controller.applyModelSelection" ||
			method == "chat.controller.applyThinkingLevel" ||
			method == "chat.controller.executeApprovalAction" ||
			method == "chat.controller.parseApprovalToken" ||
			method == "chat.controller.validateApprovalToken" ||
			method == "chat.controller.startReconcileWatchdog" ||
			method == "chat.controller.stopReconcileWatchdog" ||
			method == "chat.controller.noteInboundChatEvent" ||
			method == "chat.controller.reconcileWatchdogTick" ||
			method == "chat.controller.handleRpcResult" ||
			method == "chat.controller.getStateSnapshot" ||
			method == "chat.controller.reset";
	}

	blazeclaw::gateway::protocol::ResponseFrame DispatchNativeChatControllerBridgeRequest(
		const blazeclaw::gateway::protocol::RequestFrame& request)
	{
		auto& lifecycle = LifecycleInstance();
		auto& sendState = SendStateInstance();
		auto& streamState = StreamStateInstance();
		auto& reconcileWatchdog = ReconcileWatchdogInstance();
		auto& sessionSettings = SessionSettingsInstance();
		auto& modelSettings = ModelSettingsInstance();
		auto& approvalValidation = ApprovalValidationInstance();
		auto& speechState = SpeechStateInstance();

		if (request.method == "chat.controller.initialize")
		{
			const NativeControllerInitializeParams params = ParseInitializeParams(request);
			lifecycle.Initialize(params);
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					nlohmann::json::array(),
					"initialize").dump());
		}

		if (request.method == "chat.controller.send")
		{
			const NativeSendParams params = ParseSendParams(request);
			const auto sendSnapshot = sendState.RegisterSend(request, params);
			const auto snapshot = lifecycle.GetSnapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					nlohmann::json::array(),
					"send").dump());
		}

		if (request.method == "chat.controller.processEvents")
		{
			const NativeProcessEventsParams params = ParseProcessEventsParams(request);
			const auto processResult = streamState.ApplyEvents(params, sendState);
			if (processResult.streamSnapshot.activeRunId.empty())
			{
				reconcileWatchdog.Stop();
			}
			else
			{
				reconcileWatchdog.NoteInboundEvent("delta", CurrentSteadyClockMs());
			}
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					processResult.streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					processResult.uiOps,
					"processEvents").dump());
		}

		if (request.method == "chat.controller.loadSpeechCapabilities")
		{
			const auto params = ParseJsonObjectParams(request);
			const nlohmann::json payload = params.contains("payload") && params["payload"].is_object()
				? params["payload"]
				: nlohmann::json::object();
			const auto speechSnapshot = speechState.LoadCapabilities(payload);
			nlohmann::json uiOps = nlohmann::json::array();
			uiOps.push_back({
				{"op", "speech.set_status"},
				{"target", "speech"},
				{"data", {
					{"loaded", speechSnapshot.capabilities.loaded},
					{"sttReady", speechSnapshot.capabilities.sttReady},
					{"streamingPreviewEnabled", speechSnapshot.capabilities.streamingPreviewEnabled},
					{"provider", speechSnapshot.capabilities.provider},
				}},
			});

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"loadSpeechCapabilities").dump());
		}

		if (request.method == "chat.controller.loadSpeechErrorPolicy")
		{
			const auto params = ParseJsonObjectParams(request);
			const nlohmann::json payload = params.contains("payload") && params["payload"].is_object()
				? params["payload"]
				: nlohmann::json::object();
			speechState.LoadErrorPolicy(payload);

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					nlohmann::json::array(),
					"loadSpeechErrorPolicy").dump());
		}

		if (request.method == "chat.controller.applySpeechLifecycleUpdate")
		{
			const auto params = ParseJsonObjectParams(request);
			const nlohmann::json payload = params.contains("payload") && params["payload"].is_object()
				? params["payload"]
				: params;
			const auto speechSnapshot = speechState.ApplyLifecycleUpdate(payload);

			nlohmann::json uiOps = nlohmann::json::array();
			uiOps.push_back({
				{"op", "speech.set_preview"},
				{"target", "speech"},
				{"data", {
					{"stage", speechSnapshot.session.stage},
					{"runId", speechSnapshot.session.runId},
					{"text", speechSnapshot.session.text},
					{"segmentText", speechSnapshot.session.segmentText},
					{"errorCode", speechSnapshot.session.errorCode},
					{"errorMessage", speechSnapshot.session.errorMessage},
				}},
			});

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"applySpeechLifecycleUpdate").dump());
		}

		if (request.method == "chat.controller.transcribeSpeech")
		{
			const auto params = ParseJsonObjectParams(request);
			nlohmann::json payload = params.contains("payload") && params["payload"].is_object()
				? params["payload"]
				: nlohmann::json::object();

			if (!payload.is_object())
			{
				payload = nlohmann::json::object();
			}
			if (!payload.contains("runId") && params.contains("runId") && params["runId"].is_string())
			{
				payload["runId"] = params["runId"];
			}
			if (!payload.contains("sessionId") && params.contains("sessionId") && params["sessionId"].is_string())
			{
				payload["sessionId"] = params["sessionId"];
			}
			if (!payload.contains("stage"))
			{
				payload["stage"] = "completed";
			}
			const auto speechSnapshot = speechState.ApplyLifecycleUpdate(payload);

			nlohmann::json uiOps = nlohmann::json::array();
			uiOps.push_back({
				{"op", "speech.set_status"},
				{"target", "speech"},
				{"data", {
					{"stage", speechSnapshot.session.stage},
					{"runId", speechSnapshot.session.runId},
					{"errorCode", speechSnapshot.session.errorCode},
					{"errorClass", speechSnapshot.session.errorClass},
					{"retryGuidance", speechSnapshot.session.retryGuidance},
				}},
			});

			const bool livePreviewOnly =
				params.contains("livePreviewOnly") && params["livePreviewOnly"].is_boolean()
				? params["livePreviewOnly"].get<bool>()
				: false;
			if (!livePreviewOnly &&
				speechSnapshot.session.stage == "completed" &&
				!speechSnapshot.session.text.empty() &&
				speechSnapshot.session.errorCode.empty())
			{
				uiOps.push_back({
					{"op", "chat.request_send"},
					{"target", "chat"},
					{"data", {
						{"message", speechSnapshot.session.text},
						{"source", "speech"},
						{"runId", speechSnapshot.session.runId},
					}},
				});
			}

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"transcribeSpeech").dump());
		}

		if (request.method == "chat.controller.loadSessionOptions")
		{
			const auto params = ParseLoadSessionOptionsParams(request);
			const auto sessionSnapshot = sessionSettings.LoadOptions(params);
			nlohmann::json uiOps = nlohmann::json::array();
			uiOps.push_back({
				{"op", "session.options_update"},
				{"target", "session"},
				{"data", {
					{"sessionKey", sessionSnapshot.activeSessionKey},
					{"activeSessionKey", sessionSnapshot.activeSessionKey},
					{"switchGeneration", sessionSnapshot.switchGeneration},
				}},
			});

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"loadSessionOptions").dump());
		}

		if (request.method == "chat.controller.switchSession")
		{
			const auto params = ParseLoadSessionOptionsParams(request);
			const auto sessionSnapshot = sessionSettings.SwitchSession(params.sessionKey);
			nlohmann::json uiOps = nlohmann::json::array();
			uiOps.push_back({
				{"op", "session.options_update"},
				{"target", "session"},
				{"data", {
					{"sessionKey", sessionSnapshot.activeSessionKey},
					{"activeSessionKey", sessionSnapshot.activeSessionKey},
					{"switchGeneration", sessionSnapshot.switchGeneration},
				}},
			});
			if (sessionSnapshot.switched)
			{
				uiOps.push_back({
					{"op", "chat.set_status"},
					{"target", "chat"},
					{"data", {
						{"message", std::string("session switched: ") + sessionSnapshot.activeSessionKey},
					}},
				});
			}

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"switchSession").dump());
		}

		if (request.method == "chat.controller.loadModelOptions")
		{
			const auto params = ParseJsonObjectParams(request);
			const auto modelSnapshot = modelSettings.LoadModelOptions(params);
			nlohmann::json uiOps = nlohmann::json::array();
			uiOps.push_back({
				{"op", "model.options_update"},
				{"target", "model"},
				{"data", {
					{"selectedModel", modelSnapshot.selectedModel},
					{"activeModel", modelSnapshot.selectedModel},
					{"thinkingLevel", modelSnapshot.thinkingLevel},
					{"thinking", modelSnapshot.thinkingLevel},
				}},
			});

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"loadModelOptions").dump());
		}

		if (request.method == "chat.controller.applyModelSelection")
		{
			const auto params = ParseJsonObjectParams(request);
			std::string modelId;
			if (params.contains("modelId") && params["modelId"].is_string())
			{
				modelId = params["modelId"].get<std::string>();
			}
			else if (params.contains("model") && params["model"].is_string())
			{
				modelId = params["model"].get<std::string>();
			}
			const auto modelSnapshot = modelSettings.ApplyModelSelection(modelId);
			nlohmann::json uiOps = nlohmann::json::array();
			if (modelSnapshot.modelSelectionChanged)
			{
				uiOps.push_back({
					{"op", "model.selection_update"},
					{"target", "model"},
					{"data", {
						{"selectedModel", modelSnapshot.selectedModel},
						{"activeModel", modelSnapshot.selectedModel},
						{"generation", modelSnapshot.modelSelectionGeneration},
						{"modelSelectionGeneration", modelSnapshot.modelSelectionGeneration},
					}},
				});
			}

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"applyModelSelection").dump());
		}

		if (request.method == "chat.controller.applyThinkingLevel")
		{
			const auto params = ParseJsonObjectParams(request);
			std::string level;
			if (params.contains("level") && params["level"].is_string())
			{
				level = params["level"].get<std::string>();
			}
			else if (params.contains("thinkingLevel") && params["thinkingLevel"].is_string())
			{
				level = params["thinkingLevel"].get<std::string>();
			}
			const auto modelSnapshot = modelSettings.ApplyThinkingLevel(level);
			nlohmann::json uiOps = nlohmann::json::array();
			if (modelSnapshot.thinkingLevelChanged)
			{
				uiOps.push_back({
					{"op", "model.thinking_update"},
					{"target", "model"},
					{"data", {
						{"thinkingLevel", modelSnapshot.thinkingLevel},
						{"thinking", modelSnapshot.thinkingLevel},
						{"generation", modelSnapshot.thinkingLevelGeneration},
						{"thinkingLevelGeneration", modelSnapshot.thinkingLevelGeneration},
					}},
				});
			}

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"applyThinkingLevel").dump());
		}

		if (request.method == "chat.controller.parseApprovalToken")
		{
			const auto params = ParseJsonObjectParams(request);
			const std::string text = params.contains("text") && params["text"].is_string()
				? params["text"].get<std::string>()
				: std::string{};
			const auto approvalSnapshot = approvalValidation.ParseTokenFromText(text);
			nlohmann::json uiOps = nlohmann::json::array();
			uiOps.push_back({
				{"op", "approval.queue_update"},
				{"target", "approval"},
				{"data", {
					{"approvalToken", approvalSnapshot.approvalToken},
					{"valid", approvalSnapshot.valid},
					{"tokenPresent", approvalSnapshot.tokenPresent},
					{"errorCode", approvalSnapshot.errorCode},
					{"source", "parse"},
				}},
			});

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"parseApprovalToken").dump());
		}

		if (request.method == "chat.controller.validateApprovalToken")
		{
			const auto params = ParseJsonObjectParams(request);
			const std::string approvalToken =
				params.contains("approvalToken") && params["approvalToken"].is_string()
				? params["approvalToken"].get<std::string>()
				: std::string{};
			const auto approvalSnapshot = approvalValidation.ValidateToken(approvalToken);
			nlohmann::json uiOps = nlohmann::json::array();
			uiOps.push_back({
				{"op", "approval.queue_update"},
				{"target", "approval"},
				{"data", {
					{"approvalToken", approvalSnapshot.approvalToken},
					{"valid", approvalSnapshot.valid},
					{"tokenPresent", approvalSnapshot.tokenPresent},
					{"errorCode", approvalSnapshot.errorCode},
					{"source", "validate"},
				}},
			});

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"validateApprovalToken").dump());
		}

		if (request.method == "chat.controller.executeApprovalAction")
		{
			const auto params = ParseJsonObjectParams(request);
			NativeApprovalValidationState::NativeApprovalExecutionInput input;
			if (params.contains("approvalToken") && params["approvalToken"].is_string())
			{
				input.approvalToken = params["approvalToken"].get<std::string>();
			}
			input.approve = params.contains("approve") && params["approve"].is_boolean()
				? params["approve"].get<bool>()
				: false;

			if (params.contains("readiness") && params["readiness"].is_object())
			{
				const auto& readiness = params["readiness"];
				input.readinessKnown = true;
				input.readinessReady = readiness.contains("ready") && readiness["ready"].is_boolean()
					? readiness["ready"].get<bool>()
					: false;
				if (readiness.contains("errorCode") && readiness["errorCode"].is_string())
				{
					input.readinessCode = readiness["errorCode"].get<std::string>();
				}
				if (readiness.contains("message") && readiness["message"].is_string())
				{
					input.readinessMessage = readiness["message"].get<std::string>();
				}
				if (readiness.contains("remediation") && readiness["remediation"].is_string())
				{
					input.readinessRemediation = readiness["remediation"].get<std::string>();
				}
				if (readiness.contains("missingDependency") && readiness["missingDependency"].is_string())
				{
					input.readinessMissingDependency = readiness["missingDependency"].get<std::string>();
				}
				if (readiness.contains("installHint") && readiness["installHint"].is_string())
				{
					input.readinessInstallHint = readiness["installHint"].get<std::string>();
				}
				if (readiness.contains("configHint") && readiness["configHint"].is_string())
				{
					input.readinessConfigHint = readiness["configHint"].get<std::string>();
				}
				if (readiness.contains("bucket") && readiness["bucket"].is_string())
				{
					input.readinessBucket = readiness["bucket"].get<std::string>();
				}
			}

			if (params.contains("executePayload") && params["executePayload"].is_object())
			{
				input.executePayload = params["executePayload"];
			}
			else if (params.contains("payload") && params["payload"].is_object())
			{
				input.executePayload = params["payload"];
			}

			const auto approvalSnapshot = approvalValidation.ApplyExecutionResult(input);
			nlohmann::json uiOps = nlohmann::json::array();
			uiOps.push_back({
				{"op", "approval.status_update"},
				{"target", "approval"},
				{"data", {
					{"approvalToken", approvalSnapshot.approvalToken},
					{"approve", approvalSnapshot.approve},
					{"ok", approvalSnapshot.ok},
					{"status", approvalSnapshot.status},
					{"message", approvalSnapshot.message},
					{"errorCode", approvalSnapshot.errorCode},
					{"errorMessage", approvalSnapshot.errorMessage},
					{"remediation", approvalSnapshot.remediation},
					{"missingDependency", approvalSnapshot.missingDependency},
					{"installHint", approvalSnapshot.installHint},
					{"configHint", approvalSnapshot.configHint},
					{"failureBucket", approvalSnapshot.failureBucket},
					{"readinessKnown", approvalSnapshot.readinessKnown},
					{"readinessReady", approvalSnapshot.readinessReady},
					{"readinessCode", approvalSnapshot.readinessCode},
					{"readinessMessage", approvalSnapshot.readinessMessage},
					{"readinessRemediation", approvalSnapshot.readinessRemediation},
					{"readinessMissingDependency", approvalSnapshot.readinessMissingDependency},
					{"readinessInstallHint", approvalSnapshot.readinessInstallHint},
					{"readinessConfigHint", approvalSnapshot.readinessConfigHint},
					{"readinessBucket", approvalSnapshot.readinessBucket},
				}},
			});

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					uiOps,
					"executeApprovalAction").dump());
		}

		if (request.method == "chat.controller.startReconcileWatchdog")
		{
			const auto params = ParseWatchdogParams(request);
			const uint64_t nowMs = params.nowMs > 0 ? params.nowMs : CurrentSteadyClockMs();
			reconcileWatchdog.Start(params.runId, nowMs);
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					nlohmann::json::array(),
					"startReconcileWatchdog").dump());
		}

		if (request.method == "chat.controller.stopReconcileWatchdog")
		{
			reconcileWatchdog.Stop();
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					nlohmann::json::array(),
					"stopReconcileWatchdog").dump());
		}

		if (request.method == "chat.controller.noteInboundChatEvent")
		{
			const auto params = ParseWatchdogParams(request);
			const uint64_t nowMs = params.nowMs > 0 ? params.nowMs : CurrentSteadyClockMs();
			reconcileWatchdog.NoteInboundEvent("delta", nowMs);
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					nlohmann::json::array(),
					"noteInboundChatEvent").dump());
		}

		if (request.method == "chat.controller.reconcileWatchdogTick")
		{
			const auto params = ParseWatchdogParams(request);
			const uint64_t nowMs = params.nowMs > 0 ? params.nowMs : CurrentSteadyClockMs();
			const auto tickResult = reconcileWatchdog.EvaluateTick(NativeReconcileWatchdogTickParams{
				.sessionKey = params.sessionKey,
				.runId = params.runId,
				.bridgeAvailable = params.bridgeAvailable,
				.nowMs = nowMs,
				.queuedMessages = params.queuedMessages,
			});

			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					tickResult.snapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					tickResult.uiOps,
					"reconcileWatchdogTick").dump());
		}

		if (request.method == "chat.controller.handleRpcResult")
		{
			const auto sendSnapshot = HandleRpcCorrelationResult(request);
			const auto snapshot = lifecycle.GetSnapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					nlohmann::json::array(),
					"handleRpcResult").dump());
		}

		if (request.method == "chat.controller.getStateSnapshot")
		{
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					nlohmann::json::array(),
					"snapshot").dump());
		}

		if (request.method == "chat.controller.reset")
		{
			lifecycle.Reset();
			sendState.Reset();
			streamState.Reset();
			reconcileWatchdog.Reset();
			sessionSettings.Reset();
			modelSettings.Reset();
			approvalValidation.Reset();
			speechState.Reset();
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			const auto sessionSnapshot = sessionSettings.Snapshot();
			const auto modelSnapshot = modelSettings.Snapshot();
			const auto approvalSnapshot = approvalValidation.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					sessionSnapshot,
					modelSnapshot,
					approvalSnapshot,
					nlohmann::json::array(),
					"reset").dump());
		}

		return blazeclaw::gateway::protocol::ErrorResponse(
			request,
			"method_not_supported",
			"native chat-controller bridge method is not supported");
	}

} // namespace blazeclaw::app::chatcontroller
