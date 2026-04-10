#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

	struct ChatRunStageContext {
		using AttachmentValidationCallback =
			std::function<bool(
				const std::optional<std::string>&,
				bool&,
				std::string&,
				std::string&)>;
		using IdempotencyLookupCallback =
			std::function<std::optional<std::string>(const std::string&)>;

		std::string requestId;
		std::string method;
		std::optional<std::string> paramsJson;
		std::string runId;
		std::string requestedSessionKey;
		std::string sessionKey;
		std::string message;
		std::string normalizedMessage;
		std::string idempotencyKey;
		bool forceError = false;
		bool attachmentsValid = true;
		bool hasAttachmentPayload = false;
		bool deduped = false;
		std::string dedupedRunId;
		bool shouldReturnEarly = false;
		bool responseOk = true;
		std::string responseErrorCode;
		std::string responseErrorMessage;
		bool preferChineseResponse = false;
		std::string runtimeMessage;
		std::uint64_t nowEpochMs = 0;
		AttachmentValidationCallback validateAttachments;
		IdempotencyLookupCallback findRunByIdempotency;
		std::vector<std::string> stageTrace;
		std::string diagnostics;
	};

} // namespace blazeclaw::gateway
