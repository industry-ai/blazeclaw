#include "pch.h"
#include "GatewayHostCatalogHelpers.h"

#include "GatewayJsonBuilder.h"

namespace blazeclaw::gateway {

std::string MaskGatewaySecret(const std::string& value) {
	if (value.empty()) {
		return {};
	}

	if (value.size() <= 6) {
		return "***";
	}

	return value.substr(0, 3) + "***" + value.substr(value.size() - 2);
}

const std::vector<std::string>& GatewayEventCatalogNames() {
	static const std::vector<std::string> events = {
		"gateway.agent.update",
		"gateway.channels.accounts.update",
		"gateway.channels.update",
		"gateway.health",
		"gateway.session.reset",
		"gateway.shutdown",
		"gateway.tick",
		"gateway.tools.catalog.update",
	};

	return events;
}

std::string BuildGatewayDeepSeekConfigJson(
	const std::string& apiKey,
	const std::string& baseUrl,
	const std::string& defaultModel) {
	return JsonObject({
		{"configured", JsonBool(!apiKey.empty())},
		{"apiKeyMasked", JsonString(MaskGatewaySecret(apiKey))},
		{"baseUrl", JsonString(baseUrl)},
		{"defaultModel", JsonString(defaultModel)},
	});
}

} // namespace blazeclaw::gateway
