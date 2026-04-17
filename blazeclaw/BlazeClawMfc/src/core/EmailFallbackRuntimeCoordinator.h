#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace blazeclaw::core {

	class EmailFallbackRuntimeCoordinator {
	public:
		struct EmbeddedFailureDecision {
			bool shouldFallback = false;
			std::string fallbackReason;
		};

		[[nodiscard]] EmbeddedFailureDecision EvaluateEmbeddedFailure(
			const std::string& errorCode,
			const std::string& reason) const {
			const std::string normalizedError = ToLowerAscii(errorCode);
			const std::string normalizedReason = ToLowerAscii(reason);

			if (normalizedError == "embedded_deadline_exceeded" ||
				normalizedError == "embedded_loop_detected" ||
				normalizedError == "embedded_completion_failed" ||
				normalizedError == "embedded_tool_execution_failed") {
				return EmbeddedFailureDecision{
					.shouldFallback = true,
					.fallbackReason = errorCode.empty() ? reason : errorCode,
				};
			}

			if (normalizedReason == "deadline_exceeded" ||
				normalizedReason == "tool_execution_failed" ||
				normalizedReason == "embedded_completion_failed") {
				return EmbeddedFailureDecision{
					.shouldFallback = true,
					.fallbackReason = errorCode.empty() ? reason : errorCode,
				};
			}

			return EmbeddedFailureDecision{};
		}

	private:
		[[nodiscard]] static std::string ToLowerAscii(
			const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}
	};

} // namespace blazeclaw::core
