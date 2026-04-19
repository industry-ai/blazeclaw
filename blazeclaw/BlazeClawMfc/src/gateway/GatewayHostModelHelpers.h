#pragma once

#include <string>

namespace blazeclaw::gateway::GatewayModel {

inline constexpr const char* kSeedProviderId = "seed";
inline constexpr const char* kDeepSeekProviderId = "deepseek";
inline constexpr const char* kDefaultModelId = "default";
inline constexpr const char* kReasonerModelId = "reasoner";
inline constexpr const char* kDeepSeekChatModelId = "deepseek/deepseek-chat";
inline constexpr const char* kDeepSeekReasonerModelId = "deepseek/deepseek-reasoner";

[[nodiscard]] bool IsDeepSeekModelId(const std::string& modelId);
[[nodiscard]] std::string NormalizeModelId(const std::string& modelId);
[[nodiscard]] std::string ResolveModelProvider(const std::string& modelId);
[[nodiscard]] std::string ResolveModelDisplayName(const std::string& modelId);
[[nodiscard]] bool ResolveModelStreaming(const std::string& modelId);

/// Stable `model` object JSON for gateway introspection handlers (`gateway.models.*`).
[[nodiscard]] std::string BuildModelJson(const std::string& requestedModelId);

} // namespace blazeclaw::gateway::GatewayModel
