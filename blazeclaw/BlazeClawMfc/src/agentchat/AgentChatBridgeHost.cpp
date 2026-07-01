#include "pch.h"
#include "AgentChatBridgeHost.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <array>
#include <ctime>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

namespace blazeclaw::agentchat {
	namespace {
		using blazeclaw::gateway::protocol::RequestFrame;
		using blazeclaw::gateway::protocol::ResponseFrame;

		constexpr int kDefaultGatewayPollTimeoutMs = 600000;
		constexpr int kGatewayPollIntervalMs = 160;
		constexpr std::uint64_t kPushIdempotencyTtlMs = 6ULL * 60ULL * 60ULL * 1000ULL;
		constexpr std::size_t kPushIdempotencyMaxEntries = 2000;
		constexpr const char* kDefaultPushToken =
			"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ98s7d2b9e3c5a1f0d4";
//		constexpr const char* kDefaultChatHost = "101.132.254.212";
		constexpr const char* kDefaultChatHost = "127.0.0.1";
		constexpr std::uint16_t kDefaultChatPort = 8765;
		constexpr std::uint32_t kDefaultPushTimeoutMs = 30000;
		constexpr std::size_t kHbpcHeaderSize = 64;
		constexpr std::size_t kHbpcMaxPayloadSize = 64 * 1024;
		constexpr std::uint8_t kHbpcProtoVersion = 1;
		constexpr std::uint8_t kHbpcIrcMessageReq = 221;
		constexpr std::uint8_t kHbpcIrcMessageResp = 222;

		std::string TrimCopy(const std::string& value) {
			const auto begin = std::find_if_not(
				value.begin(),
				value.end(),
				[](unsigned char ch) {
					return std::isspace(ch) != 0;
				});
			const auto end = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](unsigned char ch) {
					return std::isspace(ch) != 0;
				}).base();
			if (begin >= end) {
				return {};
			}
			return std::string(begin, end);
		}

		AgentChatBridgeHttpResponse BuildMethodNotAllowedResponse() {
			AgentChatBridgeHttpResponse response;
			response.statusCode = 405;
			response.body = "{\"ok\":false,\"error\":\"method_not_allowed\"}";
			return response;
		}

		AgentChatBridgeHttpResponse BuildBadRequestResponse(const std::string& errorCode) {
			AgentChatBridgeHttpResponse response;
			response.statusCode = 400;
			response.body =
				"{\"ok\":false,\"error\":\"" +
				errorCode +
				"\"}";
			return response;
		}

		AgentChatBridgeHttpResponse BuildBadGatewayResponse(const std::string& errorCode) {
			AgentChatBridgeHttpResponse response;
			response.statusCode = 502;
			response.body =
				"{\"ok\":false,\"error\":\"" +
				errorCode +
				"\"}";
			return response;
		}

		std::string JsonDumpCompact(const nlohmann::json& value) {
			return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
		}

		std::string JsonStringValue(const nlohmann::json& source, const char* key) {
			if (!source.is_object() || key == nullptr || *key == '\0') {
				return {};
			}
			const auto it = source.find(key);
			if (it == source.end()) {
				return {};
			}
			if (it->is_string()) {
				return TrimCopy(it->get<std::string>());
			}
			if (it->is_number_integer()) {
				return std::to_string(it->get<long long>());
			}
			if (it->is_number_unsigned()) {
				return std::to_string(it->get<unsigned long long>());
			}
			return {};
		}

		int JsonPositiveIntValue(const nlohmann::json& source, const char* key, const int fallbackValue) {
			if (!source.is_object() || key == nullptr || *key == '\0') {
				return fallbackValue;
			}
			const auto it = source.find(key);
			if (it == source.end()) {
				return fallbackValue;
			}
			if (it->is_number_integer()) {
				const int value = it->get<int>();
				return value > 0 ? value : fallbackValue;
			}
			if (it->is_string()) {
				try {
					const int value = std::stoi(it->get<std::string>());
					return value > 0 ? value : fallbackValue;
				}
				catch (...) {
					return fallbackValue;
				}
			}
			return fallbackValue;
		}

		std::string ToUpperCopy(const std::string& value) {
			std::string upper = value;
			std::transform(
				upper.begin(),
				upper.end(),
				upper.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::toupper(ch));
				});
			return upper;
		}

		std::uint64_t CurrentEpochMilliseconds() {
			using namespace std::chrono;
			return static_cast<std::uint64_t>(duration_cast<milliseconds>(
				system_clock::now().time_since_epoch()).count());
		}

		std::string EndpointLabel(const std::string& host, const std::string& portText) {
			return host + ":" + portText;
		}

		std::uint32_t ParsePositiveUInt32(const std::string& value, const std::uint32_t fallback) {
			if (value.empty()) {
				return fallback;
			}
			try {
				const long long parsed = std::stoll(value);
				if (parsed <= 0 || parsed > static_cast<long long>((std::numeric_limits<std::uint32_t>::max)())) {
					return fallback;
				}
				return static_cast<std::uint32_t>(parsed);
			}
			catch (...) {
				return fallback;
			}
		}

		std::uint16_t ParsePort(const std::string& value, const std::uint16_t fallback) {
			if (value.empty()) {
				return fallback;
			}
			try {
				const int parsed = std::stoi(value);
				if (parsed <= 0 || parsed > 65535) {
					return fallback;
				}
				return static_cast<std::uint16_t>(parsed);
			}
			catch (...) {
				return fallback;
			}
		}

		nlohmann::json BuildPushErrorJson(const std::string& code, const std::string& message) {
			return nlohmann::json{
				{ "event", "ERROR" },
				{ "code", code },
				{ "message", message },
			};
		}

		bool StartsWithIgnoreCase(const std::string& value, const std::string& prefix) {
			if (value.size() < prefix.size()) {
				return false;
			}
			for (std::size_t index = 0; index < prefix.size(); ++index) {
				if (std::tolower(static_cast<unsigned char>(value[index])) !=
					std::tolower(static_cast<unsigned char>(prefix[index]))) {
					return false;
				}
			}
			return true;
		}

		bool IsHttpUrl(const std::string& value) {
			return StartsWithIgnoreCase(value, "http://") || StartsWithIgnoreCase(value, "https://");
		}

		std::string LastPathSegment(const std::string& value) {
			if (value.empty()) {
				return {};
			}
			const std::size_t index = value.find_last_of("/\\");
			if (index == std::string::npos || index + 1 >= value.size()) {
				return value;
			}
			return value.substr(index + 1);
		}

		int HttpStatusForPushErrorCode(const std::string& code) {
			const std::string upper = ToUpperCopy(code);
			if (upper == "INVALID_TOKEN" || upper == "NOT_AUTHENTICATED") {
				return 401;
			}
			if (upper == "BAD_REQUEST") {
				return 400;
			}
			if (upper == "ROOM_NOT_FOUND") {
				return 404;
			}
			if (upper == "INTERNAL_ERROR") {
				return 502;
			}
			return 502;
		}

		std::optional<nlohmann::json> ParseJsonObjectOrNull(const std::string& rawJson) {
			auto parsed = nlohmann::json::parse(rawJson, nullptr, false);
			if (parsed.is_discarded() || !parsed.is_object()) {
				return std::nullopt;
			}
			return std::optional<nlohmann::json>(std::move(parsed));
		}

		bool IsPersonalWorkspaceChannel(const std::string& channel) {
			static const std::regex re(
				R"((^#personal-workspace$|^#workspace[_-]))",
				std::regex::icase);
			return std::regex_search(channel, re);
		}

		std::string CurrentConversationChannel(const nlohmann::json& payload) {
			const std::string groupId = JsonStringValue(payload, "groupId");
			const std::string conversationId = JsonStringValue(payload, "conversationId");
			if (!conversationId.empty() && conversationId.front() == '#') {
				return conversationId;
			}
			if (!groupId.empty() && groupId.front() == '#') {
				return groupId;
			}
			return !groupId.empty() ? groupId : conversationId;
		}

		std::string RequesterMentionFromPayload(const nlohmann::json& payload) {
			const std::array<const char*, 5> keys = {
				"userPhone",
				"creatorPhone",
				"phone",
				"userId",
				"creatorUserId",
			};
			for (const char* key : keys) {
				const std::string value = JsonStringValue(payload, key);
				if (!value.empty()) {
					return "@" + value;
				}
			}
			return {};
		}

		std::string PrefixRequesterMention(
			const std::string& text,
			const nlohmann::json& payload) {
			const std::string channel = CurrentConversationChannel(payload);
			if (channel.empty() || channel.front() != '#') {
				return text;
			}
			if (IsPersonalWorkspaceChannel(channel)) {
				return text;
			}

			const std::string mention = RequesterMentionFromPayload(payload);
			if (mention.empty() || text.empty()) {
				return text;
			}
			if (text.rfind(mention, 0) == 0) {
				return text;
			}
			return mention + " " + text;
		}

		std::string NormalizeAgentMentionText(std::string text) {
			const std::array<const char*, 3> patterns = {
				"@炎图AI助手",
				"@炎图AI\xE2\x80\x8B助手",
				"炎图AI助手",
			};
			for (const char* pattern : patterns) {
				std::string needle(pattern);
				std::size_t pos = 0;
				while ((pos = text.find(needle, pos)) != std::string::npos) {
					text.erase(pos, needle.size());
				}
			}
			text = std::regex_replace(text, std::regex(R"(\s+)", std::regex::ECMAScript), " ");
			return TrimCopy(text);
		}

		double ParseChineseInteger(const std::string& value) {
			const std::string raw = TrimCopy(value);
			if (raw.empty()) {
				return -1;
			}
			if (raw == "半") {
				return 0.5;
			}
			if (std::regex_match(raw, std::regex(R"(^\d+$)"))) {
				return static_cast<double>(std::stoi(raw));
			}

			const std::unordered_map<std::string, int> digits = {
				{ "零", 0 },
				{ "一", 1 },
				{ "二", 2 },
				{ "两", 2 },
				{ "三", 3 },
				{ "四", 4 },
				{ "五", 5 },
				{ "六", 6 },
				{ "七", 7 },
				{ "八", 8 },
				{ "九", 9 },
			};

			if (raw == "十") {
				return 10;
			}

			const std::size_t tenIndex = raw.find("十");
			if (tenIndex != std::string::npos) {
				const std::string left = raw.substr(0, tenIndex);
				const std::string right = raw.substr(tenIndex + std::string("十").size());
				const int tens = left.empty()
					? 1
					: (digits.contains(left) ? digits.at(left) : -1);
				const int ones = right.empty()
					? 0
					: (digits.contains(right) ? digits.at(right) : -1);
				if (tens >= 0 && ones >= 0) {
					return static_cast<double>(tens * 10 + ones);
				}
			}

			if (digits.contains(raw)) {
				return static_cast<double>(digits.at(raw));
			}
			return -1;
		}

		std::uint64_t DelayMsForReminderAmount(const double amount, const std::string& unit) {
			if (amount <= 0) {
				return 0;
			}
			if (unit.rfind("秒", 0) == 0) {
				return static_cast<std::uint64_t>(amount * 1000.0);
			}
			if (unit == "分" || unit.rfind("分钟", 0) == 0) {
				return static_cast<std::uint64_t>(amount * 60.0 * 1000.0);
			}
			if (unit == "时" || unit.rfind("小时", 0) == 0) {
				return static_cast<std::uint64_t>(amount * 60.0 * 60.0 * 1000.0);
			}
			if (unit.rfind("天", 0) == 0) {
				return static_cast<std::uint64_t>(amount * 24.0 * 60.0 * 60.0 * 1000.0);
			}
			return 0;
		}

		bool IsExplicitPersonalReminderText(const std::string& text) {
			static const std::regex re(
				R"((提醒|通知|告诉|叫)\s*(一下)?\s*我|叫我|remind\s+me)",
				std::regex::icase);
			return std::regex_search(text, re);
		}

		bool IsPublishNoticeIntentText(const std::string& text) {
			static const std::regex re(
				R"(^(?:请|帮我|麻烦)?\s*(?:发布|发|发送|创建)\s*(?:一条|个)?\s*(?:通知|公告|群公告|系统通知|资讯|news)(?=\s|[:：，,。]|$))",
				std::regex::icase);
			return std::regex_search(text, re);
		}

		bool IsExplicitGroupReminderText(const std::string& rawText) {
			const std::string text = NormalizeAgentMentionText(rawText);
			if (text.empty()) {
				return false;
			}
			if (IsPublishNoticeIntentText(text)) {
				return false;
			}
			if (IsExplicitPersonalReminderText(text)) {
				return false;
			}

			const bool hasReminderKeyword = std::regex_search(
				text,
				std::regex(R"((提醒|通知|告诉|叫|定时|闹钟|timer|remind))", std::regex::icase));
			const bool hasTimeExpression =
				std::regex_search(
					text,
					std::regex(R"((\d+|半|[一二两三四五六七八九十]{1,3})\s*(秒钟|秒|分钟|分|小时|时|天)\s*(?:之后|以后|后))")) ||
				std::regex_search(
					text,
					std::regex(R"(明天\s*(早上|上午|中午|下午|晚上|夜里)?\s*(\d{1,2}|[一二两三四五六七八九十]{1,3})\s*(?:点|时))")) ||
				std::regex_search(
					text,
					std::regex(R"((?:今天|今日|今晚|晚上|下午|中午|上午|早上)?\s*(\d{1,2}|[一二两三四五六七八九十]{1,3})(?::|：|点|时)(\d{1,2}|半)?)"));

			return std::regex_search(
				text,
				std::regex(R"((提醒|通知|告诉|叫)\s*(一下)?\s*(大家|全体|所有人|每个人)|提醒大家|通知大家|群里|集合)", std::regex::icase)) ||
				(hasReminderKeyword && hasTimeExpression);
		}

		bool IsObsoleteAgentPushChannel(const std::string& channel) {
			return channel == "#group-posts-demo" || channel == "#personal-workspace";
		}

		bool IsDirectAgentPushCandidate(const nlohmann::json& body) {
			if (!body.is_object()) {
				return false;
			}

			const std::string channelCandidate = JsonStringValue(body, "channel").empty()
				? JsonStringValue(body, "conversationId")
				: JsonStringValue(body, "channel");
			const std::string cmd = ToUpperCopy(JsonStringValue(body, "cmd"));
			const bool typing = body.value("typing", false);
			const bool hasMessage = !JsonStringValue(body, "message").empty();
			return cmd == "AGENT_PUSH" ||
				typing ||
				(!channelCandidate.empty() && channelCandidate.front() == '#' && hasMessage);
		}

		std::optional<nlohmann::json> ParseJsonObjectFromAny(const nlohmann::json& value) {
			if (value.is_object()) {
				return std::optional<nlohmann::json>(value);
			}
			if (!value.is_string()) {
				return std::nullopt;
			}

			const std::string text = TrimCopy(value.get<std::string>());
			if (text.empty()) {
				return std::nullopt;
			}

			auto parsed = nlohmann::json::parse(text, nullptr, false);
			if (!parsed.is_discarded() && parsed.is_object()) {
				return std::optional<nlohmann::json>(std::move(parsed));
			}

			const std::size_t first = text.find('{');
			const std::size_t last = text.rfind('}');
			if (first == std::string::npos || last == std::string::npos || last <= first) {
				return std::nullopt;
			}
			parsed = nlohmann::json::parse(text.substr(first, last - first + 1), nullptr, false);
			if (parsed.is_discarded() || !parsed.is_object()) {
				return std::nullopt;
			}
			return std::optional<nlohmann::json>(std::move(parsed));
		}

		nlohmann::json ResolvePushRequestBody(
			const nlohmann::json& body,
			const nlohmann::json& inheritedToken,
			const int depth = 0) {
			if (depth > 4) {
				return nlohmann::json::object();
			}

			auto parsed = ParseJsonObjectFromAny(body);
			if (!parsed.has_value()) {
				return nlohmann::json::object();
			}

			nlohmann::json candidate = parsed.value();
			nlohmann::json token = inheritedToken;
			if (candidate.contains("token")) {
				token = candidate["token"];
			}

			if (IsDirectAgentPushCandidate(candidate)) {
				if (!candidate.contains("token") && !token.is_null()) {
					candidate["token"] = token;
				}
				return candidate;
			}

			static const std::array<const char*, 9> wrapperKeys = {
				"payload",
				"job",
				"summary",
				"message",
				"text",
				"data",
				"result",
				"output",
				"body",
			};

			for (const char* key : wrapperKeys) {
				if (!candidate.contains(key)) {
					continue;
				}
				nlohmann::json found = ResolvePushRequestBody(candidate[key], token, depth + 1);
				if (found.is_object() && !found.empty()) {
					return found;
				}
			}

			if (!candidate.contains("token") && !token.is_null()) {
				candidate["token"] = token;
			}
			return candidate;
		}

		struct AgentReminderIntent {
			std::uint64_t delayMs = 0;
			std::string delayLabel;
			std::string reminderText;
		};

		bool TryParseAgentReminderIntent(
			const std::string& rawText,
			AgentReminderIntent& intentOut) {
			const std::string text = NormalizeAgentMentionText(rawText);
			if (text.empty()) {
				return false;
			}

			if (!std::regex_search(
				text,
				std::regex(R"((提醒|叫我|通知|告诉|闹钟|定时|timer|remind))", std::regex::icase))) {
				return false;
			}
			if (!IsExplicitPersonalReminderText(text)) {
				return false;
			}

			auto cleanReminderText = [](std::string reminderText) {
			reminderText = std::regex_replace(
				reminderText,
				std::regex(R"(^(请|帮我|麻烦)?\s*(到时|到时候)?\s*(提醒|叫|通知|告诉)\s*(一下)?\s*我?)"),
				"");
			reminderText = std::regex_replace(
				reminderText,
				std::regex(R"((请|帮我|麻烦)?\s*(提醒|叫|通知|告诉)\s*(一下)?\s*我?$)"),
				"");
			reminderText = std::regex_replace(reminderText, std::regex(R"([，。！？!,.、\s]+$)"), "");
			reminderText = std::regex_replace(reminderText, std::regex(R"(^[，。！？!,.、\s]+)"), "");
			return TrimCopy(reminderText);
			};

			std::smatch match;
			const std::regex relativeRe(
				R"((\d+|半|[一二两三四五六七八九十]{1,3})\s*(秒钟|秒|分钟|分|小时|时|天)\s*(?:之后|以后|后))");
			if (std::regex_search(text, match, relativeRe)) {
				const double amount = ParseChineseInteger(match[1].str());
				if (amount <= 0) {
					return false;
				}
				const std::string unit = match[2].str();
				const std::uint64_t delayMs = DelayMsForReminderAmount(amount, unit);
				if (delayMs == 0 || delayMs > (30ULL * 24ULL * 60ULL * 60ULL * 1000ULL)) {
					return false;
				}

				const std::size_t begin = static_cast<std::size_t>(match.position());
				const std::size_t end = begin + static_cast<std::size_t>(match.length());
				const std::string after = text.substr((std::min)(end, text.size()));
				const std::string before = text.substr(0, begin);
				const std::string reminderText =
					cleanReminderText(after.empty() ? before : after);

				intentOut.delayMs = delayMs;
				intentOut.delayLabel = match.str();
				intentOut.reminderText = reminderText.empty() ? "这件事" : reminderText;
				return true;
			}

			const std::regex tomorrowClockRe(
				R"(明天\s*(早上|上午|中午|下午|晚上|夜里)?\s*(\d{1,2}|[一二两三四五六七八九十]{1,3})\s*(?:点|时)\s*(半)?)");
			if (std::regex_search(text, match, tomorrowClockRe)) {
				const std::string period = match[1].str();
				const double hourRaw = ParseChineseInteger(match[2].str());
				if (hourRaw < 0) {
					return false;
				}
				int hour = static_cast<int>(hourRaw);
				int minute = match[3].str().empty() ? 0 : 30;
				if ((period == "下午" || period == "晚上" || period == "夜里") && hour < 12) {
					hour += 12;
				}
				if (period == "中午" && hour < 11) {
					hour += 12;
				}
				if (hour == 24) {
					hour = 0;
				}
				if (hour < 0 || hour > 23) {
					return false;
				}

				auto now = std::chrono::system_clock::now();
				std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
				std::tm localTm{};
				localtime_s(&localTm, &nowTime);
				localTm.tm_mday += 1;
				localTm.tm_hour = hour;
				localTm.tm_min = minute;
				localTm.tm_sec = 0;
				const std::time_t targetTime = std::mktime(&localTm);
				if (targetTime <= 0) {
					return false;
				}
				const auto target = std::chrono::system_clock::from_time_t(targetTime);
				const auto delayMsChrono = std::chrono::duration_cast<std::chrono::milliseconds>(
					target - now).count();
				if (delayMsChrono <= 0 || delayMsChrono > static_cast<long long>(30ULL * 24ULL * 60ULL * 60ULL * 1000ULL)) {
					return false;
				}

				const std::size_t begin = static_cast<std::size_t>(match.position());
				const std::size_t end = begin + static_cast<std::size_t>(match.length());
				const std::string after = text.substr((std::min)(end, text.size()));
				const std::string before = text.substr(0, begin);
				const std::string reminderText =
					cleanReminderText(after.empty() ? before : after);

				intentOut.delayMs = static_cast<std::uint64_t>(delayMsChrono);
				intentOut.delayLabel = match.str();
				intentOut.reminderText = reminderText.empty() ? "这件事" : reminderText;
				return true;
			}

			const std::regex sameDayClockRe(
				R"((?:今天|今日|今晚|晚上|下午|中午|上午|早上)?\s*(\d{1,2}|[一二两三四五六七八九十]{1,3})(?::|：|点|时)(\d{1,2}|半)?)");
			if (std::regex_search(text, match, sameDayClockRe)) {
				const std::string matched = match.str();
				const std::string period =
					(matched.find("下午") != std::string::npos ||
						matched.find("晚上") != std::string::npos ||
						matched.find("今晚") != std::string::npos)
					? "下午"
					: ((matched.find("中午") != std::string::npos) ? "中午" : "");

				const double hourRaw = ParseChineseInteger(match[1].str());
				if (hourRaw < 0) {
					return false;
				}

				int hour = static_cast<int>(hourRaw);
				if ((period == "下午") && hour < 12) {
					hour += 12;
				}
				if ((period == "中午") && hour < 11) {
					hour += 12;
				}
				if (hour == 24) {
					hour = 0;
				}
				if (hour < 0 || hour > 23) {
					return false;
				}

				int minute = 0;
				const std::string minuteRaw = match[2].str();
				if (minuteRaw == "半") {
					minute = 30;
				}
				else if (!minuteRaw.empty()) {
					try {
						minute = std::stoi(minuteRaw);
					}
					catch (...) {
						return false;
					}
				}
				if (minute < 0 || minute > 59) {
					return false;
				}

				auto now = std::chrono::system_clock::now();
				std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
				std::tm localTm{};
				localtime_s(&localTm, &nowTime);
				localTm.tm_hour = hour;
				localTm.tm_min = minute;
				localTm.tm_sec = 0;
				const std::time_t targetTime = std::mktime(&localTm);
				if (targetTime <= 0) {
					return false;
				}
				const auto target = std::chrono::system_clock::from_time_t(targetTime);
				const auto delayMsChrono = std::chrono::duration_cast<std::chrono::milliseconds>(
					target - now).count();
				if (delayMsChrono <= 0 || delayMsChrono > static_cast<long long>(30ULL * 24ULL * 60ULL * 60ULL * 1000ULL)) {
					return false;
				}

				const std::size_t begin = static_cast<std::size_t>(match.position());
				const std::size_t end = begin + static_cast<std::size_t>(match.length());
				const std::string after = text.substr((std::min)(end, text.size()));
				const std::string before = text.substr(0, begin);
				const std::string reminderText =
					cleanReminderText(after.empty() ? before : after);

				intentOut.delayMs = static_cast<std::uint64_t>(delayMsChrono);
				intentOut.delayLabel = TrimCopy(match.str());
				intentOut.reminderText = reminderText.empty() ? "这件事" : reminderText;
				return true;
			}

			return false;
		}

		bool TryParseGroupReminderIntent(
			const std::string& rawText,
			AgentReminderIntent& intentOut) {
			if (!IsExplicitGroupReminderText(rawText)) {
				return false;
			}

			const std::string text = NormalizeAgentMentionText(rawText);
			std::smatch match;
			const std::regex relativeRe(
				R"((\d+|半|[一二两三四五六七八九十]{1,3})\s*(秒钟|秒|分钟|分|小时|时|天)\s*(?:之后|以后|后))");
			if (std::regex_search(text, match, relativeRe)) {
				const double amount = ParseChineseInteger(match[1].str());
				if (amount <= 0) {
					return false;
				}
				const std::uint64_t delayMs = DelayMsForReminderAmount(amount, match[2].str());
				if (delayMs == 0 || delayMs > (30ULL * 24ULL * 60ULL * 60ULL * 1000ULL)) {
					return false;
				}
				const std::size_t begin = static_cast<std::size_t>(match.position());
				const std::size_t end = begin + static_cast<std::size_t>(match.length());
				std::string reminderText = text.substr((std::min)(end, text.size()));
				if (TrimCopy(reminderText).empty()) {
					reminderText = text.substr(0, begin);
				}
				reminderText = std::regex_replace(
					reminderText,
					std::regex(R"((请|帮我|麻烦)?\s*(提醒|叫|通知|告诉)\s*(一下)?\s*(大家|全体|所有人|每个人)?$)"),
					"");
				reminderText = std::regex_replace(reminderText, std::regex(R"([，。！？!,.、\s]+$)"), "");
				intentOut.delayMs = delayMs;
				intentOut.delayLabel = match.str();
				intentOut.reminderText = TrimCopy(reminderText).empty() ? "这件事" : TrimCopy(reminderText);
				return true;
			}

			AgentReminderIntent fallback;
			if (!TryParseAgentReminderIntent(rawText, fallback)) {
				return false;
			}
			intentOut = std::move(fallback);
			return true;
		}

		std::string ToIsoUtcString(const std::uint64_t epochMs) {
			const auto now = std::chrono::system_clock::time_point(
				std::chrono::milliseconds(epochMs));
			const std::time_t tt = std::chrono::system_clock::to_time_t(now);
			std::tm utcTm{};
			gmtime_s(&utcTm, &tt);
			std::ostringstream oss;
			oss << std::put_time(&utcTm, "%Y-%m-%dT%H:%M:%SZ");
			return oss.str();
		}

		std::string GenerateRequestId(const std::string& prefix) {
			return prefix + "-" + std::to_string(CurrentEpochMilliseconds());
		}

		std::string Base64Encode(const std::string& raw) {
			static constexpr char chars[] =
				"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string encoded;
			int val = 0;
			int valb = -6;
			for (unsigned char c : raw) {
				val = (val << 8) + c;
				valb += 8;
				while (valb >= 0) {
					encoded.push_back(chars[(val >> valb) & 0x3F]);
					valb -= 6;
				}
			}
			if (valb > -6) {
				encoded.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
			}
			while (encoded.size() % 4 != 0) {
				encoded.push_back('=');
			}
			return encoded;
		}

		void AppendSseEvent(
			std::string& stream,
			const nlohmann::json& payload) {
			stream += "data: ";
			stream += JsonDumpCompact(payload);
			stream += "\n\n";
		}

		std::string ExtractTextFromContentNode(const nlohmann::json& contentNode) {
			if (contentNode.is_string()) {
				return contentNode.get<std::string>();
			}
			if (contentNode.is_array()) {
				std::string combined;
				for (const auto& part : contentNode) {
					if (!part.is_object()) {
						continue;
					}
					const std::string text = JsonStringValue(part, "text");
					if (text.empty()) {
						continue;
					}
					if (!combined.empty()) {
						combined += "\n";
					}
					combined += text;
				}
				return combined;
			}
			if (contentNode.is_object()) {
				return JsonStringValue(contentNode, "text");
			}
			return {};
		}

		std::string ExtractTextFromChatEventMessage(const nlohmann::json& messageNode) {
			if (!messageNode.is_object()) {
				return {};
			}

			const auto contentIt = messageNode.find("content");
			if (contentIt != messageNode.end()) {
				const std::string text = ExtractTextFromContentNode(*contentIt);
				if (!text.empty()) {
					return TrimCopy(text);
				}
			}

			const std::string directText = JsonStringValue(messageNode, "text");
			if (!directText.empty()) {
				return directText;
			}

			return JsonStringValue(messageNode, "message");
		}

		std::string GetEnv(const char* name) {
			if (name == nullptr || *name == '\0') {
				return {};
			}
			char* value = nullptr;
			size_t length = 0;
			if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
				return {};
			}
			std::string result(value);
			free(value);
			return result;
		}
	}

	AgentChatBridgeHost::AgentChatBridgeHost() = default;

	void AgentChatBridgeHost::SetOrchestratorAdapter(AgentChatOrchestratorAdapterPtr adapter) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_orchestratorAdapter = std::move(adapter);
	}

	void AgentChatBridgeHost::SetGatewayRequestRouter(GatewayRouter router) {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto callbackAdapter =
			std::dynamic_pointer_cast<CallbackAgentChatOrchestratorAdapter>(m_orchestratorAdapter);
		if (!callbackAdapter) {
			callbackAdapter = std::make_shared<CallbackAgentChatOrchestratorAdapter>();
			m_orchestratorAdapter = callbackAdapter;
		}
		callbackAdapter->SetRouter(std::move(router));
	}

	bool AgentChatBridgeHost::Initialize(const AgentChatBridgeConfig& config) {
		Shutdown();
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_config = config;
			if (!m_config.allowNonLoopbackHttpBind &&
				m_config.bindAddress != "127.0.0.1" &&
				m_config.bindAddress != "localhost") {
				m_running = false;
				return false;
			}
			if (!m_config.enabled) {
				m_running = false;
				return false;
			}

			std::filesystem::path resolvedStateRoot = m_config.stateRoot;
			if (resolvedStateRoot.empty()) {
				resolvedStateRoot = AgentChatBridgeStateStore::ResolveDefaultStateRoot();
			}
			m_stateStore.emplace(
				std::move(resolvedStateRoot),
				m_config.legacyStateRoot,
				m_config.legacyStateMigrationEnabled);
			m_stateStore->EnsureInitialized();
			m_stateStore->MigrateLegacyOpenClawStateIfNeeded();

			if (m_config.enableHttpListener) {
				auto listener = std::make_unique<AgentChatBridgeHttpListener>();
				const bool started = listener->Start(
					m_config.bindAddress,
					m_config.port,
					[this](const std::string& method, const std::string& path, const std::string& body) {
						return HandleRequest(method, path, body);
					});
				if (!started) {
					m_stateStore.reset();
					m_running = false;
					return false;
				}
				m_httpListener = std::move(listener);
			}

			m_running = true;
		}
		return true;
	}

	void AgentChatBridgeHost::Shutdown() {
		std::unique_ptr<AgentChatBridgeHttpListener> listenerToStop;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_running = false;
			listenerToStop = std::move(m_httpListener);
			m_stateStore.reset();
		}
		if (listenerToStop) {
			listenerToStop->Stop();
		}
	}

	bool AgentChatBridgeHost::IsRunning() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_running;
	}

	const AgentChatBridgeConfig& AgentChatBridgeHost::Config() const noexcept {
		return m_config;
	}

	std::string AgentChatBridgeHost::ResolveGatewayUrl() const {
		const std::string blazeclawGateway = GetEnv("BLAZECLAW_GATEWAY_URL");
		if (!blazeclawGateway.empty()) {
			return blazeclawGateway;
		}
		const std::string openclawGateway = GetEnv("OPENCLAW_GATEWAY_URL");
		if (!openclawGateway.empty()) {
			return openclawGateway;
		}
		return "ws://127.0.0.1:18789";
	}

	AgentChatBridgeHttpResponse AgentChatBridgeHost::HandleRequest(
		const std::string& method,
		const std::string& path,
		const std::string& requestBodyJson) const {
		bool running = false;
		bool allowAliases = true;
		bool enableUiInProcessAgentPath = true;
		bool enableHttpPushIngress = true;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			running = m_running;
			allowAliases = m_config.compatibilityOpenClawAliases;
			enableUiInProcessAgentPath = m_config.enableUiInProcessAgentPath;
			enableHttpPushIngress = m_config.enableHttpPushIngress;
		}

		if (path == AgentChatBridgeProtocol::kHealthPath) {
			if (method != "GET") {
				return BuildMethodNotAllowedResponse();
			}

			return AgentChatBridgeProtocol::BuildHealthResponse(
				ResolveGatewayUrl(),
				running);
		}

		if (AgentChatBridgeProtocol::IsAgentPath(
			path,
			allowAliases)) {
			if (enableUiInProcessAgentPath) {
				return AgentChatBridgeProtocol::BuildNotFoundResponse();
			}
			if (method == "OPTIONS") {
				AgentChatBridgeHttpResponse response;
				response.statusCode = 204;
				response.body = "{\"ok\":true}";
				return response;
			}

			if (method != "POST") {
				return BuildMethodNotAllowedResponse();
			}

			const nlohmann::json payload =
				nlohmann::json::parse(requestBodyJson, nullptr, false);
			if (payload.is_discarded() || !payload.is_object()) {
				return BuildBadRequestResponse("invalid_json");
			}
			return HandleAgentRequest(payload);
		}

		if (AgentChatBridgeProtocol::IsPushPath(
			path,
			allowAliases)) {
			if (!enableHttpPushIngress) {
				return AgentChatBridgeProtocol::BuildNotFoundResponse();
			}
			if (method == "OPTIONS") {
				AgentChatBridgeHttpResponse response;
				response.statusCode = 204;
				response.body = "{\"ok\":true}";
				return response;
			}

			if (method != "POST") {
				return BuildMethodNotAllowedResponse();
			}

			const nlohmann::json payload =
				nlohmann::json::parse(requestBodyJson, nullptr, false);
			if (payload.is_discarded() || !payload.is_object()) {
				return BuildBadRequestResponse("invalid_json");
			}
			return HandlePushRequest(payload);
		}

		if (AgentChatBridgeProtocol::IsCollaborationPath(path)) {
			if (method == "OPTIONS") {
				AgentChatBridgeHttpResponse response;
				response.statusCode = 204;
				response.body = "{\"ok\":true}";
				return response;
			}

			nlohmann::json payload = nlohmann::json::object();
			if (method == "POST") {
				payload = nlohmann::json::parse(requestBodyJson, nullptr, false);
				if (payload.is_discarded() || !payload.is_object()) {
					return BuildBadRequestResponse("invalid_json");
				}
			}
			return HandleCollaborationRequest(method, path, payload);
		}

		if (AgentChatBridgeProtocol::IsTtsSynthesizePath(path)) {
			if (method == "OPTIONS") {
				AgentChatBridgeHttpResponse response;
				response.statusCode = 204;
				response.body = "{\"ok\":true}";
				return response;
			}
			if (method != "POST") {
				return BuildMethodNotAllowedResponse();
			}
			const nlohmann::json payload =
				nlohmann::json::parse(requestBodyJson, nullptr, false);
			if (payload.is_discarded() || !payload.is_object()) {
				return BuildBadRequestResponse("invalid_json");
			}
			return HandleTtsSynthesizeRequest(payload);
		}

		return AgentChatBridgeProtocol::BuildNotFoundResponse();
	}

	AgentChatBridgeHttpResponse AgentChatBridgeHost::HandleAgentRequest(
		const nlohmann::json& payload) const {
		const std::string message = JsonStringValue(payload, "message");
		if (message.empty()) {
			return BuildBadRequestResponse("message_required");
		}

		AgentReminderIntent reminderIntent;
		if (TryParseAgentReminderIntent(message, reminderIntent)) {
			const std::string channel = CurrentConversationChannel(payload);
			const std::string pushUrl = JsonStringValue(payload, "pushUrl").empty()
				? (std::string("http://127.0.0.1:") + std::to_string(Config().port) + "/api/blazeclaw-agent-push")
				: JsonStringValue(payload, "pushUrl");
			const std::string token = ResolvePushToken(nlohmann::json::object());
			if (!channel.empty() && !token.empty()) {
				const std::uint64_t nowMs = CurrentEpochMilliseconds();
				const std::uint64_t triggerAtMs = nowMs + reminderIntent.delayMs;
				const std::string triggerAtIso = ToIsoUtcString(triggerAtMs);
				const std::string agentRequestId = GenerateRequestId("agentchat-reminder");
				const std::string messageId = JsonStringValue(payload, "messageId");
				const std::string suffix = messageId.empty() ? agentRequestId : messageId;
				const std::string idempotencyKey =
					"blazeclaw:personal-reminder:" +
					channel +
					":" +
					triggerAtIso +
					":" +
					suffix;

				nlohmann::json pushPayload = {
					{ "cmd", "AGENT_PUSH" },
					{ "token", token },
					{ "idempotencyKey", idempotencyKey },
					{ "agentRequestId", agentRequestId },
					{ "channel", channel },
					{ "message", PrefixRequesterMention("提醒：" + reminderIntent.reminderText, payload) },
					{ "attachments", nlohmann::json::array() },
					{ "scope", "personal" },
					{ "eventType", "personal_task_due" },
					{ "conversationId", JsonStringValue(payload, "conversationId").empty() ? channel : JsonStringValue(payload, "conversationId") },
					{ "deliverTo", nlohmann::json{ { "type", "conversation" }, { "channel", channel } } },
				};
				const std::string creatorUserId = JsonStringValue(payload, "userId");
				if (!creatorUserId.empty()) {
					pushPayload["creatorUserId"] = creatorUserId;
				}
				const std::string personalTaskId = JsonStringValue(payload, "personalTaskId");
				if (!personalTaskId.empty()) {
					pushPayload["personalTaskId"] = personalTaskId;
				}
				const std::string reminderId = JsonStringValue(payload, "reminderId");
				if (!reminderId.empty()) {
					pushPayload["reminderId"] = reminderId;
				}

				const std::string pushBodyB64 = Base64Encode(JsonDumpCompact(pushPayload));
				const std::string cronMessage =
					"Execute this exact PowerShell one-liner using the exec tool.\n"
					"Do not modify, decode, or ask questions.\n"
					"Reply only AGENTCHAT_PUSH_OK when done.\n\n"
					"$body = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String('" +
					pushBodyB64 +
					"'));\n"
					"Invoke-RestMethod -Uri '" +
					pushUrl +
					"' -Method Post -ContentType 'application/json; charset=utf-8' -Body $body";

				nlohmann::json cronParams = {
					{ "name", "agentchat-reminder-" + std::to_string(nowMs) },
					{ "description", "AgentChat reminder for " + channel },
					{ "enabled", true },
					{ "deleteAfterRun", true },
					{ "schedule", nlohmann::json{ { "kind", "at" }, { "at", triggerAtIso } } },
					{ "sessionTarget", "isolated" },
					{ "wakeMode", "now" },
					{ "payload", nlohmann::json{
						{ "kind", "agentTurn" },
						{ "message", cronMessage },
						{ "timeoutSeconds", 30 },
						{ "toolsAllow", nlohmann::json::array({ "exec" }) },
					} },
					{ "delivery", nlohmann::json{ { "mode", "none" } } },
				};

				const RequestFrame addRequest{
					.id = "agentchat-native-cron-add",
					.method = "cron.add",
					.paramsJson = JsonDumpCompact(cronParams),
				};
				const auto addResponse = RouteGatewayRequest(addRequest);
				if (addResponse.has_value() && addResponse->ok) {
					const std::string deliveryText = IsPersonalWorkspaceChannel(channel)
						? "我会提醒你"
						: "我会在这个群里提醒你";
					const std::string replyText = PrefixRequesterMention(
						std::string("已设置提醒！\n\n") +
						reminderIntent.delayLabel +
						"，" +
						deliveryText +
						reminderIntent.reminderText,
						payload);

					const bool stream = payload.value("stream", false);
					if (stream) {
						std::string ssePayload;
						AppendSseEvent(ssePayload, nlohmann::json{
							{ "type", "delta" },
							{ "text", replyText },
						});
						AppendSseEvent(ssePayload, nlohmann::json{
							{ "type", "final" },
							{ "text", replyText },
							{ "state", "final" },
						});
						AgentChatBridgeHttpResponse response;
						response.statusCode = 200;
						response.contentType = "text/event-stream; charset=utf-8";
						response.body = ssePayload;
						return response;
					}

					AgentChatBridgeHttpResponse response;
					response.statusCode = 200;
					response.body = JsonDumpCompact(nlohmann::json{
						{ "ok", true },
						{ "source", "blazeclaw" },
						{ "runId", agentRequestId },
						{ "text", replyText },
						{ "toolCalls", nlohmann::json::array() },
					});
					return response;
				}
			}
		}

		AgentReminderIntent groupReminderIntent;
		if (TryParseGroupReminderIntent(message, groupReminderIntent)) {
			const std::string channel = CurrentConversationChannel(payload);
			if (!channel.empty() && !IsObsoleteAgentPushChannel(channel)) {
				const std::string pushUrl = JsonStringValue(payload, "pushUrl").empty()
					? (std::string("http://127.0.0.1:") + std::to_string(Config().port) + "/api/blazeclaw-agent-push")
					: JsonStringValue(payload, "pushUrl");
				const std::string token = ResolvePushToken(nlohmann::json::object());
				if (!token.empty()) {
					const std::uint64_t nowMs = CurrentEpochMilliseconds();
					const std::uint64_t triggerAtMs = nowMs + groupReminderIntent.delayMs;
					const std::string triggerAtIso = ToIsoUtcString(triggerAtMs);
					const std::string agentRequestId = GenerateRequestId("agentchat-reminder");
					const std::string postId = JsonStringValue(payload, "postId");
					const std::string suffix = JsonStringValue(payload, "messageId").empty()
						? agentRequestId
						: JsonStringValue(payload, "messageId");
					const std::string idempotencyPrefix = postId.empty()
						? "blazeclaw:group-reminder"
						: "blazeclaw:group-task";
					const std::string idempotencyKey =
						idempotencyPrefix +
						":" +
						channel +
						":" +
						triggerAtIso +
						":" +
						suffix;
					const std::string conversationId = JsonStringValue(payload, "conversationId").empty()
						? channel
						: JsonStringValue(payload, "conversationId");

					nlohmann::json attachments = nlohmann::json::array();
					if (!postId.empty()) {
						const std::string creatorUserIdRaw = JsonStringValue(payload, "userId");
						const std::string createdById = creatorUserIdRaw.empty() ? "blazeclaw" : creatorUserIdRaw;
						const std::string userName = JsonStringValue(payload, "userName");
						const std::string createdByName = !userName.empty()
							? userName
							: (JsonStringValue(payload, "createdByName").empty()
								? createdById
								: JsonStringValue(payload, "createdByName"));
						std::string title = JsonStringValue(payload, "title");
						if (title.empty()) {
							title = "群任务提醒";
						}
						if (title.size() > 80) {
							title = title.substr(0, 80);
						}
						attachments.push_back(nlohmann::json{
							{ "type", "native_post" },
							{ "postId", postId },
							{ "conversationId", conversationId },
							{ "title", title },
							{ "summary", std::string("定时提醒：") + groupReminderIntent.delayLabel + "。" + groupReminderIntent.reminderText },
							{ "taskKind", "task" },
							{ "actionType", "create" },
							{ "resourceType", "task" },
							{ "resourceUrl", "" },
							{ "deadlineAt", triggerAtMs },
							{ "status", "published" },
							{ "visibility", "detail_only" },
							{ "createdAt", nowMs },
							{ "createdById", createdById },
							{ "createdByName", createdByName.empty() ? std::string("炎图AI助手") : createdByName },
						});
					}

					nlohmann::json notification = nlohmann::json{
						{ "scope", "group_members" },
						{ "title", "群任务提醒" },
						{ "body", std::string("提醒：") + groupReminderIntent.reminderText },
						{ "conversationId", conversationId },
						{ "triggerAt", triggerAtMs },
					};
					if (!postId.empty()) {
						notification["postId"] = postId;
					}

					nlohmann::json pushPayload = {
						{ "cmd", "AGENT_PUSH" },
						{ "token", token },
						{ "idempotencyKey", idempotencyKey },
						{ "agentRequestId", agentRequestId },
						{ "channel", channel },
						{ "message", std::string("提醒：") + groupReminderIntent.reminderText },
						{ "attachments", attachments },
						{ "scope", "group" },
						{ "eventType", "group_reminder_due" },
						{ "conversationId", conversationId },
						{ "deliverTo", nlohmann::json{ { "type", "conversation" }, { "channel", channel } } },
						{ "notifyMembers", true },
						{ "notification", notification },
					};

					if (!postId.empty()) {
						pushPayload["postId"] = postId;
						pushPayload["eventType"] = "group_task_due";
					}

					const std::string creatorUserId = JsonStringValue(payload, "userId");
					if (!creatorUserId.empty()) {
						pushPayload["creatorUserId"] = creatorUserId;
					}
					const std::string reminderId = JsonStringValue(payload, "reminderId");
					if (!reminderId.empty()) {
						pushPayload["reminderId"] = reminderId;
					}
					else {
						pushPayload["reminderId"] = GenerateRequestId("rem");
					}

					const std::string pushBodyB64 = Base64Encode(JsonDumpCompact(pushPayload));
					const std::string cronMessage =
						"Execute this exact PowerShell one-liner using the exec tool.\n"
						"Do not modify, decode, or ask questions.\n"
						"Reply only AGENTCHAT_PUSH_OK when done.\n\n"
						"$body = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String('" +
						pushBodyB64 +
						"'));\n"
						"Invoke-RestMethod -Uri '" +
						pushUrl +
						"' -Method Post -ContentType 'application/json; charset=utf-8' -Body $body";

					nlohmann::json cronParams = {
						{ "name", "agentchat-reminder-" + std::to_string(nowMs) },
						{ "description", "AgentChat reminder for " + channel },
						{ "enabled", true },
						{ "deleteAfterRun", true },
						{ "schedule", nlohmann::json{ { "kind", "at" }, { "at", triggerAtIso } } },
						{ "sessionTarget", "isolated" },
						{ "wakeMode", "now" },
						{ "payload", nlohmann::json{
							{ "kind", "agentTurn" },
							{ "message", cronMessage },
							{ "timeoutSeconds", 30 },
							{ "toolsAllow", nlohmann::json::array({ "exec" }) },
						} },
						{ "delivery", nlohmann::json{ { "mode", "none" } } },
					};

					const RequestFrame addRequest{
						.id = "agentchat-native-cron-add-group",
						.method = "cron.add",
						.paramsJson = JsonDumpCompact(cronParams),
					};
					const auto addResponse = RouteGatewayRequest(addRequest);
					if (addResponse.has_value() && addResponse->ok) {
						const std::string replyText =
							std::string("已设置提醒！\n\n") +
							groupReminderIntent.delayLabel +
							"，我会在这个群里提醒" +
							groupReminderIntent.reminderText;

						const bool stream = payload.value("stream", false);
						if (stream) {
							std::string ssePayload;
							AppendSseEvent(ssePayload, nlohmann::json{
								{ "type", "delta" },
								{ "text", replyText },
							});
							AppendSseEvent(ssePayload, nlohmann::json{
								{ "type", "final" },
								{ "text", replyText },
								{ "state", "final" },
							});
							AgentChatBridgeHttpResponse response;
							response.statusCode = 200;
							response.contentType = "text/event-stream; charset=utf-8";
							response.body = ssePayload;
							return response;
						}

						AgentChatBridgeHttpResponse response;
						response.statusCode = 200;
						response.body = JsonDumpCompact(nlohmann::json{
							{ "ok", true },
							{ "source", "blazeclaw" },
							{ "runId", agentRequestId },
							{ "text", replyText },
							{ "toolCalls", nlohmann::json::array() },
						});
						return response;
					}
				}
			}
		}

		const bool stream = payload.value("stream", false);
		std::string ssePayload;

		nlohmann::json chatSendParams = nlohmann::json::object();
		chatSendParams["sessionKey"] = JsonStringValue(payload, "sessionKey").empty()
			? "main"
			: JsonStringValue(payload, "sessionKey");
		chatSendParams["message"] = message;
		chatSendParams["deliver"] = false;
		const std::string idempotencyKey = JsonStringValue(payload, "idempotencyKey");
		if (!idempotencyKey.empty()) {
			chatSendParams["idempotencyKey"] = idempotencyKey;
		}

		const RequestFrame sendRequest{
			.id = "agentchat-native-chat-send",
			.method = "chat.send",
			.paramsJson = JsonDumpCompact(chatSendParams),
		};

		const auto sendResponse = RouteGatewayRequest(sendRequest);
		if (!sendResponse.has_value()) {
			if (stream) {
				AppendSseEvent(ssePayload, nlohmann::json{
					{ "type", "error" },
					{ "message", "native_gateway_routing_unavailable" },
				});
				AgentChatBridgeHttpResponse response;
				response.statusCode = 200;
				response.contentType = "text/event-stream; charset=utf-8";
				response.body = ssePayload;
				return response;
			}
			return BuildBadGatewayResponse("native_gateway_routing_unavailable");
		}
		if (!sendResponse->ok) {
			if (stream) {
				std::string messageText = "native_gateway_chat_send_failed";
				if (sendResponse->error.has_value() &&
					!sendResponse->error->message.empty()) {
					messageText = sendResponse->error->message;
				}
				AppendSseEvent(ssePayload, nlohmann::json{
					{ "type", "error" },
					{ "message", messageText },
				});
				AgentChatBridgeHttpResponse response;
				response.statusCode = 200;
				response.contentType = "text/event-stream; charset=utf-8";
				response.body = ssePayload;
				return response;
			}
			return BuildResponseFromGatewayResponse(*sendResponse);
		}

		std::string runId;
		if (sendResponse->payloadJson.has_value()) {
			const auto sendPayload = nlohmann::json::parse(
				sendResponse->payloadJson.value(),
				nullptr,
				false);
			if (!sendPayload.is_discarded() && sendPayload.is_object()) {
				runId = JsonStringValue(sendPayload, "runId");
				if (runId.empty()) {
					runId = JsonStringValue(sendPayload, "run_id");
				}
				if (runId.empty()) {
					runId = JsonStringValue(sendPayload, "id");
				}
			}
		}
		if (runId.empty()) {
			if (stream) {
				AppendSseEvent(ssePayload, nlohmann::json{
					{ "type", "error" },
					{ "message", "native_gateway_chat_send_missing_run_id" },
				});
				AgentChatBridgeHttpResponse response;
				response.statusCode = 200;
				response.contentType = "text/event-stream; charset=utf-8";
				response.body = ssePayload;
				return response;
			}
			return BuildBadGatewayResponse("native_gateway_chat_send_missing_run_id");
		}

		const std::string sessionKey = JsonStringValue(chatSendParams, "sessionKey");
		const int pollTimeoutMs = JsonPositiveIntValue(payload, "pollTimeoutMs", kDefaultGatewayPollTimeoutMs);
		const auto timeoutAt =
			std::chrono::steady_clock::now() + std::chrono::milliseconds(pollTimeoutMs);

		std::string finalText;
		while (std::chrono::steady_clock::now() < timeoutAt) {
			const RequestFrame pollRequest{
				.id = "agentchat-native-chat-poll",
				.method = "chat.events.poll",
				.paramsJson = JsonDumpCompact(nlohmann::json{
					{ "sessionKey", sessionKey },
					{ "limit", 20 },
					}),
			};

			const auto pollResponse = RouteGatewayRequest(pollRequest);
			if (!pollResponse.has_value()) {
				if (stream) {
					AppendSseEvent(ssePayload, nlohmann::json{
						{ "type", "error" },
						{ "message", "native_gateway_poll_unavailable" },
					});
					AgentChatBridgeHttpResponse response;
					response.statusCode = 200;
					response.contentType = "text/event-stream; charset=utf-8";
					response.body = ssePayload;
					return response;
				}
				return BuildBadGatewayResponse("native_gateway_poll_unavailable");
			}
			if (!pollResponse->ok) {
				if (stream) {
					std::string messageText = "native_gateway_poll_failed";
					if (pollResponse->error.has_value() &&
						!pollResponse->error->message.empty()) {
						messageText = pollResponse->error->message;
					}
					AppendSseEvent(ssePayload, nlohmann::json{
						{ "type", "error" },
						{ "message", messageText },
					});
					AgentChatBridgeHttpResponse response;
					response.statusCode = 200;
					response.contentType = "text/event-stream; charset=utf-8";
					response.body = ssePayload;
					return response;
				}
				return BuildResponseFromGatewayResponse(*pollResponse);
			}

			if (!pollResponse->payloadJson.has_value()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(kGatewayPollIntervalMs));
				continue;
			}

			const auto pollPayload = nlohmann::json::parse(
				pollResponse->payloadJson.value(),
				nullptr,
				false);
			if (pollPayload.is_discarded() || !pollPayload.is_object()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(kGatewayPollIntervalMs));
				continue;
			}

			const auto eventsIt = pollPayload.find("events");
			if (eventsIt == pollPayload.end() || !eventsIt->is_array()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(kGatewayPollIntervalMs));
				continue;
			}

			for (const auto& event : *eventsIt) {
				if (!event.is_object()) {
					continue;
				}
				std::string eventRunId = JsonStringValue(event, "runId");
				if (eventRunId.empty()) {
					eventRunId = JsonStringValue(event, "run_id");
				}
				if (eventRunId != runId) {
					continue;
				}

				std::string state = JsonStringValue(event, "state");
				std::transform(
					state.begin(),
					state.end(),
					state.begin(),
					[](unsigned char ch) {
						return static_cast<char>(std::tolower(ch));
					});
				if (state == "delta") {
					const auto messageIt = event.find("message");
					if (messageIt != event.end()) {
						const std::string deltaText = ExtractTextFromChatEventMessage(*messageIt);
						if (!deltaText.empty()) {
							finalText = deltaText;
							if (stream) {
								AppendSseEvent(ssePayload, nlohmann::json{
									{ "type", "delta" },
									{ "text", deltaText },
								});
							}
						}
					}
					continue;
				}

				if (state == "error" || state == "failed") {
					const std::string errorMessage = JsonStringValue(event, "errorMessage");
					if (stream) {
						AppendSseEvent(ssePayload, nlohmann::json{
							{ "type", "error" },
							{ "message", errorMessage.empty()
								? "native_gateway_chat_runtime_error"
								: errorMessage },
						});
						AgentChatBridgeHttpResponse response;
						response.statusCode = 200;
						response.contentType = "text/event-stream; charset=utf-8";
						response.body = ssePayload;
						return response;
					}
					return BuildBadGatewayResponse(
						errorMessage.empty()
							? "native_gateway_chat_runtime_error"
							: errorMessage);
				}

				if (state == "final" ||
					state == "aborted" ||
					state == "completed" ||
					state == "terminal" ||
					state == "done" ||
					state == "canceled" ||
					state == "cancelled") {
					const auto messageIt = event.find("message");
					if (messageIt != event.end()) {
						const std::string terminalText = ExtractTextFromChatEventMessage(*messageIt);
						if (!terminalText.empty()) {
							finalText = terminalText;
						}
					}

					if (stream) {
						const bool isAbortedState =
							state == "aborted" ||
							state == "canceled" ||
							state == "cancelled";
						if (isAbortedState) {
							AppendSseEvent(ssePayload, nlohmann::json{
								{ "type", "error" },
								{ "message", "OpenClaw chat runtime aborted." },
							});
						}
						AppendSseEvent(ssePayload, nlohmann::json{
							{ "type", "final" },
							{ "text", finalText.empty() ? "已处理完成，请查看当前结果。" : finalText },
							{ "state", isAbortedState ? "aborted" : "final" },
						});
						AgentChatBridgeHttpResponse response;
						response.statusCode = 200;
						response.contentType = "text/event-stream; charset=utf-8";
						response.body = ssePayload;
						return response;
					}

					nlohmann::json result = {
						{ "ok", true },
						{ "source", "blazeclaw" },
						{ "runId", runId },
						{ "text", finalText.empty() ? "已处理完成，请查看当前结果。" : finalText },
						{ "toolCalls", nlohmann::json::array() },
					};

					AgentChatBridgeHttpResponse response;
					response.statusCode = 200;
					response.body = JsonDumpCompact(result);
					return response;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(kGatewayPollIntervalMs));
		}

		if (stream) {
			AppendSseEvent(ssePayload, nlohmann::json{
				{ "type", "error" },
				{ "message", "Stream timed out waiting for gateway terminal event." },
			});
			AgentChatBridgeHttpResponse response;
			response.statusCode = 200;
			response.contentType = "text/event-stream; charset=utf-8";
			response.body = ssePayload;
			return response;
		}

		return BuildBadGatewayResponse("native_gateway_poll_timeout");
	}

	AgentChatBridgeHttpResponse AgentChatBridgeHost::HandlePushRequest(
		const nlohmann::json& payload) const {
		nlohmann::json resolvedPayload = ResolvePushRequestBody(payload, payload.contains("token") ? payload["token"] : nlohmann::json());
		if (!resolvedPayload.is_object() || resolvedPayload.empty()) {
			resolvedPayload = payload;
		}

		const std::string token = ResolvePushToken(resolvedPayload);
		const std::string channel = JsonStringValue(resolvedPayload, "channel").empty()
			? JsonStringValue(resolvedPayload, "conversationId")
			: JsonStringValue(resolvedPayload, "channel");
		const bool typing = resolvedPayload.value("typing", false);
		const std::string message = JsonStringValue(resolvedPayload, "message");
		if (IsObsoleteAgentPushChannel(channel)) {
			return BuildBadRequestResponse("push_payload_obsolete_channel");
		}
		if (token.empty() || channel.empty() || (!typing && message.empty())) {
			return BuildBadRequestResponse("push_payload_missing_required_fields");
		}
		if (!channel.starts_with('#')) {
			return BuildBadRequestResponse("push_payload_invalid_channel");
		}

		const std::string expectedToken = ResolvePushToken(nlohmann::json::object());
		if (expectedToken.empty() || token != expectedToken) {
			AgentChatBridgeHttpResponse unauthorized;
			unauthorized.statusCode = 401;
			unauthorized.body = "{\"event\":\"ERROR\",\"code\":\"INVALID_TOKEN\",\"message\":\"missing token\"}";
			return unauthorized;
		}

		std::optional<AgentChatBridgeStateStore> stateStore;
		bool pushTransportEnabled = true;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stateStore.has_value()) {
				stateStore.emplace(m_stateStore.value().StateRoot());
				stateStore->EnsureInitialized();
			}
			pushTransportEnabled = m_config.enablePushTransport;
		}

		const std::string idempotencyKey = JsonStringValue(resolvedPayload, "idempotencyKey");
		const std::uint64_t nowMs = CurrentEpochMilliseconds();
		if (!idempotencyKey.empty() && stateStore.has_value()) {
			if (stateStore->HasRecentPushIdempotencyKey(
				idempotencyKey,
				nowMs,
				kPushIdempotencyTtlMs)) {
				nlohmann::json deduplicated = {
					{ "status", "ok" },
					{ "event", "AGENT_PUSH_ACK" },
					{ "channel", channel },
					{ "idempotencyKey", idempotencyKey },
					{ "deduplicated", true },
					{ "message", "duplicate AGENT_PUSH ignored by bridge" },
				};
				AgentChatBridgeHttpResponse response;
				response.statusCode = 200;
				response.body = JsonDumpCompact(deduplicated);
				return response;
			}
		}

		if (!pushTransportEnabled) {
			return BuildBadGatewayResponse("native_push_transport_disabled");
		}

		nlohmann::json transportPayload = resolvedPayload;
		transportPayload["cmd"] = "AGENT_PUSH";
		transportPayload["token"] = token;
		transportPayload["channel"] = channel;
		if (!transportPayload.contains("agentRequestId") ||
			!transportPayload["agentRequestId"].is_string() ||
			TrimCopy(transportPayload["agentRequestId"].get<std::string>()).empty()) {
			transportPayload["agentRequestId"] = "native-agentchat-push-" + std::to_string(nowMs);
		}

		nlohmann::json transportResponse = nlohmann::json::object();
		std::string transportError;
		if (!SendAgentPushToChatServer(transportPayload, transportResponse, transportError)) {
			nlohmann::json errorPayload = BuildPushErrorJson(
				"BRIDGE_UPSTREAM_UNAVAILABLE",
				transportError.empty() ? std::string("chat server transport failed") : transportError);
			AgentChatBridgeHttpResponse response;
			response.statusCode = 502;
			response.body = JsonDumpCompact(errorPayload);
			return response;
		}

		if (!idempotencyKey.empty() && stateStore.has_value()) {
			stateStore->RememberPushIdempotencyKey(
				idempotencyKey,
				nowMs,
				kPushIdempotencyTtlMs,
				kPushIdempotencyMaxEntries);
		}

		const std::string eventName = ToUpperCopy(JsonStringValue(transportResponse, "event"));
		const std::string errorCode = JsonStringValue(transportResponse, "code");
		AgentChatBridgeHttpResponse response;
		response.statusCode =
			eventName == "ERROR" ? HttpStatusForPushErrorCode(errorCode) : 200;
		response.body = JsonDumpCompact(transportResponse);
		return response;
	}

	AgentChatBridgeHttpResponse AgentChatBridgeHost::HandleCollaborationRequest(
		const std::string& method,
		const std::string& path,
		const nlohmann::json& payload) const {
		const std::string prefix = AgentChatBridgeProtocol::kCollaborationPrefix;
		std::string subPath = path.size() > prefix.size() ? path.substr(prefix.size()) : std::string();
		if (subPath.empty()) {
			subPath = "/";
		}

		if (method == "GET") {
			if (subPath != "/" && subPath != "/current-display" && subPath.rfind("/current-display/", 0) != 0) {
				return AgentChatBridgeProtocol::BuildNotFoundResponse();
			}

			std::string conversationId;
			if (subPath.rfind("/current-display/", 0) == 0) {
				conversationId = TrimCopy(subPath.substr(std::string("/current-display/").size()));
			}
			if (conversationId.empty()) {
				return BuildBadRequestResponse("collaboration_conversation_id_required");
			}

			nlohmann::json currentDisplay = nullptr;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				const auto it = m_collaborationCurrentDisplayByConversation.find(conversationId);
				if (it != m_collaborationCurrentDisplayByConversation.end()) {
					currentDisplay = it->second;
				}
			}

			AgentChatBridgeHttpResponse response;
			response.statusCode = 200;
			response.body = JsonDumpCompact(nlohmann::json{
				{ "ok", true },
				{ "currentDisplay", currentDisplay },
			});
			return response;
		}

		if (method != "POST") {
			return BuildMethodNotAllowedResponse();
		}

		if (subPath != "/" && subPath != "/dispatch") {
			return AgentChatBridgeProtocol::BuildNotFoundResponse();
		}

		const nlohmann::json instruction = payload.contains("instruction") && payload["instruction"].is_object()
			? payload["instruction"]
			: payload;
		if (!instruction.is_object()) {
			return BuildBadRequestResponse("collaboration_bad_instruction");
		}

		const std::string protocol = JsonStringValue(instruction, "protocol");
		if (protocol != "agentchat.collaboration") {
			return BuildBadRequestResponse("collaboration_bad_protocol");
		}
		const int version = instruction.contains("version") && instruction["version"].is_number_integer()
			? instruction["version"].get<int>()
			: 0;
		if (version != 1) {
			return BuildBadRequestResponse("collaboration_bad_version");
		}

		const std::string action = JsonStringValue(instruction, "action");
		if (action != "device.open_content" &&
			action != "device.speak" &&
			action != "ai.skill_result" &&
			action != "ai.task_status") {
			return BuildBadRequestResponse("collaboration_unsupported_action");
		}

		const std::string conversationId = JsonStringValue(instruction, "conversationId");
		if (conversationId.empty()) {
			return BuildBadRequestResponse("collaboration_conversation_id_required");
		}

		const std::string dispatchId = JsonStringValue(instruction, "dispatchId");
		const std::string dedupeKeyRaw = JsonStringValue(instruction, "dedupeKey");
		const std::string dedupeKey = dedupeKeyRaw.empty() ? dispatchId : dedupeKeyRaw;
		if (dedupeKey.empty()) {
			return BuildBadRequestResponse("collaboration_dedupe_key_required");
		}

		if (!instruction.contains("payload") || !instruction["payload"].is_object()) {
			return BuildBadRequestResponse("collaboration_payload_required");
		}
		const nlohmann::json instructionPayload = instruction["payload"];

		const std::uint64_t now = CurrentEpochMilliseconds();
		const std::uint64_t ttlMs = instruction.contains("ttlMs") && instruction["ttlMs"].is_number_unsigned()
			? instruction["ttlMs"].get<std::uint64_t>()
			: 30000ULL;
		const std::uint64_t timestamp = instruction.contains("timestamp") && instruction["timestamp"].is_number_unsigned()
			? instruction["timestamp"].get<std::uint64_t>()
			: now;
		if (timestamp + ttlMs < now) {
			AgentChatBridgeHttpResponse response;
			response.statusCode = 200;
			response.body = JsonDumpCompact(nlohmann::json{
				{ "ok", false },
				{ "status", "ignored" },
				{ "code", "EXPIRED" },
				{ "message", "instruction expired" },
			});
			return response;
		}

		nlohmann::json currentDisplay = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			for (auto it = m_collaborationDedupeByKey.begin(); it != m_collaborationDedupeByKey.end();) {
				if (it->second <= now) {
					it = m_collaborationDedupeByKey.erase(it);
				}
				else {
					++it;
				}
			}

			const auto dedupeIt = m_collaborationDedupeByKey.find(dedupeKey);
			if (dedupeIt != m_collaborationDedupeByKey.end() && dedupeIt->second > now) {
				const auto currentDisplayIt = m_collaborationCurrentDisplayByConversation.find(conversationId);
				if (currentDisplayIt != m_collaborationCurrentDisplayByConversation.end()) {
					currentDisplay = currentDisplayIt->second;
				}

				AgentChatBridgeHttpResponse response;
				response.statusCode = 200;
				response.body = JsonDumpCompact(nlohmann::json{
					{ "ok", true },
					{ "status", "deduplicated" },
					{ "code", "DUPLICATED" },
					{ "currentDisplay", currentDisplay },
				});
				return response;
			}

			m_collaborationDedupeByKey.insert_or_assign(dedupeKey, now + ttlMs);
		}

		if (action == "device.open_content") {
			const std::string url = JsonStringValue(instructionPayload, "url").empty()
				? JsonStringValue(instructionPayload, "resourceUrl")
				: JsonStringValue(instructionPayload, "url");
			if (!url.empty() && !IsHttpUrl(url)) {
				return BuildBadRequestResponse("collaboration_unsupported_url");
			}
			currentDisplay = nlohmann::json{
				{ "conversationId", conversationId },
				{ "displayId", dispatchId.empty() ? dedupeKey : dispatchId },
				{ "title", JsonStringValue(instructionPayload, "title").empty() ? std::string("当前展示内容") : JsonStringValue(instructionPayload, "title") },
				{ "summary", JsonStringValue(instructionPayload, "summary") },
				{ "contentType", JsonStringValue(instructionPayload, "contentType").empty() ? std::string("webview") : JsonStringValue(instructionPayload, "contentType") },
				{ "url", url.empty() ? nlohmann::json(nullptr) : nlohmann::json(url) },
				{ "speakText", JsonStringValue(instructionPayload, "speakText") },
				{ "updatedAt", now },
			};
		}
		else if (action == "device.speak") {
			const std::string text = JsonStringValue(instructionPayload, "displayText").empty()
				? JsonStringValue(instructionPayload, "text")
				: JsonStringValue(instructionPayload, "displayText");
			if (!text.empty()) {
				currentDisplay = nlohmann::json{
					{ "conversationId", conversationId },
					{ "displayId", dispatchId.empty() ? dedupeKey : dispatchId },
					{ "title", text },
					{ "contentType", "text" },
					{ "speakText", JsonStringValue(instructionPayload, "text").empty() ? text : JsonStringValue(instructionPayload, "text") },
					{ "updatedAt", now },
				};
			}
		}
		else {
			std::string summary = JsonStringValue(instructionPayload, "summary");
			if (summary.empty()) {
				summary = JsonStringValue(instructionPayload, "message");
			}
			if (summary.empty()) {
				summary = JsonStringValue(instructionPayload, "status");
			}

			std::string url;
			const auto outputsIt = instructionPayload.find("outputs");
			if (outputsIt != instructionPayload.end() && outputsIt->is_array()) {
				for (const auto& output : *outputsIt) {
					if (!output.is_object()) {
						continue;
					}
					if (JsonStringValue(output, "type") != "webview") {
						continue;
					}
					url = JsonStringValue(output, "url");
					break;
				}
			}

			currentDisplay = nlohmann::json{
				{ "conversationId", conversationId },
				{ "displayId", dispatchId.empty() ? dedupeKey : dispatchId },
				{ "title", summary.empty() ? std::string("ai_result") : summary },
				{ "summary", JsonStringValue(instructionPayload, "skillName") },
				{ "contentType", "ai_result" },
				{ "url", IsHttpUrl(url) ? nlohmann::json(url) : nlohmann::json(nullptr) },
				{ "updatedAt", now },
			};
		}

		if (!currentDisplay.is_null()) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_collaborationCurrentDisplayByConversation.insert_or_assign(conversationId, currentDisplay);
		}

		AgentChatBridgeHttpResponse response;
		response.statusCode = 200;
		response.body = JsonDumpCompact(nlohmann::json{
			{ "ok", true },
			{ "status", "accepted" },
			{ "currentDisplay", currentDisplay },
		});
		return response;
	}

	AgentChatBridgeHttpResponse AgentChatBridgeHost::HandleTtsSynthesizeRequest(
		const nlohmann::json& payload) const {
		const std::string text = JsonStringValue(payload, "text");
		if (text.empty()) {
			return BuildBadRequestResponse("tts_text_required");
		}

		nlohmann::json params = {
			{ "text", text },
		};
		const std::string provider = JsonStringValue(payload, "provider");
		if (!provider.empty()) {
			params["provider"] = provider;
		}

		const RequestFrame request{
			.id = "agentchat-native-tts-convert",
			.method = "tts.convert",
			.paramsJson = params.dump(),
		};
		const auto routed = RouteGatewayRequest(request);
		if (!routed.has_value()) {
			return BuildBadGatewayResponse("native_gateway_routing_unavailable");
		}
		if (!routed->ok || !routed->payloadJson.has_value()) {
			AgentChatBridgeHttpResponse response;
			response.statusCode = 502;
			response.body = JsonDumpCompact(nlohmann::json{
				{ "ok", false },
				{ "error", routed->error.has_value() ? routed->error->code : std::string("tts_convert_failed") },
				{ "message", routed->error.has_value() ? routed->error->message : std::string("tts.convert failed") },
			});
			return response;
		}

		const auto ttsPayload = nlohmann::json::parse(routed->payloadJson.value(), nullptr, false);
		if (ttsPayload.is_discarded() || !ttsPayload.is_object()) {
			return BuildBadGatewayResponse("tts_convert_invalid_payload");
		}

		std::string audioUrl = JsonStringValue(ttsPayload, "audioUrl");
		if (audioUrl.empty()) {
			audioUrl = JsonStringValue(ttsPayload, "audio_url");
		}
		std::string audioPath = JsonStringValue(ttsPayload, "audioPath");
		if (audioPath.empty()) {
			audioPath = JsonStringValue(ttsPayload, "audio_path");
		}

		if (audioUrl.empty() && !audioPath.empty()) {
			const std::array<const char*, 2> names = {
				"TTS_AUDIO_PUBLIC_BASE_URL",
				"MOSS_TTS_AUDIO_PUBLIC_BASE_URL",
			};
			for (const char* name : names) {
				const std::string publicBaseUrl = TrimCopy(GetEnv(name));
				if (publicBaseUrl.empty()) {
					continue;
				}
				const std::string filename = LastPathSegment(audioPath);
				if (filename.empty()) {
					continue;
				}
				audioUrl = publicBaseUrl;
				if (!audioUrl.empty() && audioUrl.back() == '/') {
					audioUrl.pop_back();
				}
				audioUrl += "/" + filename;
				break;
			}
		}

		if (audioUrl.empty() && !audioPath.empty()) {
			audioUrl = audioPath;
		}

		if (audioUrl.empty()) {
			AgentChatBridgeHttpResponse response;
			response.statusCode = 502;
			response.body = JsonDumpCompact(nlohmann::json{
				{ "ok", false },
				{ "error", "tts_audio_url_missing" },
				{ "hint", "Set TTS_AUDIO_PUBLIC_BASE_URL to map audioPath to public URL." },
				{ "upstream", ttsPayload },
			});
			return response;
		}

		AgentChatBridgeHttpResponse response;
		response.statusCode = 200;
		response.body = JsonDumpCompact(nlohmann::json{
			{ "ok", true },
			{ "source", "native-tts" },
			{ "text", text },
			{ "audioUrl", audioUrl },
			{ "upstream", ttsPayload },
		});
		return response;
	}

	std::string AgentChatBridgeHost::ResolvePushToken(
		const nlohmann::json& payload) const {
		const std::string bodyToken = JsonStringValue(payload, "token");
		if (!bodyToken.empty()) {
			return bodyToken;
		}

		const std::array<const char*, 4> names = {
			"BLAZECLAW_AGENT_PUSH_TOKEN",
			"AGENTCHAT_BLAZECLAW_AGENT_PUSH_TOKEN",
			"OPENCLAW_AGENT_PUSH_TOKEN",
			"AGENTCHAT_OPENCLAW_AGENT_PUSH_TOKEN",
		};
		for (const char* name : names) {
			const std::string value = GetEnv(name);
			if (!value.empty()) {
				return value;
			}
		}

		const std::string nodeEnv = GetEnv("NODE_ENV");
		if (ToUpperCopy(nodeEnv) == "PRODUCTION") {
			return {};
		}
		return kDefaultPushToken;
	}

	std::string AgentChatBridgeHost::ResolveChatHost() const {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!TrimCopy(m_config.pushChatHost).empty()) {
				return TrimCopy(m_config.pushChatHost);
			}
		}
		const std::array<const char*, 8> names = {
			"BLAZECLAW_AGENT_PUSH_CHAT_HOST",
			"AGENTCHAT_BLAZECLAW_AGENT_PUSH_CHAT_HOST",
			"OPENCLAW_AGENT_PUSH_CHAT_HOST",
			"AGENTCHAT_OPENCLAW_AGENT_PUSH_CHAT_HOST",
			"CHAT_TCP_HOST",
			"VITE_CHAT_TCP_HOST",
			"CHAT_TLS_HOST",
			"BLAZECLAW_CHAT_TCP_HOST",
		};
		for (const char* name : names) {
			const std::string value = GetEnv(name);
			if (!value.empty()) {
				return value;
			}
		}
		return kDefaultChatHost;
	}

	std::uint16_t AgentChatBridgeHost::ResolveChatPort() const {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_config.pushChatPort > 0) {
				return m_config.pushChatPort;
			}
		}
		const std::array<const char*, 7> names = {
			"BLAZECLAW_AGENT_PUSH_CHAT_PORT",
			"AGENTCHAT_BLAZECLAW_AGENT_PUSH_CHAT_PORT",
			"OPENCLAW_AGENT_PUSH_CHAT_PORT",
			"AGENTCHAT_OPENCLAW_AGENT_PUSH_CHAT_PORT",
			"CHAT_TCP_PORT",
			"VITE_CHAT_TCP_PORT",
			"CHAT_TLS_PORT",
		};
		for (const char* name : names) {
			const std::string value = GetEnv(name);
			if (!value.empty()) {
				return ParsePort(value, kDefaultChatPort);
			}
		}
		return kDefaultChatPort;
	}

	std::uint32_t AgentChatBridgeHost::ResolvePushTimeoutMs() const {
		const std::array<const char*, 3> names = {
			"BLAZECLAW_AGENT_PUSH_TIMEOUT_MS",
			"OPENCLAW_AGENT_PUSH_TIMEOUT_MS",
			"CHAT_TLS_TIMEOUT_MS",
		};
		for (const char* name : names) {
			const std::string value = GetEnv(name);
			if (!value.empty()) {
				return ParsePositiveUInt32(value, kDefaultPushTimeoutMs);
			}
		}
		return kDefaultPushTimeoutMs;
	}

	bool AgentChatBridgeHost::SendAgentPushToChatServer(
		const nlohmann::json& payload,
		nlohmann::json& responseOut,
		std::string& errorOut) const {
		responseOut = nlohmann::json::object();
		errorOut.clear();

		const std::string payloadRaw = JsonDumpCompact(payload);
		if (payloadRaw.size() > kHbpcMaxPayloadSize) {
			errorOut = "payload too large";
			return false;
		}

		WSADATA wsaData{};
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			errorOut = "WSAStartup failed";
			return false;
		}

		SOCKET socketHandle = INVALID_SOCKET;
		addrinfo* addressInfo = nullptr;
		const std::string host = ResolveChatHost();
		const std::string portText = std::to_string(ResolveChatPort());
		const std::uint32_t timeoutMs = ResolvePushTimeoutMs();
		const std::string endpoint = EndpointLabel(host, portText);
		const DWORD timeoutValue = static_cast<DWORD>(timeoutMs);

		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		const int resolveResult = getaddrinfo(
			host.c_str(),
			portText.c_str(),
			&hints,
			&addressInfo);
		if (resolveResult != 0 || addressInfo == nullptr) {
			errorOut =
				"chat server address resolve failed (endpoint=" +
				endpoint +
				", error=" +
				std::to_string(resolveResult) +
				")";
			WSACleanup();
			return false;
		}

		bool connected = false;
		int lastConnectError = 0;
		for (addrinfo* current = addressInfo; current != nullptr; current = current->ai_next) {
			socketHandle = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
			if (socketHandle == INVALID_SOCKET) {
				lastConnectError = WSAGetLastError();
				continue;
			}

			setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutValue), sizeof(timeoutValue));
			setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutValue), sizeof(timeoutValue));

			if (connect(socketHandle, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
				connected = true;
				break;
			}
			lastConnectError = WSAGetLastError();

			closesocket(socketHandle);
			socketHandle = INVALID_SOCKET;
		}

		freeaddrinfo(addressInfo);
		addressInfo = nullptr;

		if (!connected || socketHandle == INVALID_SOCKET) {
			errorOut =
				"chat server connect failed (endpoint=" +
				endpoint +
				", timeoutMs=" +
				std::to_string(timeoutMs) +
				", wsaError=" +
				std::to_string(lastConnectError) +
				")";
			WSACleanup();
			return false;
		}

		std::array<char, kHbpcHeaderSize> header{};
		header[0] = 'H';
		header[1] = 'B';
		header[2] = 'P';
		header[3] = 'C';
		header[4] = static_cast<char>(kHbpcProtoVersion);
		header[5] = static_cast<char>(kHbpcIrcMessageReq);
		header[6] = 0;
		header[7] = 0;

		static std::atomic<std::uint32_t> seq{ 0 };
		const std::uint32_t requestSeq = ++seq;
		header[8] = static_cast<char>((requestSeq >> 24) & 0xFF);
		header[9] = static_cast<char>((requestSeq >> 16) & 0xFF);
		header[10] = static_cast<char>((requestSeq >> 8) & 0xFF);
		header[11] = static_cast<char>(requestSeq & 0xFF);

		const std::uint32_t payloadLength = static_cast<std::uint32_t>(payloadRaw.size());
		header[12] = static_cast<char>((payloadLength >> 24) & 0xFF);
		header[13] = static_cast<char>((payloadLength >> 16) & 0xFF);
		header[14] = static_cast<char>((payloadLength >> 8) & 0xFF);
		header[15] = static_cast<char>(payloadLength & 0xFF);

		const std::uint64_t nowMs = CurrentEpochMilliseconds();
		for (int i = 0; i < 8; ++i) {
			header[24 + i] = static_cast<char>((nowMs >> (56 - i * 8)) & 0xFF);
		}

		const std::string packet = std::string(header.data(), header.size()) + payloadRaw;
		const int sendResult = send(
			socketHandle,
			packet.data(),
			static_cast<int>(packet.size()),
			0);
		if (sendResult != static_cast<int>(packet.size())) {
			errorOut =
				"chat server send failed (endpoint=" +
				endpoint +
				", sent=" +
				std::to_string(sendResult) +
				", expected=" +
				std::to_string(packet.size()) +
				", wsaError=" +
				std::to_string(WSAGetLastError()) +
				")";
			shutdown(socketHandle, SD_BOTH);
			closesocket(socketHandle);
			WSACleanup();
			return false;
		}

		std::array<char, kHbpcHeaderSize> responseHeader{};
		int received = recv(socketHandle, responseHeader.data(), static_cast<int>(responseHeader.size()), MSG_WAITALL);
		if (received != static_cast<int>(responseHeader.size())) {
			errorOut =
				"chat server response header failed (endpoint=" +
				endpoint +
				", received=" +
				std::to_string(received) +
				", expected=" +
				std::to_string(responseHeader.size()) +
				", wsaError=" +
				std::to_string(WSAGetLastError()) +
				")";
			shutdown(socketHandle, SD_BOTH);
			closesocket(socketHandle);
			WSACleanup();
			return false;
		}
		if (responseHeader[0] != 'H' || responseHeader[1] != 'B' || responseHeader[2] != 'P' || responseHeader[3] != 'C') {
			errorOut = "invalid response magic";
			shutdown(socketHandle, SD_BOTH);
			closesocket(socketHandle);
			WSACleanup();
			return false;
		}
		if (static_cast<std::uint8_t>(responseHeader[4]) != kHbpcProtoVersion ||
			static_cast<std::uint8_t>(responseHeader[5]) != kHbpcIrcMessageResp) {
			errorOut =
				"invalid response protocol (version=" +
				std::to_string(static_cast<std::uint8_t>(responseHeader[4])) +
				", type=" +
				std::to_string(static_cast<std::uint8_t>(responseHeader[5])) +
				")";
			shutdown(socketHandle, SD_BOTH);
			closesocket(socketHandle);
			WSACleanup();
			return false;
		}

		const std::uint32_t responsePayloadLength =
			(static_cast<std::uint32_t>(static_cast<unsigned char>(responseHeader[12])) << 24) |
			(static_cast<std::uint32_t>(static_cast<unsigned char>(responseHeader[13])) << 16) |
			(static_cast<std::uint32_t>(static_cast<unsigned char>(responseHeader[14])) << 8) |
			(static_cast<std::uint32_t>(static_cast<unsigned char>(responseHeader[15])));
		if (responsePayloadLength > kHbpcMaxPayloadSize) {
			errorOut =
				"invalid response payload length (length=" +
				std::to_string(responsePayloadLength) +
				", max=" +
				std::to_string(kHbpcMaxPayloadSize) +
				")";
			shutdown(socketHandle, SD_BOTH);
			closesocket(socketHandle);
			WSACleanup();
			return false;
		}

		std::string responsePayload(responsePayloadLength, '\0');
		if (responsePayloadLength > 0) {
			received = recv(
				socketHandle,
				responsePayload.data(),
				static_cast<int>(responsePayloadLength),
				MSG_WAITALL);
			if (received != static_cast<int>(responsePayloadLength)) {
				errorOut =
					"chat server response payload failed (endpoint=" +
					endpoint +
					", received=" +
					std::to_string(received) +
					", expected=" +
					std::to_string(responsePayloadLength) +
					", wsaError=" +
					std::to_string(WSAGetLastError()) +
					")";
				shutdown(socketHandle, SD_BOTH);
				closesocket(socketHandle);
				WSACleanup();
				return false;
			}
		}

		shutdown(socketHandle, SD_BOTH);
		closesocket(socketHandle);
		WSACleanup();

		const auto parsedResponse = ParseJsonObjectOrNull(responsePayload);
		if (!parsedResponse.has_value()) {
			errorOut = "response is not valid JSON";
			return false;
		}

		responseOut = parsedResponse.value();
		return true;
	}

	AgentChatBridgeHttpResponse AgentChatBridgeHost::BuildResponseFromGatewayResponse(
		const ResponseFrame& response) const {
		if (response.ok) {
			AgentChatBridgeHttpResponse ok;
			ok.statusCode = 200;
			nlohmann::json body = {
				{ "ok", true },
				{ "source", "blazeclaw" },
			};
			if (response.payloadJson.has_value()) {
				const auto payload = nlohmann::json::parse(
					response.payloadJson.value(),
					nullptr,
					false);
				if (!payload.is_discarded()) {
					body["payload"] = payload;
				}
			}
			ok.body = JsonDumpCompact(body);
			return ok;
		}

		AgentChatBridgeHttpResponse failed;
		failed.statusCode = 502;
		nlohmann::json body = {
			{ "ok", false },
			{ "error", "gateway_request_failed" },
		};
		if (response.error.has_value()) {
			body["error"] = response.error->code.empty()
				? std::string("gateway_request_failed")
				: response.error->code;
			body["message"] = response.error->message;
		}
		failed.body = JsonDumpCompact(body);
		return failed;
	}

	std::optional<ResponseFrame> AgentChatBridgeHost::RouteGatewayRequest(
		const RequestFrame& request) const {
		AgentChatOrchestratorAdapterPtr adapter;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_config.enableGatewayRouting) {
				return std::nullopt;
			}
			adapter = m_orchestratorAdapter;
		}
		if (!adapter) {
			return std::nullopt;
		}
		return adapter->Route(request);
	}

} // namespace blazeclaw::agentchat
