#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace blazeclaw::gateway::host_search_helpers {

	std::string ToLowerCopy(std::string value);
	std::string TruncateForMatch(const std::string& text, std::size_t maxChars);
	std::string BuildMemorySearchEnvelope(
		const std::string& sessionKey,
		const std::vector<std::string>& matches);

} // namespace blazeclaw::gateway::host_search_helpers
