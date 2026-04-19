#pragma once

#include "../config/ConfigModels.h"
#include "../gateway/GatewayHost.h"

#include <functional>
#include <optional>
#include <string>

namespace blazeclaw::core {

	class OnnxEmbeddingsService;
	class RetrievalMemoryService;
	struct RetrievalMemorySnapshot;
	class AgentsModelRoutingService;
	class PiEmbeddedService;

	namespace localmodel {
		class ITextGenerationRuntime;
		struct LocalModelRuntimeSnapshot;
	}

	/// Non-owning references and callbacks supplied by ServiceManager so multi-provider
	/// chat execution stays out of the composition root's method bodies.
	struct ChatProviderRuntimeBindings {
		const blazeclaw::config::AppConfig* config = nullptr;
		OnnxEmbeddingsService* embeddingsService = nullptr;
		RetrievalMemoryService* retrievalMemoryService = nullptr;
		RetrievalMemorySnapshot* retrievalMemorySnapshot = nullptr;
		AgentsModelRoutingService* modelRouting = nullptr;
		PiEmbeddedService* piEmbedded = nullptr;
		localmodel::ITextGenerationRuntime* localModelRuntime = nullptr;
		localmodel::LocalModelRuntimeSnapshot* localModelRuntimeSnapshot = nullptr;
		bool localModelActivationEnabled = false;
		bool localModelRolloutEligible = false;
		const std::string* localModelActivationReason = nullptr;

		std::function<void(const std::string& runId)> clearDeepSeekRunCancelled;
		std::function<bool()> hasDeepSeekCredential;
		std::function<std::optional<std::string>()> resolveDeepSeekCredentialUtf8;
		std::function<blazeclaw::gateway::GatewayHost::ChatRuntimeResult(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
			const std::string& modelId,
			const std::string& apiKey)>
			invokeDeepSeekRemoteChat;

		/// Agent model string for routing (UTF-8), empty when unset.
		std::function<std::string()> getAgentModelUtf8;

		std::function<std::uint64_t()> currentEpochMs;
	};

	/// Owns DeepSeek / local-model / retrieval+embedded fallback paths for chat.send-style
	/// provider execution (shared by ServiceManager entry points).
	class ChatProviderRuntimeService {
	public:
		[[nodiscard]] blazeclaw::gateway::GatewayHost::ChatRuntimeResult ExecuteProviderPath(
			const ChatProviderRuntimeBindings& bindings,
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
			const std::string& sessionId,
			const std::string& runtimeMessage,
			const std::string& activeProvider,
			const std::string& activeModel) const;
	};

} // namespace blazeclaw::core
