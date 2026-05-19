#include "pch.h"

#include "CronModels.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>

namespace blazeclaw::cron {

	std::int64_t UtcNowMs() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	std::string ToLowerCopy(const std::string& value) {
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

	std::string TrimCopy(const std::string& value) {
		std::size_t start = 0;
		std::size_t end = value.size();
		while (start < end &&
			std::isspace(static_cast<unsigned char>(value[start])) != 0) {
			++start;
		}
		while (end > start &&
			std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
			--end;
		}
		return value.substr(start, end - start);
	}

	std::optional<std::int64_t> TryReadInt64Field(
		const CronJson& value,
		const char* key) {
		if (!value.is_object() || !value.contains(key) || value[key].is_null()) {
			return std::nullopt;
		}

		const CronJson& field = value[key];
		if (field.is_number_integer()) {
			return field.get<std::int64_t>();
		}
		if (field.is_number_unsigned()) {
			return static_cast<std::int64_t>(field.get<std::uint64_t>());
		}
		if (field.is_number_float()) {
			return static_cast<std::int64_t>(field.get<double>());
		}
		if (field.is_string()) {
			try {
				return std::stoll(TrimCopy(field.get<std::string>()));
			}
			catch (...) {
				return std::nullopt;
			}
		}

		return std::nullopt;
	}

	bool IsWakeModeValid(const std::string& value) {
		const std::string normalized = ToLowerCopy(TrimCopy(value));
		return normalized == kWakeModeNow || normalized == kWakeModeNextHeartbeat;
	}

	std::string NormalizeWakeMode(const std::string& value) {
		const std::string normalized = ToLowerCopy(TrimCopy(value));
		if (IsWakeModeValid(normalized)) {
			return normalized;
		}

		return kWakeModeNow;
	}

	std::string BuildCronRunId(const std::int64_t nowMs) {
		static std::atomic<std::uint64_t> sequence{ 1 };
		const std::uint64_t current = sequence.fetch_add(1, std::memory_order_relaxed);
		return std::string("cron-run-") + std::to_string(nowMs) + "-" + std::to_string(current);
	}

	CronJson CronRealtimeEventToJson(const CronRealtimeEvent& event) {
		CronJson payload = {
			{ "jobId", event.jobId },
			{ "action", event.action }
		};
		if (event.runAtMs.has_value()) {
			payload["runAtMs"] = event.runAtMs.value();
		}
		if (event.durationMs.has_value()) {
			payload["durationMs"] = event.durationMs.value();
		}
		if (event.nextRunAtMs.has_value()) {
			payload["nextRunAtMs"] = event.nextRunAtMs.value();
		}
		if (!event.status.empty()) {
			payload["status"] = event.status;
		}
		if (!event.error.empty()) {
			payload["error"] = event.error;
		}
		if (!event.summary.empty()) {
			payload["summary"] = event.summary;
		}
		if (event.delivered.has_value()) {
			payload["delivered"] = event.delivered.value();
		}
		if (!event.sessionId.empty()) {
			payload["sessionId"] = event.sessionId;
		}
		if (!event.sessionKey.empty()) {
			payload["sessionKey"] = event.sessionKey;
		}
		return payload;
	}

} // namespace blazeclaw::cron
