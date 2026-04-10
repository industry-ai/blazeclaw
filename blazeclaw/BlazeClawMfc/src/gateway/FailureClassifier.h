#pragma once

#include <string>

namespace blazeclaw::gateway {

	enum class FailureCategory {
		None,
		Overflow,
		Auth,
		RateLimit,
		Overloaded,
		Timeout,
		InvalidArgs,
		ToolRuntime,
		Unknown,
	};

	struct FailureClassification {
		FailureCategory category = FailureCategory::None;
		std::string canonicalCode;
		bool retryable = false;
	};

	class FailureClassifier {
	public:
		[[nodiscard]] static FailureClassification Classify(
			const std::string& errorCode,
			const std::string& errorMessage);
	};

} // namespace blazeclaw::gateway
