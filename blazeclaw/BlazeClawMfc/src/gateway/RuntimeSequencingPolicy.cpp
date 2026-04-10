#include "pch.h"
#include "RuntimeSequencingPolicy.h"

#include "GatewayJsonUtils.h"

#include <algorithm>
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

		std::string SerializeStringArrayPolicy(
			const std::vector<std::string>& values) {
			std::string json = "[";
			for (std::size_t i = 0; i < values.size(); ++i) {
				if (i > 0) {
					json += ",";
				}

				json += QuoteJson(values[i]);
			}

			json += "]";
			return json;
		}

		std::string ToLowerCopyPolicy(const std::string& value) {
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

		std::string NormalizeOrderedTargetToken(const std::string& token) {
			std::string normalized = json::Trim(token);
			while (!normalized.empty() &&
				(normalized.back() == '.' ||
					normalized.back() == ';' ||
					normalized.back() == ',' ||
					normalized.back() == ':' ||
					normalized.back() == ')' ||
					normalized.back() == '"')) {
				normalized.pop_back();
			}

			while (!normalized.empty() &&
				(normalized.front() == '(' ||
					normalized.front() == '"')) {
				normalized.erase(normalized.begin());
			}

			return ToLowerCopyPolicy(normalized);
		}

		std::string ResolvePreferredToolForNamespace(
			const std::string& normalizedNamespace,
			const std::vector<ToolCatalogEntry>& tools) {
			if (normalizedNamespace.empty()) {
				return {};
			}

			const std::string preferredSendId =
				normalizedNamespace + ".smtp.send";
			for (const auto& tool : tools) {
				if (ToLowerCopyPolicy(tool.id) == preferredSendId) {
					return tool.id;
				}
			}

			const std::string preferredSearchId =
				normalizedNamespace + ".search.web";
			for (const auto& tool : tools) {
				if (ToLowerCopyPolicy(tool.id) == preferredSearchId) {
					return tool.id;
				}
			}

			for (const auto& tool : tools) {
				const std::string toolIdLower = ToLowerCopyPolicy(tool.id);
				if (toolIdLower.rfind(normalizedNamespace + ".", 0) == 0) {
					return tool.id;
				}
			}

			return {};
		}

		std::vector<std::string> ExtractOrderedTargetsFromPrompt(
			const std::string& message,
			std::vector<std::string>* explicitCallTargets) {
			std::vector<std::string> targets;
			std::vector<std::string> explicitTargets;

			auto addTarget = [&targets](const std::string& candidate) {
				const std::string normalized =
					NormalizeOrderedTargetToken(candidate);
				if (normalized.empty()) {
					return;
				}

				if (std::find(targets.begin(), targets.end(), normalized) !=
					targets.end()) {
					return;
				}

				targets.push_back(normalized);
				};

			auto addExplicitTarget = [&explicitTargets](const std::string& candidate) {
				const std::string normalized =
					NormalizeOrderedTargetToken(candidate);
				if (normalized.empty()) {
					return;
				}

				if (std::find(
					explicitTargets.begin(),
					explicitTargets.end(),
					normalized) != explicitTargets.end()) {
					return;
				}

				explicitTargets.push_back(normalized);
				};

			const std::regex backtickTargetRegex(
				R"(`([A-Za-z0-9._-]+)`)",
				std::regex_constants::icase);
			for (std::sregex_iterator it(message.begin(), message.end(), backtickTargetRegex), end;
				it != end;
				++it) {
				if (it->size() >= 2) {
					const std::string target = (*it)[1].str();
					addExplicitTarget(target);
					addTarget(target);
				}
			}

			const std::regex callTargetRegex(
				R"(\bcall\s+([A-Za-z0-9._-]+))",
				std::regex_constants::icase);
			for (std::sregex_iterator it(message.begin(), message.end(), callTargetRegex), end;
				it != end;
				++it) {
				if (it->size() >= 2) {
					const std::string target = (*it)[1].str();
					addExplicitTarget(target);
					addTarget(target);
				}
			}

			const std::regex numberedStepTargetRegex(
				R"((?:^|\n|\r|;|\xEF\xBC\x9B)\s*(?:step\s*)?\d+\s*[\)\.:\-]\s*([A-Za-z0-9._-]+))",
				std::regex_constants::icase);
			for (std::sregex_iterator it(message.begin(), message.end(), numberedStepTargetRegex), end;
				it != end;
				++it) {
				if (it->size() >= 2) {
					addTarget((*it)[1].str());
				}
			}

			if (message.find("->") != std::string::npos) {
				std::size_t cursor = 0;
				while (cursor < message.size()) {
					const std::size_t arrow = message.find("->", cursor);
					if (arrow == std::string::npos) {
						break;
					}

					const std::size_t leftBoundary =
						message.rfind(' ', arrow) == std::string::npos
						? 0
						: message.rfind(' ', arrow) + 1;
					const std::size_t rightBoundary =
						message.find_first_of(" \n\r\t", arrow + 2);
					const std::size_t rightEnd = rightBoundary == std::string::npos
						? message.size()
						: rightBoundary;

					if (arrow > leftBoundary) {
						addTarget(message.substr(leftBoundary, arrow - leftBoundary));
					}
					if (rightEnd > arrow + 2) {
						addTarget(message.substr(arrow + 2, rightEnd - (arrow + 2)));
					}

					cursor = arrow + 2;
				}
			}

			if (explicitCallTargets != nullptr) {
				*explicitCallTargets = explicitTargets;
			}

			return targets;
		}

		bool HasStructuralSequenceSignal(const std::string& message) {
			const std::regex backtickTargetRegex(
				R"(`([A-Za-z0-9._-]+)`)",
				std::regex_constants::icase);
			std::size_t backtickTargetCount = 0;
			for (std::sregex_iterator it(message.begin(), message.end(), backtickTargetRegex), end;
				it != end;
				++it) {
				++backtickTargetCount;
				if (backtickTargetCount >= 2) {
					return true;
				}
			}

			if (message.find("->") != std::string::npos) {
				return true;
			}

			const std::regex numberedStepRegex(
				R"((?:^|\n|\r|;|\xEF\xBC\x9B)\s*(?:step\s*)?\d+\s*[\)\.:\-])",
				std::regex_constants::icase);
			std::size_t numberedStepCount = 0;
			for (std::sregex_iterator it(message.begin(), message.end(), numberedStepRegex), end;
				it != end;
				++it) {
				++numberedStepCount;
				if (numberedStepCount >= 2) {
					return true;
				}
			}

			const std::regex callDirectiveRegex(
				R"(\bcall\s+[A-Za-z0-9._-]+)",
				std::regex_constants::icase);
			std::size_t callDirectiveCount = 0;
			for (std::sregex_iterator it(message.begin(), message.end(), callDirectiveRegex), end;
				it != end;
				++it) {
				++callDirectiveCount;
				if (callDirectiveCount >= 2) {
					return true;
				}
			}

			return false;
		}

		std::string ResolveOrderedTargetToToolId(
			const std::string& target,
			const std::vector<ToolCatalogEntry>& tools,
			const std::vector<SkillsCatalogGatewayEntry>& skillsCatalogEntries) {
			if (target.empty()) {
				return {};
			}

			const std::string normalizedTarget = ToLowerCopyPolicy(target);
			std::string normalizedNamespace = normalizedTarget;
			std::replace(
				normalizedNamespace.begin(),
				normalizedNamespace.end(),
				'-',
				'_');

			for (const auto& tool : tools) {
				const std::string toolIdLower = ToLowerCopyPolicy(tool.id);
				if (toolIdLower == normalizedTarget) {
					return tool.id;
				}

				if (toolIdLower == normalizedTarget + ".search.web") {
					return tool.id;
				}

				if (toolIdLower == normalizedTarget + ".smtp.send") {
					return tool.id;
				}
			}

			const std::string preferredByNamespace =
				ResolvePreferredToolForNamespace(normalizedNamespace, tools);
			if (!preferredByNamespace.empty()) {
				return preferredByNamespace;
			}

			for (const auto& entry : skillsCatalogEntries) {
				const std::string nameLower = ToLowerCopyPolicy(entry.name);
				const std::string keyLower = ToLowerCopyPolicy(entry.skillKey);
				const std::string commandLower = ToLowerCopyPolicy(entry.commandName);
				const std::string commandToolLower = ToLowerCopyPolicy(entry.commandToolName);

				if (nameLower == normalizedTarget ||
					keyLower == normalizedTarget ||
					commandLower == normalizedTarget ||
					commandToolLower == normalizedTarget) {
					if (!entry.commandToolName.empty()) {
						return entry.commandToolName;
					}

					std::string entryNamespace = keyLower.empty()
						? nameLower
						: keyLower;
					std::replace(
						entryNamespace.begin(),
						entryNamespace.end(),
						'-',
						'_');
					const std::string resolvedBySkill =
						ResolvePreferredToolForNamespace(entryNamespace, tools);
					if (!resolvedBySkill.empty()) {
						return resolvedBySkill;
					}

					const bool modelInvocationAllowed =
						entry.eligible &&
						!entry.disabled &&
						!entry.blockedByAllowlist &&
						!entry.disableModelInvocation;
					if (modelInvocationAllowed) {
						return std::string("model_skill.") +
							(entry.skillKey.empty() ? entry.name : entry.skillKey);
					}

					return {};
				}
			}

			return {};
		}

		std::uint64_t CurrentEpochMsPolicy() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}
	}

	OrderedSequencePreflight RuntimeSequencingPolicy::BuildOrderedSequencePreflight(
		const std::string& message,
		const std::vector<ToolCatalogEntry>& tools,
		const std::vector<SkillsCatalogGatewayEntry>& skillsCatalogEntries) {
		OrderedSequencePreflight preflight;
		std::vector<std::string> inferredTargets;
		preflight.explicitCallTargets.clear();
		inferredTargets = ExtractOrderedTargetsFromPrompt(
			message,
			&preflight.explicitCallTargets);

		if (!preflight.explicitCallTargets.empty()) {
			preflight.orderedTargets = preflight.explicitCallTargets;
			preflight.strictAllowlist = true;
			preflight.enforced = true;
		}
		else {
			if (!HasStructuralSequenceSignal(message)) {
				return preflight;
			}

			preflight.orderedTargets = std::move(inferredTargets);
			preflight.strictAllowlist = false;
			preflight.enforced = preflight.orderedTargets.size() >= 2;
		}

		preflight.resolvedToolTargets.reserve(preflight.orderedTargets.size());
		if (!preflight.enforced) {
			return preflight;
		}

		for (const auto& target : preflight.orderedTargets) {
			const std::string resolvedTool = ResolveOrderedTargetToToolId(
				target,
				tools,
				skillsCatalogEntries);
			preflight.resolvedToolTargets.push_back(resolvedTool);
			if (resolvedTool.empty()) {
				preflight.missingTargets.push_back(target);
			}
		}

		return preflight;
	}

	bool RuntimeSequencingPolicy::IsResolvedRuntimeToolTarget(
		const std::string& resolvedToolId,
		const std::vector<ToolCatalogEntry>& runtimeTools) {
		if (resolvedToolId.empty()) {
			return false;
		}

		const std::string lowered = ToLowerCopyPolicy(resolvedToolId);
		if (lowered.rfind("model_skill.", 0) == 0) {
			return false;
		}

		for (const auto& tool : runtimeTools) {
			if (!tool.enabled) {
				continue;
			}

			if (ToLowerCopyPolicy(tool.id) == lowered) {
				return true;
			}
		}

		return false;
	}

	std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
		RuntimeSequencingPolicy::BuildOrderedPreflightTaskDeltas(
			const std::string& runId,
			const std::string& sessionKey,
			const OrderedSequencePreflight& preflight,
			const bool terminalFailure,
			const std::string& terminalErrorCode,
			const std::string& terminalErrorMessage) {
		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> taskDeltas;
		if (!preflight.enforced) {
			return taskDeltas;
		}

		const std::uint64_t baseMs = CurrentEpochMsPolicy();
		taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
			.index = taskDeltas.size(),
			.runId = runId,
			.sessionId = sessionKey,
			.phase = "plan",
		 .resultJson = SerializeStringArrayPolicy(preflight.orderedTargets),
			.status = "ok",
			.startedAtMs = baseMs,
			.completedAtMs = baseMs,
			.latencyMs = 0,
			.stepLabel = "ordered_execution_plan",
			});

		for (std::size_t index = 0; index < preflight.orderedTargets.size(); ++index) {
			const std::string& target = preflight.orderedTargets[index];
			const std::string resolvedTarget =
				index < preflight.resolvedToolTargets.size() &&
				!preflight.resolvedToolTargets[index].empty()
				? preflight.resolvedToolTargets[index]
				: target;
			const bool missing = std::find(
				preflight.missingTargets.begin(),
				preflight.missingTargets.end(),
				target) != preflight.missingTargets.end();

			taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
				.index = taskDeltas.size(),
				.runId = runId,
				.sessionId = sessionKey,
				.phase = "preflight",
				.toolName = resolvedTarget,
				.argsJson = target,
				.status = missing ? "missing" : "ok",
				.errorCode = missing ? "step_target_unavailable" : std::string(),
				.startedAtMs = baseMs + index + 1,
				.completedAtMs = baseMs + index + 1,
				.latencyMs = 0,
				.stepLabel = "ordered_step_precheck",
				});
		}

		if (terminalFailure) {
			taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
				.index = taskDeltas.size(),
				.runId = runId,
				.sessionId = sessionKey,
				.phase = "final",
				.resultJson = terminalErrorMessage,
				.status = "failed",
				.errorCode = terminalErrorCode,
				.startedAtMs = baseMs + preflight.orderedTargets.size() + 1,
				.completedAtMs = baseMs + preflight.orderedTargets.size() + 1,
				.latencyMs = 0,
				.stepLabel = "run_terminal",
				});
		}

		return taskDeltas;
	}

	std::string RuntimeSequencingPolicy::JoinOrderedTargets(
		const std::vector<std::string>& targets) {
		std::string joined;
		for (std::size_t i = 0; i < targets.size(); ++i) {
			if (i > 0) {
				joined += ", ";
			}

			joined += targets[i];
		}

		return joined;
	}

	std::string RuntimeSequencingPolicy::JoinOrderedResolution(
		const OrderedSequencePreflight& preflight) {
		std::string joined;
		for (std::size_t i = 0; i < preflight.orderedTargets.size(); ++i) {
			if (i > 0) {
				joined += ", ";
			}

			const std::string requested = preflight.orderedTargets[i];
			const std::string resolved = i < preflight.resolvedToolTargets.size()
				? preflight.resolvedToolTargets[i]
				: std::string{};
			if (!resolved.empty() && resolved != requested) {
				joined += requested + "=>" + resolved;
			}
			else {
				joined += requested;
			}
		}

		return joined;
	}

} // namespace blazeclaw::gateway
