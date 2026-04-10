#include "pch.h"
#include "RunLoopBudget.h"

namespace blazeclaw::gateway {

	RunLoopBudget::RunLoopBudget(const Limits limits)
		: m_limits(limits) {}

	bool RunLoopBudget::ConsumeIteration() {
		if (m_iterationsUsed >= m_limits.maxIterations) {
			return false;
		}

		++m_iterationsUsed;
		return true;
	}

	bool RunLoopBudget::ConsumeRetry() {
		if (m_retryUsed >= m_limits.maxRetry) {
			return false;
		}

		++m_retryUsed;
		return true;
	}

	bool RunLoopBudget::ConsumeProfileFallback() {
		if (m_profileFallbackUsed >= m_limits.maxProfileFallback) {
			return false;
		}

		++m_profileFallbackUsed;
		return true;
	}

	bool RunLoopBudget::ConsumeCompaction() {
		if (m_compactionUsed >= m_limits.maxCompaction) {
			return false;
		}

		++m_compactionUsed;
		return true;
	}

	bool RunLoopBudget::ConsumeTruncation() {
		if (m_truncationUsed >= m_limits.maxTruncation) {
			return false;
		}

		++m_truncationUsed;
		return true;
	}

	const RunLoopBudget::Limits& RunLoopBudget::GetLimits() const noexcept {
		return m_limits;
	}

	std::size_t RunLoopBudget::IterationsUsed() const noexcept {
		return m_iterationsUsed;
	}

} // namespace blazeclaw::gateway
