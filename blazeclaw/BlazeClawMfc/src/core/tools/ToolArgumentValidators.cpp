#include "pch.h"
#include "ToolArgumentValidators.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <regex>
#include <sstream>

namespace blazeclaw::core::tools {

	namespace {

		std::string ToLowerAscii(const std::string& value)
		{
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const unsigned char ch)
				{
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		std::string TrimAsciiLocal(const std::string& value)
		{
			const std::size_t first = value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
			{
				return {};
			}

			const std::size_t last = value.find_last_not_of(" \t\r\n");
			return value.substr(first, last - first + 1);
		}

		std::optional<std::string> JsonValueToCliString(const nlohmann::json& value)
		{
			if (value.is_string())
			{
				return value.get<std::string>();
			}

			if (value.is_boolean())
			{
				return value.get<bool>() ? "true" : "false";
			}

			if (value.is_number_integer())
			{
				return std::to_string(value.get<long long>());
			}

			if (value.is_number_unsigned())
			{
				return std::to_string(value.get<unsigned long long>());
			}

			if (value.is_number_float())
			{
				std::ostringstream stream;
				stream << value.get<double>();
				return stream.str();
			}

			return std::nullopt;
		}

		void AppendFlagWithValue(
			std::vector<std::string>& args,
			const std::string& flag,
			const std::optional<std::string>& value)
		{
			if (!value.has_value() || value->empty())
			{
				return;
			}

			args.push_back("--" + flag);
			args.push_back(value.value());
		}

		void AppendBoolAsValue(
			std::vector<std::string>& args,
			const std::string& flag,
			const nlohmann::json& params)
		{
			const auto it = params.find(flag);
			if (it == params.end() || !it->is_boolean())
			{
				return;
			}

			args.push_back("--" + flag);
			args.push_back(it->get<bool>() ? "true" : "false");
		}

		std::string ExtractSummaryField(
			const std::string& text,
			const std::string& label)
		{
			const std::string needle = label + ":";
			const std::size_t begin = text.find(needle);
			if (begin == std::string::npos)
			{
				return {};
			}

			const std::size_t valueStart = begin + needle.size();
			const std::size_t valueEnd = text.find('\n', valueStart);
			return TrimAsciiLocal(
				text.substr(
					valueStart,
					valueEnd == std::string::npos
					? std::string::npos
					: valueEnd - valueStart));
		}

		bool ParseBoolEnvOrDefault(const char* name, bool defaultValue)
		{
			const DWORD required = GetEnvironmentVariableA(name, nullptr, 0);
			if (required == 0)
			{
				return defaultValue;
			}

			std::string value(static_cast<std::size_t>(required), '\0');
			const DWORD written = GetEnvironmentVariableA(name, value.data(), required);
			if (written == 0)
			{
				return defaultValue;
			}

			if (!value.empty() && value.back() == '\0')
			{
				value.pop_back();
			}

			std::string lowered = ToLowerAscii(TrimAsciiLocal(value));
			if (lowered.empty())
			{
				return defaultValue;
			}

			if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
			{
				return true;
			}

			if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
			{
				return false;
			}

			return defaultValue;
		}

		bool IsMultilingualExtractorEnabled()
		{
			return ParseBoolEnvOrDefault(
				"BLAZECLAW_SUMMARIZE_MULTILINGUAL_EXTRACTOR_ENABLED",
				true);
		}

		bool IsExtractionDiagnosticsEnabled()
		{
			return ParseBoolEnvOrDefault(
				"BLAZECLAW_SUMMARIZE_EXTRACTION_DIAGNOSTICS_ENABLED",
				false);
		}

		bool ContainsLikelyCjkMarkers(const std::string& text)
		{
			const std::vector<std::string> markers = {
				"下周",
				"周",
				"会议室",
				"老板",
				"我们",
				"需求",
				"下午",
				"二楼",
			};

			for (const auto& marker : markers)
			{
				if (!marker.empty() && text.find(marker) != std::string::npos)
				{
					return true;
				}
			}
			return false;
		}

		void Utf8CountCodepointsAndHan(
			const std::string& utf8,
			std::size_t& outCodepoints,
			std::size_t& outHanIdeographs)
		{
			outCodepoints = 0;
			outHanIdeographs = 0;
			for (std::size_t i = 0; i < utf8.size();)
			{
				const unsigned char c0 = static_cast<unsigned char>(utf8[i]);
				std::uint32_t cp = 0;
				std::size_t len = 1;
				if (c0 < 0x80u)
				{
					cp = c0;
					len = 1;
				}
				else if ((c0 & 0xE0u) == 0xC0u && i + 1 < utf8.size())
				{
					const unsigned char c1 = static_cast<unsigned char>(utf8[i + 1]);
					if ((c1 & 0xC0u) != 0x80u)
					{
						++i;
						continue;
					}
					cp = (static_cast<std::uint32_t>(c0 & 0x1Fu) << 6) |
						static_cast<std::uint32_t>(c1 & 0x3Fu);
					len = 2;
				}
				else if ((c0 & 0xF0u) == 0xE0u && i + 2 < utf8.size())
				{
					const unsigned char c1 = static_cast<unsigned char>(utf8[i + 1]);
					const unsigned char c2 = static_cast<unsigned char>(utf8[i + 2]);
					if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u)
					{
						++i;
						continue;
					}
					cp = (static_cast<std::uint32_t>(c0 & 0x0Fu) << 12) |
						(static_cast<std::uint32_t>(c1 & 0x3Fu) << 6) |
						static_cast<std::uint32_t>(c2 & 0x3Fu);
					len = 3;
				}
				else if ((c0 & 0xF8u) == 0xF0u && i + 3 < utf8.size())
				{
					const unsigned char c1 = static_cast<unsigned char>(utf8[i + 1]);
					const unsigned char c2 = static_cast<unsigned char>(utf8[i + 2]);
					const unsigned char c3 = static_cast<unsigned char>(utf8[i + 3]);
					if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u)
					{
						++i;
						continue;
					}
					cp = (static_cast<std::uint32_t>(c0 & 0x07u) << 18) |
						(static_cast<std::uint32_t>(c1 & 0x3Fu) << 12) |
						(static_cast<std::uint32_t>(c2 & 0x3Fu) << 6) |
						static_cast<std::uint32_t>(c3 & 0x3Fu);
					len = 4;
				}
				else
				{
					++i;
					continue;
				}

				++outCodepoints;
				if ((cp >= 0x4E00u && cp <= 0x9FFFu) || (cp >= 0x3400u && cp <= 0x4DBFu))
				{
					++outHanIdeographs;
				}
				i += len;
			}
		}

		bool Utf8MeetsSubstantiveDraftThreshold(const std::string& trimmed)
		{
			if (trimmed.size() >= 20)
			{
				return true;
			}
			std::size_t codepoints = 0;
			std::size_t han = 0;
			Utf8CountCodepointsAndHan(trimmed, codepoints, han);
			if (codepoints >= 8)
			{
				return true;
			}
			if (han >= 4)
			{
				return true;
			}
			return ContainsLikelyCjkMarkers(trimmed);
		}

		std::string SelectLikelyDraftText(const std::string& value);

		std::optional<std::string> CoerceJsonValueToPlainText(
			const nlohmann::json& node,
			const int depth = 0)
		{
			if (depth > 8)
			{
				return std::nullopt;
			}
			if (node.is_string())
			{
				return node.get<std::string>();
			}
			if (node.is_array())
			{
				std::string joined;
				for (const auto& el : node)
				{
					const auto piece = CoerceJsonValueToPlainText(el, depth + 1);
					if (!piece.has_value())
					{
						continue;
					}
					const std::string t = TrimAsciiLocal(*piece);
					if (t.empty())
					{
						continue;
					}
					if (!joined.empty())
					{
						joined.push_back('\n');
					}
					joined += t;
				}
				return joined.empty() ? std::nullopt : std::optional<std::string>(std::move(joined));
			}
			if (!node.is_object())
			{
				return std::nullopt;
			}

			static const char* kObjectTextKeys[] = {
				"text",
				"content",
				"value",
				"message",
				"body",
				"input",
				"draft",
			};
			for (const auto* key : kObjectTextKeys)
			{
				const auto it = node.find(key);
				if (it == node.end())
				{
					continue;
				}
				return CoerceJsonValueToPlainText(*it, depth + 1);
			}

			const auto typeIt = node.find("type");
			const auto textIt = node.find("text");
			if (typeIt != node.end() && typeIt->is_string() &&
				ToLowerAscii(typeIt->get<std::string>()) == "text" && textIt != node.end())
			{
				return CoerceJsonValueToPlainText(*textIt, depth + 1);
			}

			return std::nullopt;
		}

		std::optional<std::string> TryExtractTextFromMessagesArray(const nlohmann::json& params)
		{
			const auto it = params.find("messages");
			if (it == params.end() || !it->is_array())
			{
				return std::nullopt;
			}

			std::string combined;
			for (const auto& msg : *it)
			{
				if (!msg.is_object())
				{
					continue;
				}
				const auto roleIt = msg.find("role");
				if (roleIt == msg.end() || !roleIt->is_string())
				{
					continue;
				}
				const std::string role = TrimAsciiLocal(ToLowerAscii(roleIt->get<std::string>()));
				if (role != "user" && role != "assistant")
				{
					continue;
				}
				const auto contentIt = msg.find("content");
				if (contentIt == msg.end())
				{
					continue;
				}
				const auto piece = CoerceJsonValueToPlainText(*contentIt);
				if (!piece.has_value())
				{
					continue;
				}
				const std::string t = TrimAsciiLocal(*piece);
				if (t.empty())
				{
					continue;
				}
				if (!combined.empty())
				{
					combined += "\n\n";
				}
				combined += t;
			}

			if (combined.empty())
			{
				return std::nullopt;
			}

			const std::string value = TrimAsciiLocal(combined);

			const std::string selected = SelectLikelyDraftText(value);
			if (selected.empty())
			{
				return std::nullopt;
			}
			return selected;
		}

#ifdef _WIN32
		std::wstring Utf8ToWide(const std::string& utf8)
		{
			if (utf8.empty())
			{
				return {};
			}

			auto convert = [](const std::string& in, DWORD flags) -> std::wstring {
				const int required = MultiByteToWideChar(
					CP_UTF8,
					flags,
					in.data(),
					static_cast<int>(in.size()),
					nullptr,
					0);
				if (required <= 0)
				{
					return {};
				}

				std::wstring wide(static_cast<std::size_t>(required), L'\0');
				if (MultiByteToWideChar(
					CP_UTF8,
					flags,
					in.data(),
					static_cast<int>(in.size()),
					wide.data(),
					required) <= 0)
				{
					return {};
				}

				return wide;
				};

			std::wstring wide = convert(utf8, MB_ERR_INVALID_CHARS);
			if (wide.empty() && !utf8.empty())
			{
				// Best-effort: some callers embed lone surrogate bytes or mixed encodings;
				// still run wide-string regex rather than failing draft extraction entirely.
				wide = convert(utf8, 0);
			}

			return wide;
		}

		std::string WideToUtf8(const std::wstring& wide)
		{
			if (wide.empty())
			{
				return {};
			}

			const int required = WideCharToMultiByte(
				CP_UTF8,
				0,
				wide.data(),
				static_cast<int>(wide.size()),
				nullptr,
				0,
				nullptr,
				nullptr);
			if (required <= 0)
			{
				return {};
			}

			std::string utf8(static_cast<std::size_t>(required), '\0');
			if (WideCharToMultiByte(
				CP_UTF8,
				0,
				wide.data(),
				static_cast<int>(wide.size()),
				utf8.data(),
				required,
				nullptr,
				nullptr) <= 0)
			{
				return {};
			}

			return utf8;
		}

		bool TryMatchChineseTimeUtf8Wide(const std::string& utf8Text, std::string& outTime)
		{
			const std::wstring wtext = Utf8ToWide(utf8Text);
			if (wtext.empty() && !utf8Text.empty())
			{
				return false;
			}

			try
			{
				static const std::wregex kWeekday(
					LR"(((?:(?:下周|本周|这周)[一二三四五六日天]|周[一二三四五六日天])(?:上午|下午|晚上|中午)?(?:\d{1,2}|[一二三四五六七八九十两]+)点(?:半|[0-5]?\d分?)?))");
				static const std::wregex kRelative(
					LR"(((?:今天|明天|后天)(?:上午|下午|晚上|中午)?(?:\d{1,2}|[一二三四五六七八九十两]+)点(?:半|[0-5]?\d分?)?))");
				static const std::wregex kClock(LR"(((?:上午|下午|晚上|中午)?\d{1,2}[:：]\d{2}))");

				std::wsmatch m;
				if (std::regex_search(wtext, m, kWeekday) && m.size() >= 2)
				{
					outTime = WideToUtf8(m[1].str());
					return true;
				}
				if (std::regex_search(wtext, m, kRelative) && m.size() >= 2)
				{
					outTime = WideToUtf8(m[1].str());
					return true;
				}
				if (std::regex_search(wtext, m, kClock) && m.size() >= 2)
				{
					outTime = WideToUtf8(m[1].str());
					return true;
				}
			}
			catch (const std::regex_error&)
			{
			}

			return false;
		}

		bool TryMatchChineseLocationUtf8Wide(const std::string& utf8Text, std::string& outLocation)
		{
			const std::wstring wtext = Utf8ToWide(utf8Text);
			if (wtext.empty() && !utf8Text.empty())
			{
				return false;
			}

			try
			{
				static const std::wregex kLocation(
					LR"((?:在)\s*([^，。,；;\n]{1,40}(?:会议室|会议厅|room)))");
				std::wsmatch m;
				if (std::regex_search(wtext, m, kLocation) && m.size() >= 2)
				{
					outLocation = WideToUtf8(m[1].str());
					return true;
				}
			}
			catch (const std::regex_error&)
			{
			}

			return false;
		}
#endif

		// Last "在" before "会议室" — avoids binding the first 在 in "在14:00在二楼会议室".
		bool TryMeetingRoomNearSuffixUtf8(const std::string& text, std::string& outLocation)
		{
			static const char kAt[] = "\xe5\x9c\xa8"; // 在
			static const char kRoom[] = "\xe4\xbc\x9a\xe8\xae\xae\xe5\xae\xa4"; // 会议室
			static constexpr std::size_t kAtLen = 3;
			static constexpr std::size_t kRoomLen = 9;

			const std::size_t roomPos = text.find(kRoom);
			if (roomPos == std::string::npos)
			{
				return false;
			}

			const std::size_t roomEnd = roomPos + kRoomLen;
			const std::size_t beforeRoom = roomPos == 0 ? 0 : roomPos - 1;
			const std::size_t zaiPos = text.rfind(kAt, beforeRoom);
			if (zaiPos == std::string::npos)
			{
				return false;
			}

			const std::size_t contentStart = zaiPos + kAtLen;
			if (contentStart > roomEnd)
			{
				return false;
			}

			outLocation = TrimAsciiLocal(text.substr(contentStart, roomEnd - contentStart));
			return !outLocation.empty();
		}

		bool ContainsPolishOrchestrationNoise(const std::string& value)
		{
			if (value.empty())
			{
				return false;
			}

			const std::string lowered = ToLowerAscii(value);
			if (value.find("内容润色") != std::string::npos)
			{
				return true;
			}
			if (value.find("分发流") != std::string::npos)
			{
				return true;
			}
			if (value.find("口语化草稿") != std::string::npos)
			{
				return true;
			}
			if (lowered.find("humanizer") != std::string::npos)
			{
				return true;
			}
			if (lowered.find("summarize") != std::string::npos)
			{
				return true;
			}
			if (lowered.find("imap_smtp") != std::string::npos)
			{
				return true;
			}

			return false;
		}

		// When quote/marker heuristics disagree, accept the full trimmed blob if it still
		// looks like meeting-oriented content (avoids invalid_arguments on valid drafts).
		bool LooksLikeLikelyMeetingDraftBody(const std::string& value)
		{
			if (value.size() < 10)
			{
				return false;
			}

			const std::string lowered = ToLowerAscii(value);
			if (value.find("会议室") != std::string::npos ||
				value.find("会议") != std::string::npos ||
				value.find("开会") != std::string::npos ||
				value.find("老板") != std::string::npos ||
				value.find("经理") != std::string::npos ||
				value.find("同事") != std::string::npos ||
				value.find("下周") != std::string::npos ||
				value.find("本周") != std::string::npos ||
				value.find("今天") != std::string::npos ||
				value.find("明天") != std::string::npos ||
				value.find("二楼") != std::string::npos ||
				value.find("三楼") != std::string::npos ||
				value.find("办公室") != std::string::npos ||
				value.find("参加") != std::string::npos ||
				value.find("参会") != std::string::npos ||
				lowered.find("boss") != std::string::npos ||
				lowered.find("meeting room") != std::string::npos ||
				lowered.find("calendar") != std::string::npos ||
				lowered.find("schedule") != std::string::npos ||
				lowered.find("invite") != std::string::npos)
			{
				return true;
			}

			return false;
		}

		void EmitExtractionDiagnostic(
			const std::string& stage,
			const std::string& payload)
		{
			if (!IsExtractionDiagnosticsEnabled())
			{
				return;
			}

#ifdef _WIN32
			const std::string line =
				"[content-polish] " + stage + " " + payload + "\n";
			OutputDebugStringA(line.c_str());
#else
			(void)stage;
			(void)payload;
#endif
		}

		bool IsMissingSummaryField(const std::string& value)
		{
			const std::string trimmed = TrimAsciiLocal(value);
			if (trimmed.empty())
			{
				return true;
			}

			const std::string lowered = ToLowerAscii(trimmed);
			return lowered == "not found" || lowered == "n/a" || lowered == "na";
		}

		bool LooksLikeInstructionArtifact(const std::string& value)
		{
			const std::string trimmed = TrimAsciiLocal(value);
			if (trimmed.empty())
			{
				return true;
			}

			const std::string lowered = ToLowerAscii(trimmed);
			const std::vector<std::string> controlFragmentsAscii = {
				"call summarize",
				"call humanizer",
				"call imap",
				"call smtp",
				"content polishing",
				"distribution flow",
				"send to",
			};
			const std::vector<std::string> controlFragmentsAsciiLoose = {
				// Match only on ASCII-normalized text; avoid substring hits inside UTF-8 CJK.
				"extract",
				"rewrite",
			};
			const std::vector<std::string> controlFragmentsUtf8 = {
				"去 ai 化",
				"去ai化",
				// Avoid bare "调用"/"提取" — they appear inside normal Chinese prose.
				"提取其中的",
				"提取时间",
				"提取人物",
				"提取地点",
				"调用 summarize",
				"调用 humanizer",
				"重写",
				"发送给",
			};
			const std::vector<std::string> businessFragmentsAscii = {
				"meeting",
				"review",
				"ui",
				"requirement",
				"next ",
				"room",
				"boss",
				"attend",
			};
			const std::vector<std::string> businessFragmentsUtf8 = {
				"下周",
				"周",
				"会议室",
				"老板",
				"参加",
				"需求",
			};

			bool hasControl = false;
			for (const auto& fragment : controlFragmentsAscii)
			{
				if (!fragment.empty() && lowered.find(fragment) != std::string::npos)
				{
					hasControl = true;
					break;
				}
			}
			if (!hasControl)
			{
				for (const auto& fragment : controlFragmentsAsciiLoose)
				{
					if (!fragment.empty() && lowered.find(fragment) != std::string::npos)
					{
						hasControl = true;
						break;
					}
				}
			}
			if (!hasControl)
			{
				for (const auto& fragment : controlFragmentsUtf8)
				{
					if (fragment.empty())
					{
						continue;
					}
					// Match on ASCII-lowercased UTF-8 so phrases like "去 AI 化" match fragment "去 ai 化".
					if (lowered.find(ToLowerAscii(fragment)) != std::string::npos)
					{
						hasControl = true;
						break;
					}
				}
			}

			if (!hasControl)
			{
				return false;
			}

			for (const auto& fragment : businessFragmentsAscii)
			{
				if (!fragment.empty() && lowered.find(fragment) != std::string::npos)
				{
					return false;
				}
			}
			for (const auto& fragment : businessFragmentsUtf8)
			{
				if (!fragment.empty() && trimmed.find(fragment) != std::string::npos)
				{
					return false;
				}
			}

			return true;
		}

		int CountMissingCriticalSummaryFields(
			const std::string& timeValue,
			const std::string& locationValue,
			const std::string& peopleValue)
		{
			int missing = 0;
			if (IsMissingSummaryField(timeValue))
			{
				++missing;
			}
			if (IsMissingSummaryField(locationValue))
			{
				++missing;
			}
			if (IsMissingSummaryField(peopleValue))
			{
				++missing;
			}
			return missing;
		}

		bool ContainsAnyFragment(
			const std::string& value,
			const std::vector<std::string>& fragments)
		{
			for (const auto& fragment : fragments)
			{
				if (!fragment.empty() && value.find(fragment) != std::string::npos)
				{
					return true;
				}
			}
			return false;
		}

		std::vector<std::string> CollectQuotedCandidates(const std::string& input)
		{
			const std::vector<std::pair<std::string, std::string>> quotePairs = {
				{ "\"", "\"" },
				{ "\'", "\'" },
				{ "“", "”" },
				{ "‘", "’" },
				{ "「", "」" },
				{ "『", "』" },
			};

			std::vector<std::string> candidates;
			for (const auto& pair : quotePairs)
			{
				std::size_t startPos = 0;
				while (startPos < input.size())
				{
					const std::size_t openPos = input.find(pair.first, startPos);
					if (openPos == std::string::npos)
					{
						break;
					}

					const std::size_t contentStart = openPos + pair.first.size();
					const std::size_t closePos = input.find(pair.second, contentStart);
					if (closePos == std::string::npos)
					{
						break;
					}

					const std::string quoted = TrimAsciiLocal(
						input.substr(contentStart, closePos - contentStart));
					if (!quoted.empty())
					{
						candidates.push_back(quoted);
					}

					startPos = closePos + pair.second.size();
				}
			}

			return candidates;
		}

		std::optional<std::string> ExtractDraftByWorkflowMarkers(const std::string& input)
		{
#ifdef _WIN32
			const std::wstring winput = Utf8ToWide(input);
			if (!(winput.empty() && !input.empty()))
			{
				try
				{
					static const std::wregex kChineseWorkflowDraftMarker(
						LR"((?:读取我提供的这段口语化草稿|口语化草稿|草稿(?:内容)?)\s*[：:]\s*[“"「『]?\s*(.+?)\s*[”"」』]?\s*(?:(?:[；;。]\s*(?:2|第二步|步骤2|步骤二)\s*(?:[\.\)）:：、])?)|$))");
					static const std::wregex kChineseDraftMarker(
						LR"((?:草稿|口语化草稿|草稿内容)\s*[：:]\s*[“"「『]?\s*([^;；\n。]+?)\s*[”"」』]?\s*(?:$|[；;。]))");

					std::wsmatch wmatch;
					if (std::regex_search(winput, wmatch, kChineseWorkflowDraftMarker) &&
						wmatch.size() >= 2)
					{
						const std::string value = TrimAsciiLocal(WideToUtf8(wmatch[1].str()));
						if (!value.empty())
						{
							return value;
						}
					}

					if (std::regex_search(winput, wmatch, kChineseDraftMarker) &&
						wmatch.size() >= 2)
					{
						const std::string value = TrimAsciiLocal(WideToUtf8(wmatch[1].str()));
						if (!value.empty())
						{
							return value;
						}
					}
				}
				catch (const std::regex_error&)
				{
				}
			}
#endif

			static const std::regex kChineseDraftMarker(
				R"((?:草稿|口语化草稿|草稿内容)\s*[：:]\s*[“"「『]?\s*([^;；\n。]+?)\s*[”"」』]?\s*(?:$|[；;。]))");
			static const std::regex kChineseWorkflowDraftMarker(
				R"((?:读取我提供的这段口语化草稿|口语化草稿|草稿(?:内容)?)\s*[：:]\s*[“"「『]?\s*(.+?)\s*[”"」』]?\s*(?:(?:[；;。]\s*(?:2|第二步|步骤2|步骤二)\s*(?:[\.\)）:：、])?)|$))");
			static const std::regex kEnglishDraftMarker(
				R"((?:draft)\s*[:]\s*(.+?)(?:;\s*2[\.\)]|$))",
				std::regex_constants::icase);

			std::smatch match;
			if (std::regex_search(input, match, kChineseWorkflowDraftMarker) && match.size() >= 2)
			{
				const std::string value = TrimAsciiLocal(match[1].str());
				if (!value.empty())
				{
					return value;
				}
			}

			if (std::regex_search(input, match, kChineseDraftMarker) && match.size() >= 2)
			{
				const std::string value = TrimAsciiLocal(match[1].str());
				if (!value.empty())
				{
					return value;
				}
			}

			if (std::regex_search(input, match, kEnglishDraftMarker) && match.size() >= 2)
			{
				const std::string value = TrimAsciiLocal(match[1].str());
				if (!value.empty())
				{
					return value;
				}
			}

			const bool likelyWorkflowPrompt =
				input.find("口语化草稿") != std::string::npos ||
				input.find("草稿") != std::string::npos ||
				ToLowerAscii(input).find("draft") != std::string::npos;
			if (likelyWorkflowPrompt)
			{
				const std::vector<std::string> quotedCandidates = CollectQuotedCandidates(input);
				const std::vector<std::string> controlFragments = {
					"调用 summarize",
					"调用 humanizer",
					"提取其中的",
					"发送给",
					"call summarize",
					"call humanizer",
					"imap-smtp-email",
				};
				for (const auto& candidate : quotedCandidates)
				{
					const std::string trimmed = TrimAsciiLocal(candidate);
					if (trimmed.size() < 8)
					{
						continue;
					}

					if (ContainsAnyFragment(trimmed, controlFragments) ||
						ContainsAnyFragment(ToLowerAscii(trimmed), controlFragments))
					{
						continue;
					}

					return trimmed;
				}
			}

			return std::nullopt;
		}

		bool LooksLikeControlOnlyText(const std::string& value)
		{
			if (value.empty())
			{
				return true;
			}

			const std::string lowered = ToLowerAscii(value);
			const std::vector<std::string> englishControlFragments = {
				"call summarize",
				"call humanizer",
				"call imap",
				"call smtp",
				"imap-smtp-email",
				"content polishing",
				"distribution flow",
				"extract time",
				"extract location",
				"extract people",
				"core request",
				"summarize",
				"humanizer",
			};
			const std::vector<std::string> chineseControlFragments = {
				"内容润色分发流",
				"去 ai 化",
				"去AI化",
				"提取其中的",
				"提取时间",
				"提取人物",
				"提取地点",
				"调用 summarize",
				"调用 humanizer",
				"重写",
				"发送给",
			};
			const std::vector<std::string> businessSignalFragments = {
				"next ",
				"meeting room",
				"boss",
				"ui",
				"下周",
				"周三",
				"会议室",
				"老板",
				"参加",
				"需求",
			};

			const bool hasControlChinese = [&]() {
				for (const auto& fragment : chineseControlFragments)
				{
					if (fragment.empty())
					{
						continue;
					}
					if (value.find(fragment) != std::string::npos)
					{
						return true;
					}
					if (lowered.find(ToLowerAscii(fragment)) != std::string::npos)
					{
						return true;
					}
				}
				return false;
				}();
			const bool hasControl =
				ContainsAnyFragment(lowered, englishControlFragments) || hasControlChinese;
			const bool hasBusinessSignal = ContainsAnyFragment(lowered, businessSignalFragments) ||
				ContainsAnyFragment(value, businessSignalFragments);

			if (!hasControl)
			{
				return false;
			}

			if (hasBusinessSignal)
			{
				return false;
			}

			return value.size() < 80;
		}

		bool ShouldFallbackTrimmedDraftBlob(const std::string& trimmed)
		{
			if (LooksLikeLikelyMeetingDraftBody(trimmed))
			{
				return true;
			}
			if (LooksLikeControlOnlyText(trimmed))
			{
				return false;
			}
			return Utf8MeetsSubstantiveDraftThreshold(trimmed);
		}

		int ScoreDraftCandidate(const std::string& value)
		{
			if (value.empty())
			{
				return (std::numeric_limits<int>::min)();
			}

			int score = static_cast<int>((std::min<std::size_t>)(value.size(), 400));
			const std::string lowered = ToLowerAscii(value);
			const std::vector<std::string> timingSignals = {
				"next ",
				" at ",
				"pm",
				"am",
				"下周",
				"周",
				"下午",
				"点",
			};
			const std::vector<std::string> locationSignals = {
				"meeting room",
				" in ",
				" at ",
				"会议室",
				"二楼",
				"在",
			};
			const std::vector<std::string> peopleSignals = {
				"boss",
				"we",
				"team",
				"老板",
				"我们",
				"团队",
			};

			if (ContainsAnyFragment(lowered, timingSignals) || ContainsAnyFragment(value, timingSignals))
			{
				score += 120;
			}
			if (ContainsAnyFragment(lowered, locationSignals) || ContainsAnyFragment(value, locationSignals))
			{
				score += 120;
			}
			if (ContainsAnyFragment(lowered, peopleSignals) || ContainsAnyFragment(value, peopleSignals))
			{
				score += 80;
			}
			if (LooksLikeControlOnlyText(value))
			{
				score -= 1000;
			}
			return score;
		}

		std::string SelectLikelyDraftText(const std::string& value)
		{
			std::vector<std::string> candidates = CollectQuotedCandidates(value);
			if (const auto markerDraft = ExtractDraftByWorkflowMarkers(value);
				markerDraft.has_value())
			{
				candidates.push_back(markerDraft.value());
			}
			const std::string trimmed = TrimAsciiLocal(value);
			if (!trimmed.empty())
			{
				candidates.push_back(trimmed);
			}

			std::string bestCandidate;
			int bestScore = (std::numeric_limits<int>::min)();
			for (const auto& candidate : candidates)
			{
				const int score = ScoreDraftCandidate(candidate);
				if (score > bestScore)
				{
					bestScore = score;
					bestCandidate = candidate;
				}
			}

			if (!trimmed.empty() && ContainsPolishOrchestrationNoise(trimmed))
			{
				int bestEmbeddedScore = (std::numeric_limits<int>::min)();
				std::string bestEmbedded;
				for (const auto& candidate : candidates)
				{
					if (candidate.empty() || candidate.size() + 6 >= trimmed.size())
					{
						continue;
					}
					if (trimmed.find(candidate) == std::string::npos)
					{
						continue;
					}
					const int embeddedScore = ScoreDraftCandidate(candidate);
					if (embeddedScore > bestEmbeddedScore)
					{
						bestEmbeddedScore = embeddedScore;
						bestEmbedded = candidate;
					}
				}
				if (!bestEmbedded.empty() &&
					!LooksLikeControlOnlyText(bestEmbedded) &&
					bestEmbedded.size() >= 8 &&
					bestEmbeddedScore + 500 >= bestScore)
				{
					bestCandidate = bestEmbedded;
					bestScore = bestEmbeddedScore + 500;
					EmitExtractionDiagnostic(
						"draft_select",
						"candidate_count=" + std::to_string(candidates.size()) +
						" selected_len=" + std::to_string(bestCandidate.size()) +
						" confidence=embedded_orchestration");
				}
			}

			if (bestCandidate.empty())
			{
				EmitExtractionDiagnostic(
					"draft_select",
					"candidate_count=" + std::to_string(candidates.size()) +
					" selected_len=0 confidence=none rejection_reason=no_candidate");
				if (ShouldFallbackTrimmedDraftBlob(trimmed))
				{
					EmitExtractionDiagnostic(
						"draft_select",
						"candidate_count=" + std::to_string(candidates.size()) +
						" selected_len=" + std::to_string(trimmed.size()) +
						" confidence=fallback_trimmed_draft_blob");
					return trimmed;
				}
				EmitExtractionDiagnostic(
					"draft_reject",
					"rejection_reason=no_candidate candidate_count=" +
					std::to_string(candidates.size()));
				return {};
			}

			if (LooksLikeControlOnlyText(bestCandidate))
			{
				for (const auto& candidate : candidates)
				{
					if (!LooksLikeControlOnlyText(candidate) && candidate.size() >= 8)
					{
						EmitExtractionDiagnostic(
							"draft_select",
							"candidate_count=" + std::to_string(candidates.size()) +
							" selected_len=" + std::to_string(candidate.size()) +
							" confidence=high");
						return candidate;
					}
				}
				EmitExtractionDiagnostic(
					"draft_select",
					"candidate_count=" + std::to_string(candidates.size()) +
					" selected_len=0 confidence=none rejection_reason=control_only");
				if (ShouldFallbackTrimmedDraftBlob(trimmed))
				{
					EmitExtractionDiagnostic(
						"draft_select",
						"candidate_count=" + std::to_string(candidates.size()) +
						" selected_len=" + std::to_string(trimmed.size()) +
						" confidence=fallback_trimmed_draft_blob");
					return trimmed;
				}
				EmitExtractionDiagnostic(
					"draft_reject",
					"rejection_reason=control_only selected_len=" +
					std::to_string(bestCandidate.size()));
				return {};
			}

			if (bestCandidate.size() < 8)
			{
				for (const auto& candidate : candidates)
				{
					if (candidate.size() >= 8 && !LooksLikeControlOnlyText(candidate))
					{
						EmitExtractionDiagnostic(
							"draft_select",
							"candidate_count=" + std::to_string(candidates.size()) +
							" selected_len=" + std::to_string(candidate.size()) +
							" confidence=medium");
						return candidate;
					}
				}
				EmitExtractionDiagnostic(
					"draft_select",
					"candidate_count=" + std::to_string(candidates.size()) +
					" selected_len=0 confidence=none rejection_reason=too_short");
				if (ShouldFallbackTrimmedDraftBlob(trimmed))
				{
					EmitExtractionDiagnostic(
						"draft_select",
						"candidate_count=" + std::to_string(candidates.size()) +
						" selected_len=" + std::to_string(trimmed.size()) +
						" confidence=fallback_trimmed_draft_blob");
					return trimmed;
				}
				EmitExtractionDiagnostic(
					"draft_reject",
					"rejection_reason=too_short selected_len=" +
					std::to_string(bestCandidate.size()));
				return {};
			}

			EmitExtractionDiagnostic(
				"draft_select",
				"candidate_count=" + std::to_string(candidates.size()) +
				" selected_len=" + std::to_string(bestCandidate.size()) +
				" confidence=high");
			return bestCandidate;
		}

		std::optional<std::string> TryExtractTextFromJsonObject(
			const nlohmann::json& params,
			const int parseDepth = 0)
		{
			if (parseDepth > 5)
			{
				return std::nullopt;
			}

			static const char* kKeys[] = {
				"text",
				"draft",
				"content",
				"message",
				"input",
				"body",
				"prompt",
				"source",
				"query",
				"instruction",
				"instructions",
				"user_message",
				"userMessage",
				"value",
				"raw",
				"description",
				"markdown",
				"user_input",
				"userInput",
				"output",
				"transcript",
			};

			for (const auto* key : kKeys)
			{
				const auto it = params.find(key);
				if (it == params.end())
				{
					continue;
				}
				if (!it->is_string() && !it->is_array() && !it->is_object())
				{
					continue;
				}

				const auto coerced = CoerceJsonValueToPlainText(*it);
				if (!coerced.has_value())
				{
					continue;
				}

				std::string value = TrimAsciiLocal(*coerced);
				if (value.empty())
				{
					continue;
				}

				if (parseDepth < 4 && value.size() >= 2 && value.front() == '{')
				{
					try
					{
						const nlohmann::json inner = nlohmann::json::parse(value);
						if (inner.is_object())
						{
							if (const auto nested = TryExtractTextFromJsonObject(inner, parseDepth + 1);
								nested.has_value())
							{
								return nested;
							}
						}
					}
					catch (const nlohmann::json::parse_error&)
					{
					}
					catch (const std::exception&)
					{
					}
				}

				const std::string selected = SelectLikelyDraftText(value);
				if (!selected.empty())
				{
					return selected;
				}
			}

			if (parseDepth == 0)
			{
				return TryExtractTextFromMessagesArray(params);
			}

			return std::nullopt;
		}

	} // namespace

	std::vector<ImapSmtpToolRuntimeSpec> BuildImapSmtpToolRuntimeSpecs()
	{
		return {
			{ "imap_smtp_email.imap.check", "IMAP Check", "scripts/imap.js", "check" },
			{ "imap_smtp_email.imap.fetch", "IMAP Fetch", "scripts/imap.js", "fetch" },
			{ "imap_smtp_email.imap.download", "IMAP Download Attachments", "scripts/imap.js", "download" },
			{ "imap_smtp_email.imap.search", "IMAP Search", "scripts/imap.js", "search" },
			{ "imap_smtp_email.imap.mark_read", "IMAP Mark Read", "scripts/imap.js", "mark-read" },
			{ "imap_smtp_email.imap.mark_unread", "IMAP Mark Unread", "scripts/imap.js", "mark-unread" },
			{ "imap_smtp_email.imap.list_mailboxes", "IMAP List Mailboxes", "scripts/imap.js", "list-mailboxes" },
			{ "imap_smtp_email.imap.list_accounts", "IMAP List Accounts", "scripts/imap.js", "list-accounts" },
			{ "imap_smtp_email.smtp.send", "SMTP Send", "scripts/smtp.js", "send" },
			{ "imap_smtp_email.smtp.test", "SMTP Test", "scripts/smtp.js", "test" },
			{ "imap_smtp_email.smtp.list_accounts", "SMTP List Accounts", "scripts/smtp.js", "list-accounts" },
		};
	}

	std::vector<BraveSearchToolRuntimeSpec> BuildBraveSearchToolRuntimeSpecs()
	{
		return {
			{ "brave_search.search.web", "Brave Web Search", "scripts/search.js" },
			{ "brave_search.fetch.content", "Brave Fetch Content", "scripts/content.js" },
			{ "web_browsing.search.web", "Web Browsing Search", "scripts/search.js" },
			{ "web_browsing.fetch.content", "Web Browsing Fetch Content", "scripts/content.js" },
		};
	}

	bool IsBraveSearchWebToolId(const std::string& toolId)
	{
		return toolId == "brave_search.search.web" ||
			toolId == "web_browsing.search.web";
	}

	bool IsBraveFetchContentToolId(const std::string& toolId)
	{
		return toolId == "brave_search.fetch.content" ||
			toolId == "web_browsing.fetch.content";
	}

	std::vector<BaiduSearchToolRuntimeSpec> BuildBaiduSearchToolRuntimeSpecs()
	{
		return {
			{ "baidu-search.search.web", "Baidu Web Search", "scripts/search.py" },
		};
	}

	std::vector<ContentPolishingToolRuntimeSpec> BuildContentPolishingToolRuntimeSpecs()
	{
		return {
			{ "summarize.extract", "Summarize Extract" },
			{ "humanizer.rewrite", "Humanizer Rewrite" },
		};
	}

	std::vector<NanoPdfToolRuntimeSpec> BuildNanoPdfToolRuntimeSpecs()
	{
		return {
			{ "nano_pdf.edit", "Nano PDF Edit", "scripts/nano_pdf_bridge.py" },
			{ "nano_pdf.generate", "Nano PDF Generate", "scripts/nano_pdf_bridge.py" },
		};
	}

	std::optional<std::string> ExtractTextArgument(const nlohmann::json& params)
	{
		if (!params.is_object())
		{
			return std::nullopt;
		}

		if (const auto direct = TryExtractTextFromJsonObject(params);
			direct.has_value())
		{
			return direct;
		}

		static const char* kNested[] = {
			"arguments",
			"args",
			"payload",
			"parameters",
			"tool_arguments",
			"toolArguments",
			"params",
		};

		for (const auto* nest : kNested)
		{
			const auto it = params.find(nest);
			if (it == params.end() || !it->is_object())
			{
				continue;
			}

			if (const auto nested = TryExtractTextFromJsonObject(it.value(), 1);
				nested.has_value())
			{
				return nested;
			}
		}

		return std::nullopt;
	}

	std::optional<std::string> ExtractHumanizerTextArgument(const nlohmann::json& params)
	{
		const char* kPreferredKeys[] = {
			"summary",
			"extracted",
			"text",
			"draft",
			"content",
			"message",
			"input",
		};

		for (const auto* key : kPreferredKeys)
		{
			const auto it = params.find(key);
			if (it == params.end() || !it->is_string())
			{
				continue;
			}

			const std::string value = TrimAsciiLocal(it->get<std::string>());
			if (!value.empty())
			{
				return value;
			}
		}

		// Backward-compatible object-to-text assembly path.
		const std::string timeValue = TrimAsciiLocal(
			params.value("time", std::string()));
		const std::string locationValue = TrimAsciiLocal(
			params.value("location", std::string()));
		const std::string peopleValue = TrimAsciiLocal(
			params.value("people", std::string()));
		const std::string requestValue = TrimAsciiLocal(
			params.value("coreRequest", params.value("core_request", std::string())));

		if (!timeValue.empty() ||
			!locationValue.empty() ||
			!peopleValue.empty() ||
			!requestValue.empty())
		{
			return
				"Time: " + (timeValue.empty() ? "not found" : timeValue) + "\n" +
				"Location: " + (locationValue.empty() ? "not found" : locationValue) + "\n" +
				"People: " + (peopleValue.empty() ? "not found" : peopleValue) + "\n" +
				"Core request: " + (requestValue.empty() ? "not found" : requestValue);
		}

		return std::nullopt;
	}

	std::string BuildSummarizeExtractOutput(const std::string& text)
	{
		const bool multilingualEnabled = IsMultilingualExtractorEnabled();
		const bool multilingualFallbackByContent =
			!multilingualEnabled && ContainsLikelyCjkMarkers(text);
		const bool effectiveMultilingualEnabled =
			multilingualEnabled || multilingualFallbackByContent;
		std::string timeValue = "not found";
		std::string locationValue = "not found";
		std::string peopleValue = "not found";
		std::string requestValue = TrimAsciiLocal(text);

		static const std::regex kTimeRegexEnglish(
			R"(((next\s+\w+)\s+at\s+\d{1,2}[:.]\d{2}\s*(?:AM|PM|am|pm)?))",
			std::regex_constants::icase);
#ifndef _WIN32
		// "下周三" is 下周 + 三, not 下周 + 周三 — optional week prefix must not consume 周 before 周几.
		static const std::regex kTimeRegexChineseWeekday(
			R"(((?:(?:下周|本周|这周)[一二三四五六日天]|周[一二三四五六日天])(?:上午|下午|晚上|中午)?(?:\d{1,2}|[一二三四五六七八九十两]+)点(?:半|[0-5]?\d分?)?))");
		static const std::regex kTimeRegexChineseRelativeDay(
			R"(((?:今天|明天|后天)(?:上午|下午|晚上|中午)?(?:\d{1,2}|[一二三四五六七八九十两]+)点(?:半|[0-5]?\d分?)?))");
		static const std::regex kTimeRegexClock(
			R"(((?:上午|下午|晚上|中午)?\d{1,2}[:：]\d{2}))");
#endif

		std::smatch match;
		if (std::regex_search(text, match, kTimeRegexEnglish) && match.size() >= 2)
		{
			timeValue = match[1].str();
		}
		else if (effectiveMultilingualEnabled)
		{
#ifdef _WIN32
			std::string wideTimeMatch;
			if (TryMatchChineseTimeUtf8Wide(text, wideTimeMatch))
			{
				timeValue = wideTimeMatch;
			}
#else
			if (std::regex_search(text, match, kTimeRegexChineseWeekday) &&
				match.size() >= 2)
			{
				timeValue = match[1].str();
			}
			else if (std::regex_search(text, match, kTimeRegexChineseRelativeDay) &&
				match.size() >= 2)
			{
				timeValue = match[1].str();
			}
			else if (std::regex_search(text, match, kTimeRegexClock) &&
				match.size() >= 2)
			{
				timeValue = match[1].str();
			}
#endif
		}

		static const std::regex kLocationRegexEnglish(
			R"((?:in|at)\s+the\s+([^,.;\n]+(?:meeting\s+room|room)))",
			std::regex_constants::icase);
#ifndef _WIN32
		static const std::regex kLocationRegexChinese(
			R"((?:在)\s*([^，。,；;\n]{1,40}(?:会议室|会议厅|room)))");
#endif

		if (std::regex_search(text, match, kLocationRegexEnglish) && match.size() >= 2)
		{
			locationValue = TrimAsciiLocal(match[1].str());
		}
		else if (effectiveMultilingualEnabled)
		{
#ifdef _WIN32
			std::string wideLocationMatch;
			if (TryMatchChineseLocationUtf8Wide(text, wideLocationMatch))
			{
				locationValue = TrimAsciiLocal(wideLocationMatch);
			}
#else
			if (std::regex_search(text, match, kLocationRegexChinese) &&
				match.size() >= 2)
			{
				locationValue = TrimAsciiLocal(match[1].str());
			}
#endif
			std::string nearRoom;
			if (TryMeetingRoomNearSuffixUtf8(text, nearRoom))
			{
				if (IsMissingSummaryField(locationValue) ||
					nearRoom.size() + 6 <= locationValue.size())
				{
					locationValue = nearRoom;
				}
			}
		}

		std::vector<std::string> people;
		const std::string lowered = ToLowerAscii(text);
		if (lowered.find("boss") != std::string::npos ||
			(effectiveMultilingualEnabled &&
				(text.find("老板") != std::string::npos ||
					text.find("领导") != std::string::npos)))
		{
			people.push_back("boss");
		}
		if (lowered.find("we") != std::string::npos ||
			lowered.find("team") != std::string::npos ||
			(effectiveMultilingualEnabled &&
				(text.find("我们") != std::string::npos ||
					text.find("团队") != std::string::npos)))
		{
			people.push_back("requester team");
		}
		if (!people.empty())
		{
			peopleValue.clear();
			for (std::size_t i = 0; i < people.size(); ++i)
			{
				if (i > 0)
				{
					peopleValue += ", ";
				}
				peopleValue += people[i];
			}
		}

		if (requestValue.size() > 220)
		{
			requestValue = requestValue.substr(0, 220) + "...";
		}

		const int missingCritical = CountMissingCriticalSummaryFields(
			timeValue,
			locationValue,
			peopleValue);
		const std::string confidence =
			missingCritical >= 2 ? "low" : (missingCritical == 1 ? "medium" : "high");
		EmitExtractionDiagnostic(
			"structured_extract",
			"multilingual_enabled=" + std::string(multilingualEnabled ? "true" : "false") +
			" multilingual_effective=" +
			std::string(effectiveMultilingualEnabled ? "true" : "false") +
			" multilingual_fallback_by_content=" +
			std::string(multilingualFallbackByContent ? "true" : "false") +
			" confidence=" + confidence +
			" time_missing=" + std::string(IsMissingSummaryField(timeValue) ? "true" : "false") +
			" location_missing=" + std::string(IsMissingSummaryField(locationValue) ? "true" : "false") +
			" people_missing=" + std::string(IsMissingSummaryField(peopleValue) ? "true" : "false"));

		return
			"Time: " + timeValue + "\n" +
			"Location: " + locationValue + "\n" +
			"People: " + peopleValue + "\n" +
			"Core request: " + requestValue;
	}

	std::string BuildHumanizerRewriteOutput(const std::string& text)
	{
		std::string timeValue = ExtractSummaryField(text, "Time");
		std::string locationValue = ExtractSummaryField(text, "Location");
		std::string peopleValue = ExtractSummaryField(text, "People");
		std::string requestValue = ExtractSummaryField(text, "Core request");

		// If the input is not a structured summary payload, recover from raw text first.
		if (timeValue.empty() &&
			locationValue.empty() &&
			peopleValue.empty() &&
			requestValue.empty())
		{
			const std::string candidate = SelectLikelyDraftText(text);
			const std::string recoverySource = candidate.empty() ? TrimAsciiLocal(text) : candidate;
			if (!recoverySource.empty())
			{
				const std::string recoveredSummary =
					BuildSummarizeExtractOutput(recoverySource);
				timeValue = ExtractSummaryField(recoveredSummary, "Time");
				locationValue = ExtractSummaryField(recoveredSummary, "Location");
				peopleValue = ExtractSummaryField(recoveredSummary, "People");
				requestValue = ExtractSummaryField(recoveredSummary, "Core request");
			}
		}

		// Phase 3: low-confidence summaries attempt deterministic recovery.
		int missingCritical = CountMissingCriticalSummaryFields(
			timeValue,
			locationValue,
			peopleValue);
		if (missingCritical >= 2 &&
			!requestValue.empty() &&
			!LooksLikeInstructionArtifact(requestValue))
		{
			const std::string recoveredSummary = BuildSummarizeExtractOutput(requestValue);
			const std::string recoveredTime = ExtractSummaryField(recoveredSummary, "Time");
			const std::string recoveredLocation = ExtractSummaryField(recoveredSummary, "Location");
			const std::string recoveredPeople = ExtractSummaryField(recoveredSummary, "People");

			if (IsMissingSummaryField(timeValue) && !IsMissingSummaryField(recoveredTime))
			{
				timeValue = recoveredTime;
			}
			if (IsMissingSummaryField(locationValue) &&
				!IsMissingSummaryField(recoveredLocation))
			{
				locationValue = recoveredLocation;
			}
			if (IsMissingSummaryField(peopleValue) && !IsMissingSummaryField(recoveredPeople))
			{
				peopleValue = recoveredPeople;
			}
		}

		// Secondary recovery: re-extract from full input text for workflow-wrapped prompts.
		missingCritical = CountMissingCriticalSummaryFields(
			timeValue,
			locationValue,
			peopleValue);
		if (missingCritical >= 2)
		{
			const std::string candidate = SelectLikelyDraftText(text);
			const std::string recoverySource =
				candidate.empty() ? TrimAsciiLocal(text) : candidate;
			// Do not gate recovery on LooksLikeInstructionArtifact(recoverySource): orchestration
			// prompts legitimately contain both control verbs and business draft content.
			if (!recoverySource.empty() && !LooksLikeControlOnlyText(recoverySource))
			{
				const std::string recoveredSummary =
					BuildSummarizeExtractOutput(recoverySource);
				const std::string recoveredTime = ExtractSummaryField(recoveredSummary, "Time");
				const std::string recoveredLocation = ExtractSummaryField(recoveredSummary, "Location");
				const std::string recoveredPeople = ExtractSummaryField(recoveredSummary, "People");
				const std::string recoveredRequest = ExtractSummaryField(recoveredSummary, "Core request");

				if (IsMissingSummaryField(timeValue) && !IsMissingSummaryField(recoveredTime))
				{
					timeValue = recoveredTime;
				}
				if (IsMissingSummaryField(locationValue) &&
					!IsMissingSummaryField(recoveredLocation))
				{
					locationValue = recoveredLocation;
				}
				if (IsMissingSummaryField(peopleValue) &&
					!IsMissingSummaryField(recoveredPeople))
				{
					peopleValue = recoveredPeople;
				}
				if (requestValue.empty() && !LooksLikeInstructionArtifact(recoveredRequest))
				{
					requestValue = recoveredRequest;
				}
			}
		}

		if (LooksLikeInstructionArtifact(requestValue))
		{
			requestValue.clear();
		}

		missingCritical = CountMissingCriticalSummaryFields(
			timeValue,
			locationValue,
			peopleValue);
		if (missingCritical >= 2)
		{
			std::string body;
			body += "Dear Sir,\n\n";
			body += "Thank you for the draft. The extracted meeting details are currently incomplete and need confirmation before sending.\n\n";
			body += "Please confirm the following:\n";
			body += "- Time\n";
			body += "- Location\n";
			body += "- Participants\n";
			if (!requestValue.empty())
			{
				body += "\nDraft intent: " + requestValue + "\n";
			}
			body += "\nBest regards,";
			return body;
		}

		std::string body;
		body += "Dear Sir,\n\n";
		body += "I would like to cordially invite you to the upcoming review meeting regarding the latest UI requirements.\n\n";
		if (!timeValue.empty())
		{
			body += "Time: " + timeValue + "\n";
		}
		if (!locationValue.empty())
		{
			body += "Location: " + locationValue + "\n";
		}
		if (!peopleValue.empty())
		{
			body += "Participants: " + peopleValue + "\n";
		}
		if (!requestValue.empty())
		{
			body += "\nPurpose: " + requestValue + "\n";
		}
		body += "\nYour attendance would be greatly appreciated.\n\n";
		body += "Best regards,";
		return body;
	}

	std::string TrimAsciiForBraveSearch(const std::string& value)
	{
		return TrimAsciiLocal(value);
	}

	bool HasControlCharsForBraveSearch(const std::string& value)
	{
		for (const unsigned char ch : value)
		{
			if ((ch < 0x20 && ch != '\t') || ch == 0x7F)
			{
				return true;
			}
		}

		return false;
	}

	bool IsHttpUrlForBraveSearch(const std::string& value)
	{
		const std::string lowered = ToLowerAscii(value);
		return lowered.rfind("http://", 0) == 0 ||
			lowered.rfind("https://", 0) == 0;
	}

	bool IsDateRangeTokenForBaiduSearch(const std::string& value)
	{
		if (value.size() != 22)
		{
			return false;
		}

		if (value[4] != '-' || value[7] != '-' || value[10] != 't' ||
			value[11] != 'o' || value[14] != '-' || value[17] != '-')
		{
			return false;
		}

		const std::size_t digitPositions[] = {
			0, 1, 2, 3, 5, 6, 8, 9, 12, 13, 15, 16, 18, 19, 20, 21
		};
		for (const auto pos : digitPositions)
		{
			if (!std::isdigit(static_cast<unsigned char>(value[pos])))
			{
				return false;
			}
		}

		return true;
	}

	std::string TruncateBraveToolOutput(
		const std::string& output,
		const std::size_t maxBytes)
	{
		if (output.size() <= maxBytes)
		{
			return output;
		}

		const std::size_t retained = maxBytes > 48 ? maxBytes - 48 : maxBytes;
		return output.substr(0, retained) +
			"\n...(truncated by blazeclaw runtime output limit)";
	}

	std::string ClassifyBraveFailureCode(const std::string& output)
	{
		const std::string lowered = ToLowerAscii(output);
		if (lowered.find("http 401") != std::string::npos ||
			lowered.find("http 403") != std::string::npos)
		{
			return "auth_error";
		}

		if (lowered.find("http 429") != std::string::npos)
		{
			return "rate_limited";
		}

		if (lowered.find("http 500") != std::string::npos ||
			lowered.find("http 502") != std::string::npos ||
			lowered.find("http 503") != std::string::npos ||
			lowered.find("http 504") != std::string::npos)
		{
			return "upstream_unavailable";
		}

		if (lowered.find("enotfound") != std::string::npos ||
			lowered.find("eai_again") != std::string::npos ||
			lowered.find("network") != std::string::npos ||
			lowered.find("fetch failed") != std::string::npos)
		{
			return "network_error";
		}

		return "script_runtime_error";
	}

	bool IsBraveNetworkTimeoutFailure(const std::string& output)
	{
		const std::string lowered = ToLowerAscii(output);
		return lowered.find("und_err_connect_timeout") != std::string::npos ||
			lowered.find("connect timeout") != std::string::npos ||
			lowered.find("etimedout") != std::string::npos ||
			lowered.find("timed out") != std::string::npos ||
			lowered.find("timeout") != std::string::npos;
	}

	std::string ClassifyBaiduFailureCode(const std::string& output)
	{
		const std::string lowered = ToLowerAscii(output);

		std::smatch httpStatusMatch;
		if (std::regex_search(
			lowered,
			httpStatusMatch,
			std::regex(R"(http(?:\s+error)?\s*[:=]?\s*([0-9]{3}))")) &&
			httpStatusMatch.size() >= 2)
		{
			const int status = std::atoi(httpStatusMatch[1].str().c_str());
			if (status == 401 || status == 403)
			{
				return "auth_error";
			}

			if (status == 429)
			{
				return "rate_limited";
			}

			if (status >= 500 && status < 600)
			{
				return "upstream_unavailable";
			}

			if (status >= 400 && status < 500)
			{
				return "invalid_arguments";
			}
		}

		if (lowered.find("baidu_api_key") != std::string::npos &&
			lowered.find("must be set") != std::string::npos)
		{
			return "baidu_api_key_missing";
		}

		if (lowered.find("unauthorized") != std::string::npos ||
			lowered.find("forbidden") != std::string::npos ||
			lowered.find("invalid token") != std::string::npos ||
			lowered.find("access token") != std::string::npos ||
			lowered.find("invalid api key") != std::string::npos ||
			lowered.find("authentication") != std::string::npos)
		{
			return "auth_error";
		}

		if (lowered.find("rate limit") != std::string::npos ||
			lowered.find("too many requests") != std::string::npos ||
			lowered.find("quota") != std::string::npos)
		{
			return "rate_limited";
		}

		if (lowered.find("http 401") != std::string::npos ||
			lowered.find("http 403") != std::string::npos)
		{
			return "auth_error";
		}

		if (lowered.find("http 429") != std::string::npos)
		{
			return "rate_limited";
		}

		if (lowered.find("timeout") != std::string::npos ||
			lowered.find("timed out") != std::string::npos)
		{
			return "network_timeout";
		}

		if (lowered.find("no module named") != std::string::npos ||
			lowered.find("modulenotfounderror") != std::string::npos)
		{
			return "dependency_missing";
		}

		if (lowered.find("json parse error") != std::string::npos ||
			lowered.find("request body must be a json object") != std::string::npos ||
			lowered.find("query must be present") != std::string::npos ||
			lowered.find("freshness must be") != std::string::npos)
		{
			return "invalid_arguments";
		}

		if (lowered.find("network") != std::string::npos ||
			lowered.find("connection") != std::string::npos ||
			lowered.find("ssl") != std::string::npos ||
			lowered.find("certificate") != std::string::npos ||
			lowered.find("httpsconnectionpool") != std::string::npos ||
			lowered.find("urlopen error") != std::string::npos ||
			lowered.find("proxyerror") != std::string::npos ||
			lowered.find("name resolution") != std::string::npos ||
			lowered.find("winerror") != std::string::npos ||
			lowered.find("max retries exceeded") != std::string::npos ||
			lowered.find("name or service not known") != std::string::npos)
		{
			return "network_error";
		}

		return "script_runtime_error";
	}

	std::optional<std::vector<std::string>> BuildImapSmtpCliArgs(
		const ImapSmtpToolRuntimeSpec& spec,
		const nlohmann::json& params,
		std::string& errorCode,
		std::string& errorMessage)
	{
		errorCode.clear();
		errorMessage.clear();

		std::vector<std::string> args;
		if (const auto accountIt = params.find("account");
			accountIt != params.end())
		{
			AppendFlagWithValue(args, "account", JsonValueToCliString(*accountIt));
		}

		args.push_back(spec.command);

		if (spec.id == "imap_smtp_email.imap.fetch" ||
			spec.id == "imap_smtp_email.imap.download")
		{
			const auto uidIt = params.find("uid");
			if (uidIt == params.end())
			{
				errorCode = "invalid_arguments";
				errorMessage = "uid is required";
				return std::nullopt;
			}

			const auto uid = JsonValueToCliString(*uidIt);
			if (!uid.has_value() || uid->empty())
			{
				errorCode = "invalid_arguments";
				errorMessage = "uid is invalid";
				return std::nullopt;
			}

			args.push_back(uid.value());
		}

		if (spec.id == "imap_smtp_email.imap.mark_read" ||
			spec.id == "imap_smtp_email.imap.mark_unread")
		{
			const auto uidsIt = params.find("uids");
			if (uidsIt == params.end() || !uidsIt->is_array() || uidsIt->empty())
			{
				errorCode = "invalid_arguments";
				errorMessage = "uids array is required";
				return std::nullopt;
			}

			for (const auto& uidNode : *uidsIt)
			{
				const auto uid = JsonValueToCliString(uidNode);
				if (uid.has_value() && !uid->empty())
				{
					args.push_back(uid.value());
				}
			}
			if (args.back() == spec.command)
			{
				errorCode = "invalid_arguments";
				errorMessage = "uids array must contain at least one value";
				return std::nullopt;
			}
		}

		auto appendIfPresent = [&](const std::string& jsonKey, const std::string& cliFlag)
			{
				const auto it = params.find(jsonKey);
				if (it == params.end())
				{
					return;
				}

				AppendFlagWithValue(args, cliFlag, JsonValueToCliString(*it));
			};

		if (spec.id == "imap_smtp_email.imap.check")
		{
			appendIfPresent("limit", "limit");
			appendIfPresent("mailbox", "mailbox");
			appendIfPresent("recent", "recent");
			AppendBoolAsValue(args, "unseen", params);
		}
		else if (spec.id == "imap_smtp_email.imap.download")
		{
			appendIfPresent("mailbox", "mailbox");
			appendIfPresent("dir", "dir");
			appendIfPresent("file", "file");
		}
		else if (spec.id == "imap_smtp_email.imap.search")
		{
			appendIfPresent("from", "from");
			appendIfPresent("subject", "subject");
			appendIfPresent("recent", "recent");
			appendIfPresent("since", "since");
			appendIfPresent("before", "before");
			appendIfPresent("limit", "limit");
			appendIfPresent("mailbox", "mailbox");
			AppendBoolAsValue(args, "unseen", params);
			AppendBoolAsValue(args, "seen", params);
		}
		else if (spec.id == "imap_smtp_email.imap.fetch" ||
			spec.id == "imap_smtp_email.imap.mark_read" ||
			spec.id == "imap_smtp_email.imap.mark_unread")
		{
			appendIfPresent("mailbox", "mailbox");
		}
		else if (spec.id == "imap_smtp_email.smtp.send")
		{
			appendIfPresent("to", "to");
			appendIfPresent("subject", "subject");
			appendIfPresent("subjectFile", "subject-file");
			appendIfPresent("body", "body");
			appendIfPresent("bodyFile", "body-file");
			appendIfPresent("htmlFile", "html-file");
			appendIfPresent("cc", "cc");
			appendIfPresent("bcc", "bcc");
			appendIfPresent("from", "from");

			const auto htmlIt = params.find("html");
			if (htmlIt != params.end() && htmlIt->is_boolean() && htmlIt->get<bool>())
			{
				args.push_back("--html");
				args.push_back("true");
			}

			const auto attachIt = params.find("attach");
			if (attachIt != params.end())
			{
				if (attachIt->is_array())
				{
					std::string joined;
					for (const auto& item : *attachIt)
					{
						const auto value = JsonValueToCliString(item);
						if (!value.has_value() || value->empty())
						{
							continue;
						}

						if (!joined.empty())
						{
							joined += ",";
						}
						joined += value.value();
					}

					if (!joined.empty())
					{
						args.push_back("--attach");
						args.push_back(joined);
					}
				}
				else
				{
					appendIfPresent("attach", "attach");
				}
			}

			const bool hasTo = params.contains("to");
			const bool hasSubject =
				params.contains("subject") || params.contains("subjectFile");
			if (!hasTo || !hasSubject)
			{
				errorCode = "invalid_arguments";
				errorMessage = "smtp.send requires to and subject or subjectFile";
				return std::nullopt;
			}
		}

		return args;
	}

	std::optional<std::vector<std::string>> BuildBaiduSearchCliArgs(
		const BaiduSearchToolRuntimeSpec& spec,
		const nlohmann::json& params,
		std::string& errorCode,
		std::string& errorMessage)
	{
		errorCode.clear();
		errorMessage.clear();

		if (spec.id != "baidu-search.search.web")
		{
			errorCode = "unsupported_tool";
			errorMessage = "unsupported baidu tool";
			return std::nullopt;
		}

		nlohmann::json cliPayload = nlohmann::json::object();
		const auto queryIt = params.find("query");
		if (queryIt == params.end() || !queryIt->is_string())
		{
			errorCode = "invalid_arguments";
			errorMessage = "query is required";
			return std::nullopt;
		}

		const std::string query = TrimAsciiForBraveSearch(queryIt->get<std::string>());
		if (query.empty() || query.size() > 512 || HasControlCharsForBraveSearch(query))
		{
			errorCode = "invalid_arguments";
			errorMessage = "query failed safety validation";
			return std::nullopt;
		}

		cliPayload["query"] = query;

		if (const auto countIt = params.find("count"); countIt != params.end())
		{
			if (!countIt->is_number_integer())
			{
				errorCode = "invalid_arguments";
				errorMessage = "count must be an integer";
				return std::nullopt;
			}

			const int count = countIt->get<int>();
			if (count < 1 || count > 50)
			{
				errorCode = "invalid_arguments";
				errorMessage = "count must be between 1 and 50";
				return std::nullopt;
			}

			cliPayload["count"] = count;
		}

		if (const auto freshnessIt = params.find("freshness");
			freshnessIt != params.end())
		{
			if (!freshnessIt->is_string())
			{
				errorCode = "invalid_arguments";
				errorMessage = "freshness must be a string";
				return std::nullopt;
			}

			const std::string freshness =
				TrimAsciiForBraveSearch(freshnessIt->get<std::string>());
			if (freshness.empty())
			{
				errorCode = "invalid_arguments";
				errorMessage = "freshness must be non-empty";
				return std::nullopt;
			}

			if (freshness != "pd" && freshness != "pw" && freshness != "pm" &&
				freshness != "py" && !IsDateRangeTokenForBaiduSearch(freshness))
			{
				errorCode = "invalid_arguments";
				errorMessage =
					"freshness must be pd/pw/pm/py or YYYY-MM-DDtoYYYY-MM-DD";
				return std::nullopt;
			}

			cliPayload["freshness"] = freshness;
		}

		return std::vector<std::string>{ cliPayload.dump() };
	}

	std::optional<std::vector<std::string>> BuildBraveSearchCliArgs(
		const BraveSearchToolRuntimeSpec& spec,
		const nlohmann::json& params,
		std::string& errorCode,
		std::string& errorMessage)
	{
		errorCode.clear();
		errorMessage.clear();

		std::vector<std::string> args;
		if (IsBraveSearchWebToolId(spec.id))
		{
			const auto queryIt = params.find("query");
			if (queryIt == params.end() || !queryIt->is_string())
			{
				errorCode = "invalid_arguments";
				errorMessage = "query is required";
				return std::nullopt;
			}

			const std::string query = TrimAsciiForBraveSearch(
				queryIt->get<std::string>());
			if (query.empty())
			{
				errorCode = "invalid_arguments";
				errorMessage = "query is required";
				return std::nullopt;
			}

			if (query.size() > 512 || HasControlCharsForBraveSearch(query))
			{
				errorCode = "invalid_arguments";
				errorMessage = "query failed safety validation";
				return std::nullopt;
			}

			args.push_back(query);

			int count = 0;
			if (const auto countIt = params.find("count");
				countIt != params.end() && countIt->is_number_integer())
			{
				count = countIt->get<int>();
			}
			else if (const auto countIt = params.find("count");
				countIt != params.end())
			{
				errorCode = "invalid_arguments";
				errorMessage = "count must be an integer";
				return std::nullopt;
			}
			else if (const auto topKIt = params.find("topK");
				topKIt != params.end() && topKIt->is_number_integer())
			{
				count = topKIt->get<int>();
			}
			else if (const auto topKIt = params.find("topK");
				topKIt != params.end())
			{
				errorCode = "invalid_arguments";
				errorMessage = "topK must be an integer";
				return std::nullopt;
			}

			if (count > 0)
			{
				if (count < 1 || count > 20)
				{
					errorCode = "invalid_arguments";
					errorMessage = "count must be between 1 and 20";
					return std::nullopt;
				}

				args.push_back("-n");
				args.push_back(std::to_string(count));
			}

			if (const auto contentIt = params.find("content");
				contentIt != params.end() && contentIt->is_boolean() &&
				contentIt->get<bool>())
			{
				args.push_back("--content");
			}
			else if (const auto contentIt = params.find("content");
				contentIt != params.end() && !contentIt->is_boolean())
			{
				errorCode = "invalid_arguments";
				errorMessage = "content must be a boolean";
				return std::nullopt;
			}
		}
		else if (IsBraveFetchContentToolId(spec.id))
		{
			const auto urlIt = params.find("url");
			if (urlIt == params.end() || !urlIt->is_string())
			{
				errorCode = "invalid_arguments";
				errorMessage = "url is required";
				return std::nullopt;
			}

			const std::string url = TrimAsciiForBraveSearch(urlIt->get<std::string>());
			if (url.empty())
			{
				errorCode = "invalid_arguments";
				errorMessage = "url is required";
				return std::nullopt;
			}

			if (url.size() > 2048 ||
				HasControlCharsForBraveSearch(url) ||
				!IsHttpUrlForBraveSearch(url))
			{
				errorCode = "invalid_arguments";
				errorMessage = "url failed safety validation";
				return std::nullopt;
			}

			args.push_back(url);
		}

		return args;
	}

	std::optional<std::vector<std::string>> BuildNanoPdfCliArgs(
		const NanoPdfToolRuntimeSpec& spec,
		const nlohmann::json& params,
		std::string& errorCode,
		std::string& errorMessage)
	{
		errorCode.clear();
		errorMessage.clear();

		if (spec.id != "nano_pdf.edit" && spec.id != "nano_pdf.generate")
		{
			errorCode = "unsupported_tool";
			errorMessage = "unsupported nano-pdf tool";
			return std::nullopt;
		}

		nlohmann::json cliPayload = nlohmann::json::object();
		cliPayload["toolId"] = spec.id;

		if (spec.id == "nano_pdf.generate")
		{
			const auto contentIt = params.find("content");
			if (contentIt == params.end() || !contentIt->is_string())
			{
				errorCode = "invalid_args";
				errorMessage = "content is required";
				return std::nullopt;
			}

			const std::string content = TrimAsciiForBraveSearch(contentIt->get<std::string>());
			if (content.empty() || HasControlCharsForBraveSearch(content) || content.size() > 30000)
			{
				errorCode = "invalid_args";
				errorMessage = "content failed safety validation";
				return std::nullopt;
			}
			cliPayload["content"] = content;

			const auto outputPathIt = params.find("outputPath");
			if (outputPathIt == params.end() || !outputPathIt->is_string())
			{
				errorCode = "invalid_args";
				errorMessage = "outputPath is required";
				return std::nullopt;
			}

			const std::string outputPath = TrimAsciiForBraveSearch(outputPathIt->get<std::string>());
			if (outputPath.empty() || HasControlCharsForBraveSearch(outputPath) ||
				outputPath.size() > 4096)
			{
				errorCode = "invalid_args";
				errorMessage = "outputPath failed safety validation";
				return std::nullopt;
			}
			cliPayload["outputPath"] = outputPath;

			if (const auto titleIt = params.find("title");
				titleIt != params.end())
			{
				if (!titleIt->is_string())
				{
					errorCode = "invalid_args";
					errorMessage = "title must be a string";
					return std::nullopt;
				}

				const std::string title = TrimAsciiForBraveSearch(titleIt->get<std::string>());
				if (title.empty() || HasControlCharsForBraveSearch(title) || title.size() > 200)
				{
					errorCode = "invalid_args";
					errorMessage = "title failed safety validation";
					return std::nullopt;
				}
				cliPayload["title"] = title;
			}

			return std::vector<std::string>{ cliPayload.dump() };
		}

		const auto inputPathIt = params.find("inputPath");
		if (inputPathIt == params.end() || !inputPathIt->is_string())
		{
			errorCode = "invalid_args";
			errorMessage =
				"inputPath is required; nano_pdf.edit only edits an existing PDF. "
				"Create a draft PDF first and pass it via inputPath";
			return std::nullopt;
		}

		const std::string inputPath = TrimAsciiForBraveSearch(inputPathIt->get<std::string>());
		if (inputPath.empty() || HasControlCharsForBraveSearch(inputPath) || inputPath.size() > 4096)
		{
			errorCode = "invalid_args";
			errorMessage = "inputPath failed safety validation";
			return std::nullopt;
		}

		const std::string loweredInputPath = ToLowerAscii(inputPath);
		if (loweredInputPath.size() < 4 ||
			loweredInputPath.rfind(".pdf") != loweredInputPath.size() - 4)
		{
			errorCode = "invalid_args";
			errorMessage = "inputPath must target a .pdf file";
			return std::nullopt;
		}

		cliPayload["inputPath"] = inputPath;

		const auto pageIndexIt = params.find("pageIndex");
		if (pageIndexIt == params.end() || !pageIndexIt->is_number_integer())
		{
			errorCode = "invalid_args";
			errorMessage = "pageIndex must be an integer";
			return std::nullopt;
		}

		const auto pageIndex = pageIndexIt->get<long long>();
		if (pageIndex < 0 || pageIndex > 100000)
		{
			errorCode = "invalid_args";
			errorMessage = "pageIndex must be between 0 and 100000";
			return std::nullopt;
		}
		cliPayload["pageIndex"] = pageIndex;

		const auto instructionIt = params.find("instruction");
		if (instructionIt == params.end() || !instructionIt->is_string())
		{
			errorCode = "invalid_args";
			errorMessage = "instruction is required";
			return std::nullopt;
		}

		const std::string instruction = TrimAsciiForBraveSearch(
			instructionIt->get<std::string>());
		if (instruction.empty() || HasControlCharsForBraveSearch(instruction) ||
			instruction.size() > 4000)
		{
			errorCode = "invalid_args";
			errorMessage = "instruction failed safety validation";
			return std::nullopt;
		}
		cliPayload["instruction"] = instruction;

		if (const auto outputPathIt = params.find("outputPath");
			outputPathIt != params.end())
		{
			if (!outputPathIt->is_string())
			{
				errorCode = "invalid_args";
				errorMessage = "outputPath must be a string";
				return std::nullopt;
			}

			const std::string outputPath = TrimAsciiForBraveSearch(
				outputPathIt->get<std::string>());
			if (outputPath.empty() || HasControlCharsForBraveSearch(outputPath) ||
				outputPath.size() > 4096)
			{
				errorCode = "invalid_args";
				errorMessage = "outputPath failed safety validation";
				return std::nullopt;
			}
			cliPayload["outputPath"] = outputPath;
		}

		std::error_code fsError;
		const std::filesystem::path inputFsPath(inputPath);
		const bool exists = std::filesystem::exists(inputFsPath, fsError);
		const bool isFile = exists && std::filesystem::is_regular_file(inputFsPath, fsError);
		if (!exists || !isFile)
		{
			errorCode = "invalid_args";
			errorMessage =
				"missing_input_artifact: inputPath does not exist. "
				"nano_pdf.edit requires an existing PDF file";
			return std::nullopt;
		}

		return std::vector<std::string>{ cliPayload.dump() };
	}

} // namespace blazeclaw::core::tools
