#include "pch.h"
#include "GatewayJsonUtils.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

namespace blazeclaw::gateway::json {

	namespace {

		int ParseHexDigit(const char ch) {
			if (ch >= '0' && ch <= '9') {
				return ch - '0';
			}

			if (ch >= 'a' && ch <= 'f') {
				return 10 + (ch - 'a');
			}

			if (ch >= 'A' && ch <= 'F') {
				return 10 + (ch - 'A');
			}

			return -1;
		}

	}

	std::size_t SkipWhitespace(const std::string& text, std::size_t index) {
		while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
			++index;
		}

		return index;
	}

	std::string Trim(const std::string& value) {
		std::size_t start = 0;
		std::size_t end = value.size();

		while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
			++start;
		}

		while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
			--end;
		}

		return value.substr(start, end - start);
	}

	bool ParseJsonStringAt(const std::string& text, std::size_t& index, std::string& outValue) {
		if (index >= text.size() || text[index] != '"') {
			return false;
		}

		++index;
		outValue.clear();

		while (index < text.size()) {
			const char ch = text[index++];
			if (ch == '"') {
				return true;
			}

			if (ch == '\\') {
				if (index >= text.size()) {
					return false;
				}

				const char escaped = text[index++];
				switch (escaped) {
				case '"':
				case '\\':
				case '/':
					outValue.push_back(escaped);
					break;
				case 'b':
					outValue.push_back('\b');
					break;
				case 'f':
					outValue.push_back('\f');
					break;
				case 'n':
					outValue.push_back('\n');
					break;
				case 'r':
					outValue.push_back('\r');
					break;
				case 't':
					outValue.push_back('\t');
					break;
				case 'u': {
					if (index + 4 > text.size()) {
						return false;
					}

					std::uint32_t codepoint = 0;
					for (int i = 0; i < 4; ++i) {
						const int hex = ParseHexDigit(text[index + i]);
						if (hex < 0) {
							return false;
						}

						codepoint = (codepoint << 4) | static_cast<std::uint32_t>(hex);
					}

					index += 4;
					outValue.push_back(
						static_cast<char>(codepoint <= 0x7F ? codepoint : '?'));
					break;
				}
				default:
					return false;
				}

				continue;
			}

			outValue.push_back(ch);
		}

		return false;
	}

	bool FindStringField(const std::string& text, const std::string& fieldName, std::string& outValue) {
		const std::string token = "\"" + fieldName + "\"";
		const std::size_t keyPos = text.find(token);
		if (keyPos == std::string::npos) {
			return false;
		}

		std::size_t index = keyPos + token.size();
		index = SkipWhitespace(text, index);
		if (index >= text.size() || text[index] != ':') {
			return false;
		}

		++index;
		index = SkipWhitespace(text, index);

		return ParseJsonStringAt(text, index, outValue);
	}

	bool FindRawField(const std::string& text, const std::string& fieldName, std::string& outValue) {
		const std::string token = "\"" + fieldName + "\"";
		const std::size_t keyPos = text.find(token);
		if (keyPos == std::string::npos) {
			return false;
		}

		std::size_t index = keyPos + token.size();
		index = SkipWhitespace(text, index);
		if (index >= text.size() || text[index] != ':') {
			return false;
		}

		++index;
		index = SkipWhitespace(text, index);
		if (index >= text.size()) {
			return false;
		}

		const std::size_t start = index;
		const char opener = text[index];

		if (opener == '{' || opener == '[') {
			const char closer = opener == '{' ? '}' : ']';
			int depth = 0;
			bool inString = false;

			for (; index < text.size(); ++index) {
				const char ch = text[index];
				if (inString) {
					if (ch == '\\') {
						++index;
						continue;
					}

					if (ch == '"') {
						inString = false;
					}

					continue;
				}

				if (ch == '"') {
					inString = true;
					continue;
				}

				if (ch == opener) {
					++depth;
				}
				else if (ch == closer) {
					--depth;
					if (depth == 0) {
						outValue = text.substr(start, (index - start) + 1);
						return true;
					}
				}
			}

			return false;
		}

		if (opener == '"') {
			std::string parsed;
			if (!ParseJsonStringAt(text, index, parsed)) {
				return false;
			}

			outValue = "\"" + parsed + "\"";
			return true;
		}

		while (index < text.size() && text[index] != ',' && text[index] != '}') {
			++index;
		}

		outValue = text.substr(start, index - start);
		return true;
	}

	bool FindBoolField(const std::string& text, const std::string& fieldName, bool& outValue) {
		std::string raw;
		if (!FindRawField(text, fieldName, raw)) {
			return false;
		}

		raw = Trim(raw);
		if (raw == "true") {
			outValue = true;
			return true;
		}

		if (raw == "false") {
			outValue = false;
			return true;
		}

		return false;
	}

	bool FindUInt64Field(const std::string& text, const std::string& fieldName, std::uint64_t& outValue) {
		std::string raw;
		if (!FindRawField(text, fieldName, raw)) {
			return false;
		}

		raw = Trim(raw);
		if (raw.empty()) {
			return false;
		}

		try {
			std::size_t consumed = 0;
			const std::uint64_t parsed = std::stoull(raw, &consumed);
			if (consumed != raw.size()) {
				return false;
			}

			outValue = parsed;
			return true;
		}
		catch (...) {
			return false;
		}
	}

	bool IsJsonObjectShape(const std::string& value) {
		const std::string trimmed = Trim(value);
		return trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}';
	}

	bool IsFieldValueType(const std::string& text, const std::string& fieldName, char expectedFirstChar) {
		const std::string token = "\"" + fieldName + "\"";
		const std::size_t tokenPos = text.find(token);
		if (tokenPos == std::string::npos) {
			return false;
		}

		std::size_t valuePos = text.find(':', tokenPos);
		if (valuePos == std::string::npos) {
			return false;
		}

		++valuePos;
		while (valuePos < text.size() && std::isspace(static_cast<unsigned char>(text[valuePos])) != 0) {
			++valuePos;
		}

		if (valuePos >= text.size()) {
			return false;
		}

		return text[valuePos] == expectedFirstChar;
	}

} // namespace blazeclaw::gateway::json

namespace blazeclaw::gateway::prompt {

	namespace {

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

		std::optional<std::string> TryParsePromptSendAt(
			const std::string& message) {
			static const std::regex kTwelveHourRegex(
				R"((\b\d{1,2})(?::(\d{2}))?\s*(am|pm)\b)",
				std::regex_constants::icase);
			static const std::regex kTwentyFourHourRegex(
				R"((\b\d{1,2}):(\d{2})\b)");

			std::smatch twelveHourMatch;
			if (std::regex_search(message, twelveHourMatch, kTwelveHourRegex) &&
				twelveHourMatch.size() >= 4) {
				int hour = 0;
				int minute = 0;
				try {
					hour = std::stoi(twelveHourMatch[1].str());
					minute = twelveHourMatch[2].matched
						? std::stoi(twelveHourMatch[2].str())
						: 0;
				}
				catch (...) {
					return std::nullopt;
				}

				if (hour < 1 || hour > 12 || minute < 0 || minute > 59) {
					return std::nullopt;
				}

				std::string meridiem = ToLowerCopy(twelveHourMatch[3].str());
				if (meridiem == "am") {
					hour = hour == 12 ? 0 : hour;
				}
				else {
					hour = hour == 12 ? 12 : hour + 12;
				}

				std::ostringstream time;
				time << std::setw(2) << std::setfill('0') << hour
					<< ":"
					<< std::setw(2) << std::setfill('0') << minute;
				return time.str();
			}

			std::smatch twentyFourHourMatch;
			if (std::regex_search(message, twentyFourHourMatch, kTwentyFourHourRegex) &&
				twentyFourHourMatch.size() >= 3) {
				int hour = 0;
				int minute = 0;
				try {
					hour = std::stoi(twentyFourHourMatch[1].str());
					minute = std::stoi(twentyFourHourMatch[2].str());
				}
				catch (...) {
					return std::nullopt;
				}

				if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
					return std::nullopt;
				}

				std::ostringstream time;
				time << std::setw(2) << std::setfill('0') << hour
					<< ":"
					<< std::setw(2) << std::setfill('0') << minute;
				return time.str();
			}

			return std::nullopt;
		}

		std::string ResolveCurrentLocalTimeHHmm() {
			std::time_t now = std::time(nullptr);
			std::tm localTime = {};
#if defined(_WIN32)
			localtime_s(&localTime, &now);
#else
			localtime_r(&now, &localTime);
#endif

			std::ostringstream output;
			output << std::setw(2) << std::setfill('0') << localTime.tm_hour
				<< ":"
				<< std::setw(2) << std::setfill('0') << localTime.tm_min;
			return output.str();
		}

		std::string ExtractFirstEmailAddress(const std::string& text) {
			static const std::regex kEmailRegex(
				R"(([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}))");

			std::smatch match;
			if (std::regex_search(text, match, kEmailRegex) && !match.empty()) {
				return match[1].str();
			}

			return {};
		}

		std::string NormalizePromptText(const std::string& message) {
			std::string normalized = message;
			for (char& ch : normalized) {
				switch (static_cast<unsigned char>(ch)) {
				case '\t':
				case '\r':
				case '\n':
					ch = ' ';
					break;
				default:
					break;
				}
			}

			return normalized;
		}

		bool ContainsAnyToken(
			const std::string& text,
			const std::vector<std::string>& tokens) {
			for (const auto& token : tokens) {
				if (token.empty()) {
					continue;
				}

				if (text.find(token) != std::string::npos) {
					return true;
				}
			}

			return false;
		}

		std::string ResolveDateValue(const std::string& lowered) {
			if (ContainsAnyToken(
				lowered,
				{ "today", "今天" })) {
				return "today";
			}

			if (ContainsAnyToken(
				lowered,
				{ "tomorrow", "明天" })) {
				return "tomorrow";
			}

			return "tomorrow";
		}

		std::string StripKnownDatePrefixes(std::string value) {
			std::string normalized = json::Trim(value);
			if (normalized.empty()) {
				return {};
			}

			for (const std::string& prefix : { std::string("今天"), std::string("明天") }) {
				if (normalized.rfind(prefix, 0) == 0) {
					normalized = json::Trim(normalized.substr(prefix.size()));
					break;
				}
			}

			while (!normalized.empty() &&
				normalized.rfind("的", 0) == 0) {
				normalized = json::Trim(normalized.substr(std::string("的").size()));
			}

			return normalized;
		}

		std::string ExtractExplicitLocationValue(const std::string& message) {
			static const std::regex kEnglishLocationRegex(
				R"(\b(?:in|at|for)\s+([A-Za-z][A-Za-z\-' ]{1,48}))",
				std::regex_constants::icase);
			static const std::regex kChineseLocationRegex(
				R"((?:在|查一下|查下|查询|看一下|看下)(?:(?:今天|明天)\s*)?([^，。！？；\s]{1,16}?)(?:的)?(?:天气|气温|预报|温度))");

			std::smatch chineseMatch;
			if (std::regex_search(message, chineseMatch, kChineseLocationRegex) &&
				chineseMatch.size() >= 2) {
				const std::string candidate =
					StripKnownDatePrefixes(chineseMatch[1].str());
				if (!candidate.empty()) {
					return candidate;
				}
			}

			std::smatch englishMatch;
			if (std::regex_search(message, englishMatch, kEnglishLocationRegex) &&
				englishMatch.size() >= 2) {
				std::string candidate = json::Trim(englishMatch[1].str());
				while (!candidate.empty() && std::ispunct(static_cast<unsigned char>(candidate.back())) != 0) {
					candidate.pop_back();
				}

				if (!candidate.empty()) {
					return candidate;
				}
			}

			return {};
		}

		bool IsImmediateScheduleKeyword(const std::string& lowered) {
			return ContainsAnyToken(
				lowered,
				{
					"right now",
					"immediately",
					"as soon as possible",
					"now",
					"现在",
					"马上",
					"立即",
					"尽快",
					"立刻",
					"马上发送",
					"立即发送",
				});
		}

	} // namespace

	OrchestrationStructuralSignals AnalyzeOrchestrationStructuralSignals(
		const std::string& message) {
		OrchestrationStructuralSignals signals;
		const std::string normalized = NormalizePromptText(message);
		const std::string lowered = ToLowerCopy(normalized);

		signals.hasWeatherCapabilityIntent = ContainsAnyToken(
			lowered,
			{
				"weather",
				"天气",
				"气温",
				"预报",
				"温度",
			});

		signals.hasEmailCapabilityIntent = ContainsAnyToken(
			lowered,
			{
				"email",
				"mail",
				"邮件",
				"电子邮件",
				"发邮件",
				"发送",
				"发到",
			});

		signals.hasReportIntent = ContainsAnyToken(
			lowered,
			{
				"report",
				"summary",
				"write",
				"报告",
				"简报",
				"总结",
				"写",
			});

		signals.recipient = ExtractFirstEmailAddress(normalized);
		signals.hasRecipient = !signals.recipient.empty();

		const auto parsedTime = TryParsePromptSendAt(normalized);
		if (parsedTime.has_value()) {
			signals.hasScheduleIntent = true;
			signals.sendAt = parsedTime.value();
			signals.scheduleKind = "clock_time";
		}
		else {
			if (IsImmediateScheduleKeyword(lowered) ||
				lowered.rfind("now", 0) == 0) {
				signals.hasScheduleIntent = true;
				signals.sendAt = ResolveCurrentLocalTimeHHmm();
				signals.scheduleKind = "immediate_keyword";
			}
			else {
				signals.hasScheduleIntent = false;
				signals.sendAt = "13:00";
				signals.scheduleKind = "default_fallback";
			}
		}

		signals.date = ResolveDateValue(lowered);
		signals.hasDateIntent =
			ContainsAnyToken(
				lowered,
				{
					"today",
					"tomorrow",
				 "今天",
					"明天",
				});
		signals.city = ExtractExplicitLocationValue(normalized);

		if (!signals.hasWeatherCapabilityIntent) {
			signals.missReasons.push_back("missing_weather");
		}

		if (!signals.hasEmailCapabilityIntent) {
			signals.missReasons.push_back("missing_email_action");
		}

		if (!signals.hasRecipient) {
			signals.missReasons.push_back("missing_recipient_email");
		}

		if (!signals.hasReportIntent) {
			signals.missReasons.push_back("missing_report_instruction");
		}

		if (signals.city.empty()) {
			signals.missReasons.push_back("missing_city");
		}

		if (!signals.hasScheduleIntent) {
			signals.missReasons.push_back("missing_schedule_format");
		}

		signals.weatherEmailFlowCandidate =
			signals.hasWeatherCapabilityIntent &&
			signals.hasEmailCapabilityIntent &&
			signals.hasRecipient &&
			!signals.city.empty();

		return signals;
	}

	WeatherEmailPromptIntent AnalyzeWeatherEmailPromptIntent(
		const std::string& message) {
		WeatherEmailPromptIntent intent;
		const auto signals = AnalyzeOrchestrationStructuralSignals(message);
		intent.hasWeather = signals.hasWeatherCapabilityIntent;
		intent.hasEmail = signals.hasEmailCapabilityIntent;
		intent.hasReport = signals.hasReportIntent;
		intent.hasRecipient = signals.hasRecipient;
		intent.hasSchedule = signals.hasScheduleIntent;
		intent.city = signals.city;
		intent.date = signals.date;
		intent.recipient = signals.recipient;
		intent.sendAt = signals.sendAt;
		intent.scheduleKind = signals.scheduleKind;
		intent.missReasons = signals.missReasons;
		intent.matched = signals.weatherEmailFlowCandidate;
		intent.decompositionSteps = intent.matched ? 3 : 0;
		return intent;
	}

} // namespace blazeclaw::gateway::prompt
