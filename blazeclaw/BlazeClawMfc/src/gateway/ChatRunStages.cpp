#include "pch.h"
#include "ChatRunStages.h"

#include "GatewayJsonUtils.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	namespace {

		std::vector<std::string> ParseStringArrayField(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return {};
			}

			std::string raw;
			if (!json::FindRawField(paramsJson.value(), fieldName, raw)) {
				return {};
			}

			try {
				const auto parsed = nlohmann::json::parse(raw);
				if (!parsed.is_array()) {
					return {};
				}

				std::vector<std::string> values;
				for (const auto& item : parsed) {
					if (!item.is_string()) {
						continue;
					}
					values.push_back(item.get<std::string>());
				}

				return values;
			}
			catch (...) {
				return {};
			}
		}

		ChatRunStageResult AppendStage(
			ChatRunStageContext& context,
			const char* stageName,
			std::string nextStage,
			std::string status) {
			context.stageTrace.push_back(stageName == nullptr ? std::string{} : std::string(stageName));
			return ChatRunStageResult{
				.ok = true,
				.nextStage = std::move(nextStage),
				.status = std::move(status),
			};
		}

	} // namespace

	const char* ChatTransportStage::Name() const noexcept {
		return "transport";
	}

	ChatRunStageResult ChatTransportStage::Execute(ChatRunStageContext& context) const {
		const std::string requestedSessionKey = [&context]() {
			if (!context.paramsJson.has_value()) {
				return std::string{};
			}

			std::string value;
			if (!json::FindStringField(context.paramsJson.value(), "sessionKey", value)) {
				return std::string{};
			}

			return value;
			}();

		context.requestedSessionKey = requestedSessionKey;
		context.sessionKey = requestedSessionKey.empty() ? "main" : requestedSessionKey;

		if (!context.paramsJson.has_value()) {
			context.hasAttachmentPayload = false;
			return AppendStage(context, Name(), "control", "ok");
		}

		std::string attachmentsRaw;
		context.hasAttachmentPayload =
			json::FindRawField(context.paramsJson.value(), "attachments", attachmentsRaw) &&
			json::Trim(attachmentsRaw) != "[]";

		return AppendStage(context, Name(), "control", "ok");
	}

	const char* ChatControlStage::Name() const noexcept {
		return "control";
	}

	ChatRunStageResult ChatControlStage::Execute(ChatRunStageContext& context) const {
		if (!context.paramsJson.has_value()) {
			context.message.clear();
			context.normalizedMessage.clear();
			context.idempotencyKey.clear();
			context.forceError = false;
			return AppendStage(context, Name(), "decomposition", "ok");
		}

		std::string message;
		json::FindStringField(context.paramsJson.value(), "message", message);
		context.message = message;
		context.normalizedMessage = json::Trim(message);

		std::string idempotencyKey;
		json::FindStringField(
			context.paramsJson.value(),
			"idempotencyKey",
			idempotencyKey);
		context.idempotencyKey = idempotencyKey;

		bool deliver = false;
		if (json::FindBoolField(context.paramsJson.value(), "deliver", deliver)) {
			context.deliver = deliver;
		}
		else {
			context.deliver = false;
		}

		std::string routeChannel;
		json::FindStringField(context.paramsJson.value(), "originatingChannel", routeChannel);
		context.routeChannel = routeChannel;

		std::string routeTo;
		json::FindStringField(context.paramsJson.value(), "originatingTo", routeTo);
		context.routeTo = routeTo;

		std::string clientMode;
		json::FindStringField(context.paramsJson.value(), "clientMode", clientMode);
		context.clientMode = clientMode;

		context.clientCaps = ParseStringArrayField(context.paramsJson, "clientCaps");

		bool forceError = false;
		if (json::FindBoolField(context.paramsJson.value(), "forceError", forceError)) {
			context.forceError = forceError;
		}
		else {
			context.forceError = false;
		}

		bool hasAttachments = false;
		std::string attachmentsErrorCode;
		std::string attachmentsErrorMessage;
		if (context.validateAttachments) {
			context.attachmentsValid = context.validateAttachments(
				context.paramsJson,
				hasAttachments,
				attachmentsErrorCode,
				attachmentsErrorMessage);
		}
		else {
			context.attachmentsValid = true;
			hasAttachments = context.hasAttachmentPayload;
		}

		context.hasAttachmentPayload = hasAttachments;
		if (context.extractAttachmentMimeTypes) {
			context.attachmentMimeTypes =
				context.extractAttachmentMimeTypes(context.paramsJson);
		}
		else {
			context.attachmentMimeTypes.clear();
		}

		if (!context.attachmentsValid) {
			context.shouldReturnEarly = true;
			context.responseOk = false;
			context.responseErrorCode = attachmentsErrorCode;
			context.responseErrorMessage = attachmentsErrorMessage;
			context.responseError = protocol::ErrorShape{
				 .code = context.responseErrorCode,
				 .message = context.responseErrorMessage,
				 .detailsJson = std::nullopt,
				 .retryable = false,
				 .retryAfterMs = std::nullopt,
			};
			return AppendStage(context, Name(), {}, "validation_failed");
		}

		if (context.normalizedMessage.empty() && !context.hasAttachmentPayload) {
			context.shouldReturnEarly = true;
			context.responseOk = false;
			context.responseErrorCode = "invalid_message";
			context.responseErrorMessage =
				"chat.send requires non-empty message or attachments.";
			context.responseError = protocol::ErrorShape{
				 .code = context.responseErrorCode,
				 .message = context.responseErrorMessage,
				 .detailsJson = std::nullopt,
				 .retryable = false,
				 .retryAfterMs = std::nullopt,
			};
			return AppendStage(context, Name(), {}, "validation_failed");
		}

		if (!context.idempotencyKey.empty() && context.findRunByIdempotency) {
			const auto dedupedRunId =
				context.findRunByIdempotency(context.idempotencyKey);
			if (dedupedRunId.has_value() && !dedupedRunId->empty()) {
				context.deduped = true;
				context.dedupedRunId = *dedupedRunId;
				context.shouldReturnEarly = true;
				context.responseOk = true;
				context.responseError.reset();
				return AppendStage(context, Name(), {}, "deduped");
			}
		}

		return AppendStage(context, Name(), "decomposition", "ok");
	}

	const char* ChatDecompositionStage::Name() const noexcept {
		return "decomposition";
	}

	ChatRunStageResult ChatDecompositionStage::Execute(ChatRunStageContext& context) const {
		context.preferChineseResponse = [&context]() {
			if (context.normalizedMessage.empty()) {
				return false;
			}

			for (std::size_t i = 0; i < context.normalizedMessage.size();) {
				const unsigned char lead =
					static_cast<unsigned char>(context.normalizedMessage[i]);
				std::uint32_t codePoint = 0;
				std::size_t advance = 1;

				if ((lead & 0x80u) == 0) {
					codePoint = lead;
				}
				else if ((lead & 0xE0u) == 0xC0u &&
					i + 1 < context.normalizedMessage.size()) {
					const unsigned char b1 =
						static_cast<unsigned char>(context.normalizedMessage[i + 1]);
					if ((b1 & 0xC0u) != 0x80u) {
						i += 1;
						continue;
					}

					codePoint =
						(static_cast<std::uint32_t>(lead & 0x1Fu) << 6) |
						static_cast<std::uint32_t>(b1 & 0x3Fu);
					advance = 2;
				}
				else if ((lead & 0xF0u) == 0xE0u &&
					i + 2 < context.normalizedMessage.size()) {
					const unsigned char b1 =
						static_cast<unsigned char>(context.normalizedMessage[i + 1]);
					const unsigned char b2 =
						static_cast<unsigned char>(context.normalizedMessage[i + 2]);
					if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
						i += 1;
						continue;
					}

					codePoint =
						(static_cast<std::uint32_t>(lead & 0x0Fu) << 12) |
						(static_cast<std::uint32_t>(b1 & 0x3Fu) << 6) |
						static_cast<std::uint32_t>(b2 & 0x3Fu);
					advance = 3;
				}

				if ((codePoint >= 0x4E00u && codePoint <= 0x9FFFu) ||
					(codePoint >= 0x3400u && codePoint <= 0x4DBFu)) {
					return true;
				}

				i += advance;
			}

			return false;
			}();

		if (context.normalizedMessage.empty()) {
			context.runtimeMessage.clear();
			return AppendStage(context, Name(), "runtime", "ok");
		}

		if (context.preferChineseResponse) {
			context.runtimeMessage =
				context.normalizedMessage +
				"\n\nPlease reply in Chinese for this user message.";
		}
		else {
			context.runtimeMessage = context.normalizedMessage;
		}

		return AppendStage(context, Name(), "runtime", "ok");
	}

	const char* ChatRuntimeStage::Name() const noexcept {
		return "runtime";
	}

	ChatRunStageResult ChatRuntimeStage::Execute(ChatRunStageContext& context) const {
		if (!context.requestId.empty()) {
			context.runId = context.requestId;
		}
		else {
			const auto hashSeed = std::hash<std::string>{}(context.sessionKey + context.normalizedMessage);
			context.runId = "chat-run-stage-" + std::to_string(hashSeed);
		}

		if (context.nowEpochMs == 0) {
			context.nowEpochMs =
				static_cast<std::uint64_t>(std::time(nullptr)) * 1000ull;
		}

		return AppendStage(context, Name(), "recovery", "ok");
	}

	const char* ChatRecoveryStage::Name() const noexcept {
		return "recovery";
	}

	ChatRunStageResult ChatRecoveryStage::Execute(ChatRunStageContext& context) const {
		return AppendStage(context, Name(), "finalize", "ok");
	}

	const char* ChatFinalizeStage::Name() const noexcept {
		return "finalize";
	}

	ChatRunStageResult ChatFinalizeStage::Execute(ChatRunStageContext& context) const {
		return AppendStage(context, Name(), {}, "completed");
	}

} // namespace blazeclaw::gateway
