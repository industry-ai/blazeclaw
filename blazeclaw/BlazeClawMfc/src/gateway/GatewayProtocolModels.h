#pragma once

#include "../app/pch.h"

namespace blazeclaw::gateway::protocol {

	struct ErrorShape {
		std::string code;
		std::string message;
		std::optional<std::string> detailsJson;
		std::optional<bool> retryable;
		std::optional<std::uint64_t> retryAfterMs;
	};

	struct RequestFrame {
		std::string id;
		std::string method;
		std::optional<std::string> paramsJson;
	};

	struct ResponseFrame {
		std::string id;
		bool ok = false;
		std::optional<std::string> payloadJson;
		std::optional<ErrorShape> error;
	};

	/// Successful JSON-RPC-style response: copies `request.id`, sets `ok`, payload, no error.
	[[nodiscard]] inline ResponseFrame OkResponse(
		const RequestFrame& request,
		std::string payloadJson) {
		return ResponseFrame{
			.id = request.id,
			.ok = true,
			.payloadJson = std::move(payloadJson),
			.error = std::nullopt,
		};
	}

	/// Success response whose body may be omitted (`std::nullopt` payload), e.g. inline policy skip paths.
	/// Separate name avoids overload ambiguity with string literals (they could otherwise match `std::string` or `std::optional<std::string>`).
	[[nodiscard]] inline ResponseFrame OkResponseOptionalPayload(
		const RequestFrame& request,
		std::optional<std::string> payloadJson) {
		return ResponseFrame{
			.id = request.id,
			.ok = true,
			.payloadJson = std::move(payloadJson),
			.error = std::nullopt,
		};
	}

	struct StateVersion {
		std::optional<std::uint64_t> presence;
		std::optional<std::uint64_t> health;

		StateVersion() = default;
		StateVersion(std::uint64_t legacyVersion)
			: presence(legacyVersion),
			health(legacyVersion) {}
	};

	struct EventFrame {
		std::string eventName;
		std::optional<std::string> payloadJson;
		std::optional<std::uint64_t> seq;
		std::optional<StateVersion> stateVersion;
	};

} // namespace blazeclaw::gateway::protocol
