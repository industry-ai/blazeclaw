#include "pch.h"
#include "chat-controller.h"
#include "../gateway/GatewayProtocolModels.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace blazeclaw::app::chatcontroller {

	namespace {

		std::string TrimCopy(const std::string& value);

		NativeChatControllerLifecycle& LifecycleInstance()
		{
			static NativeChatControllerLifecycle instance;
			return instance;
		}

		struct NativeSendParams {
			std::string sessionKey;
			std::string message;
			std::string idempotencyKey;
			bool detached = false;
			bool forceError = false;
			std::size_t attachmentCount = 0;
		};

		struct NativeSendCorrelationSnapshot {
			std::string requestCorrelationId;
			std::string status;
			std::string activeRunId;
			std::size_t queueDepth = 0;
			std::size_t pendingCorrelationCount = 0;
			bool queued = false;
		};

		struct NativeStreamStateSnapshot {
			std::string activeRunId;
			std::string streamText;
			std::string terminalState;
			bool hasStreamDraft = false;
			std::size_t deltaCount = 0;
			std::size_t terminalCount = 0;
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

		class NativeChatSendState final {
		public:
			NativeSendCorrelationSnapshot RegisterSend(
				const blazeclaw::gateway::protocol::RequestFrame& request,
				const NativeSendParams& params)
			{
				std::lock_guard<std::mutex> lock(m_mutex);

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
				}

				m_pendingCorrelations[correlationId] = CorrelationEntry{
					.method = "chat.send",
					.sessionKey = [&params]() {
						const std::string key = TrimCopy(params.sessionKey);
						return key.empty() ? std::string("main") : key;
					}(),
					.status = hasActiveRun ? "queued" : "dispatched",
				};

				return BuildSnapshot(correlationId, hasActiveRun);
			}

			NativeSendCorrelationSnapshot HandleRpcResult(
				const std::string& correlationId,
				bool ok,
				const std::string& runId,
				bool terminal)
			{
				std::lock_guard<std::mutex> lock(m_mutex);

				const std::string normalizedCorrelationId = TrimCopy(correlationId);
				auto it = m_pendingCorrelations.find(normalizedCorrelationId);
				if (it != m_pendingCorrelations.end())
				{
					it->second.status = ok ? "acknowledged" : "failed";
					m_pendingCorrelations.erase(it);
				}

				const std::string normalizedRunId = TrimCopy(runId);
				if (!normalizedRunId.empty())
				{
					m_activeRunId = normalizedRunId;
				}

				if (terminal)
				{
					m_activeRunId.clear();
					if (!m_sendQueue.empty())
					{
						m_sendQueue.pop_front();
					}
				}

				return BuildSnapshot(normalizedCorrelationId, false);
			}

			NativeSendCorrelationSnapshot Snapshot() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return NativeSendCorrelationSnapshot{
					.requestCorrelationId = "",
					.status = "snapshot",
					.activeRunId = m_activeRunId,
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
			}

		private:
			struct CorrelationEntry {
				std::string method;
				std::string sessionKey;
				std::string status;
			};

			NativeSendCorrelationSnapshot BuildSnapshot(
				const std::string& correlationId,
				bool queued) const
			{
				return NativeSendCorrelationSnapshot{
					.requestCorrelationId = correlationId,
					.status = queued ? "queued" : "dispatched",
					.activeRunId = m_activeRunId,
					.queueDepth = m_sendQueue.size(),
					.pendingCorrelationCount = m_pendingCorrelations.size(),
					.queued = queued,
				};
			}

			mutable std::mutex m_mutex;
			std::deque<std::string> m_sendQueue;
			std::unordered_map<std::string, CorrelationEntry> m_pendingCorrelations;
			std::string m_activeRunId;
		};

		NativeChatSendState& SendStateInstance()
		{
			static NativeChatSendState instance;
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
					const std::string text = ParseTextFromMessageField(event);

					if (state == "delta")
					{
						if (!runId.empty())
						{
							m_snapshot.activeRunId = runId;
						}
						if (!text.empty() && !IsSilentReplyText(text) && text.size() >= m_snapshot.streamText.size())
						{
							m_snapshot.streamText = text;
							m_snapshot.hasStreamDraft = true;
							m_snapshot.terminalState = "delta";
							result.uiOps.push_back({
								{"op", "chat.update_stream"},
								{"target", "messages"},
								{"data", {
									{"runId", m_snapshot.activeRunId},
									{"text", m_snapshot.streamText},
								}},
							});
						}
						m_snapshot.deltaCount += 1;
						result.processedEventCount += 1;
						continue;
					}

					if (IsTerminalState(state))
					{
						const std::string terminalText = !text.empty()
							? text
							: m_snapshot.streamText;
						const std::string effectiveRunId = !runId.empty()
							? runId
							: m_snapshot.activeRunId;

						if (!terminalText.empty() && !IsSilentReplyText(terminalText))
						{
							result.uiOps.push_back({
								{"op", "chat.finalize_stream"},
								{"target", "messages"},
								{"data", {
									{"runId", effectiveRunId},
									{"text", terminalText},
									{"terminalState", state},
								}},
							});
						}

						sendState.HandleRpcResult("", true, effectiveRunId, true);
						m_snapshot.activeRunId.clear();
						m_snapshot.streamText.clear();
						m_snapshot.hasStreamDraft = false;
						m_snapshot.terminalState = state;
						m_snapshot.terminalCount += 1;
						result.processedEventCount += 1;
					}
				}

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
			}

		private:
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

			static std::string ParseTextFromMessageField(const nlohmann::json& event)
			{
				if (!event.is_object() || !event.contains("message"))
				{
					return "";
				}

				const auto& message = event["message"];
				if (message.is_string())
				{
					return TrimCopy(message.get<std::string>());
				}
				return ParseTextFromMessageObject(message);
			}

			mutable std::mutex m_mutex;
			NativeStreamStateSnapshot m_snapshot;
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
			const nlohmann::json& uiOps,
			const char* operation)
		{
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
						{"queueDepth", sendState.queueDepth},
						{"pendingCorrelationCount", sendState.pendingCorrelationCount},
						{"queued", sendState.queued},
					}},
					{"chatStream", {
						{"activeRunId", streamState.activeRunId},
						{"streamText", streamState.streamText},
						{"terminalState", streamState.terminalState},
						{"hasStreamDraft", streamState.hasStreamDraft},
						{"deltaCount", streamState.deltaCount},
						{"terminalCount", streamState.terminalCount},
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
				}},
				{"uiOps", uiOps.is_array() ? uiOps : nlohmann::json::array()},
				{"diagnostics", {
					{"counters", {
						{"chatSend.queueDepth", sendState.queueDepth},
						{"chatSend.pendingCorrelations", sendState.pendingCorrelationCount},
						{"chatStream.deltaCount", streamState.deltaCount},
						{"chatStream.terminalCount", streamState.terminalCount},
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
			const bool terminal = state == "completed" ||
				state == "failed" ||
				state == "aborted";

			return SendStateInstance().HandleRpcResult(correlationId, ok, runId, terminal);
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

		if (request.method == "chat.controller.initialize")
		{
			const NativeControllerInitializeParams params = ParseInitializeParams(request);
			lifecycle.Initialize(params);
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
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
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
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
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					processResult.streamSnapshot,
					reconcileSnapshot,
					processResult.uiOps,
					"processEvents").dump());
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
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
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
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
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
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
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
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					tickResult.snapshot,
					tickResult.uiOps,
					"reconcileWatchdogTick").dump());
		}

		if (request.method == "chat.controller.handleRpcResult")
		{
			const auto sendSnapshot = HandleRpcCorrelationResult(request);
			const auto snapshot = lifecycle.GetSnapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					nlohmann::json::array(),
					"handleRpcResult").dump());
		}

		if (request.method == "chat.controller.getStateSnapshot")
		{
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					nlohmann::json::array(),
					"snapshot").dump());
		}

		if (request.method == "chat.controller.reset")
		{
			lifecycle.Reset();
			sendState.Reset();
			streamState.Reset();
			reconcileWatchdog.Reset();
			const auto snapshot = lifecycle.GetSnapshot();
			const auto sendSnapshot = sendState.Snapshot();
			const auto streamSnapshot = streamState.Snapshot();
			const auto reconcileSnapshot = reconcileWatchdog.Snapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(
					snapshot,
					lifecycle,
					sendSnapshot,
					streamSnapshot,
					reconcileSnapshot,
					nlohmann::json::array(),
					"reset").dump());
		}

		return blazeclaw::gateway::protocol::ErrorResponse(
			request,
			"method_not_supported",
			"native chat-controller bridge method is not supported");
	}

} // namespace blazeclaw::app::chatcontroller
