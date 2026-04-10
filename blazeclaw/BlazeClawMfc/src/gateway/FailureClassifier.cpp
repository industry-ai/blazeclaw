#include "pch.h"
#include "FailureClassifier.h"

#include <algorithm>

namespace blazeclaw::gateway {
	namespace {
		std::string ToLowerCopyClassifier(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		bool ContainsAny(
			const std::string& haystack,
			const std::initializer_list<const char*>& needles) {
			for (const char* needle : needles) {
				if (needle != nullptr && haystack.find(needle) != std::string::npos) {
					return true;
				}
			}

			return false;
		}
	}

	FailureClassification FailureClassifier::Classify(
		const std::string& errorCode,
		const std::string& errorMessage) {
		const std::string code = ToLowerCopyClassifier(errorCode);
		const std::string message = ToLowerCopyClassifier(errorMessage);
		const std::string signal = code + " " + message;

		if (signal.empty()) {
			return FailureClassification{};
		}

		if (ContainsAny(signal, { "timeout", "deadline", "timed out", "embedded_deadline_exceeded" })) {
			return FailureClassification{
				.category = FailureCategory::Timeout,
				.canonicalCode = "timeout",
				.retryable = true,
			};
		}

		if (ContainsAny(signal, { "rate_limit", "rate limit", "429" })) {
			return FailureClassification{
				.category = FailureCategory::RateLimit,
				.canonicalCode = "rate_limit",
				.retryable = true,
			};
		}

		if (ContainsAny(signal, { "overloaded", "overload", "capacity", "busy" })) {
			return FailureClassification{
				.category = FailureCategory::Overloaded,
				.canonicalCode = "overloaded",
				.retryable = true,
			};
		}

		if (ContainsAny(signal, { "auth", "unauthorized", "forbidden", "credential" })) {
			return FailureClassification{
				.category = FailureCategory::Auth,
				.canonicalCode = "auth",
				.retryable = false,
			};
		}

		if (ContainsAny(signal, { "overflow", "too_large", "too large", "token_limit" })) {
			return FailureClassification{
				.category = FailureCategory::Overflow,
				.canonicalCode = "overflow",
				.retryable = true,
			};
		}

		if (ContainsAny(signal, { "invalid_args", "invalid_arguments", "invalid argument" })) {
			return FailureClassification{
				.category = FailureCategory::InvalidArgs,
				.canonicalCode = "invalid_args",
				.retryable = false,
			};
		}

		if (ContainsAny(signal, { "tool", "executor", "runtime_error", "plugin" })) {
			return FailureClassification{
				.category = FailureCategory::ToolRuntime,
				.canonicalCode = "tool_runtime",
				.retryable = true,
			};
		}

		return FailureClassification{
			.category = FailureCategory::Unknown,
			.canonicalCode = code.empty() ? "unknown" : code,
			.retryable = false,
		};
	}

} // namespace blazeclaw::gateway
