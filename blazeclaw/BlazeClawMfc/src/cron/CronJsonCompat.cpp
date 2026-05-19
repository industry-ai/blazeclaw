#include "pch.h"

#include "CronJsonCompat.h"

namespace blazeclaw::cron {

	namespace {

		std::string StripJsonComments(const std::string& raw) {
			std::string out;
			out.reserve(raw.size());
			bool inString = false;
			bool escape = false;
			for (std::size_t index = 0; index < raw.size(); ++index) {
				const char ch = raw[index];
				if (inString) {
					out.push_back(ch);
					if (escape) {
						escape = false;
						continue;
					}
					if (ch == '\\') {
						escape = true;
						continue;
					}
					if (ch == '"') {
						inString = false;
					}
					continue;
				}

				if (ch == '"') {
					inString = true;
					out.push_back(ch);
					continue;
				}

				if (ch == '/' && index + 1 < raw.size()) {
					if (raw[index + 1] == '/') {
						index += 2;
						while (index < raw.size() && raw[index] != '\n' && raw[index] != '\r') {
							++index;
						}
						if (index < raw.size()) {
							out.push_back(raw[index]);
						}
						continue;
					}
					if (raw[index + 1] == '*') {
						index += 2;
						while (index + 1 < raw.size() &&
							!(raw[index] == '*' && raw[index + 1] == '/')) {
							++index;
						}
						index += 1;
						continue;
					}
				}

				out.push_back(ch);
			}
			return out;
		}

		std::string StripTrailingCommas(const std::string& raw) {
			std::string out;
			out.reserve(raw.size());
			bool inString = false;
			bool escape = false;
			for (std::size_t index = 0; index < raw.size(); ++index) {
				const char ch = raw[index];
				if (inString) {
					out.push_back(ch);
					if (escape) {
						escape = false;
						continue;
					}
					if (ch == '\\') {
						escape = true;
						continue;
					}
					if (ch == '"') {
						inString = false;
					}
					continue;
				}

				if (ch == '"') {
					inString = true;
					out.push_back(ch);
					continue;
				}

				if (ch == ',') {
					std::size_t lookahead = index + 1;
					while (lookahead < raw.size() &&
						std::isspace(static_cast<unsigned char>(raw[lookahead])) != 0) {
						++lookahead;
					}
					if (lookahead < raw.size() &&
						(raw[lookahead] == '}' || raw[lookahead] == ']')) {
						continue;
					}
				}

				out.push_back(ch);
			}
			return out;
		}

		std::string StripUtf8Bom(const std::string& raw) {
			if (raw.size() >= 3 &&
				static_cast<unsigned char>(raw[0]) == 0xEF &&
				static_cast<unsigned char>(raw[1]) == 0xBB &&
				static_cast<unsigned char>(raw[2]) == 0xBF) {
				return raw.substr(3);
			}
			return raw;
		}

		std::string NormalizeJson5Like(const std::string& raw) {
			return StripTrailingCommas(StripJsonComments(StripUtf8Bom(raw)));
		}

		CronJson TryParseJson(const std::string& raw) {
			CronJson parsed = CronJson::parse(raw, nullptr, false);
			if (parsed.is_discarded() || parsed.is_null()) {
				return CronJson();
			}
			return parsed;
		}

	} // namespace

	CronJson ParseJsonWithJson5Fallback(const std::string& raw) {
		CronJson parsed = TryParseJson(raw);
		if (IsUsableJsonDocument(parsed)) {
			return parsed;
		}

		const std::string normalized = NormalizeJson5Like(raw);
		parsed = TryParseJson(normalized);
		if (IsUsableJsonDocument(parsed)) {
			return parsed;
		}

		return CronJson();
	}

	bool IsUsableJsonDocument(const CronJson& parsed) {
		return !parsed.is_discarded() && !parsed.is_null() &&
			(parsed.is_object() || parsed.is_array());
	}

	CronJson ParseJsonStreamWithJson5Fallback(std::istream& stream) {
		const std::string raw{
			std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>()
		};
		return ParseJsonWithJson5Fallback(raw);
	}

} // namespace blazeclaw::cron
