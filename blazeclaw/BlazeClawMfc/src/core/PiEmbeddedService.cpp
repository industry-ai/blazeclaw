#include "pch.h"
#include "PiEmbeddedService.h"

#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <unordered_set>

namespace blazeclaw::core {

	namespace {

		std::uint64_t ResolveStartedAtMs(const std::size_t ordinal) {
			return 1735689700000 + static_cast<std::uint64_t>(ordinal);
		}

		std::uint64_t CurrentEpochMs() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		std::string ToLowerCopy(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		bool ContainsAny(const std::string& text, const std::vector<std::string>& needles) {
			for (const auto& needle : needles) {
				if (!needle.empty() && text.find(needle) != std::string::npos) {
					return true;
				}
			}

			return false;
		}

		std::optional<std::string> TryExtractQuoted(const std::string& message) {
			static const std::regex kChineseSingleQuote(R"(‘([^’]{1,120})’)");
			static const std::regex kAsciiSingleQuote(R"('([^']{1,120})')");

			std::smatch match;
			if (std::regex_search(message, match, kChineseSingleQuote) && match.size() >= 2) {
				return match[1].str();
			}

			if (std::regex_search(message, match, kAsciiSingleQuote) && match.size() >= 2) {
				return match[1].str();
			}

			return std::nullopt;
		}

		std::optional<std::string> FindToolByAlias(
			const std::vector<blazeclaw::gateway::ToolCatalogEntry>& tools,
			const std::vector<EmbeddedToolBinding>& bindings,
			const std::vector<std::string>& aliases) {
			for (const auto& alias : aliases) {
				for (const auto& tool : tools) {
					const std::string loweredTool = ToLowerCopy(tool.id);
					if (loweredTool.find(alias) != std::string::npos) {
						return tool.id;
					}
				}

				for (const auto& binding : bindings) {
					const std::string loweredCommand = ToLowerCopy(binding.commandName);
					const std::string loweredTool = ToLowerCopy(binding.toolName);
					if (loweredCommand.find(alias) != std::string::npos ||
						loweredTool.find(alias) != std::string::npos) {
						return binding.toolName;
					}
				}
			}

			return std::nullopt;
		}

		std::vector<std::string> ResolveLegacyAliasExecutionPlan(
			const EmbeddedRuntimeExecutionRequest& request) {
			const std::string loweredMessage = ToLowerCopy(request.run.message);
			const bool hasSearchIntent = ContainsAny(
				loweredMessage,
				{ "brave", "search", "搜索", "news", "动态" });
			const bool hasSummarizeIntent = ContainsAny(
				loweredMessage,
				{ "summarize", "summary", "摘要", "浓缩" });
			const bool hasNotionIntent = ContainsAny(
				loweredMessage,
				{ "notion", "每日早报", "页面", "写入" });

			std::vector<std::string> executionPlan;
			if (hasSearchIntent) {
				if (const auto tool = FindToolByAlias(
					request.runtimeTools,
					request.toolBindings,
					{ "brave", "search" });
					tool.has_value()) {
					executionPlan.push_back(tool.value());
				}
			}

			if (hasSummarizeIntent) {
				if (const auto tool = FindToolByAlias(
					request.runtimeTools,
					request.toolBindings,
					{ "summarize", "summary", "摘要" });
					tool.has_value()) {
					executionPlan.push_back(tool.value());
				}
			}

			if (hasNotionIntent) {
				if (const auto tool = FindToolByAlias(
					request.runtimeTools,
					request.toolBindings,
					{ "notion", "page", "write" });
					tool.has_value()) {
					executionPlan.push_back(tool.value());
				}
			}

			executionPlan.erase(
				std::unique(executionPlan.begin(), executionPlan.end()),
				executionPlan.end());
			return executionPlan;
		}

		nlohmann::json BuildLegacyToolArgs(
			const std::string& toolName,
			const std::string& query,
			const std::string& runMessage,
			const std::string& lastOutput) {
			nlohmann::json args = nlohmann::json::object();
			const std::string loweredTool = ToLowerCopy(toolName);
			if (loweredTool.find("search") != std::string::npos ||
				loweredTool.find("brave") != std::string::npos) {
				args["query"] = query;
				args["topK"] = 3;
			}
			else if (loweredTool.find("summ") != std::string::npos) {
				args["text"] = lastOutput.empty() ? runMessage : lastOutput;
				args["maxChars"] = 100;
			}
			else if (loweredTool.find("notion") != std::string::npos) {
				args["page"] = "每日早报";
				args["content"] = lastOutput.empty() ? runMessage : lastOutput;
			}
			else {
				args["input"] = lastOutput.empty() ? runMessage : lastOutput;
			}

			return args;
		}

		std::string ResolveBindingArgMode(
			const EmbeddedRuntimeExecutionRequest& request,
			const std::string& toolName) {
			for (const auto& binding : request.toolBindings) {
				if (binding.toolName == toolName) {
					return ToLowerCopy(binding.argMode);
				}
			}

			return {};
		}

		std::optional<std::string> ResolveNextToolFromPromptWindow(
			const EmbeddedRuntimeExecutionRequest& request,
			const std::string& promptWindow,
			const std::set<std::string>& visited) {
			std::vector<std::pair<std::size_t, std::string>> rankedTools;
			auto considerTool = [&](const std::string& candidateTool,
				const std::string& sourceName) {
					if (candidateTool.empty() ||
						visited.find(candidateTool) != visited.end()) {
						return;
					}

					const std::string loweredSource = ToLowerCopy(sourceName);
					const std::size_t pos = promptWindow.find(loweredSource);
					if (pos == std::string::npos) {
						return;
					}

					rankedTools.push_back({ pos, candidateTool });
				};

			for (const auto& binding : request.toolBindings) {
				considerTool(binding.toolName, binding.commandName);
				considerTool(binding.toolName, binding.toolName);
			}

			for (const auto& runtimeTool : request.runtimeTools) {
				considerTool(runtimeTool.id, runtimeTool.id);
				considerTool(runtimeTool.id, runtimeTool.label);
			}

			if (rankedTools.empty()) {
				return std::nullopt;
			}

			std::sort(
				rankedTools.begin(),
				rankedTools.end(),
				[](const auto& left, const auto& right) {
					return left.first < right.first;
				});

			return rankedTools.front().second;
		}

		std::vector<std::string> ResolveDynamicExecutionPlan(
			const EmbeddedRuntimeExecutionRequest& request,
			const std::size_t maxSteps) {
			std::vector<std::string> plan;
			if (maxSteps == 0) {
				return plan;
			}

			const std::string promptWindow =
				ToLowerCopy(request.skillsPrompt + "\n" + request.run.message);
			std::set<std::string> visited;

			for (std::size_t step = 0; step < maxSteps; ++step) {
				const auto nextTool = ResolveNextToolFromPromptWindow(
					request,
					promptWindow,
					visited);
				if (!nextTool.has_value()) {
					break;
				}

				visited.insert(nextTool.value());
				plan.push_back(nextTool.value());
			}

			if (plan.empty()) {
				plan = ResolveLegacyAliasExecutionPlan(request);
			}

			return plan;
		}

		nlohmann::json BuildDynamicToolArgs(
			const EmbeddedRuntimeExecutionRequest& request,
			const std::string& toolName,
			const std::string& query,
			const std::string& runMessage,
			const std::string& lastOutput,
			const std::size_t stepIndex) {
			const std::string argMode = ResolveBindingArgMode(request, toolName);
			if (argMode == "none") {
				return nlohmann::json::object();
			}

			if (argMode == "text") {
				nlohmann::json args = nlohmann::json::object();
				args["text"] = lastOutput.empty() ? runMessage : lastOutput;
				args["stepIndex"] = stepIndex;
				return args;
			}

			nlohmann::json args = BuildLegacyToolArgs(
				toolName,
				query,
				runMessage,
				lastOutput);
			args["stepIndex"] = stepIndex;
			args["sessionKey"] =
				request.run.sessionId.empty() ? "main" : request.run.sessionId;
			return args;
		}

		bool IsAllowedRuntimeTool(
			const EmbeddedRuntimeExecutionRequest& request,
			const std::string& toolName) {
			return std::any_of(
				request.runtimeTools.begin(),
				request.runtimeTools.end(),
				[&](const blazeclaw::gateway::ToolCatalogEntry& entry) {
					return entry.enabled && entry.id == toolName;
				});
		}

		bool ValidateArgsForArgMode(
			const std::string& argMode,
			const nlohmann::json& args) {
			if (!args.is_object()) {
				return false;
			}

			if (argMode == "none") {
				return args.empty();
			}

			if (argMode == "text") {
				return args.contains("text") && args["text"].is_string();
			}

			return true;
		}

		bool IsTransientExecutionFailure(
			const blazeclaw::gateway::ToolExecuteResultV2& execution) {
			const std::string status = ToLowerCopy(execution.status);
			const std::string errorCode = ToLowerCopy(execution.errorCode);
			return status == "timeout" ||
				status == "temporary_error" ||
				status == "throttled" ||
				errorCode == "timeout" ||
				errorCode == "network_error" ||
				errorCode == "transient";
		}

		std::string BuildToolCallSignature(
			const std::string& toolName,
			const nlohmann::json& args) {
			return toolName + "::" + args.dump();
		}

		blazeclaw::gateway::ToolExecuteResultV2 ExecuteToolWithCompatibility(
			const EmbeddedRuntimeExecutionRequest& request,
			const std::string& toolName,
			const std::string& argsJson,
			const std::string& correlationId,
			const std::optional<std::uint64_t> deadlineEpochMs) {
			const std::uint64_t startedAtMs = CurrentEpochMs();
			if (request.toolExecutorV2) {
				blazeclaw::gateway::ToolExecuteResultV2 result = request.toolExecutorV2(
					blazeclaw::gateway::ToolExecuteRequestV2{
						.tool = toolName,
						.argsJson = argsJson,
						.correlationId = correlationId,
						.deadlineEpochMs = deadlineEpochMs,
					});
				if (result.tool.empty()) {
					result.tool = toolName;
				}
				if (result.correlationId.empty()) {
					result.correlationId = correlationId;
				}
				if (result.startedAtMs == 0) {
					result.startedAtMs = startedAtMs;
				}
				if (result.completedAtMs == 0) {
					result.completedAtMs = CurrentEpochMs();
				}
				result.latencyMs = result.completedAtMs >= result.startedAtMs
					? (result.completedAtMs - result.startedAtMs)
					: 0;
				return result;
			}

			if (request.toolExecutor) {
				const auto legacy = request.toolExecutor(toolName, argsJson);
				const std::uint64_t completedAtMs = CurrentEpochMs();
				return blazeclaw::gateway::ToolExecuteResultV2{
					.tool = legacy.tool,
					.executed = legacy.executed,
					.status = legacy.status,
					.result = legacy.output,
					.errorCode =
						(legacy.executed && legacy.status != "error")
						? std::string()
						: std::string("legacy_execution_failed"),
					.errorMessage =
						(legacy.executed && legacy.status != "error")
						? std::string()
						: legacy.output,
					.startedAtMs = startedAtMs,
					.completedAtMs = completedAtMs,
					.latencyMs = completedAtMs >= startedAtMs
						? (completedAtMs - startedAtMs)
						: 0,
					.correlationId = correlationId,
				};
			}

			const std::uint64_t completedAtMs = CurrentEpochMs();
			return blazeclaw::gateway::ToolExecuteResultV2{
				.tool = toolName,
				.executed = false,
				.status = "unavailable_runtime",
				.result = "runtime_executor_missing",
				.errorCode = "runtime_executor_missing",
				.errorMessage = "runtime executor missing",
				.startedAtMs = startedAtMs,
				.completedAtMs = completedAtMs,
				.latencyMs = completedAtMs >= startedAtMs
					? (completedAtMs - startedAtMs)
					: 0,
				.correlationId = correlationId,
			};
		}

		void FinalizeExecutionResult(
			EmbeddedRuntimeExecutionResult& result,
			const std::string& status,
			const std::string& reason,
			const std::string& errorCode,
			const std::string& errorMessage,
			const std::string& assistantText) {
			result.status = status;
			result.reason = reason;
			result.errorCode = errorCode;
			result.errorMessage = errorMessage;
			result.assistantText = assistantText;
			result.success = status == "completed";
		}

	} // namespace

	void PiEmbeddedService::Configure(const blazeclaw::config::AppConfig& appConfig) {
		m_config = appConfig;
	}

	EmbeddedRunResult PiEmbeddedService::QueueRun(const EmbeddedRunRequest& request) {
		if (!m_config.embedded.enabled) {
			return EmbeddedRunResult{
			  .accepted = false,
			  .runId = {},
			  .status = "blocked",
			  .reason = "embedded_runtime_disabled",
			  .startedAtMs = 0,
			};
		}

		if (ActiveRuns() >= m_config.embedded.maxQueueDepth) {
			return EmbeddedRunResult{
			  .accepted = false,
			  .runId = {},
			  .status = "blocked",
			  .reason = "embedded_queue_full",
			  .startedAtMs = 0,
			};
		}

		const std::uint64_t startedAtMs = ResolveStartedAtMs(m_runsById.size());
		const std::string runId =
			"embedded-" + std::to_string(startedAtMs) + "-" +
			(request.agentId.empty() ? "default" : request.agentId);

		EmbeddedRunRecord record;
		record.runId = runId;
		record.sessionId = request.sessionId.empty() ? "main" : request.sessionId;
		record.agentId = request.agentId.empty() ? "default" : request.agentId;
		record.message = request.message;
		record.status = "running";
		record.startedAtMs = startedAtMs;

		m_runsById.insert_or_assign(runId, record);

		return EmbeddedRunResult{
		  .accepted = true,
		  .runId = runId,
		  .status = "running",
		  .reason = "accepted",
		  .startedAtMs = startedAtMs,
		};
	}

	EmbeddedRuntimeExecutionResult PiEmbeddedService::ExecuteRun(
		const EmbeddedRuntimeExecutionRequest& request) {
		EmbeddedRuntimeExecutionResult result;
		if (!request.enableDynamicToolLoop) {
			result.accepted = true;
			result.handled = false;
			result.success = true;
			result.status = "completed";
			result.reason = "dynamic_tool_loop_disabled";
			return result;
		}

		const std::vector<std::string> executionPlan =
			ResolveDynamicExecutionPlan(request, 6);

		if (executionPlan.empty()) {
			result.accepted = true;
			result.handled = false;
			result.success = true;
			result.status = "completed";
			result.reason = "no_tool_orchestration_match";
			return result;
		}

		const auto queued = QueueRun(request.run);
		result.accepted = queued.accepted;
		result.runId = queued.runId;
		result.status = queued.status;
		result.reason = queued.reason;
		result.startedAtMs = queued.startedAtMs;

		if (!queued.accepted) {
			result.errorCode = "embedded_run_rejected";
			result.errorMessage = queued.reason;
			return result;
		}

		const std::string sessionId =
			request.run.sessionId.empty() ? "main" : request.run.sessionId;
		const std::uint64_t deadlineEpochMs =
			queued.startedAtMs + static_cast<std::uint64_t>(m_config.embedded.runTimeoutMs);
		const auto hasTimedOut = [&](const std::uint64_t nowMs) {
			return nowMs >= deadlineEpochMs;
			};
		const auto appendDelta =
			[this, &result, &queued, &sessionId](const EmbeddedTaskDelta& rawDelta) {
			EmbeddedTaskDelta delta = rawDelta;
			delta.runId = queued.runId;
			delta.sessionId = sessionId;
			if (delta.startedAtMs == 0) {
				delta.startedAtMs = CurrentEpochMs();
			}
			if (delta.completedAtMs == 0) {
				delta.completedAtMs = delta.startedAtMs;
			}
			delta.latencyMs = delta.completedAtMs >= delta.startedAtMs
				? delta.completedAtMs - delta.startedAtMs
				: 0;
			AppendTaskDelta(queued.runId, delta);
			};

		result.handled = true;
		static constexpr std::size_t kMaxPolicySteps = 6;
		if (executionPlan.size() > kMaxPolicySteps) {
			FinalizeExecutionResult(
				result,
				"failed",
				"max_steps_exceeded",
				"embedded_max_steps_exceeded",
				"decomposition exceeded maximum policy steps",
				{});
			appendDelta(EmbeddedTaskDelta{
				.phase = "final",
				.resultJson = result.errorMessage,
				.status = "failed",
				.errorCode = result.errorCode,
				.stepLabel = "run_terminal",
				});
			result.taskDeltas = GetTaskDeltas(queued.runId);
			return result;
		}

		result.decompositionSteps = executionPlan.size();
		appendDelta(EmbeddedTaskDelta{
			.phase = "plan",
			.resultJson = nlohmann::json(executionPlan).dump(),
			.status = "ok",
			.stepLabel = "execution_plan",
			});

		const std::string query =
			TryExtractQuoted(request.run.message).value_or(request.run.message);
		std::string lastOutput;

		std::unordered_map<std::string, std::size_t> repeatCounts;
		for (std::size_t stepIndex = 0; stepIndex < executionPlan.size(); ++stepIndex) {
			const std::string& toolName = executionPlan[stepIndex];
			if (hasTimedOut(CurrentEpochMs())) {
				FinalizeExecutionResult(
					result,
					"failed",
					"deadline_exceeded",
					"embedded_deadline_exceeded",
					"embedded run exceeded timeout",
					{});
				appendDelta(EmbeddedTaskDelta{
					.phase = "final",
					.resultJson = result.errorMessage,
					.status = "failed",
					.errorCode = result.errorCode,
					.stepLabel = "run_terminal",
					});
				result.taskDeltas = GetTaskDeltas(queued.runId);
				return result;
			}

			result.assistantDeltas.push_back("tools.execute.start tool=" + toolName);
			nlohmann::json args = BuildDynamicToolArgs(
				request,
				toolName,
				query,
				request.run.message,
				lastOutput,
				stepIndex);
			const std::string argMode = ResolveBindingArgMode(request, toolName);
			if (!ValidateArgsForArgMode(argMode, args)) {
				FinalizeExecutionResult(
					result,
					"failed",
					"invalid_args",
					"embedded_invalid_args",
					"tool args do not satisfy binding arg mode",
					{});
				appendDelta(EmbeddedTaskDelta{
					.phase = "final",
					.resultJson = result.errorMessage,
					.status = "failed",
					.errorCode = result.errorCode,
					.stepLabel = "run_terminal",
					});
				result.taskDeltas = GetTaskDeltas(queued.runId);
				return result;
			}

			if (!IsAllowedRuntimeTool(request, toolName)) {
				FinalizeExecutionResult(
					result,
					"failed",
					"tool_blocked",
					"embedded_tool_blocked",
					"tool is not enabled in runtime catalog",
					{});
				appendDelta(EmbeddedTaskDelta{
					.phase = "final",
					.resultJson = result.errorMessage,
					.status = "failed",
					.errorCode = result.errorCode,
					.stepLabel = "run_terminal",
					});
				result.taskDeltas = GetTaskDeltas(queued.runId);
				return result;
			}

			appendDelta(EmbeddedTaskDelta{
				.phase = "tool_call",
				.toolName = toolName,
				.argsJson = args.dump(),
				.status = "requested",
				.modelTurnId = "model-turn-" + std::to_string(stepIndex),
				.stepLabel = "tool_request",
				});

			const std::string correlationId =
				queued.runId + ":" + std::to_string(result.assistantDeltas.size());
			const std::string callSignature = BuildToolCallSignature(toolName, args);
			static constexpr std::size_t kMaxRepeatCalls = 2;
			auto repeatIt = repeatCounts.find(callSignature);
			const std::size_t repeatCount = repeatIt == repeatCounts.end()
				? 0
				: repeatIt->second;
			if (repeatCount >= kMaxRepeatCalls) {
				FinalizeExecutionResult(
					result,
					"failed",
					"loop_detected",
					"embedded_loop_detected",
					"repeated tool call signature exceeded limit",
					{});
				appendDelta(EmbeddedTaskDelta{
					.phase = "final",
					.resultJson = result.errorMessage,
					.status = "failed",
					.errorCode = result.errorCode,
					.stepLabel = "run_terminal",
					});
				result.taskDeltas = GetTaskDeltas(queued.runId);
				return result;
			}
			repeatCounts.insert_or_assign(callSignature, repeatCount + 1);

			blazeclaw::gateway::ToolExecuteResultV2 execution{};
			static constexpr std::size_t kMaxRetries = 2;
			for (std::size_t attempt = 0; attempt < kMaxRetries; ++attempt) {
				execution = ExecuteToolWithCompatibility(
					request,
					toolName,
					args.dump(),
					correlationId,
					deadlineEpochMs);
				if ((execution.executed && execution.status != "error") ||
					!IsTransientExecutionFailure(execution)) {
					break;
				}
			}
			result.assistantDeltas.push_back(
				"tools.execute.result tool=" +
				toolName +
				" status=" +
				execution.status);

			appendDelta(EmbeddedTaskDelta{
				.phase = "tool_result",
				.toolName = toolName,
				.argsJson = args.dump(),
			 .resultJson = execution.result,
				.status = execution.status,
			  .errorCode = execution.executed
					? std::string()
					: (execution.errorCode.empty()
						? std::string("not_executed")
						: execution.errorCode),
				.startedAtMs = execution.startedAtMs,
				.completedAtMs = execution.completedAtMs,
				.latencyMs = execution.latencyMs,
				.modelTurnId = execution.correlationId,
				.stepLabel = "tool_result",
				});

			if (!execution.executed || execution.status == "error") {
				if (!CompleteRun(queued.runId, "failed", queued.startedAtMs + 1)) {
					FinalizeExecutionResult(
						result,
						"failed",
						"embedded_completion_failed",
						"embedded_completion_failed",
						"embedded completion failed",
						{});
				}
				FinalizeExecutionResult(
					result,
					"failed",
					"tool_execution_failed",
					"embedded_tool_execution_failed",
					execution.errorMessage.empty()
					? "tool execution failed"
					: execution.errorMessage,
					{});
				appendDelta(EmbeddedTaskDelta{
					.phase = "final",
					.resultJson = result.errorMessage,
					.status = "failed",
					.errorCode = result.errorCode,
					.stepLabel = "run_terminal",
					});
				result.taskDeltas = GetTaskDeltas(queued.runId);
				return result;
			}

			lastOutput = execution.result;
		}

		if (!CompleteRun(queued.runId, "completed", queued.startedAtMs + 1)) {
			FinalizeExecutionResult(
				result,
				"failed",
				"embedded_completion_failed",
				"embedded_completion_failed",
				"embedded completion failed",
				{});
			appendDelta(EmbeddedTaskDelta{
				.phase = "final",
				.resultJson = result.errorMessage,
				.status = "failed",
				.errorCode = result.errorCode,
				.stepLabel = "run_terminal",
				});
			result.taskDeltas = GetTaskDeltas(queued.runId);
			return result;
		}
		FinalizeExecutionResult(
			result,
			"completed",
			"orchestrated",
			{},
			{},
			lastOutput.empty()
			? "Embedded orchestration completed."
			: lastOutput);
		appendDelta(EmbeddedTaskDelta{
			  .phase = "final",
			  .resultJson = result.assistantText,
			  .status = "completed",
			  .stepLabel = "run_terminal",
			});
		result.taskDeltas = GetTaskDeltas(queued.runId);
		return result;
	}

	bool PiEmbeddedService::CompleteRun(
		const std::string& runId,
		const std::string& status,
		const std::uint64_t completedAtMs) {
		const auto it = m_runsById.find(runId);
		if (it == m_runsById.end()) {
			return false;
		}

		it->second.status = status.empty() ? "completed" : status;
		it->second.completedAtMs = completedAtMs;
		return true;
	}

	std::size_t PiEmbeddedService::ActiveRuns() const {
		std::size_t active = 0;
		for (const auto& [_, run] : m_runsById) {
			if (!run.completedAtMs.has_value()) {
				++active;
			}
		}

		return active;
	}

	std::optional<EmbeddedRunRecord> PiEmbeddedService::GetRun(
		const std::string& runId) const {
		const auto it = m_runsById.find(runId);
		if (it == m_runsById.end()) {
			return std::nullopt;
		}

		return it->second;
	}

	std::vector<EmbeddedTaskDelta> PiEmbeddedService::GetTaskDeltas(
		const std::string& runId) const {
		const auto it = m_taskDeltasByRunId.find(runId);
		if (it == m_taskDeltasByRunId.end()) {
			return {};
		}

		return it->second;
	}

	void PiEmbeddedService::ClearTaskDeltas(const std::string& runId) {
		if (runId.empty()) {
			m_taskDeltasByRunId.clear();
			return;
		}

		m_taskDeltasByRunId.erase(runId);
	}

	void PiEmbeddedService::AppendTaskDelta(
		const std::string& runId,
		const EmbeddedTaskDelta& delta) {
		if (runId.empty()) {
			return;
		}

		auto& deltas = m_taskDeltasByRunId[runId];
		EmbeddedTaskDelta normalized = delta;
		normalized.index = deltas.size();
		deltas.push_back(std::move(normalized));
	}

	bool PiEmbeddedService::ValidateFixtureScenarios(
		const std::filesystem::path& /*fixturesRoot*/,
		std::wstring& outError) const {
		outError.clear();

		blazeclaw::config::AppConfig cfg;
		cfg.embedded.enabled = true;
		cfg.embedded.maxQueueDepth = 1;
		cfg.embedded.dynamicToolLoopEnabled = true;

		PiEmbeddedService service;
		service.Configure(cfg);

		const auto first = service.QueueRun(EmbeddedRunRequest{
			.sessionId = "main",
			.agentId = "alpha",
			.message = "hello",
			});

		if (!first.accepted) {
			outError = L"Fixture validation failed: expected first embedded run accepted.";
			return false;
		}

		const auto second = service.QueueRun(EmbeddedRunRequest{
			.sessionId = "main",
			.agentId = "beta",
			.message = "hello2",
			});

		if (second.accepted || second.reason != "embedded_queue_full") {
			outError = L"Fixture validation failed: expected queue-full rejection.";
			return false;
		}

		if (!service.CompleteRun(first.runId, "completed", first.startedAtMs + 1)) {
			outError = L"Fixture validation failed: expected run completion success.";
			return false;
		}

		const auto resolved = service.GetRun(first.runId);
		if (!resolved.has_value() || resolved->status != "completed") {
			outError = L"Fixture validation failed: expected completed embedded run status.";
			return false;
		}

		const auto disabledLoop = service.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "search then summarize",
					},
				.skillsPrompt = "",
				.toolBindings = {},
				.runtimeTools = {},
				.enableDynamicToolLoop = false,
				.toolExecutor = {},
			});
		if (disabledLoop.handled ||
			disabledLoop.reason != "dynamic_tool_loop_disabled") {
			outError = L"Fixture validation failed: expected dynamic loop disabled baseline path.";
			return false;
		}

		PiEmbeddedService deltaService;
		deltaService.Configure(cfg);
		std::vector<blazeclaw::gateway::ToolCatalogEntry> tools = {
			blazeclaw::gateway::ToolCatalogEntry{
				.id = "brave-search",
				.label = "Brave Search",
				.category = "search",
				.enabled = true,
				},
			blazeclaw::gateway::ToolCatalogEntry{
				.id = "summarize",
				.label = "Summarize",
				.category = "transform",
				.enabled = true,
				},
			blazeclaw::gateway::ToolCatalogEntry{
				.id = "notion.write",
				.label = "Notion Write",
				.category = "sink",
				.enabled = true,
				},
		};

		const auto dynamicLoop = deltaService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "search news summarize and write to notion",
					},
			 .skillsPrompt =
					"Use brave-search then summarize then notion.write for this request.",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "brave-search",
						.description = "search",
						.toolName = "brave-search",
						.argMode = "raw",
					},
					EmbeddedToolBinding{
						.commandName = "summarize",
						.description = "summarize",
						.toolName = "summarize",
						.argMode = "text",
					},
					EmbeddedToolBinding{
						.commandName = "notion",
						.description = "notion",
						.toolName = "notion.write",
						.argMode = "raw",
					},
				},
				.runtimeTools = std::move(tools),
				.enableDynamicToolLoop = true,
				.toolExecutor = [](const std::string& requestedTool,
					const std::optional<std::string>& argsJson) {
					return blazeclaw::gateway::ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = argsJson.value_or("{}"),
						};
				},
			});

		if (!dynamicLoop.success || dynamicLoop.taskDeltas.empty()) {
			outError = L"Fixture validation failed: expected dynamic loop task deltas.";
			return false;
		}

		const bool hasPlan = std::any_of(
			dynamicLoop.taskDeltas.begin(),
			dynamicLoop.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "plan";
			});
		const bool hasToolCall = std::any_of(
			dynamicLoop.taskDeltas.begin(),
			dynamicLoop.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "tool_call";
			});
		const bool hasToolResult = std::any_of(
			dynamicLoop.taskDeltas.begin(),
			dynamicLoop.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "tool_result";
			});
		const bool hasFinal = std::any_of(
			dynamicLoop.taskDeltas.begin(),
			dynamicLoop.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "final";
			});
		if (!hasPlan || !hasToolCall || !hasToolResult || !hasFinal) {
			outError = L"Fixture validation failed: expected plan/tool_call/tool_result/final phases.";
			return false;
		}

		for (std::size_t i = 1; i < dynamicLoop.taskDeltas.size(); ++i) {
			if (dynamicLoop.taskDeltas[i].index != i) {
				outError = L"Fixture validation failed: expected ordered task delta indexes.";
				return false;
			}
		}

		const std::vector<std::string> toolCalls = {
			"brave-search",
			"summarize",
			"notion.write",
		};
		std::size_t callIndex = 0;
		for (const auto& delta : dynamicLoop.taskDeltas) {
			if (delta.phase != "tool_call") {
				continue;
			}

			if (callIndex >= toolCalls.size() || delta.toolName != toolCalls[callIndex]) {
				outError = L"Fixture validation failed: expected dynamic decomposition tool order.";
				return false;
			}
			++callIndex;
		}
		if (callIndex < toolCalls.size()) {
			outError = L"Fixture validation failed: expected complete dynamic decomposition tool order.";
			return false;
		}

		blazeclaw::config::AppConfig timeoutCfg = cfg;
		timeoutCfg.embedded.runTimeoutMs = 0;
		PiEmbeddedService timeoutService;
		timeoutService.Configure(timeoutCfg);
		const auto timeoutResult = timeoutService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "search then summarize",
					},
				.skillsPrompt = "search summarize",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "search",
						.description = "search",
						.toolName = "brave-search",
						.argMode = "raw",
					},
				},
				.runtimeTools = {
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "brave-search",
						.label = "Brave Search",
						.category = "search",
						.enabled = true,
					},
				},
				.enableDynamicToolLoop = true,
				.toolExecutor = [](const std::string& requestedTool,
					const std::optional<std::string>& argsJson) {
					return blazeclaw::gateway::ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = argsJson.value_or("{}"),
					};
				},
			});
		if (timeoutResult.success ||
			timeoutResult.errorCode != "embedded_deadline_exceeded") {
			outError = L"Fixture validation failed: expected deadline policy failure.";
			return false;
		}

		PiEmbeddedService blockedToolService;
		blockedToolService.Configure(cfg);
		const auto blockedToolResult = blockedToolService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "use blocked tool",
					},
				.skillsPrompt = "blocked",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "blocked",
						.description = "blocked",
						.toolName = "blocked-tool",
						.argMode = "raw",
					},
				},
				.runtimeTools = {
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "allowed-tool",
						.label = "Allowed",
						.category = "misc",
						.enabled = true,
					},
				},
				.enableDynamicToolLoop = true,
				.toolExecutor = [](const std::string& requestedTool,
					const std::optional<std::string>& argsJson) {
					return blazeclaw::gateway::ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = argsJson.value_or("{}"),
					};
				},
			});
		if (blockedToolResult.success ||
			blockedToolResult.errorCode != "embedded_tool_blocked") {
			outError = L"Fixture validation failed: expected blocked tool policy failure.";
			return false;
		}

		PiEmbeddedService parityService;
		parityService.Configure(cfg);
		std::vector<blazeclaw::gateway::ToolCatalogEntry> parityTools = {
			blazeclaw::gateway::ToolCatalogEntry{
				.id = "brave-search",
				.label = "Brave Search",
				.category = "search",
				.enabled = true,
				},
			blazeclaw::gateway::ToolCatalogEntry{
				.id = "summarize",
				.label = "Summarize",
				.category = "transform",
				.enabled = true,
				},
			blazeclaw::gateway::ToolCatalogEntry{
				.id = "notion.write",
				.label = "Notion Write",
				.category = "sink",
				.enabled = true,
				},
		};

		const std::string parityPrompt =
			"使用 brave-search multi-search-engine搜索‘今日大模型行业最新动态’，"
			"提取前 3 条核心新闻。接着用 Summarize 把它们浓缩成一段 100 字以内的摘要，"
			"最后调用 Notion 技能，将摘要写入我的 Notion ‘每日早报’ 页面中。";

		const auto parityResult = parityService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = parityPrompt,
					},
				.skillsPrompt =
					"Use brave-search then summarize then notion.write for parity lane.",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "brave-search",
						.description = "search",
						.toolName = "brave-search",
						.argMode = "raw",
					},
					EmbeddedToolBinding{
						.commandName = "summarize",
						.description = "summarize",
						.toolName = "summarize",
						.argMode = "text",
					},
					EmbeddedToolBinding{
						.commandName = "notion",
						.description = "notion",
						.toolName = "notion.write",
						.argMode = "raw",
					},
				},
				.runtimeTools = std::move(parityTools),
				.enableDynamicToolLoop = true,
				.toolExecutor = [](const std::string& requestedTool,
					const std::optional<std::string>& argsJson) {
					if (requestedTool == "brave-search") {
						return blazeclaw::gateway::ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "Top3: model A launch; model B benchmark; model C funding",
						};
					}

					if (requestedTool == "summarize") {
						return blazeclaw::gateway::ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "今日大模型要闻：A发布新版本，B刷新评测，C完成融资。",
						};
					}

					if (requestedTool == "notion.write") {
						return blazeclaw::gateway::ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "Notion write result confirming update to page '每日早报'.",
						};
					}

					return blazeclaw::gateway::ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = argsJson.value_or("{}"),
					};
				},
			});

		if (!parityResult.success || parityResult.decompositionSteps < 3) {
			outError = L"Fixture validation failed: expected parity decomposition steps >= 3.";
			return false;
		}

		const std::vector<std::string> parityOrderedTools = {
			"brave-search",
			"summarize",
			"notion.write",
		};
		std::size_t parityCallIndex = 0;
		for (const auto& delta : parityResult.taskDeltas) {
			if (delta.phase != "tool_call") {
				continue;
			}

			if (parityCallIndex >= parityOrderedTools.size() ||
				delta.toolName != parityOrderedTools[parityCallIndex]) {
				outError = L"Fixture validation failed: expected parity tool call order brave-search -> summarize -> notion.write.";
				return false;
			}

			++parityCallIndex;
		}

		if (parityCallIndex < parityOrderedTools.size()) {
			outError = L"Fixture validation failed: expected complete parity tool call sequence.";
			return false;
		}

		if (parityResult.assistantText.find("Notion write result confirming update to page '每日早报'.") == std::string::npos) {
			outError = L"Fixture validation failed: expected final Notion write result evidence.";
			return false;
		}

		PiEmbeddedService opsSmokeService;
		opsSmokeService.Configure(cfg);
		std::vector<blazeclaw::gateway::ToolCatalogEntry> opsTools = {
			blazeclaw::gateway::ToolCatalogEntry{
				.id = "weather.lookup",
				.label = "Weather Lookup",
				.category = "data",
				.enabled = true,
				},
			blazeclaw::gateway::ToolCatalogEntry{
				.id = "report.compose",
				.label = "Report Compose",
				.category = "transform",
				.enabled = true,
				},
			blazeclaw::gateway::ToolCatalogEntry{
				.id = "email.schedule",
				.label = "Email Schedule",
				.category = "communication",
				.enabled = true,
				},
		};

		const std::string opsPrompt =
			"Check today's weather in Wuhan, write a short report, and email it to "
			"jichengwhu@163.com now.";

		const auto opsResult = opsSmokeService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = opsPrompt,
					},
				.skillsPrompt =
					"Use weather.lookup then report.compose then email.schedule for immediate execution.",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "weather",
						.description = "weather lookup",
						.toolName = "weather.lookup",
						.argMode = "raw",
					},
					EmbeddedToolBinding{
						.commandName = "report",
						.description = "report compose",
						.toolName = "report.compose",
						.argMode = "text",
					},
					EmbeddedToolBinding{
						.commandName = "email",
						.description = "email schedule",
						.toolName = "email.schedule",
						.argMode = "raw",
					},
				},
				.runtimeTools = std::move(opsTools),
				.enableDynamicToolLoop = true,
				.toolExecutor = [](const std::string& requestedTool,
					const std::optional<std::string>& argsJson) {
					if (requestedTool == "weather.lookup") {
						return blazeclaw::gateway::ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "Wuhan today: cloudy, 26C, light breeze",
						};
					}

					if (requestedTool == "report.compose") {
						return blazeclaw::gateway::ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "Wuhan weather report: cloudy, 26C, light breeze. Suggested light outdoor plan.",
						};
					}

					if (requestedTool == "email.schedule") {
						return blazeclaw::gateway::ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = "Email scheduling/sending result for jichengwhu@163.com with report content.",
						};
					}

					return blazeclaw::gateway::ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = argsJson.value_or("{}"),
					};
				},
			});

		if (!opsResult.success || opsResult.decompositionSteps < 3) {
			outError = L"Fixture validation failed: expected operational decomposition steps >= 3.";
			return false;
		}

		const std::vector<std::string> opsOrderedTools = {
			"weather.lookup",
			"report.compose",
			"email.schedule",
		};
		std::size_t opsCallIndex = 0;
		for (const auto& delta : opsResult.taskDeltas) {
			if (delta.phase != "tool_call") {
				continue;
			}

			if (opsCallIndex >= opsOrderedTools.size() ||
				delta.toolName != opsOrderedTools[opsCallIndex]) {
				outError = L"Fixture validation failed: expected operational tool call order weather.lookup -> report.compose -> email.schedule.";
				return false;
			}

			++opsCallIndex;
		}

		if (opsCallIndex < opsOrderedTools.size()) {
			outError = L"Fixture validation failed: expected complete operational tool call sequence.";
			return false;
		}

		if (opsResult.assistantText.find(
			"Email scheduling/sending result for jichengwhu@163.com with report content.") ==
			std::string::npos) {
			outError = L"Fixture validation failed: expected final email scheduling result evidence.";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core
