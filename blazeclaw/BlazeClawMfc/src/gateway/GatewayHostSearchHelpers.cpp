#include "pch.h"
#include "GatewayHostSearchHelpers.h"

#include "GatewayJsonBuilder.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::gateway::host_search_helpers {

	std::string ToLowerCopy(std::string value) {
		std::transform(
			value.begin(),
			value.end(),
			value.begin(),
			[](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
		return value;
	}

	std::string TruncateForMatch(const std::string& text, std::size_t maxChars) {
		if (text.size() <= maxChars) {
			return text;
		}

		return text.substr(0, maxChars) + "...";
	}

	std::string BuildMemorySearchEnvelope(
		const std::string& sessionKey,
		const std::vector<std::string>& matches) {
		std::vector<std::string> rows;
		rows.reserve(matches.size());
		for (const auto& text : matches) {
			rows.push_back(JsonObject({ { "text", JsonString(text) } }));
		}

		return JsonObject({
			{"sessionKey", JsonString(sessionKey)},
			{"matches", JsonArray(rows)},
			{"count", JsonNumber(static_cast<std::uint64_t>(matches.size()))},
		});
	}

} // namespace blazeclaw::gateway::host_search_helpers
