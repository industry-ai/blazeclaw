#include "pch.h"
#include "GatewayUtf8CloseReason.h"

namespace blazeclaw::gateway {
	namespace {

		bool IsContinuationByte(std::uint8_t value) noexcept {
			return (value & 0xC0u) == 0x80u;
		}

		/// Returns length of one well-formed UTF-8 code unit sequence starting at `i`, or 0 if invalid / incomplete.
		std::size_t WellFormedUtf8SequenceLength(std::string_view s, std::size_t i) noexcept {
			if (i >= s.size()) {
				return 0;
			}

			const std::uint8_t b0 = static_cast<std::uint8_t>(s[i]);
			if (b0 <= 0x7Fu) {
				return 1;
			}

			if (b0 >= 0xC2u && b0 <= 0xDFu) {
				if (i + 1 >= s.size() || !IsContinuationByte(static_cast<std::uint8_t>(s[i + 1]))) {
					return 0;
				}

				return 2;
			}

			if (b0 == 0xE0u) {
				if (i + 2 >= s.size()) {
					return 0;
				}

				const std::uint8_t b1 = static_cast<std::uint8_t>(s[i + 1]);
				const std::uint8_t b2 = static_cast<std::uint8_t>(s[i + 2]);
				if (b1 < 0xA0u || b1 > 0xBFu || !IsContinuationByte(b2)) {
					return 0;
				}

				return 3;
			}

			if ((b0 >= 0xE1u && b0 <= 0xECu) || (b0 >= 0xEEu && b0 <= 0xEFu)) {
				if (i + 2 >= s.size() ||
					!IsContinuationByte(static_cast<std::uint8_t>(s[i + 1])) ||
					!IsContinuationByte(static_cast<std::uint8_t>(s[i + 2]))) {
					return 0;
				}

				return 3;
			}

			if (b0 == 0xEDu) {
				if (i + 2 >= s.size()) {
					return 0;
				}

				const std::uint8_t b1 = static_cast<std::uint8_t>(s[i + 1]);
				const std::uint8_t b2 = static_cast<std::uint8_t>(s[i + 2]);
				if (b1 < 0x80u || b1 > 0x9Fu || !IsContinuationByte(b2)) {
					return 0;
				}

				return 3;
			}

			if (b0 == 0xF0u) {
				if (i + 3 >= s.size()) {
					return 0;
				}

				const std::uint8_t b1 = static_cast<std::uint8_t>(s[i + 1]);
				if (b1 < 0x90u || b1 > 0xBFu ||
					!IsContinuationByte(static_cast<std::uint8_t>(s[i + 2])) ||
					!IsContinuationByte(static_cast<std::uint8_t>(s[i + 3]))) {
					return 0;
				}

				return 4;
			}

			if (b0 >= 0xF1u && b0 <= 0xF3u) {
				if (i + 3 >= s.size() ||
					!IsContinuationByte(static_cast<std::uint8_t>(s[i + 1])) ||
					!IsContinuationByte(static_cast<std::uint8_t>(s[i + 2])) ||
					!IsContinuationByte(static_cast<std::uint8_t>(s[i + 3]))) {
					return 0;
				}

				return 4;
			}

			if (b0 == 0xF4u) {
				if (i + 3 >= s.size()) {
					return 0;
				}

				const std::uint8_t b1 = static_cast<std::uint8_t>(s[i + 1]);
				if (b1 < 0x80u || b1 > 0x8Fu ||
					!IsContinuationByte(static_cast<std::uint8_t>(s[i + 2])) ||
					!IsContinuationByte(static_cast<std::uint8_t>(s[i + 3]))) {
					return 0;
				}

				return 4;
			}

			return 0;
		}

	} // namespace

	std::string TruncateUtf8CloseReason(std::string_view reason, std::size_t maxUtf8Bytes) {
		if (reason.empty()) {
			return std::string("invalid handshake");
		}

		std::string out;
		// Avoid std::min: Windows headers may define a min() macro that breaks std::min.
		const std::size_t reserveHint = reason.size() < maxUtf8Bytes ? reason.size() : maxUtf8Bytes;
		out.reserve(reserveHint);

		for (std::size_t i = 0; i < reason.size();) {
			const std::size_t seqLen = WellFormedUtf8SequenceLength(reason, i);
			if (seqLen == 0) {
				break;
			}

			if (out.size() + seqLen > maxUtf8Bytes) {
				break;
			}

			out.append(reason.data() + i, seqLen);
			i += seqLen;
		}

		return out;
	}

} // namespace blazeclaw::gateway
