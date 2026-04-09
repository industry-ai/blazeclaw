#pragma once

#include <cstddef>
#include <cstdint>

namespace blazeclaw::core::runtime::contracts {

	inline constexpr std::size_t kDefaultQueueCapacity = 64;
	inline constexpr std::uint64_t kDefaultQueueWaitTimeoutMs = 15000;
	inline constexpr std::uint64_t kDefaultExecutionTimeoutMs = 120000;

	inline constexpr const char* kErrorQueueFull = "chat_runtime_queue_full";
	inline constexpr const char* kErrorCancelled = "chat_runtime_cancelled";
	inline constexpr const char* kErrorTimedOut = "chat_runtime_timed_out";
	inline constexpr const char* kErrorWorkerUnavailable =
		"chat_runtime_worker_unavailable";

} // namespace blazeclaw::core::runtime::contracts
