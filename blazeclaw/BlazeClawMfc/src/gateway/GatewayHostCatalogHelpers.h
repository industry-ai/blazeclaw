#pragma once

#include <string>
#include <vector>

namespace blazeclaw::gateway {

/// Masks API keys and similar secrets for gateway introspection payloads (stable shape vs `GatewayHost` handlers).
[[nodiscard]] std::string MaskGatewaySecret(const std::string& value);

/// Fixed catalog of gateway event names used by `gateway.events.*` query handlers and snapshots.
[[nodiscard]] const std::vector<std::string>& GatewayEventCatalogNames();

/// JSON object fragment for DeepSeek runtime fields (`configured`, masked key, baseUrl, defaultModel).
[[nodiscard]] std::string BuildGatewayDeepSeekConfigJson(
	const std::string& apiKey,
	const std::string& baseUrl,
	const std::string& defaultModel);

} // namespace blazeclaw::gateway
