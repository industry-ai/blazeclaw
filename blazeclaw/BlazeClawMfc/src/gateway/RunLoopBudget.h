#pragma once

#include "FailureClassifier.h"

#include <cstddef>

namespace blazeclaw::gateway {

	class RunLoopBudget {
	public:
		struct Limits {
			std::size_t maxIterations = 4;
			std::size_t maxRetry = 1;
			std::size_t maxProfileFallback = 1;
			std::size_t maxCompaction = 1;
			std::size_t maxTruncation = 1;
		};

		explicit RunLoopBudget(Limits limits = Limits{});

		[[nodiscard]] bool ConsumeIteration();
		[[nodiscard]] bool ConsumeRetry();
		[[nodiscard]] bool ConsumeProfileFallback();
		[[nodiscard]] bool ConsumeCompaction();
		[[nodiscard]] bool ConsumeTruncation();

		[[nodiscard]] const Limits& GetLimits() const noexcept;
		[[nodiscard]] std::size_t IterationsUsed() const noexcept;

	private:
		Limits m_limits;
		std::size_t m_iterationsUsed = 0;
		std::size_t m_retryUsed = 0;
		std::size_t m_profileFallbackUsed = 0;
		std::size_t m_compactionUsed = 0;
		std::size_t m_truncationUsed = 0;
	};

} // namespace blazeclaw::gateway
