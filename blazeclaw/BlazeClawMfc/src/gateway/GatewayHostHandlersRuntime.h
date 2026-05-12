#pragma once

namespace blazeclaw::gateway {

class GatewayHost;

namespace handlers::runtime {

/// Plugin/runtime surface, embeddings, governance, task deltas, chat history - split out of
/// `RegisterRuntimeHandlers` to avoid a single mega-function (wire payloads unchanged).
struct RuntimeSurfaceHandlers {
	static void RegisterAll(GatewayHost& host);
};

/// `chat.send` pipeline, skills catalog, and related chat-heavy registrations.
struct ChatPipelineHandlers {
	static void RegisterAll(GatewayHost& host);
};

/// ASR transcription gateway surface.
struct SpeechRecognitionHandlers {
	static void RegisterAll(GatewayHost& host);
};

/// Orchestration/status, streaming, models/failover tail, and static metric tables invocation.
struct RuntimeOrchestrationStreamingHandlers {
	static void RegisterAll(GatewayHost& host);
};

} // namespace handlers::runtime

} // namespace blazeclaw::gateway
