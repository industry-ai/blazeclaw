#include "pch.h"
#include "PiEmbeddedService.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <unordered_set>

namespace blazeclaw::core {

	namespace {
		constexpr const char* kPhasePlan = "plan";
		constexpr const char* kPhaseToolCall = "tool_call";
		constexpr const char* kPhaseToolResult = "tool_result";
		constexpr const char* kPhaseFinal = "final";

		constexpr const char* kStatusPlanned = "planned";
		constexpr const char* kStatusRequested = "requested";
		constexpr const char* kStatusRunning = "running";
		constexpr const char* kStatusCompleted = "completed";
		constexpr const char* kStatusFailed = "failed";
		constexpr const char* kStatusSkipped = "skipped";

		constexpr const char* kErrorPlanEmptyTool = "embedded_plan_empty_tool";
		constexpr const char* kErrorInvalidArgMode = "embedded_invalid_arg_mode";
		constexpr const char* kErrorDuplicateLoopRisk = "embedded_duplicate_loop_risk";
		constexpr const char* kErrorMaxStepsExceeded = "embedded_max_steps_exceeded";
		constexpr const char* kErrorDeadlineExceeded = "embedded_deadline_exceeded";
		constexpr const char* kErrorInvalidArgs = "embedded_invalid_args";
		constexpr const char* kErrorToolBlocked = "embedded_tool_blocked";
		constexpr const char* kErrorLoopDetected = "embedded_loop_detected";
		constexpr const char* kErrorCompletionFailed = "embedded_completion_failed";
		constexpr const char* kErrorToolExecutionFailed = "embedded_tool_execution_failed";
		constexpr const char* kErrorRunCancelled = "embedded_run_cancelled";
		constexpr std::size_t kPlanningMaxSteps = 12;
		constexpr std::size_t kMaxPolicySteps = 6;

		struct EmbeddedExecutionPlanStep {
			std::size_t index = 0;
			std::string toolName;
			std::string argMode;
			std::string stepLabel;
		};

		struct EmbeddedPlanValidationFailure {
			std::string reason;
			std::string errorCode;
			std::string errorMessage;
		};

		std::uint64_t CurrentEpochMs() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		std::string ResolveFallbackActionForExecution(
			const blazeclaw::gateway::ToolExecuteResultV2& execution) {
			auto toLower = [](const std::string& value) {
				std::string lowered = value;
				std::transform(
					lowered.begin(),
					lowered.end(),
					lowered.begin(),
					[](const unsigned char ch) {
						return static_cast<char>(std::tolower(ch));
					});
				return lowered;
				};

			const std::string code = toLower(execution.errorCode);
			const std::string status = toLower(execution.status);

			if (code.find("missing") != std::string::npos ||
				code.find("unavailable") != std::string::npos ||
				status == "unavailable_runtime") {
				return "continue";
			}

			if (code.find("auth") != std::string::npos ||
				code.find("token") != std::string::npos ||
				code.find("credential") != std::string::npos) {
				return "stop";
			}

			const bool transient =
				status == "timeout" ||
				status == "temporary_error" ||
				status == "throttled" ||
				code == "timeout" ||
				code == "network_error" ||
				code == "transient";
			if (transient) {
				return "retry_then_continue";
			}

			return "stop";
		}

		bool IsSupportedArgMode(const std::string& argMode) {
			if (argMode.empty()) {
				return true;
			}

			static constexpr std::array<const char*, 3> kAllowedModes = {
				"raw",
				"text",
				"none",
			};

			return std::any_of(
				kAllowedModes.begin(),
				kAllowedModes.end(),
				[&](const char* mode) {
					return argMode == mode;
				});
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
			static const std::regex kAsciiDoubleQuote("\"([^\"]{1,160})\"");
			static const std::regex kChineseDoubleQuote(R"(“([^”]{1,160})”)");

			std::smatch match;
			if (std::regex_search(message, match, kChineseSingleQuote) && match.size() >= 2) {
				return match[1].str();
			}

			if (std::regex_search(message, match, kAsciiSingleQuote) && match.size() >= 2) {
				return match[1].str();
			}

			if (std::regex_search(message, match, kChineseDoubleQuote) && match.size() >= 2) {
				return match[1].str();
			}

			if (std::regex_search(message, match, kAsciiDoubleQuote) && match.size() >= 2) {
				return match[1].str();
			}

			return std::nullopt;
		}

		std::string NormalizeSearchQueryText(
			const std::string& input) {
			std::string normalized;
			normalized.reserve(input.size());

			bool previousWasSpace = true;
			for (const unsigned char rawCh : input) {
				if (rawCh < 0x20) {
					continue;
				}

				const char ch = static_cast<char>(rawCh);
				if (std::isspace(rawCh) != 0) {
					if (!previousWasSpace) {
						normalized.push_back(' ');
						previousWasSpace = true;
					}
					continue;
				}

				normalized.push_back(ch);
				previousWasSpace = false;
			}

			while (!normalized.empty() && normalized.front() == ' ') {
				normalized.erase(normalized.begin());
			}
			while (!normalized.empty() && normalized.back() == ' ') {
				normalized.pop_back();
			}

			return normalized;
		}

		std::optional<std::string> DeriveCompactSearchQuery(
			const std::string& source) {
			constexpr std::size_t kMaxQueryChars = 240;
			std::string normalized = NormalizeSearchQueryText(source);
			if (normalized.empty()) {
				return std::nullopt;
			}

			if (normalized.size() <= kMaxQueryChars) {
				return normalized;
			}

			std::string compact = normalized.substr(0, kMaxQueryChars);
			const auto lastSpace = compact.find_last_of(' ');
			if (lastSpace != std::string::npos && lastSpace > 40) {
				compact = compact.substr(0, lastSpace);
			}

			compact = NormalizeSearchQueryText(compact);
			if (compact.empty()) {
				return std::nullopt;
			}

			return compact;
		}

		std::optional<std::string> TryExtractEmailAddress(const std::string& text) {
			static const std::regex kEmailRegex(
				R"(([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}))");

			std::smatch match;
			if (std::regex_search(text, match, kEmailRegex) && match.size() >= 2) {
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
				const std::string sourceQuery = query.empty() ? runMessage : query;
				const auto compactQuery = DeriveCompactSearchQuery(sourceQuery);
				if (compactQuery.has_value()) {
					args["query"] = compactQuery.value();
				}
				args["topK"] = 3;
			}
			else if (loweredTool.find("smtp") != std::string::npos ||
				(loweredTool.find("email") != std::string::npos &&
					loweredTool.find("send") != std::string::npos)) {
				const std::string emailSource = runMessage + "\n" + lastOutput;
				if (const auto to = TryExtractEmailAddress(emailSource);
					to.has_value() && !to->empty()) {
					args["to"] = to.value();
				}

				args["subject"] = "Preview";
				args["body"] = lastOutput.empty() ? runMessage : lastOutput;
			}
			else if (loweredTool.find("summ") != std::string::npos) {
				args["text"] = lastOutput.empty() ? runMessage : lastOutput;
				args["maxChars"] = 100;
			}
			else if (loweredTool.find("humanizer") != std::string::npos ||
				loweredTool.find("rewrite") != std::string::npos) {
				args["text"] = lastOutput.empty() ? runMessage : lastOutput;
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

		bool IsSearchToolId(const std::string& toolName) {
			const std::string lowered = ToLowerCopy(toolName);
			return lowered.find("search") != std::string::npos ||
				lowered.find("brave") != std::string::npos;
		}

		bool IsSinkToolForAssistantOutput(const std::string& toolName) {
			const std::string lowered = ToLowerCopy(toolName);
			return lowered.find("smtp.send") != std::string::npos ||
				lowered.find("notion.write") != std::string::npos ||
				(lowered.find("email") != std::string::npos &&
					lowered.find("send") != std::string::npos);
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

			return plan;
		}

		std::vector<EmbeddedExecutionPlanStep> BuildExecutionPlan(
			const EmbeddedRuntimeExecutionRequest& request,
			const std::size_t maxSteps) {
			std::vector<std::string> toolPlan;
			if (request.enforceOrderedAllowlist &&
				!request.orderedAllowedToolTargets.empty()) {
				for (const auto& orderedTool : request.orderedAllowedToolTargets) {
					if (orderedTool.empty()) {
						continue;
					}

					if (std::find(toolPlan.begin(), toolPlan.end(), orderedTool) !=
						toolPlan.end()) {
						continue;
					}

					toolPlan.push_back(orderedTool);
					if (toolPlan.size() >= maxSteps) {
						break;
					}
				}
			}
			else {
				toolPlan = ResolveDynamicExecutionPlan(request, maxSteps);
			}

			std::vector<EmbeddedExecutionPlanStep> plan;
			plan.reserve(toolPlan.size());

			for (std::size_t index = 0; index < toolPlan.size(); ++index) {
				const std::string& toolName = toolPlan[index];
				const std::string stepLabel = request.enforceOrderedAllowlist
					? "ordered-step-" + std::to_string(index + 1)
					: "step-" + std::to_string(index + 1);
				plan.push_back(EmbeddedExecutionPlanStep{
					.index = index,
					.toolName = toolName,
					.argMode = ResolveBindingArgMode(request, toolName),
				   .stepLabel = stepLabel,
					});
			}

			return plan;
		}

		std::optional<EmbeddedPlanValidationFailure> ValidateExecutionPlan(
			const std::vector<EmbeddedExecutionPlanStep>& plan) {
			std::unordered_set<std::string> signatures;
			for (const auto& step : plan) {
				if (step.toolName.empty()) {
					return EmbeddedPlanValidationFailure{
						.reason = "plan_validation_failed",
						.errorCode = kErrorPlanEmptyTool,
						.errorMessage = "execution plan contains an empty tool reference",
					};
				}

				if (!IsSupportedArgMode(step.argMode)) {
					return EmbeddedPlanValidationFailure{
						.reason = "plan_validation_failed",
						.errorCode = kErrorInvalidArgMode,
						.errorMessage = "execution plan contains unsupported arg mode",
					};
				}

				const std::string signature = step.toolName + "::" + step.argMode;
				if (signatures.find(signature) != signatures.end()) {
					return EmbeddedPlanValidationFailure{
						.reason = "plan_validation_failed",
						.errorCode = kErrorDuplicateLoopRisk,
						.errorMessage = "execution plan contains duplicate tool-call signature risk",
					};
				}

				signatures.insert(signature);
			}

			return std::nullopt;
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

		void NormalizeTaskDeltaForContract(EmbeddedTaskDelta& delta) {
			if (delta.phase.empty()) {
				delta.phase = "unknown";
			}

			if (delta.status.empty()) {
				if (delta.phase == kPhasePlan) {
					delta.status = kStatusPlanned;
				}
				else if (delta.phase == kPhaseToolCall) {
					delta.status = kStatusRequested;
				}
				else if (delta.phase == kPhaseFinal) {
					delta.status = kStatusCompleted;
				}
				else {
					delta.status = kStatusRunning;
				}
			}

			if (delta.stepLabel.empty()) {
				delta.stepLabel = delta.phase;
			}

			if (delta.completedAtMs < delta.startedAtMs) {
				delta.completedAtMs = delta.startedAtMs;
			}
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

		const std::uint64_t startedAtMs = CurrentEpochMs();
		const std::string agentId =
			request.agentId.empty() ? "default" : request.agentId;
		std::size_t runOrdinal = 0;
		std::string runId;
		do {
			runId =
				"embedded-" +
				std::to_string(startedAtMs) +
				"-" +
				agentId +
				"-" +
				std::to_string(runOrdinal);
			++runOrdinal;
		} while (m_runsById.find(runId) != m_runsById.end());

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

		const std::vector<EmbeddedExecutionPlanStep> executionPlan =
			BuildExecutionPlan(request, kPlanningMaxSteps);

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
		const auto finalizeFailure =
			[this, &result, &queued, &appendDelta](
				const std::string& reason,
				const std::string& errorCode,
				const std::string& errorMessage) {
					const bool completed =
						CompleteRun(queued.runId, kStatusFailed, CurrentEpochMs());
					FinalizeExecutionResult(
						result,
						kStatusFailed,
						completed ? reason : std::string("embedded_completion_failed"),
						completed ? errorCode : std::string(kErrorCompletionFailed),
						completed ? errorMessage : std::string("embedded completion failed"),
						{});
					appendDelta(EmbeddedTaskDelta{
						.phase = kPhaseFinal,
						.resultJson = result.errorMessage,
						.status = kStatusFailed,
						.errorCode = result.errorCode,
						.stepLabel = "run_terminal",
						});
			};
		const auto finalizeCancelled =
			[this, &result, &queued, &appendDelta]() {
			const bool completed =
				CompleteRun(queued.runId, kStatusSkipped, CurrentEpochMs());
			FinalizeExecutionResult(
				result,
				kStatusSkipped,
				completed ? std::string("cancelled") : std::string("embedded_completion_failed"),
				completed ? std::string(kErrorRunCancelled) : std::string(kErrorCompletionFailed),
				completed ? std::string("embedded run cancelled") : std::string("embedded completion failed"),
				{});
			appendDelta(EmbeddedTaskDelta{
				.phase = kPhaseFinal,
				.resultJson = result.errorMessage,
				.status = kStatusSkipped,
				.errorCode = result.errorCode,
				.stepLabel = "run_terminal",
				});
			};
		const auto isCancelled =
			[this, &request, &queued]() {
			if (request.isCancellationRequested && request.isCancellationRequested()) {
				return true;
			}

			return IsRunCancelled(queued.runId);
			};
		const auto completeWithSnapshot =
			[this, &result, &queued]() {
			ClearRunCancellation(queued.runId);
			result.taskDeltas = GetTaskDeltas(queued.runId);
			};

		result.handled = true;
		if (executionPlan.size() > kMaxPolicySteps) {
			finalizeFailure(
				"max_steps_exceeded",
				kErrorMaxStepsExceeded,
				"decomposition exceeded maximum policy steps");
			completeWithSnapshot();
			return result;
		}

		if (const auto validationFailure = ValidateExecutionPlan(executionPlan);
			validationFailure.has_value()) {
			finalizeFailure(
				validationFailure->reason,
				validationFailure->errorCode,
				validationFailure->errorMessage);
			completeWithSnapshot();
			return result;
		}

		result.decompositionSteps = executionPlan.size();
		nlohmann::json planJson = nlohmann::json::array();
		for (const auto& step : executionPlan) {
			planJson.push_back(
				nlohmann::json{
					{ "index", step.index },
					{ "phase", kPhaseToolCall },
					{ "toolName", step.toolName },
					{ "argMode", step.argMode },
					{ "status", kStatusPlanned },
					{ "stepLabel", step.stepLabel },
				});
		}
		appendDelta(EmbeddedTaskDelta{
			.phase = kPhasePlan,
			.resultJson = planJson.dump(),
			.status = kStatusPlanned,
			.stepLabel = "execution_plan",
			});

		const std::string query =
			TryExtractQuoted(request.run.message).value_or(request.run.message);
		std::string lastOutput;
		std::string lastRenderableOutput;

		std::unordered_map<std::string, std::size_t> repeatCounts;
		for (const auto& step : executionPlan) {
			if (isCancelled()) {
				finalizeCancelled();
				completeWithSnapshot();
				return result;
			}

			const std::size_t stepIndex = step.index;
			const std::string& toolName = step.toolName;
			if (hasTimedOut(CurrentEpochMs())) {
				finalizeFailure(
					"deadline_exceeded",
					kErrorDeadlineExceeded,
					"embedded run exceeded timeout");
				completeWithSnapshot();
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
			if (IsSearchToolId(toolName)) {
				const auto queryIt = args.find("query");
				const bool hasQuery =
					queryIt != args.end() &&
					queryIt->is_string() &&
					!NormalizeSearchQueryText(queryIt->get<std::string>()).empty();
				if (!hasQuery) {
					finalizeFailure(
						"planner_invalid_search_query",
						kErrorInvalidArgs,
						"planner could not derive a safe query for search tool");
					completeWithSnapshot();
					return result;
				}
			}

			const std::string argMode = step.argMode;
			if (!ValidateArgsForArgMode(argMode, args)) {
				finalizeFailure(
					"invalid_args",
					kErrorInvalidArgs,
					"tool args do not satisfy binding arg mode");
				completeWithSnapshot();
				return result;
			}

			if (!IsAllowedRuntimeTool(request, toolName)) {
				finalizeFailure(
					"tool_blocked",
					kErrorToolBlocked,
					"tool is not enabled in runtime catalog");
				completeWithSnapshot();
				return result;
			}

			appendDelta(EmbeddedTaskDelta{
			   .phase = kPhaseToolCall,
				.toolName = toolName,
				.argsJson = args.dump(),
			  .status = kStatusRequested,
				.modelTurnId = "model-turn-" + std::to_string(stepIndex),
				.stepLabel = step.stepLabel,
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
				finalizeFailure(
					"loop_detected",
					kErrorLoopDetected,
					"repeated tool call signature exceeded limit");
				completeWithSnapshot();
				return result;
			}
			repeatCounts.insert_or_assign(callSignature, repeatCount + 1);

			blazeclaw::gateway::ToolExecuteResultV2 execution{};
			static constexpr std::size_t kMaxRetries = 2;
			for (std::size_t attempt = 0; attempt < kMaxRetries; ++attempt) {
				if (isCancelled()) {
					break;
				}

				if (hasTimedOut(CurrentEpochMs())) {
					execution = blazeclaw::gateway::ToolExecuteResultV2{
						.tool = toolName,
						.executed = false,
						.status = "timeout",
						.result = {},
						.errorCode = "timeout",
						.errorMessage = "embedded run exceeded timeout",
						.startedAtMs = CurrentEpochMs(),
						.completedAtMs = CurrentEpochMs(),
						.latencyMs = 0,
						.correlationId = correlationId,
					};
					break;
				}

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

			if (isCancelled()) {
				finalizeCancelled();
				completeWithSnapshot();
				return result;
			}

			if (hasTimedOut(CurrentEpochMs())) {
				finalizeFailure(
					"deadline_exceeded",
					kErrorDeadlineExceeded,
					"embedded run exceeded timeout");
				completeWithSnapshot();
				return result;
			}
			const std::string effectiveFailureStatus =
				(!execution.errorCode.empty() &&
					(execution.status.empty() || execution.status == "error"))
				? execution.errorCode
				: execution.status;
			result.assistantDeltas.push_back(
				"tools.execute.result tool=" +
				toolName +
				" status=" +
				effectiveFailureStatus);
			const std::string toolResultStatus =
				effectiveFailureStatus.empty()
				? (execution.executed ? kStatusCompleted : kStatusFailed)
				: effectiveFailureStatus;

			const std::string fallbackAction =
				ResolveFallbackActionForExecution(execution);
			appendDelta(EmbeddedTaskDelta{
				.phase = kPhaseToolResult,
				.toolName = toolName,
				.fallbackBackend = toolName,
				.fallbackAction = fallbackAction,
				.fallbackAttempt = repeatCount + 1,
				.fallbackMaxAttempts = kMaxRepeatCalls,
				.argsJson = args.dump(),
				.resultJson = execution.result,
				.status = toolResultStatus,
				.errorCode = execution.executed
					? std::string()
					: (execution.errorCode.empty()
						? std::string("not_executed")
						: execution.errorCode),
				.startedAtMs = execution.startedAtMs,
				.completedAtMs = execution.completedAtMs,
				.latencyMs = execution.latencyMs,
			 .modelTurnId = execution.correlationId,
				.stepLabel = step.stepLabel,
				});

			if (!execution.executed || execution.status == "error") {
				finalizeFailure(
					"tool_execution_failed",
					kErrorToolExecutionFailed,
					execution.errorMessage.empty()
					? "tool execution failed"
					: execution.errorMessage);
				completeWithSnapshot();
				return result;
			}

			lastOutput = execution.result;
			if (!execution.result.empty() && !IsSinkToolForAssistantOutput(toolName)) {
				lastRenderableOutput = execution.result;
			}
		}

		if (!CompleteRun(queued.runId, kStatusCompleted, CurrentEpochMs())) {
			finalizeFailure(
				"embedded_completion_failed",
				kErrorCompletionFailed,
				"embedded completion failed");
			completeWithSnapshot();
			return result;
		}
		FinalizeExecutionResult(
			result,
			kStatusCompleted,
			"orchestrated",
			{},
			{},
			(lastRenderableOutput.empty() ? lastOutput : lastRenderableOutput).empty()
			? "Embedded orchestration completed."
			: (lastRenderableOutput.empty() ? lastOutput : lastRenderableOutput));
		appendDelta(EmbeddedTaskDelta{
			 .phase = kPhaseFinal,
				.resultJson = result.assistantText,
				.status = kStatusCompleted,
				.stepLabel = "run_terminal",
			});
		completeWithSnapshot();
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

	void PiEmbeddedService::AbortRun(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_cancelMutex);
		m_cancelledRunIds.insert(runId);
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
		NormalizeTaskDeltaForContract(normalized);
		normalized.index = deltas.size();
		deltas.push_back(std::move(normalized));
	}

	bool PiEmbeddedService::IsRunCancelled(const std::string& runId) const {
		if (runId.empty()) {
			return false;
		}

		std::scoped_lock lock(m_cancelMutex);
		return m_cancelledRunIds.find(runId) != m_cancelledRunIds.end();
	}

	void PiEmbeddedService::ClearRunCancellation(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_cancelMutex);
		m_cancelledRunIds.erase(runId);
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

		if (!service.CompleteRun(first.runId, "completed", CurrentEpochMs())) {
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

		PiEmbeddedService maxStepService;
		maxStepService.Configure(cfg);
		const auto maxStepResult = maxStepService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "run step1 step2 step3 step4 step5 step6 step7",
					},
				.skillsPrompt =
					"Use step1 step2 step3 step4 step5 step6 step7 in strict order.",
				.toolBindings = {
					EmbeddedToolBinding{.commandName = "step1", .description = "s1", .toolName = "step1", .argMode = "raw" },
					EmbeddedToolBinding{.commandName = "step2", .description = "s2", .toolName = "step2", .argMode = "raw" },
					EmbeddedToolBinding{.commandName = "step3", .description = "s3", .toolName = "step3", .argMode = "raw" },
					EmbeddedToolBinding{.commandName = "step4", .description = "s4", .toolName = "step4", .argMode = "raw" },
					EmbeddedToolBinding{.commandName = "step5", .description = "s5", .toolName = "step5", .argMode = "raw" },
					EmbeddedToolBinding{.commandName = "step6", .description = "s6", .toolName = "step6", .argMode = "raw" },
					EmbeddedToolBinding{.commandName = "step7", .description = "s7", .toolName = "step7", .argMode = "raw" },
				},
				.runtimeTools = {
					blazeclaw::gateway::ToolCatalogEntry{.id = "step1", .label = "Step1", .category = "policy", .enabled = true },
					blazeclaw::gateway::ToolCatalogEntry{.id = "step2", .label = "Step2", .category = "policy", .enabled = true },
					blazeclaw::gateway::ToolCatalogEntry{.id = "step3", .label = "Step3", .category = "policy", .enabled = true },
					blazeclaw::gateway::ToolCatalogEntry{.id = "step4", .label = "Step4", .category = "policy", .enabled = true },
					blazeclaw::gateway::ToolCatalogEntry{.id = "step5", .label = "Step5", .category = "policy", .enabled = true },
					blazeclaw::gateway::ToolCatalogEntry{.id = "step6", .label = "Step6", .category = "policy", .enabled = true },
					blazeclaw::gateway::ToolCatalogEntry{.id = "step7", .label = "Step7", .category = "policy", .enabled = true },
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

		if (maxStepResult.success ||
			maxStepResult.errorCode != "embedded_max_steps_exceeded") {
			outError = L"Fixture validation failed: expected max-step policy failure.";
			return false;
		}

		PiEmbeddedService cancelledRunService;
		cancelledRunService.Configure(cfg);
		bool cancellationRequested = false;
		std::size_t cancelExecutions = 0;
		const auto cancelledResult = cancelledRunService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "cancel after first tool",
					},
				.skillsPrompt = "cancel.step1 cancel.step2",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "cancel.step1",
						.description = "cancel step1",
						.toolName = "cancel.step1",
						.argMode = "raw",
					},
					EmbeddedToolBinding{
						.commandName = "cancel.step2",
						.description = "cancel step2",
						.toolName = "cancel.step2",
						.argMode = "raw",
					},
				},
				.runtimeTools = {
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "cancel.step1",
						.label = "Cancel Step1",
						.category = "cancel",
						.enabled = true,
					},
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "cancel.step2",
						.label = "Cancel Step2",
						.category = "cancel",
						.enabled = true,
					},
				},
				.enableDynamicToolLoop = true,
				.toolExecutor = [&](const std::string& requestedTool,
					const std::optional<std::string>& argsJson) {
					++cancelExecutions;
					cancellationRequested = true;
					return blazeclaw::gateway::ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = argsJson.value_or("{}"),
					};
				},
				.isCancellationRequested = [&]() {
					return cancellationRequested;
				},
			});

		if (cancelledResult.success ||
			cancelledResult.errorCode != "embedded_run_cancelled" ||
			cancelExecutions != 1) {
			outError = L"Fixture validation failed: expected cancellation to stop execution with deterministic cancelled terminal state.";
			return false;
		}

		const bool hasCancelledTerminal = std::any_of(
			cancelledResult.taskDeltas.begin(),
			cancelledResult.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "final" &&
					delta.status == "skipped" &&
					delta.errorCode == "embedded_run_cancelled";
			});
		if (!hasCancelledTerminal) {
			outError = L"Fixture validation failed: expected cancellation final delta normalization.";
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

		const auto emailResultIt = std::find_if(
			opsResult.taskDeltas.begin(),
			opsResult.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "tool_result" &&
					delta.toolName == "email.schedule";
			});
		if (emailResultIt == opsResult.taskDeltas.end()) {
			outError = L"Fixture validation failed: expected email.schedule tool_result delta.";
			return false;
		}

		if (emailResultIt->fallbackBackend.empty() ||
			emailResultIt->fallbackAction.empty() ||
			emailResultIt->fallbackAttempt == 0 ||
			emailResultIt->fallbackMaxAttempts == 0) {
			outError = L"Fixture validation failed: expected fallback metadata in email.schedule tool_result delta.";
			return false;
		}

		const auto opsFinalIt = std::find_if(
			opsResult.taskDeltas.begin(),
			opsResult.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "final";
			});
		if (opsFinalIt == opsResult.taskDeltas.end() ||
			opsFinalIt->status.empty()) {
			outError = L"Fixture validation failed: expected non-empty terminal status in operational final delta.";
			return false;
		}

		if (opsResult.assistantText.find(
			"Email scheduling/sending result for jichengwhu@163.com with report content.") ==
			std::string::npos) {
			outError = L"Fixture validation failed: expected final email scheduling result evidence.";
			return false;
		}

		auto runMatrixCase = [&](const std::string& caseName,
			const std::vector<std::string>& toolsInOrder,
			const std::string& terminalText) -> bool {
				PiEmbeddedService matrixService;
				matrixService.Configure(cfg);

				std::vector<blazeclaw::gateway::ToolCatalogEntry> runtimeTools;
				runtimeTools.reserve(toolsInOrder.size());
				std::vector<EmbeddedToolBinding> bindings;
				bindings.reserve(toolsInOrder.size());
				std::string skillsPrompt = "Use tools in this order:";

				for (const auto& tool : toolsInOrder) {
					runtimeTools.push_back(
						blazeclaw::gateway::ToolCatalogEntry{
							.id = tool,
							.label = tool,
							.category = "matrix",
							.enabled = true,
						});
					bindings.push_back(
						EmbeddedToolBinding{
							.commandName = tool,
							.description = "matrix",
							.toolName = tool,
							.argMode = "raw",
						});
					skillsPrompt += " " + tool;
				}

				const auto matrixResult = matrixService.ExecuteRun(
					EmbeddedRuntimeExecutionRequest{
						.run = EmbeddedRunRequest{
							.sessionId = "main",
							.agentId = "default",
							.message = "matrix execution " + caseName,
							},
						.skillsPrompt = skillsPrompt,
						.toolBindings = std::move(bindings),
						.runtimeTools = std::move(runtimeTools),
						.enableDynamicToolLoop = true,
						.toolExecutor = [&](const std::string& requestedTool,
							const std::optional<std::string>&) {
							const bool isTerminal = requestedTool == toolsInOrder.back();
							return blazeclaw::gateway::ToolExecuteResult{
								.tool = requestedTool,
								.executed = true,
								.status = "ok",
								.output = isTerminal
									? terminalText
									: ("ok:" + requestedTool),
							};
						},
					});

				if (!matrixResult.success ||
					matrixResult.decompositionSteps < toolsInOrder.size()) {
					outError = L"Fixture validation failed: matrix " +
						std::wstring(caseName.begin(), caseName.end()) +
						L" expected decomposition steps.";
					return false;
				}

				std::size_t callIndex = 0;
				for (const auto& delta : matrixResult.taskDeltas) {
					if (delta.phase != "tool_call") {
						continue;
					}

					if (callIndex >= toolsInOrder.size() ||
						delta.toolName != toolsInOrder[callIndex]) {
						outError = L"Fixture validation failed: matrix " +
							std::wstring(caseName.begin(), caseName.end()) +
							L" expected ordered tool calls.";
						return false;
					}

					++callIndex;
				}

				if (callIndex < toolsInOrder.size()) {
					outError = L"Fixture validation failed: matrix " +
						std::wstring(caseName.begin(), caseName.end()) +
						L" expected full tool-call sequence.";
					return false;
				}

				if (matrixResult.assistantText.find(terminalText) == std::string::npos) {
					outError = L"Fixture validation failed: matrix " +
						std::wstring(caseName.begin(), caseName.end()) +
						L" expected terminal evidence text.";
					return false;
				}

				return true;
			};

		if (!runMatrixCase(
			"two-step",
			{ "matrix.fetch", "matrix.write" },
			"matrix-two-step-complete")) {
			return false;
		}

		if (!runMatrixCase(
			"three-step",
			{ "matrix.fetch", "matrix.summarize", "matrix.write" },
			"matrix-three-step-complete")) {
			return false;
		}

		if (!runMatrixCase(
			"four-step",
			{ "matrix.fetch", "matrix.transform", "matrix.validate", "matrix.publish" },
			"matrix-four-step-complete")) {
			return false;
		}

		PiEmbeddedService retryService;
		retryService.Configure(cfg);
		std::size_t transientAttempts = 0;
		const auto retryResult = retryService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "retry transient",
					},
				.skillsPrompt = "retry.fetch retry.finalize",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "retry.fetch",
						.description = "retry fetch",
						.toolName = "retry.fetch",
						.argMode = "raw",
					},
					EmbeddedToolBinding{
						.commandName = "retry.finalize",
						.description = "retry finalize",
						.toolName = "retry.finalize",
						.argMode = "raw",
					},
				},
				.runtimeTools = {
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "retry.fetch",
						.label = "Retry Fetch",
						.category = "retry",
						.enabled = true,
					},
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "retry.finalize",
						.label = "Retry Finalize",
						.category = "retry",
					 .enabled = true,
					},
				},
			   .enableDynamicToolLoop = true,
				.toolExecutorV2 = [&](const blazeclaw::gateway::ToolExecuteRequestV2& toolRequest) {
					if (toolRequest.tool == "retry.fetch") {
						++transientAttempts;
						if (transientAttempts == 1) {
							return blazeclaw::gateway::ToolExecuteResultV2{
								.tool = toolRequest.tool,
								.executed = false,
								.status = "timeout",
								.result = {},
								.errorCode = "timeout",
								.errorMessage = "temporary timeout",
								.startedAtMs = CurrentEpochMs(),
								.completedAtMs = CurrentEpochMs(),
								.latencyMs = 0,
								.correlationId = toolRequest.correlationId,
							};
						}

						return blazeclaw::gateway::ToolExecuteResultV2{
							.tool = toolRequest.tool,
							.executed = true,
							.status = "ok",
							.result = "retry.fetch.ok",
							.errorCode = {},
							.errorMessage = {},
							.startedAtMs = CurrentEpochMs(),
							.completedAtMs = CurrentEpochMs(),
							.latencyMs = 0,
							.correlationId = toolRequest.correlationId,
						};
					}

					return blazeclaw::gateway::ToolExecuteResultV2{
						.tool = toolRequest.tool,
						.executed = true,
						.status = "ok",
						.result = "retry-final-result",
						.errorCode = {},
						.errorMessage = {},
						.startedAtMs = CurrentEpochMs(),
						.completedAtMs = CurrentEpochMs(),
						.latencyMs = 0,
						.correlationId = toolRequest.correlationId,
					};
				},
			});

		if (!retryResult.success || transientAttempts < 2) {
			outError = L"Fixture validation failed: expected transient retry to recover.";
			return false;
		}

		PiEmbeddedService hardFailureService;
		hardFailureService.Configure(cfg);
		const auto hardFailureResult = hardFailureService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "hard failure",
					},
				.skillsPrompt = "failure.tool",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "failure.tool",
						.description = "failure",
						.toolName = "failure.tool",
						.argMode = "raw",
					},
				},
				.runtimeTools = {
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "failure.tool",
						.label = "Failure Tool",
						.category = "failure",
						.enabled = true,
					},
				},
				.enableDynamicToolLoop = true,
				.toolExecutor = [](const std::string& requestedTool,
					const std::optional<std::string>&) {
					return blazeclaw::gateway::ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "error",
						.output = "hard failure",
					};
				},
			});

		if (hardFailureResult.success ||
			hardFailureResult.errorCode != "embedded_tool_execution_failed") {
			outError = L"Fixture validation failed: expected deterministic hard failure code.";
			return false;
		}

		const bool hasHardFailureToolCall = std::any_of(
			hardFailureResult.taskDeltas.begin(),
			hardFailureResult.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "tool_call" &&
					delta.toolName == "failure.tool";
			});
		const bool hasHardFailureToolResult = std::any_of(
			hardFailureResult.taskDeltas.begin(),
			hardFailureResult.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "tool_result" &&
					delta.toolName == "failure.tool";
			});
		const bool hasHardFailureTerminal = std::any_of(
			hardFailureResult.taskDeltas.begin(),
			hardFailureResult.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "final" &&
					delta.status == "failed" &&
					delta.errorCode == "embedded_tool_execution_failed";
			});
		if (!hasHardFailureToolCall ||
			!hasHardFailureToolResult ||
			!hasHardFailureTerminal) {
			outError = L"Fixture validation failed: expected deterministic partial-failure task-delta chain with final terminal delta.";
			return false;
		}

		PiEmbeddedService approvalService;
		approvalService.Configure(cfg);
		const auto approvalResult = approvalService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "approval gated run",
					},
				.skillsPrompt = "approval.prepare approval.commit",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "approval.prepare",
						.description = "approval prepare",
						.toolName = "approval.prepare",
						.argMode = "raw",
					},
					EmbeddedToolBinding{
						.commandName = "approval.commit",
						.description = "approval commit",
						.toolName = "approval.commit",
						.argMode = "raw",
					},
				},
				.runtimeTools = {
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "approval.prepare",
						.label = "Approval Prepare",
						.category = "approval",
						.enabled = true,
					},
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "approval.commit",
						.label = "Approval Commit",
						.category = "approval",
						.enabled = true,
					},
				},
				.enableDynamicToolLoop = true,
				.toolExecutor = [](const std::string& requestedTool,
					const std::optional<std::string>&) {
					if (requestedTool == "approval.prepare") {
						return blazeclaw::gateway::ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "needs_approval",
							.output = "approval token issued",
						};
					}

					return blazeclaw::gateway::ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = "approval completed",
					};
				},
			});

		if (!approvalResult.success ||
			approvalResult.assistantText.find("approval completed") == std::string::npos) {
			outError = L"Fixture validation failed: expected approval-gated scenario to complete.";
			return false;
		}

		const bool hasNeedsApprovalDelta = std::any_of(
			approvalResult.taskDeltas.begin(),
			approvalResult.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "tool_result" && delta.status == "needs_approval";
			});
		if (!hasNeedsApprovalDelta) {
			outError = L"Fixture validation failed: expected needs_approval tool_result delta.";
			return false;
		}

		PiEmbeddedService legacyCompatService;
		legacyCompatService.Configure(cfg);
		const auto legacyCompatResult = legacyCompatService.ExecuteRun(
			EmbeddedRuntimeExecutionRequest{
				.run = EmbeddedRunRequest{
					.sessionId = "main",
					.agentId = "default",
					.message = "legacy compatibility",
					},
				.skillsPrompt = "legacy.tool",
				.toolBindings = {
					EmbeddedToolBinding{
						.commandName = "legacy.tool",
						.description = "legacy tool",
						.toolName = "legacy.tool",
						.argMode = {},
					},
				},
				.runtimeTools = {
					blazeclaw::gateway::ToolCatalogEntry{
						.id = "legacy.tool",
						.label = "Legacy Tool",
						.category = "compat",
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

		if (!legacyCompatResult.success || legacyCompatResult.taskDeltas.empty()) {
			outError = L"Fixture validation failed: expected legacy snapshot compatibility success.";
			return false;
		}

		const auto legacyToolCallIt = std::find_if(
			legacyCompatResult.taskDeltas.begin(),
			legacyCompatResult.taskDeltas.end(),
			[](const EmbeddedTaskDelta& delta) {
				return delta.phase == "tool_call" && delta.toolName == "legacy.tool";
			});
		if (legacyToolCallIt == legacyCompatResult.taskDeltas.end() ||
			legacyToolCallIt->argsJson.empty()) {
			outError = L"Fixture validation failed: expected legacy tool_call args to be generated.";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core
