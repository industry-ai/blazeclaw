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

	/// Success response when only the wire correlation id is fixed (fixture parity / contract tests).
	/// Same shape as `OkResponse(request, payload)` with `request.id == responseId`.
	[[nodiscard]] inline ResponseFrame OkResponse(std::string responseId, std::string payloadJson) {
		return ResponseFrame{
			.id = std::move(responseId),
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

	/// Failed JSON-RPC-style response: copies `request.id`, sets `ok = false`, no payload, structured error.
	[[nodiscard]] inline ResponseFrame ErrorResponse(
		const RequestFrame& request,
		ErrorShape error) {
		return ResponseFrame{
			.id = request.id,
			.ok = false,
			.payloadJson = std::nullopt,
			.error = std::move(error),
		};
	}

	/// Error with code + message only (non-retryable, no details).
	[[nodiscard]] inline ResponseFrame ErrorResponse(
		const RequestFrame& request,
		std::string code,
		std::string message) {
		return ErrorResponse(request, ErrorShape{
			.code = std::move(code),
			.message = std::move(message),
			.detailsJson = std::nullopt,
			.retryable = false,
			.retryAfterMs = std::nullopt,
		});
	}

	/// Error with optional JSON details fragment (object or string shape as already encoded).
	[[nodiscard]] inline ResponseFrame ErrorResponse(
		const RequestFrame& request,
		std::string code,
		std::string message,
		std::optional<std::string> detailsJson) {
		return ErrorResponse(request, ErrorShape{
			.code = std::move(code),
			.message = std::move(message),
			.detailsJson = std::move(detailsJson),
			.retryable = false,
			.retryAfterMs = std::nullopt,
		});
	}

	/// Full control over retry hints (matches common `ErrorShape` usage in handlers).
	[[nodiscard]] inline ResponseFrame ErrorResponse(
		const RequestFrame& request,
		std::string code,
		std::string message,
		std::optional<std::string> detailsJson,
		std::optional<bool> retryable,
		std::optional<std::uint64_t> retryAfterMs) {
		return ErrorResponse(request, ErrorShape{
			.code = std::move(code),
			.message = std::move(message),
			.detailsJson = std::move(detailsJson),
			.retryable = std::move(retryable),
			.retryAfterMs = std::move(retryAfterMs),
		});
	}

	/// Idempotency replay: same `ok` / payload / error as a stored result, with `id` taken from `request`.
	[[nodiscard]] inline ResponseFrame ReplayFromStored(
		const RequestFrame& request,
		bool ok,
		std::optional<std::string> payloadJson,
		std::optional<ErrorShape> error) {
		return ResponseFrame{
			.id = request.id,
			.ok = ok,
			.payloadJson = std::move(payloadJson),
			.error = std::move(error),
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
