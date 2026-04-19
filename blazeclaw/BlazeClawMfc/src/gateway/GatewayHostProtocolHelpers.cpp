#include "pch.h"
#include "GatewayHostProtocolHelpers.h"

#include <chrono>

namespace blazeclaw::gateway {

std::uint64_t GatewayEpochMilliseconds() {
	const auto now = std::chrono::system_clock::now();
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch())
		.count());
}

bool IsUnsafeGatewayAgentFilePath(const std::string& path) {
	if (path.empty()) {
		return true;
	}

	if (path.find("..") != std::string::npos) {
		return true;
	}

	if (path.find('\\') != std::string::npos ||
		path.find(':') != std::string::npos) {
		return true;
	}

	if (!path.empty() && path.front() == '/') {
		return true;
	}

	return false;
}

} // namespace blazeclaw::gateway
