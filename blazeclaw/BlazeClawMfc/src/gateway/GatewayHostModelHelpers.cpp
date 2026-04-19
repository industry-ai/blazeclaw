#include "pch.h"
#include "GatewayHostModelHelpers.h"

#include "GatewayJsonBuilder.h"

namespace blazeclaw::gateway::GatewayModel {

bool IsDeepSeekModelId(const std::string& modelId) {
	return modelId == kDeepSeekChatModelId ||
		modelId == kDeepSeekReasonerModelId ||
		modelId == kDeepSeekProviderId;
}

std::string NormalizeModelId(const std::string& modelId) {
	if (modelId == kDeepSeekProviderId) {
		return kDeepSeekChatModelId;
	}

	return modelId.empty() ? kDefaultModelId : modelId;
}

std::string ResolveModelProvider(const std::string& modelId) {
	return IsDeepSeekModelId(modelId) ? kDeepSeekProviderId : kSeedProviderId;
}

std::string ResolveModelDisplayName(const std::string& modelId) {
	if (modelId == kReasonerModelId) {
		return "Reasoner Model";
	}

	if (modelId == kDeepSeekChatModelId || modelId == kDeepSeekProviderId) {
		return "DeepSeek Chat";
	}

	if (modelId == kDeepSeekReasonerModelId) {
		return "DeepSeek Reasoner";
	}

	return "Default Model";
}

bool ResolveModelStreaming(const std::string& modelId) {
	if (modelId == kReasonerModelId) {
		return false;
	}

	return true;
}

std::string BuildModelJson(const std::string& requestedModelId) {
	const std::string modelId = NormalizeModelId(requestedModelId);
	return JsonObject({
		{"id", JsonString(modelId)},
		{"provider", JsonString(ResolveModelProvider(modelId))},
		{"displayName", JsonString(ResolveModelDisplayName(modelId))},
		{"streaming", JsonBool(ResolveModelStreaming(modelId))},
	});
}

} // namespace blazeclaw::gateway::GatewayModel
