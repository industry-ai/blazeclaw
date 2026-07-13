#pragma once

#include "GatewayRequestParams.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace blazeclaw::gateway {

class GatewaySessionRegistry;

namespace handlers::runtime {

/// Shared request-field normalization for `ChatPipelineHandlers` routes.
class ChatPipelineRequestNormalization {
public:
	static constexpr std::size_t kDefaultPollLimit = 20;
	static constexpr std::size_t kMinPollLimit = 1;
	static constexpr std::size_t kMaxPollLimit = 100;
	static constexpr const char* kDefaultSessionKey = "main";
	static constexpr const char* kDefaultConnectionId = "local";

	explicit ChatPipelineRequestNormalization(GatewaySessionRegistry& sessionRegistry) noexcept;

	[[nodiscard]] std::string ResolveSessionId(const std::optional<std::string>& paramsJson) const;
	[[nodiscard]] static std::string ResolveConnectionId(const std::optional<std::string>& paramsJson);

	struct SessionKeyRoute {
		std::string sessionKey;
		std::string requestedSessionKey;
	};

	[[nodiscard]] static SessionKeyRoute NormalizeSessionKeyRoute(
		const std::optional<std::string>& paramsJson,
		std::string_view fieldName = "sessionKey");

	struct PollRoute {
		SessionKeyRoute session;
		std::size_t limit = kDefaultPollLimit;
	};

	[[nodiscard]] static PollRoute NormalizePollRoute(const std::optional<std::string>& paramsJson);

	struct AbortRoute {
		SessionKeyRoute session;
		std::string requestedRunId;
	};

	[[nodiscard]] static AbortRoute NormalizeAbortRoute(const std::optional<std::string>& paramsJson);

	struct InjectRoute {
		SessionKeyRoute session;
		std::string message;
		std::string label;
	};

	[[nodiscard]] static InjectRoute NormalizeInjectRoute(const std::optional<std::string>& paramsJson);

	struct SubscriberRoute {
		std::string sessionId;
		std::string connectionId;
	};

	[[nodiscard]] SubscriberRoute NormalizeSubscriberRoute(
		const std::optional<std::string>& paramsJson) const;

private:
	[[nodiscard]] static std::string ResolveSessionKeyOrDefault(std::string requestedSessionKey);
	[[nodiscard]] static std::size_t ResolveBoundedPollLimit(std::size_t requestedLimit);

	GatewaySessionRegistry& m_sessionRegistry;
};

} // namespace handlers::runtime

} // namespace blazeclaw::gateway
