#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace blazeclaw::gateway {

/// Matches OpenClaw `close-reason.ts` default (`CLOSE_REASON_MAX_BYTES = 120`).
inline constexpr std::size_t kGatewayCloseReasonMaxUtf8BytesOpenClawPolicy = 120;

/// RFC 6455: close frame body is at most 125 bytes (2-byte status + UTF-8 reason).
inline constexpr std::size_t kRfc6455CloseFrameBodyMaxBytes = 125;
inline constexpr std::size_t kRfc6455CloseReasonMaxUtf8Bytes = 123;

/// Empty input becomes the same default string as OpenClaw `truncateCloseReason`.
/// Non-empty input is truncated to at most `maxUtf8Bytes` UTF-8 bytes without splitting a code point
/// and without emitting an ill-formed trailing sequence (invalid bytes stop accumulation).
[[nodiscard]] std::string TruncateUtf8CloseReason(
	std::string_view reason,
	std::size_t maxUtf8Bytes = kGatewayCloseReasonMaxUtf8BytesOpenClawPolicy);

} // namespace blazeclaw::gateway
