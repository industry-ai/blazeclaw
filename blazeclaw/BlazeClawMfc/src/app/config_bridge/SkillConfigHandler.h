#pragma once

#include "ConfigBridgeContext.h"
#include <string>

namespace blazeclaw
{
namespace config_bridge
{

/// <summary>
/// Dedicated orchestrator for skill configuration bridge operations.
/// Handles skill config load/save/validate/cancel channels from WebView.
/// Preserves dual config UX: uses config.html when present, schema-based fallback otherwise.
/// </summary>
class SkillConfigHandler
{
public:
	/// <summary>
	/// Handle skill config bridge message.
	/// Returns true if the message was recognized and handled, false otherwise.
	/// </summary>
	static bool HandleMessage(
		const std::string& messageJson,
		const ConfigBridgeContext& ctx);

private:
	static void LoadSkillConfigToBridge(
		const std::string& skillKey,
		const std::string& correlationId,
		const ConfigBridgeContext& ctx);

	static void PersistSkillConfigFromPayload(
		const std::string& skillKey,
		const std::string& correlationId,
		const std::string& payloadJson,
		const ConfigBridgeContext& ctx);
};

} // namespace config_bridge
} // namespace blazeclaw
