#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace blazeclaw::cron {

	using CronJson = nlohmann::json;

	inline constexpr const char* kWakeModeNow = "now";
	inline constexpr const char* kWakeModeNextHeartbeat = "next-heartbeat";

	inline constexpr std::int64_t kCronWakeNowBusyMaxWaitMs = 120'000;
	inline constexpr std::int64_t kCronWakeNowBusyRetryDelayMs = 250;

	inline constexpr std::int64_t kCronMinRefireGapMs = 2'000;
	inline constexpr std::int64_t kCronStuckRunMs = 2 * 60 * 60'000;
	inline constexpr std::int64_t kCronDefaultMissedJobStaggerMs = 5'000;
	inline constexpr std::size_t kCronDefaultMaxMissedJobsPerRestart = 5;
	inline constexpr std::int64_t kCronMaxTimerDelayMs = 60'000;
	inline constexpr std::size_t kCronDefaultMaxConcurrentRuns = 1;

	struct CronScheduleNotificationEvent {
		std::string text;
		std::string agentId;
		std::string sessionKey;
		std::string contextKey;
		std::string heartbeatWakeReason;
	};

	struct CronRealtimeEvent {
		std::string jobId;
		std::string action;
		std::optional<std::int64_t> runAtMs;
		std::optional<std::int64_t> durationMs;
		std::optional<std::int64_t> nextRunAtMs;
		std::string status;
		std::string error;
		std::string summary;
		std::optional<bool> delivered;
		std::string sessionId;
		std::string sessionKey;
	};

	struct CronSchedulerConfig {
		std::size_t maxConcurrentRuns = kCronDefaultMaxConcurrentRuns;
		std::int64_t missedJobStaggerMs = kCronDefaultMissedJobStaggerMs;
		std::size_t maxMissedJobsPerRestart = kCronDefaultMaxMissedJobsPerRestart;
	};

	CronJson CronRealtimeEventToJson(const CronRealtimeEvent& event);

	std::int64_t UtcNowMs();
	std::string ToLowerCopy(const std::string& value);
	std::string TrimCopy(const std::string& value);

	std::optional<std::int64_t> TryReadInt64Field(
		const CronJson& value,
		const char* key);

	bool IsWakeModeValid(const std::string& value);
	std::string NormalizeWakeMode(const std::string& value);
	std::string BuildCronRunId(std::int64_t nowMs);

} // namespace blazeclaw::cron
