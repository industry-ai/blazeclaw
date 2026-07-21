#include "pch.h"
#include "ChatPipelineRequestNormalization.h"

#include "GatewaySessionRegistry.h"

#include <algorithm>

namespace blazeclaw::gateway::handlers::runtime {

ChatPipelineRequestNormalization::ChatPipelineRequestNormalization(
	GatewaySessionRegistry& sessionRegistry) noexcept
	: m_sessionRegistry(sessionRegistry) {}

std::string ChatPipelineRequestNormalization::ResolveSessionId(
	const std::optional<std::string>& paramsJson) const {
	const RequestParamsView params(paramsJson);
	const std::string requestedSessionId = params.GetString("sessionId");
	return m_sessionRegistry.Resolve(requestedSessionId).id;
}

std::string ChatPipelineRequestNormalization::ResolveConnectionId(
	const std::optional<std::string>& paramsJson) {
	const RequestParamsView params(paramsJson);
	std::string connectionId = params.GetString("connectionId");
	if (connectionId.empty()) {
		connectionId = params.GetString("clientConnectionId");
	}
	if (connectionId.empty()) {
		connectionId = kDefaultConnectionId;
	}
	return connectionId;
}

ChatPipelineRequestNormalization::SessionKeyRoute
ChatPipelineRequestNormalization::NormalizeSessionKeyRoute(
	const std::optional<std::string>& paramsJson,
	const std::string_view fieldName) {
	const RequestParamsView params(paramsJson);
	SessionKeyRoute route{
		.requestedSessionKey = params.GetString(fieldName),
	};
	route.sessionKey = ResolveSessionKeyOrDefault(route.requestedSessionKey);
	return route;
}

ChatPipelineRequestNormalization::PollRoute
ChatPipelineRequestNormalization::NormalizePollRoute(
	const std::optional<std::string>& paramsJson) {
	const RequestParamsView params(paramsJson);
	PollRoute route{
		.session = NormalizeSessionKeyRoute(paramsJson),
		.limit = ResolveBoundedPollLimit(
			params.GetSize("limit").value_or(kDefaultPollLimit)),
	};
	return route;
}

ChatPipelineRequestNormalization::AbortRoute
ChatPipelineRequestNormalization::NormalizeAbortRoute(
	const std::optional<std::string>& paramsJson) {
	const RequestParamsView params(paramsJson);
	return AbortRoute{
		.session = NormalizeSessionKeyRoute(paramsJson),
		.requestedRunId = params.GetString("runId"),
	};
}

ChatPipelineRequestNormalization::InjectRoute
ChatPipelineRequestNormalization::NormalizeInjectRoute(
	const std::optional<std::string>& paramsJson) {
	const RequestParamsView params(paramsJson);
	return InjectRoute{
		.session = NormalizeSessionKeyRoute(paramsJson),
		.message = params.GetString("message"),
		.label = params.GetString("label"),
	};
}

ChatPipelineRequestNormalization::SubscriberRoute
ChatPipelineRequestNormalization::NormalizeSubscriberRoute(
	const std::optional<std::string>& paramsJson) const {
	return SubscriberRoute{
		.sessionId = ResolveSessionId(paramsJson),
		.connectionId = ResolveConnectionId(paramsJson),
	};
}

std::string ChatPipelineRequestNormalization::ResolveSessionKeyOrDefault(
	std::string requestedSessionKey) {
	if (requestedSessionKey.empty()) {
		return kDefaultSessionKey;
	}
	return requestedSessionKey;
}

std::size_t ChatPipelineRequestNormalization::ResolveBoundedPollLimit(
	const std::size_t requestedLimit) {
	return (std::max)(kMinPollLimit, (std::min)(requestedLimit, kMaxPollLimit));
}

} // namespace blazeclaw::gateway::handlers::runtime
