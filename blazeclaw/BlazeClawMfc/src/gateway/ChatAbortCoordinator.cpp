#include "pch.h"
#include "ChatAbortCoordinator.h"
#include "ChatTranscriptStore.h"
#include "GatewayJsonUtils.h"
#include "RuntimeTranscriptGuard.h"

namespace blazeclaw::gateway {

	ChatAbortCoordinator::PersistPartialResult
		ChatAbortCoordinator::PersistAbortedPartial(
			const PersistPartialParams& params) const {
		const std::string trimmedText = json::Trim(params.text);
		if (trimmedText.empty() ||
			RuntimeTranscriptGuard::IsSilentReplyText(trimmedText)) {
			return PersistPartialResult{};
		}

		const ChatTranscriptStore transcriptStore;
		const auto appendResult = transcriptStore.AppendAssistantMessage(
			ChatTranscriptStore::AppendParams{
				.sessionKey = params.sessionKey,
				.message = trimmedText,
				.label = std::string("abort:") +
					(params.origin.empty() ? std::string("rpc") : params.origin) +
					":" +
				   params.runId,
				.idempotencyKey = std::string("abort:") +
					(params.origin.empty() ? std::string("rpc") : params.origin) +
					":" +
					params.runId,
			});
		if (!appendResult.ok) {
			return PersistPartialResult{
				.persisted = false,
				.messageId = {},
				.messageJson = {},
				.error = appendResult.error,
			};
		}

		return PersistPartialResult{
			.persisted = true,
			.messageId = appendResult.messageId,
			.messageJson = appendResult.messageJson,
			.error = {},
		};
	}

} // namespace blazeclaw::gateway
