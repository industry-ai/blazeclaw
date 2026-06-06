#pragma once

#include "ConfigBridgeContext.h"
#include <string>

namespace blazeclaw
{
namespace config_bridge
{

/// <summary>
/// Dedicated orchestrator for email skill configuration bridge operations.
/// Handles email config open/load/save/cancel channels from WebView.
/// Preserves dual config UX: uses config.html when present, schema-based fallback otherwise.
/// </summary>
class EmailConfigHandler
{
public:
	/// <summary>
	/// Handle email config bridge message.
	/// Returns true if the message was recognized and handled, false otherwise.
	/// </summary>
	static bool HandleMessage(
		const std::string& messageJson,
		const ConfigBridgeContext& ctx);

private:
	static void LoadEmailConfigToBridge(const ConfigBridgeContext& ctx);

	static void PersistEmailConfigFromPayload(
		const std::string& payloadJson,
		const ConfigBridgeContext& ctx);
};

} // namespace config_bridge
} // namespace blazeclaw
