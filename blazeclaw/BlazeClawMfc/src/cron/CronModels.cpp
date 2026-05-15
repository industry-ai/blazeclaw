#include "pch.h"

#include "CronModels.h"

#include <algorithm>
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
		return std::string("cron-run-") + std::to_string(nowMs);
	}

} // namespace blazeclaw::cron
