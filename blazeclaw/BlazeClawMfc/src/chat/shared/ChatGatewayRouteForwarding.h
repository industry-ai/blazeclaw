#pragma once

#include "ChatSharedContracts.h"

#include <functional>
#include <optional>
#include <string>

namespace blazeclaw::chat::shared::route_forwarding {

	using RequestFrame = blazeclaw::gateway::protocol::RequestFrame;
	using ResponseFrame = blazeclaw::gateway::protocol::ResponseFrame;
	using ErrorShape = blazeclaw::gateway::protocol::ErrorShape;
	using RouteFn = std::function<std::optional<ResponseFrame>(const RequestFrame& request)>;

	[[nodiscard]] inline ResponseFrame BuildErrorResponse(
		const RequestFrame& request,
		const std::string& code,
		const std::string& message,
		const bool retryable = false) {
		return ResponseFrame{
			.id = request.id,
			.ok = false,
			.payloadJson = std::nullopt,
			.error = ErrorShape{
				.code = code,
				.message = message,
				.detailsJson = std::nullopt,
				.retryable = retryable,
				.retryAfterMs = std::nullopt,
			},
		};
	}

	[[nodiscard]] inline std::optional<ResponseFrame> ValidateRequestShape(
		const RequestFrame& request) {
		if (request.id.empty()) {
			return BuildErrorResponse(
				request,
				"invalid_request_id",
				"Request id is required.");
		}
		if (request.method.empty()) {
			return BuildErrorResponse(
				request,
				"invalid_request_method",
				"Request method is required.");
		}
		return std::nullopt;
	}

	[[nodiscard]] inline ResponseFrame ForwardRequest(
		const RequestFrame& request,
		const RouteFn& route,
		const std::string& unavailableCode,
		const std::string& unavailableMessage) {
		if (auto invalid = ValidateRequestShape(request); invalid.has_value()) {
			return invalid.value();
		}
		if (!static_cast<bool>(route)) {
			return BuildErrorResponse(
				request,
				"route_unavailable",
				"Shared chat route callback is unavailable.");
		}

		auto routed = route(request);
		if (!routed.has_value()) {
			return BuildErrorResponse(
				request,
				unavailableCode,
				unavailableMessage);
		}

		ResponseFrame response = routed.value();
		if (response.id.empty()) {
			response.id = request.id;
		}
		return response;
	}

} // namespace blazeclaw::chat::shared::route_forwarding
