#include "pch.h"
#include "GatewayHostCronProductionAdapter.h"
#include "GatewayHost.h"
#include "cron/CronOpsService.h"
#include "Telemetry.h"
#include "GatewayJsonUtils.h"
#include <thread>
#include <chrono>

namespace blazeclaw::gateway {

	namespace cron_production {
		using CronJson = cron::CronJson;

		std::string ReadStringField(const CronJson& value, const char* key) {
			if (!value.contains(key) || !value[key].is_string()) {
				return {};
			}
			return cron::TrimCopy(value[key].get<std::string>());
		}

		std::string ResolveCronChatSessionKey(const CronJson& job) {
			std::string sessionKey = ReadStringField(job, "sessionKey");
			if (!sessionKey.empty()) {
				return sessionKey;
			}

			if (job.contains("payload") && job["payload"].is_object()) {
				sessionKey = ReadStringField(job["payload"], "sessionKey");
				if (!sessionKey.empty()) {
					return sessionKey;
				}
			}

			const std::string sessionTargetRaw =
				cron::TrimCopy(job.value("sessionTarget", std::string("main")));
			const std::string sessionTarget = cron::ToLowerCopy(sessionTargetRaw);
			if (sessionTarget.rfind("session:", 0) == 0 &&
				sessionTargetRaw.size() > std::string("session:").size()) {
				return cron::TrimCopy(sessionTargetRaw.substr(std::string("session:").size()));
			}

			const std::string agentId = ReadStringField(job, "agentId");
			if (!agentId.empty()) {
				return agentId;
			}

			return "main";
		}

		std::string BuildCronAdapterRunId(const CronJson& job, const std::int64_t nowMs) {
			const std::string jobId = job.value("id", std::string());
			if (!jobId.empty()) {
				return "cron-run-" + jobId + "-" + std::to_string(nowMs);
			}
			return cron::BuildCronRunId(nowMs);
		}

		bool IsCronOwnedRunId(const std::string& runId) {
			return runId.rfind("cron-run-", 0) == 0 || runId.rfind("cron-", 0) == 0;
		}

		std::optional<CronJson> BuildCronRuntimeUnavailableResult(
			const std::string& reason,
			const std::int64_t nowMs) {
			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "error", reason },
				{ "errorCategory", "runtime_unavailable" },
				{ "summary", reason },
				{ "retryable", true },
				{ "observedAtMs", nowMs }
			};
		}

		std::string ResolveCronPayloadKind(const CronJson& payload) {
			std::string payloadKind =
				cron::ToLowerCopy(cron::TrimCopy(payload.value("kind", std::string())));
			if (!payloadKind.empty()) {
				return payloadKind;
			}

			if (payload.contains("message") && payload["message"].is_string()) {
				return "agentturn";
			}

			if (payload.contains("text") && payload["text"].is_string()) {
				return "systemevent";
			}

			return std::string();
		}

		std::optional<CronJson> MapChatRuntimeResultToCron(
			const GatewayHost::ChatRuntimeResult& result,
			const std::string& runtimeModule = std::string()) {
			CronJson response = CronJson::object();
			response["handled"] = true;
			response["observedAtMs"] = cron::UtcNowMs();
			if (result.retryAfterMs.has_value() && result.retryAfterMs.value() >= 0) {
				response["retryAfterMs"] = result.retryAfterMs.value();
			}

			if (result.ok) {
				response["status"] = "ok";
				std::string summary = cron::TrimCopy(result.assistantText);
				if (summary.size() > 512) {
					summary.resize(512);
				}
				if (summary.empty()) {
					summary = "Cron runtime completed";
				}
				response["summary"] = summary;
			}
			else {
				const std::string errorMessage = result.errorMessage.empty()
					? "Cron runtime execution failed"
					: result.errorMessage;
				const std::string errorCode = result.errorCode.empty()
					? "runtime_error"
					: result.errorCode;
				response["status"] = "error";
				response["error"] = errorMessage;
				response["errorCategory"] = errorCode;
				response["summary"] = errorMessage;
				response["retryable"] =
					errorCode == "timeout" ||
					errorCode == "network" ||
					errorCode == "rate_limit";
				if (errorCode == "timeout") {
					response["timedOut"] = true;
				}
				if (errorCode == "aborted" || errorCode == "cancelled") {
					response["aborted"] = true;
				}
			}

			if (!result.modelId.empty()) {
				response["model"] = result.modelId;
			}

			const std::string assistantText = cron::TrimCopy(result.assistantText);
			if (!assistantText.empty()) {
				const std::int64_t promptTokens =
					(std::max)(static_cast<std::int64_t>(1),
						static_cast<std::int64_t>(assistantText.size() / 4));
				response["usage"] = CronJson{
					{ "promptTokens", promptTokens },
					{ "completionTokens", promptTokens },
					{ "totalTokens", promptTokens * 2 }
				};
			}

			if (!result.taskDeltas.empty()) {
				response["taskDeltaCount"] = result.taskDeltas.size();
			}

			if (!runtimeModule.empty()) {
				response["runtimeModule"] = runtimeModule;
			}

			return response;
		}

		TaskDeltaEntry BuildCronTaskDeltaEntry(
			const CronJson& payload,
			const std::size_t index,
			const bool terminal) {
			using TaskDeltaEntry = GatewayHost::ChatRuntimeResult::TaskDeltaEntry;
			const std::uint64_t nowMs = static_cast<std::uint64_t>(cron::UtcNowMs());
			TaskDeltaEntry entry;
			entry.index = index;
			entry.runId = ReadStringField(payload, "runId");
			entry.sessionId = ReadStringField(payload, "sessionId");
			if (entry.sessionId.empty()) {
				entry.sessionId = ReadStringField(payload, "sessionKey");
			}
			if (entry.sessionId.empty()) {
				entry.sessionId = "cron";
			}
			entry.phase = ReadStringField(payload, "phase");
			if (entry.phase.empty()) {
				entry.phase = ReadStringField(payload, "taskLedgerPhase");
			}
			if (entry.phase.empty()) {
				entry.phase = terminal ? "terminal" : "active";
			}
			entry.status = ReadStringField(payload, "taskLedgerStatus");
			if (entry.status.empty()) {
				entry.status = ReadStringField(payload, "status");
			}
			if (entry.status.empty()) {
				entry.status = terminal ? "ok" : "running";
			}
			entry.stepLabel = "cron:" + ReadStringField(payload, "jobId");
			if (entry.stepLabel == "cron:") {
				entry.stepLabel = "cron";
			}
			entry.resultJson = ReadStringField(payload, "summary");
			entry.errorCode = ReadStringField(payload, "errorCategory");
			entry.errorMessage = ReadStringField(payload, "error");
			entry.startedAtMs = nowMs;
			entry.completedAtMs = terminal ? nowMs : 0;
			entry.latencyMs = terminal ? 0 : 0;
			return entry;
		}

	} // namespace cron_production

	void GatewayHostCronProductionAdapter::WireIntegration(const AdapterContext& context) {
		if (m_wired) {
			return;
		}

		// Cast opaque context pointers back to typed host state
		const auto& chatRuntimeCallback = 
			*static_cast<const GatewayHost::ChatRuntimeCallback*>(context.chatRuntimeCallbackPtr);
		const auto& chatRunsById = 
			*static_cast<const std::unordered_map<std::string, GatewayHost::ChatRunState>*>(context.chatRunsByIdPtr);

		auto mainSessionAdapter =
			[chatRuntimeCallback, context, &chatRunsById](const cron::CronJson& job, const std::int64_t nowMs)
			-> std::optional<cron::CronJson> {
				// Build modified context with typed state for execution
				AdapterContext execContext = context;
				execContext.chatRuntimeCallbackPtr = const_cast<void*>(static_cast<const void*>(&chatRuntimeCallback));
				execContext.chatRunsByIdPtr = static_cast<const void*>(&chatRunsById);

				GatewayHostCronProductionAdapter tempAdapter;
				return tempAdapter.ExecuteMainSessionRuntime(job, nowMs, execContext);
			};
		auto isolatedSessionAdapter =
			[chatRuntimeCallback, context, &chatRunsById](const cron::CronJson& job, const std::int64_t nowMs)
			-> std::optional<cron::CronJson> {
				AdapterContext execContext = context;
				execContext.chatRuntimeCallbackPtr = const_cast<void*>(static_cast<const void*>(&chatRuntimeCallback));
				execContext.chatRunsByIdPtr = static_cast<const void*>(&chatRunsById);

				GatewayHostCronProductionAdapter tempAdapter;
				return tempAdapter.ExecuteIsolatedSessionRuntime(job, nowMs, execContext);
			};

		cron::CronRuntimeExecutionAdapters adapters;
		adapters.mainSession = std::move(mainSessionAdapter);
		adapters.isolatedSession = std::move(isolatedSessionAdapter);
		adapters.preferRuntimeExecution = true;
		cron::GetCronOpsService().SetRuntimeExecutionAdapters(std::move(adapters));

		cron::CronOpsService::TaskLedgerHooks hooks;
		hooks.createRunningTaskRun =
			[this, context](const cron::CronJson& payload) {
				HandleTaskLedgerCreateRunning(payload, context);
			};
		hooks.completeTaskRunByRunId =
			[this, context](const cron::CronJson& payload) {
				HandleTaskLedgerComplete(payload, context);
			};
		hooks.failTaskRunByRunId =
			[this, context](const cron::CronJson& payload) {
				HandleTaskLedgerFail(payload, context);
			};
		cron::GetCronOpsService().SetTaskLedgerHooks(std::move(hooks));

		cron::CronOpsService::ScheduleNotificationHooks scheduleHooks;
		scheduleHooks.enqueueSystemEvent =
			[this, context](const cron::CronScheduleNotificationEvent& event) {
				DispatchScheduleAutoDisableNotification(event, context);
			};
		scheduleHooks.requestHeartbeatNow =
			[](const cron::CronScheduleNotificationEvent& event) {
				cron::CronJson wakeParams = {
					{ "mode", cron::kWakeModeNextHeartbeat }
				};
				if (!event.heartbeatWakeReason.empty()) {
					wakeParams["text"] = event.heartbeatWakeReason;
				}
				cron::GetCronOpsService().EnqueueDeferredWakeRequest(wakeParams);
			};
		cron::GetCronOpsService().SetScheduleNotificationHooks(std::move(scheduleHooks));

		cron::CronSchedulerConfig schedulerConfig;
		schedulerConfig.maxConcurrentRuns = 1;
		schedulerConfig.missedJobStaggerMs = cron::kCronDefaultMissedJobStaggerMs;
		schedulerConfig.maxMissedJobsPerRestart = cron::kCronDefaultMaxMissedJobsPerRestart;
		cron::GetCronOpsService().SetSchedulerConfig(schedulerConfig);

		cron::CronOpsService::CronRealtimeEventHooks realtimeHooks;
		realtimeHooks.onEvent =
			[this, context](const cron::CronRealtimeEvent& event) {
				BroadcastRealtimeEvent(event, context);
			};
		cron::GetCronOpsService().SetRealtimeEventHooks(std::move(realtimeHooks));

		m_wired = true;
		context.emitTelemetry(
			"gateway.cron.production_integration.wired",
			"{\"runtimeAdapters\":true,\"taskLedgerHooks\":true,\"scheduleNotificationHooks\":true,\"schedulerHardening\":true,\"realtimeEvents\":true}");
	}

	bool GatewayHostCronProductionAdapter::IsChatSessionBusy(
		const std::string& sessionKey,
		const AdapterContext& context) const {
		if (sessionKey.empty() || !context.chatRunsByIdPtr || !context.transportRecipientRegistry) {
			return false;
		}

		const auto& chatRunsById = 
			*static_cast<const std::unordered_map<std::string, GatewayHost::ChatRunState>*>(context.chatRunsByIdPtr);

		for (const auto& [runId, run] : chatRunsById) {
			if (run.sessionKey != sessionKey || !run.active) {
				continue;
			}
			if (cron_production::IsCronOwnedRunId(runId)) {
				continue;
			}
			return true;
		}

		for (const auto& runId :
			context.transportRecipientRegistry->ActiveRunsForSession(sessionKey)) {
			if (cron_production::IsCronOwnedRunId(runId)) {
				continue;
			}
			return true;
		}

		return false;
	}

	std::optional<nlohmann::json> GatewayHostCronProductionAdapter::ExecuteMainSessionRuntime(
		const nlohmann::json& job,
		const std::int64_t nowMs,
		const AdapterContext& context) {
		using CronJson = cron::CronJson;
		using ChatRuntimeRequest = GatewayHost::ChatRuntimeRequest;
		using ChatRuntimeResult = GatewayHost::ChatRuntimeResult;

		const CronJson& cronJob = job;
		if (!cronJob.contains("payload") || !cronJob["payload"].is_object()) {
			return std::nullopt;
		}

		const CronJson& payload = cronJob["payload"];
		const std::string payloadKind = cron_production::ResolveCronPayloadKind(payload);
		if (payloadKind != "systemevent") {
			return std::nullopt;
		}

		const std::string text = cron::TrimCopy(payload.value("text", std::string()));
		if (text.empty()) {
			return std::nullopt;
		}

		const std::string sessionKey = cron_production::ResolveCronChatSessionKey(cronJob);
		const std::string wakeMode =
			cron::NormalizeWakeMode(cronJob.value("wakeMode", std::string(cron::kWakeModeNow)));
		const bool wakeNow = wakeMode == cron::kWakeModeNow;
		const std::string scheduleKind = cronJob.contains("schedule") &&
			cronJob["schedule"].is_object()
			? cron::ToLowerCopy(
				cron::TrimCopy(cronJob["schedule"].value("kind", std::string())))
			: std::string();
		const bool isRecurringJob = scheduleKind != "at";

		if (wakeNow) {
			const std::int64_t maxWaitMs = (std::max)(
				static_cast<std::int64_t>(1),
				cron::TryReadInt64Field(payload, "wakeNowHeartbeatBusyMaxWaitMs")
					.value_or(cron::kCronWakeNowBusyMaxWaitMs));
			const std::int64_t retryDelayMs = (std::max)(
				static_cast<std::int64_t>(1),
				cron::TryReadInt64Field(payload, "wakeNowHeartbeatBusyRetryDelayMs")
					.value_or(cron::kCronWakeNowBusyRetryDelayMs));
			const std::int64_t waitStartedAtMs = nowMs;

			for (;;) {
				bool sessionBusy = false;
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					sessionBusy = IsChatSessionBusy(sessionKey, context);
				}
				if (!sessionBusy) {
					break;
				}

				if (isRecurringJob) {
					return CronJson{
						{ "handled", true },
						{ "busy", true },
						{ "status", "ok" },
						{ "summary", text },
						{ "errorCategory", "heartbeat_busy_fallback" },
						{ "sessionKey", sessionKey },
						{ "sessionId", "main" },
						{ "heartbeatFallbackWakeRequested", true },
						{ "heartbeatFallbackWakeRequestedAtMs", nowMs },
						{ "observedAtMs", nowMs }
					};
				}

				if (cron::UtcNowMs() - waitStartedAtMs > maxWaitMs) {
					return CronJson{
						{ "handled", true },
						{ "busy", true },
						{ "status", "error" },
						{ "error", "main heartbeat busy fallback wake requested" },
						{ "errorCategory", "heartbeat_busy_fallback" },
						{ "summary",
							"Main heartbeat busy after wake-now wait; fallback wake requested" },
						{ "sessionKey", sessionKey },
						{ "sessionId", "main" },
						{ "heartbeatFallbackWakeRequested", true },
						{ "heartbeatFallbackWakeRequestedAtMs", nowMs },
						{ "observedAtMs", nowMs }
					};
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
			}
		}
		else {
			std::lock_guard<std::mutex> lock(m_mutex);
			if (IsChatSessionBusy(sessionKey, context)) {
				return CronJson{
					{ "handled", true },
					{ "busy", true },
					{ "status", "ok" },
					{ "summary", text },
					{ "errorCategory", "heartbeat_busy_fallback" },
					{ "sessionKey", sessionKey },
					{ "sessionId", "main" },
					{ "observedAtMs", nowMs }
				};
			}
		}

		if (!context.chatRuntimeCallbackPtr) {
			return cron_production::BuildCronRuntimeUnavailableResult(
				"chat runtime callback is not configured",
				nowMs);
		}

		const auto& chatRuntimeCallback = 
			*static_cast<const GatewayHost::ChatRuntimeCallback*>(context.chatRuntimeCallbackPtr);

		const std::string runId = cron_production::BuildCronAdapterRunId(cronJob, nowMs);
		ChatRuntimeRequest request;
		request.runId = runId;
		request.sessionKey = sessionKey;
		request.message = "[cron] " + text;
		request.bodyForCommands = text;
		request.bodyForAgent = request.message;
		if (payload.contains("model") && payload["model"].is_string()) {
			request.modelIdOverride = cron::TrimCopy(payload["model"].get<std::string>());
		}
		if (payload.contains("provider") && payload["provider"].is_string()) {
			request.providerOverride = cron::TrimCopy(payload["provider"].get<std::string>());
		}
		if (cronJob.contains("model") && cronJob["model"].is_string() &&
			request.modelIdOverride.empty()) {
			request.modelIdOverride = cron::TrimCopy(cronJob["model"].get<std::string>());
		}
		if (cronJob.contains("provider") && cronJob["provider"].is_string() &&
			request.providerOverride.empty()) {
			request.providerOverride = cron::TrimCopy(cronJob["provider"].get<std::string>());
		}
		const auto timeoutSeconds = cron::TryReadInt64Field(payload, "timeoutSeconds");
		if (timeoutSeconds.has_value() && timeoutSeconds.value() > 0) {
			request.timeoutSeconds = timeoutSeconds.value();
		}
		request.shouldLoadInlineSkillCommands = false;
		request.allowInlineToolImmediateExecution = false;

		ChatRuntimeResult runtimeResult;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			runtimeResult = chatRuntimeCallback(request);
		}

		auto mapped = cron_production::MapChatRuntimeResultToCron(
			runtimeResult,
			"main-session");
		if (!mapped.has_value()) {
			return std::nullopt;
		}

		mapped.value()["sessionKey"] = sessionKey;
		mapped.value()["sessionId"] = "main";
		mapped.value()["runtimeModule"] = "main-session";
		if (!request.modelIdOverride.empty()) {
			mapped.value()["model"] = request.modelIdOverride;
		}
		if (!request.providerOverride.empty()) {
			mapped.value()["provider"] = request.providerOverride;
		}
		return mapped;
	}

	std::optional<nlohmann::json> GatewayHostCronProductionAdapter::ExecuteIsolatedSessionRuntime(
		const nlohmann::json& job,
		const std::int64_t nowMs,
		const AdapterContext& context) {
		using CronJson = cron::CronJson;
		using ChatRuntimeRequest = GatewayHost::ChatRuntimeRequest;
		using ChatRuntimeResult = GatewayHost::ChatRuntimeResult;

		const CronJson& cronJob = job;
		if (!cronJob.contains("payload") || !cronJob["payload"].is_object()) {
			return std::nullopt;
		}

		const CronJson& payload = cronJob["payload"];
		const std::string payloadKind = cron_production::ResolveCronPayloadKind(payload);
		if (payloadKind != "agentturn") {
			return std::nullopt;
		}

		const std::string message = cron::TrimCopy(payload.value("message", std::string()));
		if (message.empty()) {
			return std::nullopt;
		}

		if (!context.chatRuntimeCallbackPtr) {
			return cron_production::BuildCronRuntimeUnavailableResult(
				"chat runtime callback is not configured",
				nowMs);
		}

		const auto& chatRuntimeCallback = 
			*static_cast<const GatewayHost::ChatRuntimeCallback*>(context.chatRuntimeCallbackPtr);

		const std::string sessionKey = cron_production::ResolveCronChatSessionKey(cronJob);
		const std::string runId = cron_production::BuildCronAdapterRunId(cronJob, nowMs);
		ChatRuntimeRequest request;
		request.runId = runId;
		request.sessionKey = sessionKey;
		request.message = message;
		request.bodyForCommands = message;
		request.bodyForAgent = message;
		if (payload.contains("model") && payload["model"].is_string()) {
			request.modelIdOverride = cron::TrimCopy(payload["model"].get<std::string>());
		}
		if (payload.contains("provider") && payload["provider"].is_string()) {
			request.providerOverride = cron::TrimCopy(payload["provider"].get<std::string>());
		}
		if (cronJob.contains("model") && cronJob["model"].is_string() &&
			request.modelIdOverride.empty()) {
			request.modelIdOverride = cron::TrimCopy(cronJob["model"].get<std::string>());
		}
		if (cronJob.contains("provider") && cronJob["provider"].is_string() &&
			request.providerOverride.empty()) {
			request.providerOverride = cron::TrimCopy(cronJob["provider"].get<std::string>());
		}
		const auto timeoutSeconds = cron::TryReadInt64Field(payload, "timeoutSeconds");
		if (timeoutSeconds.has_value() && timeoutSeconds.value() > 0) {
			request.timeoutSeconds = timeoutSeconds.value();
			if (timeoutSeconds.value() <= 1) {
				return CronJson{
					{ "handled", true },
					{ "status", "error" },
					{ "error", "cron: job execution timed out" },
					{ "errorCategory", "timeout" },
					{ "summary", "Agent turn timed out" },
					{ "retryable", true },
					{ "timedOut", true },
					{ "sessionKey", sessionKey },
					{ "sessionId", sessionKey.empty() ? "isolated" : sessionKey },
					{ "observedAtMs", nowMs }
				};
			}
		}
		request.shouldLoadInlineSkillCommands = false;
		request.allowInlineToolImmediateExecution = true;

		ChatRuntimeResult runtimeResult;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			runtimeResult = chatRuntimeCallback(request);
		}

		auto mapped = cron_production::MapChatRuntimeResultToCron(
			runtimeResult,
			"isolated-agent");
		if (!mapped.has_value()) {
			return std::nullopt;
		}

		mapped.value()["sessionKey"] = sessionKey;
		mapped.value()["sessionId"] = sessionKey.empty() ? "isolated" : sessionKey;
		mapped.value()["runtimeModule"] = "isolated-agent";
		if (payload.contains("fallbacks") && payload["fallbacks"].is_array()) {
			mapped.value()["fallbackPolicy"] = CronJson{
				{ "fallbackCount", payload["fallbacks"].size() }
			};
		}
		if (payload.contains("thinking") && payload["thinking"].is_string()) {
			mapped.value()["thinkingLevel"] =
				cron::TrimCopy(payload["thinking"].get<std::string>());
		}
		if (payload.contains("model") && payload["model"].is_string()) {
			mapped.value()["model"] = cron::TrimCopy(payload["model"].get<std::string>());
		}
		if (payload.contains("provider") && payload["provider"].is_string()) {
			mapped.value()["provider"] = cron::TrimCopy(payload["provider"].get<std::string>());
		}
		return mapped;
	}

	void GatewayHostCronProductionAdapter::UpsertTaskLedgerEntry(
		const nlohmann::json& payload,
		const bool terminal,
		const AdapterContext& context) {
		using TaskDeltaEntry = GatewayHost::ChatRuntimeResult::TaskDeltaEntry;
		if (!context.taskDeltaRepository) {
			return;
		}

		const cron::CronJson& cronPayload = payload;
		const std::string runId = cron_production::ReadStringField(cronPayload, "runId");
		if (runId.empty()) {
			return;
		}

		std::vector<TaskDeltaEntry> entries;
		if (const auto existing = context.taskDeltaRepository->Get(runId, false)) {
			entries = existing.value();
		}

		entries.push_back(
			cron_production::BuildCronTaskDeltaEntry(cronPayload, entries.size(), terminal));
		if (!context.taskDeltaRepository->Upsert(runId, entries)) {
			return;
		}

		if (terminal) {
			DispatchAnnounceDeliveryNotification(payload, context);
			DispatchFailureAlertNotification(payload, context);
		}
	}

	void GatewayHostCronProductionAdapter::BroadcastRealtimeEvent(
		const cron::CronRealtimeEvent& event,
		const AdapterContext& context) {
		if (!context.transportRunning || !context.eventFanoutService || !context.cronPushEventSeq) {
			return;
		}

		const cron::CronJson payload = cron::CronRealtimeEventToJson(event);
		std::string broadcastError;
		context.transportBroadcast(
			context.eventFanoutService->BuildCronEventFrame(payload.dump(), ++(*context.cronPushEventSeq)),
			broadcastError);
		if (!broadcastError.empty()) {
			context.emitTelemetry(
				"gateway.cron.realtime.broadcast_error",
				std::string("{\"error\":") + JsonString(broadcastError) + "}");
		}
	}

	void GatewayHostCronProductionAdapter::DispatchScheduleAutoDisableNotification(
		const cron::CronScheduleNotificationEvent& event,
		const AdapterContext& context) {
		if (event.text.empty() || !context.chatRuntimeCallbackPtr) {
			return;
		}

		const auto& chatRuntimeCallback = 
			*static_cast<const GatewayHost::ChatRuntimeCallback*>(context.chatRuntimeCallbackPtr);
		using ChatRuntimeRequest = GatewayHost::ChatRuntimeRequest;

		std::string sessionKey = cron::TrimCopy(event.sessionKey);
		if (sessionKey.empty() && !event.agentId.empty()) {
			sessionKey = event.agentId;
		}
		if (sessionKey.empty()) {
			sessionKey = "main";
		}

		ChatRuntimeRequest request;
		request.runId =
			"cron-schedule-auto-disable-" + std::to_string(cron::UtcNowMs());
		request.sessionKey = sessionKey;
		request.message = "[cron] " + event.text;
		request.bodyForCommands = event.text;
		request.bodyForAgent = request.message;
		request.shouldLoadInlineSkillCommands = false;
		request.allowInlineToolImmediateExecution = false;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			(void)chatRuntimeCallback(request);
		}

		if (context.transportRunning && context.eventFanoutService && context.chatPushEventSeq) {
			cron::CronJson eventPayload = {
				{ "kind", "cron.notification" },
				{ "subtype", "schedule-auto-disable" },
				{ "sessionKey", sessionKey },
				{ "text", request.message },
				{ "contextKey", event.contextKey }
			};
			if (!event.agentId.empty()) {
				eventPayload["agentId"] = event.agentId;
			}

			std::string broadcastError;
			context.transportBroadcast(
				context.eventFanoutService->BuildChatEventFrame(
					eventPayload.dump(),
					++(*context.chatPushEventSeq)),
				broadcastError);
			if (!broadcastError.empty()) {
				context.emitTelemetry(
					"gateway.cron.schedule_auto_disable.broadcast_error",
					std::string("{\"error\":") + JsonString(broadcastError) + "}");
			}
		}
	}

	void GatewayHostCronProductionAdapter::DispatchFailureAlertNotification(
		const nlohmann::json& payload,
		const AdapterContext& context) {
		const cron::CronJson& cronPayload = payload;
		if (!context.chatRuntimeCallbackPtr) {
			return;
		}

		const auto& chatRuntimeCallback = 
			*static_cast<const GatewayHost::ChatRuntimeCallback*>(context.chatRuntimeCallbackPtr);
		using ChatRuntimeRequest = GatewayHost::ChatRuntimeRequest;

		if (!cronPayload.contains("failureAlertTriggered") ||
			!cronPayload["failureAlertTriggered"].is_boolean() ||
			!cronPayload["failureAlertTriggered"].get<bool>()) {
			return;
		}

		std::string sessionKey = cron::TrimCopy(
			cron_production::ReadStringField(cronPayload, "sessionKey"));
		if (sessionKey.empty()) {
			sessionKey = "main";
		}

		const std::string jobId =
			cron_production::ReadStringField(cronPayload, "jobId");
		const std::string mode =
			cron_production::ReadStringField(cronPayload, "failureAlertMode");
		const std::string target =
			cron_production::ReadStringField(cronPayload, "failureAlertTarget");
		const std::string channel =
			cron_production::ReadStringField(cronPayload, "failureAlertChannel");
		const std::string accountId =
			cron_production::ReadStringField(cronPayload, "failureAlertAccountId");
		const std::string summary =
			cron_production::ReadStringField(cronPayload, "summary");
		const std::string error =
			cron_production::ReadStringField(cronPayload, "error");

		std::string text = "[cron][failure-alert]";
		if (!jobId.empty()) {
			text += " job=" + jobId;
		}
		if (!mode.empty()) {
			text += " mode=" + mode;
		}
		if (!target.empty()) {
			text += " target=" + target;
		}
		if (!channel.empty()) {
			text += " channel=" + channel;
		}
		if (!accountId.empty()) {
			text += " accountId=" + accountId;
		}
		if (!summary.empty()) {
			text += " summary=" + summary;
		}
		if (!error.empty()) {
			text += " error=" + error;
		}

		ChatRuntimeRequest request;
		request.runId = "cron-failure-alert-" + std::to_string(cron::UtcNowMs());
		request.sessionKey = sessionKey;
		request.message = text;
		request.bodyForCommands = text;
		request.bodyForAgent = text;
		request.shouldLoadInlineSkillCommands = false;
		request.allowInlineToolImmediateExecution = false;

		try {
			(void)chatRuntimeCallback(request);
		}
		catch (...) {
		}

		if (context.transportRunning && context.eventFanoutService && context.chatPushEventSeq) {
			cron::CronJson eventPayload = {
				{ "kind", "cron.notification" },
				{ "subtype", "failure-alert" },
				{ "sessionKey", sessionKey },
				{ "text", text },
				{ "jobId", jobId },
				{ "mode", mode },
				{ "target", target },
				{ "channel", channel },
				{ "accountId", accountId }
			};
			if (!error.empty()) {
				eventPayload["error"] = error;
			}

			std::string broadcastError;
			context.transportBroadcast(
				context.eventFanoutService->BuildChatEventFrame(
					eventPayload.dump(),
					++(*context.chatPushEventSeq)),
				broadcastError);
			if (!broadcastError.empty()) {
				context.emitTelemetry(
					"gateway.cron.failure_alert.broadcast_error",
					std::string("{\"error\":") + JsonString(broadcastError) + "}");
			}
		}
	}

	void GatewayHostCronProductionAdapter::DispatchAnnounceDeliveryNotification(
		const nlohmann::json& payload,
		const AdapterContext& context) {
		const cron::CronJson& cronPayload = payload;
		if (!context.chatRuntimeCallbackPtr) {
			return;
		}

		const auto& chatRuntimeCallback = 
			*static_cast<const GatewayHost::ChatRuntimeCallback*>(context.chatRuntimeCallbackPtr);
		using ChatRuntimeRequest = GatewayHost::ChatRuntimeRequest;

		const std::string deliveryMode =
			cron::ToLowerCopy(cron_production::ReadStringField(cronPayload, "deliveryMode"));
		if (deliveryMode != "announce") {
			return;
		}

		const std::string deliveryStatus =
			cron::ToLowerCopy(cron_production::ReadStringField(cronPayload, "deliveryStatus"));
		if (deliveryStatus != "delivered") {
			return;
		}

		std::string sessionKey = cron::TrimCopy(
			cron_production::ReadStringField(cronPayload, "sessionKey"));
		const std::string deliveryTarget =
			cron_production::ReadStringField(cronPayload, "deliveryTarget");
		if (sessionKey.empty() && !deliveryTarget.empty()) {
			sessionKey = cron::TrimCopy(deliveryTarget);
		}
		if (sessionKey.empty()) {
			sessionKey = "main";
		}

		const std::string jobId =
			cron_production::ReadStringField(cronPayload, "jobId");
		const std::string channel =
			cron_production::ReadStringField(cronPayload, "deliveryChannel");
		const std::string accountId =
			cron_production::ReadStringField(cronPayload, "deliveryAccountId");
		const std::string summary =
			cron_production::ReadStringField(cronPayload, "summary");

		std::string text = "[cron][announce]";
		if (!jobId.empty()) {
			text += " job=" + jobId;
		}
		if (!deliveryTarget.empty()) {
			text += " target=" + deliveryTarget;
		}
		if (!channel.empty()) {
			text += " channel=" + channel;
		}
		if (!accountId.empty()) {
			text += " accountId=" + accountId;
		}
		if (!summary.empty()) {
			text += " summary=" + summary;
		}

		ChatRuntimeRequest request;
		request.runId = "cron-announce-" + std::to_string(cron::UtcNowMs());
		request.sessionKey = sessionKey;
		request.message = text;
		request.bodyForCommands = text;
		request.bodyForAgent = text;
		request.shouldLoadInlineSkillCommands = false;
		request.allowInlineToolImmediateExecution = false;

		try {
			(void)chatRuntimeCallback(request);
		}
		catch (...) {
		}

		if (context.transportRunning && context.eventFanoutService && context.chatPushEventSeq) {
			cron::CronJson eventPayload = {
				{ "kind", "cron.notification" },
				{ "subtype", "announce" },
				{ "sessionKey", sessionKey },
				{ "text", text },
				{ "jobId", jobId },
				{ "target", deliveryTarget },
				{ "channel", channel },
				{ "accountId", accountId }
			};

			std::string broadcastError;
			context.transportBroadcast(
				context.eventFanoutService->BuildChatEventFrame(
					eventPayload.dump(),
					++(*context.chatPushEventSeq)),
				broadcastError);
			if (!broadcastError.empty()) {
				context.emitTelemetry(
					"gateway.cron.announce.broadcast_error",
					std::string("{\"error\":") + JsonString(broadcastError) + "}");
			}
		}
	}

	void GatewayHostCronProductionAdapter::HandleTaskLedgerCreateRunning(
		const nlohmann::json& payload,
		const AdapterContext& context) {
		std::lock_guard<std::mutex> lock(m_mutex);
		UpsertTaskLedgerEntry(payload, false, context);
	}

	void GatewayHostCronProductionAdapter::HandleTaskLedgerComplete(
		const nlohmann::json& payload,
		const AdapterContext& context) {
		std::lock_guard<std::mutex> lock(m_mutex);
		UpsertTaskLedgerEntry(payload, true, context);
	}

	void GatewayHostCronProductionAdapter::HandleTaskLedgerFail(
		const nlohmann::json& payload,
		const AdapterContext& context) {
		std::lock_guard<std::mutex> lock(m_mutex);
		UpsertTaskLedgerEntry(payload, true, context);
	}

} // namespace blazeclaw::gateway
