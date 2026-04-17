#pragma once

#include "../config/ConfigModels.h"
#include "../gateway/GatewayHost.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	class ChatRuntimeOrchestrationCoordinator {
	public:
		struct PreparedChatRequest {
			std::string commandBodyForInline;
			bool shouldLoadInlineSkillCommands = false;
			std::string sessionId;
			std::optional<std::string> resolvedSkillInvocationToolTarget;
			std::optional<std::string> rewrittenSkillPromptMessage;
			std::string baseAgentMessage;
			std::string inboundMessageForAgent;
			std::vector<std::string> orderedAllowedTargets;
		};

		using ShouldLoadSkillCommandsFn = std::function<bool(
			bool allowTextCommands,
			const std::string& commandBodyNormalized)>;
		using ResolveSkillInvocationToolTargetFn =
			std::function<std::optional<std::string>(
				const std::string& commandBodyNormalized)>;
		using ResolveSkillInvocationPromptRewriteFn =
			std::function<std::optional<std::string>(
				const std::string& commandBodyNormalized)>;
		using BuildOrderedAllowedToolTargetsFn =
			std::function<std::vector<std::string>(
				const std::vector<std::string>& requestedTargets,
				const std::optional<std::string>& resolvedTarget)>;

		[[nodiscard]] PreparedChatRequest PrepareChatRequest(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
			const ShouldLoadSkillCommandsFn& shouldLoadSkillCommands,
			const ResolveSkillInvocationToolTargetFn& resolveSkillInvocationToolTarget,
			const ResolveSkillInvocationPromptRewriteFn&
			resolveSkillInvocationPromptRewrite,
			const BuildOrderedAllowedToolTargetsFn& buildOrderedAllowedToolTargets) const {
			PreparedChatRequest prepared;
			prepared.commandBodyForInline = !request.bodyForCommands.empty()
				? request.bodyForCommands
				: request.message;
			prepared.shouldLoadInlineSkillCommands =
				request.shouldLoadInlineSkillCommands ||
				shouldLoadSkillCommands(true, prepared.commandBodyForInline);
			prepared.sessionId =
				request.sessionKey.empty() ? "main" : request.sessionKey;
			prepared.resolvedSkillInvocationToolTarget =
				prepared.shouldLoadInlineSkillCommands
				? resolveSkillInvocationToolTarget(prepared.commandBodyForInline)
				: std::nullopt;
			prepared.rewrittenSkillPromptMessage =
				prepared.shouldLoadInlineSkillCommands
				? resolveSkillInvocationPromptRewrite(prepared.commandBodyForInline)
				: std::nullopt;
			prepared.baseAgentMessage = !request.bodyForAgent.empty()
				? request.bodyForAgent
				: request.message;
			prepared.inboundMessageForAgent =
				prepared.rewrittenSkillPromptMessage.has_value()
				? prepared.rewrittenSkillPromptMessage.value()
				: prepared.baseAgentMessage;
			prepared.orderedAllowedTargets = buildOrderedAllowedToolTargets(
				request.orderedAllowedToolTargets,
				prepared.resolvedSkillInvocationToolTarget);
			return prepared;
		}

		[[nodiscard]] static std::string BuildSkillsInjectedMessage(
			const std::string& userMessage,
			const std::wstring& skillsPrompt,
			const std::size_t maxPromptChars) {
			if (skillsPrompt.empty()) {
				return userMessage;
			}

			std::string narrowedPrompt = WideToNarrowAscii(skillsPrompt);
			if (narrowedPrompt.empty()) {
				return userMessage;
			}

			if (maxPromptChars > 0 && narrowedPrompt.size() > maxPromptChars) {
				narrowedPrompt.resize(maxPromptChars);
			}

			std::string injected;
			injected.reserve(userMessage.size() + narrowedPrompt.size() + 64);
			injected += "[skills_prompt]\n";
			injected += narrowedPrompt;
			injected += "\n\n[user_message]\n";
			injected += userMessage;
			return injected;
		}

	private:
		[[nodiscard]] static std::string WideToNarrowAscii(
			const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const auto ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return output;
		}
	};

} // namespace blazeclaw::core
