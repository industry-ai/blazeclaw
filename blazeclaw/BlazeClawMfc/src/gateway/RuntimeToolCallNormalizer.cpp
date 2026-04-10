#include "pch.h"
#include "RuntimeToolCallNormalizer.h"

#include "GatewayJsonUtils.h"

#include <algorithm>
#include <chrono>
#include <regex>

namespace blazeclaw::gateway {
	namespace {
		std::string QuoteJson(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);
			for (const char ch : value) {
				switch (ch) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}

			return "\"" + escaped + "\"";
		}

		std::string ToLowerCopyNormalizer(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		std::uint64_t CurrentEpochMsNormalizer() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		GatewayHost::ChatRuntimeResult::TaskDeltaEntry NormalizeTaskDeltaEntry(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& source,
			const std::string& runId,
			const std::string& sessionKey,
			const std::size_t defaultIndex) {
			GatewayHost::ChatRuntimeResult::TaskDeltaEntry normalized = source;
			normalized.index = source.index == 0 && defaultIndex > 0
				? defaultIndex
				: source.index;
			if (normalized.runId.empty()) {
				normalized.runId = runId;
			}

			if (normalized.sessionId.empty()) {
				normalized.sessionId = sessionKey;
			}

			if (normalized.phase.empty()) {
				normalized.phase = "unknown";
			}

			if (normalized.status.empty()) {
				normalized.status = normalized.phase == "final"
					? "completed"
					: "running";
			}

			if (normalized.stepLabel.empty()) {
				normalized.stepLabel = normalized.phase;
			}

			if (normalized.startedAtMs == 0) {
				normalized.startedAtMs = CurrentEpochMsNormalizer();
			}

			if (normalized.completedAtMs == 0 ||
				normalized.completedAtMs < normalized.startedAtMs) {
				normalized.completedAtMs = normalized.startedAtMs;
			}

			normalized.latencyMs =
				normalized.completedAtMs - normalized.startedAtMs;
			return normalized;
		}

		bool IsInvalidArgumentsResult(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
			if (delta.phase != "tool_result") {
				return false;
			}

			const std::string status = ToLowerCopyNormalizer(delta.status);
			const std::string errorCode = ToLowerCopyNormalizer(delta.errorCode);
			return status == "invalid_arguments" ||
				status == "invalid_args" ||
				errorCode == "invalid_arguments" ||
				errorCode == "invalid_args";
		}

		bool EndsWithLocal(const std::string& value, const std::string& suffix) {
			if (value.size() < suffix.size()) {
				return false;
			}

			return value.compare(
				value.size() - suffix.size(),
				suffix.size(),
				suffix) == 0;
		}

		std::optional<std::string> DeriveCompactSearchQueryLocal(
			const std::string& source) {
			constexpr std::size_t kMaxQueryChars = 240;
			std::string normalized;
			normalized.reserve(source.size());
			bool previousWasSpace = true;
			for (const unsigned char rawCh : source) {
				if (rawCh < 0x20) {
					continue;
				}

				if (std::isspace(rawCh) != 0) {
					if (!previousWasSpace) {
						normalized.push_back(' ');
						previousWasSpace = true;
					}
					continue;
				}

				normalized.push_back(static_cast<char>(rawCh));
				previousWasSpace = false;
			}

			normalized = json::Trim(normalized);
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

			compact = json::Trim(compact);
			if (compact.empty()) {
				return std::nullopt;
			}

			return compact;
		}

		std::string ExtractFirstEmailAddress(const std::string& text) {
			static const std::regex kEmailRegex(
				R"(([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}))");

			std::smatch match;
			if (std::regex_search(text, match, kEmailRegex) && !match.empty()) {
				return match[1].str();
			}

			return {};
		}

		std::string ExtractFirstHttpUrl(const std::string& text) {
			static const std::regex kHttpRegex(
				R"((https?://[^\s\)\]\>\"]+))",
				std::regex_constants::icase);
			std::smatch match;
			if (std::regex_search(text, match, kHttpRegex) && !match.empty()) {
				return match[1].str();
			}

			return {};
		}

		bool TryBuildRecoveredArgsJson(
			const std::string& toolId,
			const std::string& message,
			std::string& outArgsJson) {
			outArgsJson.clear();
			const std::string trimmedMessage = json::Trim(message);
			if (trimmedMessage.empty()) {
				return false;
			}

			const std::string lowerTool = ToLowerCopyNormalizer(toolId);
			if (EndsWithLocal(lowerTool, ".search.web")) {
				const auto compactQuery = DeriveCompactSearchQueryLocal(trimmedMessage);
				if (!compactQuery.has_value()) {
					return false;
				}

				outArgsJson =
					"{\"query\":" + QuoteJson(compactQuery.value()) +
					",\"count\":5}";
				return true;
			}

			if (EndsWithLocal(lowerTool, ".fetch.content")) {
				const std::string firstUrl = ExtractFirstHttpUrl(trimmedMessage);
				if (firstUrl.empty()) {
					return false;
				}

				outArgsJson = "{\"url\":" + QuoteJson(firstUrl) + "}";
				return true;
			}

			if (EndsWithLocal(lowerTool, ".smtp.send")) {
				const std::string recipient = ExtractFirstEmailAddress(trimmedMessage);
				if (recipient.empty()) {
					return false;
				}

				outArgsJson =
					"{\"to\":" + QuoteJson(recipient) +
					",\"subject\":\"Preview\",\"body\":" +
					QuoteJson(trimmedMessage) + "}";
				return true;
			}

			return false;
		}

		std::string SkillNamespaceOfToolId(const std::string& toolId) {
			const auto dot = toolId.find('.');
			if (dot == std::string::npos || dot == 0) {
				return {};
			}

			return toolId.substr(0, dot);
		}

		int IntentSimilarityScore(
			const std::string& loweredMessage,
			const ToolCatalogEntry& tool,
			const std::string& referenceCategory,
			const std::string& referenceNamespace) {
			int score = 0;
			const std::string toolIdLower = ToLowerCopyNormalizer(tool.id);
			const std::string toolLabelLower = ToLowerCopyNormalizer(tool.label);
			const std::string toolCategoryLower = ToLowerCopyNormalizer(tool.category);

			if (!referenceCategory.empty() &&
				toolCategoryLower == referenceCategory) {
				score += 2;
			}

			if (!referenceNamespace.empty() &&
				toolIdLower.rfind(referenceNamespace + ".", 0) == 0) {
				score += 3;
			}

			if (loweredMessage.find("search") != std::string::npos &&
				toolIdLower.find("search") != std::string::npos) {
				score += 2;
			}

			if ((loweredMessage.find("email") != std::string::npos ||
				loweredMessage.find("mail") != std::string::npos) &&
				(toolIdLower.find("smtp") != std::string::npos ||
					toolIdLower.find("imap") != std::string::npos ||
					toolLabelLower.find("email") != std::string::npos)) {
				score += 2;
			}

			if (loweredMessage.find("fetch") != std::string::npos &&
				toolIdLower.find("fetch") != std::string::npos) {
				score += 1;
			}

			return score;
		}
	}

	std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
		RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
			const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas,
			const std::string& runId,
			const std::string& sessionKey,
			const bool success,
			const std::string& assistantText,
			const std::string& errorCode,
			const std::string& errorMessage) {
		if (!taskDeltas.empty()) {
			return taskDeltas;
		}

		const std::uint64_t nowMs = CurrentEpochMsNormalizer();
		return std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>{
			GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
				.index = 0,
				.runId = runId,
				.sessionId = sessionKey,
				.phase = "plan",
				.status = "ok",
				.startedAtMs = nowMs,
				.completedAtMs = nowMs,
				.latencyMs = 0,
				.stepLabel = "execution_plan",
			},
			GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
				.index = 1,
				.runId = runId,
				.sessionId = sessionKey,
				.phase = "final",
				.resultJson = success ? assistantText : errorMessage,
				.status = success ? "completed" : "failed",
				.errorCode = success ? std::string() : errorCode,
				.startedAtMs = nowMs,
				.completedAtMs = nowMs,
				.latencyMs = 0,
				.stepLabel = "run_terminal",
			},
		};
	}

	std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
		RuntimeToolCallNormalizer::ApplyInvalidArgumentsRecoveryPolicy(
			const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& source,
			const std::string& runId,
			const std::string& sessionKey,
			const std::string& message,
			GatewayToolRegistry& toolRegistry) {
		if (source.empty()) {
			return source;
		}

		auto recovered = source;
		auto invalidIt = std::find_if(
			recovered.begin(),
			recovered.end(),
			[](const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
				return IsInvalidArgumentsResult(delta);
			});
		if (invalidIt == recovered.end() || invalidIt->toolName.empty()) {
			return recovered;
		}

		const auto allTools = toolRegistry.List();
		const std::string failedTool = invalidIt->toolName;
		const std::string failedToolLower = ToLowerCopyNormalizer(failedTool);
		const std::string failedNamespace = SkillNamespaceOfToolId(failedToolLower);
		std::string failedCategory;
		for (const auto& tool : allTools) {
			if (ToLowerCopyNormalizer(tool.id) == failedToolLower) {
				failedCategory = ToLowerCopyNormalizer(tool.category);
				break;
			}
		}

		auto appendAttempt =
			[&](const std::string& toolId,
				const std::string& action,
				const std::size_t attempt,
				const std::size_t maxAttempts,
				const std::string& argsJson,
				const ToolExecuteResult& execution) {
					const std::uint64_t nowMs = CurrentEpochMsNormalizer();
					recovered.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
						.index = recovered.size(),
						.runId = runId,
						.sessionId = sessionKey,
						.phase = "tool_call",
						.toolName = toolId,
						.fallbackAction = action,
						.fallbackAttempt = attempt,
						.fallbackMaxAttempts = maxAttempts,
						.argsJson = argsJson,
						.status = "requested",
						.startedAtMs = nowMs,
						.completedAtMs = nowMs,
						.latencyMs = 0,
						.stepLabel = "tool_retry_request",
						});

					const std::string resultStatus = execution.status.empty()
						? (execution.executed ? "ok" : "error")
						: execution.status;
					const std::string resultErrorCode =
						ToLowerCopyNormalizer(resultStatus) == "ok"
						? std::string{}
						: (ToLowerCopyNormalizer(resultStatus) == "invalid_args"
							? std::string("invalid_arguments")
							: resultStatus);
					recovered.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
						.index = recovered.size(),
						.runId = runId,
						.sessionId = sessionKey,
						.phase = "tool_result",
						.toolName = toolId,
						.fallbackAction = action,
						.fallbackAttempt = attempt,
						.fallbackMaxAttempts = maxAttempts,
						.argsJson = argsJson,
						.resultJson = execution.output,
						.status = resultStatus,
						.errorCode = resultErrorCode,
						.startedAtMs = nowMs,
						.completedAtMs = nowMs,
						.latencyMs = 0,
						.stepLabel = "tool_retry_result",
						});
			};

		auto tryExecuteRecovered =
			[&](const std::string& toolId,
				const std::string& action,
				const std::size_t attempt,
				const std::size_t maxAttempts) {
					std::string rebuiltArgsJson;
					if (!TryBuildRecoveredArgsJson(toolId, message, rebuiltArgsJson)) {
						return false;
					}

					const ToolExecuteResult execution = toolRegistry.Execute(
						toolId,
						rebuiltArgsJson);
					appendAttempt(
						toolId,
						action,
						attempt,
						maxAttempts,
						rebuiltArgsJson,
						execution);

					const std::string statusLower = ToLowerCopyNormalizer(execution.status);
					return execution.executed &&
						statusLower != "error" &&
						statusLower != "invalid_args" &&
						statusLower != "invalid_arguments";
			};

		if (tryExecuteRecovered(
			failedTool,
			"args_rebuild_retry",
			1,
			1)) {
			return recovered;
		}

		std::size_t sameSkillAttempt = 1;
		for (const auto& tool : allTools) {
			const std::string toolIdLower = ToLowerCopyNormalizer(tool.id);
			if (!failedNamespace.empty() &&
				toolIdLower.rfind(failedNamespace + ".", 0) != 0) {
				continue;
			}

			if (toolIdLower == failedToolLower || !tool.enabled) {
				continue;
			}

			if (tryExecuteRecovered(
				tool.id,
				"same_skill_candidate_retry",
				sameSkillAttempt,
				2)) {
				return recovered;
			}

			++sameSkillAttempt;
			if (sameSkillAttempt > 2) {
				break;
			}
		}

		const std::string loweredMessage = ToLowerCopyNormalizer(message);
		const ToolCatalogEntry* bestCandidate = nullptr;
		int bestScore = 0;
		for (const auto& tool : allTools) {
			const std::string toolIdLower = ToLowerCopyNormalizer(tool.id);
			if (!tool.enabled || toolIdLower == failedToolLower) {
				continue;
			}

			std::string candidateArgs;
			if (!TryBuildRecoveredArgsJson(tool.id, message, candidateArgs)) {
				continue;
			}

			const int score = IntentSimilarityScore(
				loweredMessage,
				tool,
				failedCategory,
				failedNamespace);
			if (score > bestScore) {
				bestScore = score;
				bestCandidate = &tool;
			}
		}

		if (bestCandidate != nullptr && bestScore > 0) {
			(void)tryExecuteRecovered(
				bestCandidate->id,
				"cross_skill_guarded_retry",
				1,
				1);
		}
		else {
			const std::uint64_t nowMs = CurrentEpochMsNormalizer();
			recovered.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
				.index = recovered.size(),
				.runId = runId,
				.sessionId = sessionKey,
				.phase = "fallback",
				.toolName = failedTool,
				.fallbackAction = "cross_skill_guarded_retry",
				.status = "skipped",
				.errorCode = "cross_skill_fallback_not_eligible",
				.startedAtMs = nowMs,
				.completedAtMs = nowMs,
				.latencyMs = 0,
				.stepLabel = "fallback_gate",
				});
		}

		for (std::size_t i = 0; i < recovered.size(); ++i) {
			recovered[i] = NormalizeTaskDeltaEntry(
				recovered[i],
				runId,
				sessionKey,
				i);
		}

		return recovered;
	}

} // namespace blazeclaw::gateway
