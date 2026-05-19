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
