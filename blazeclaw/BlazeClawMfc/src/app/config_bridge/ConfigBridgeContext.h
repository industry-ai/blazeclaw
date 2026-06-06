#pragma once

#include <functional>
#include <string>

// Forward declaration in global namespace
class CBlazeClawMFCDoc;

namespace blazeclaw
{
namespace config_bridge
{

/// <summary>
/// Shared dependency carrier for skill/email config bridge orchestration handlers.
/// Provides access to diagnostics, bridge posting, and document persistence operations.
/// </summary>
struct ConfigBridgeContext
{
	/// <summary>
	/// Post JSON message to WebView bridge.
	/// </summary>
	std::function<void(const std::wstring&)> postBridgeMessageJson;

	/// <summary>
	/// Append status line to chat procedure diagnostics.
	/// </summary>
	std::function<void(const std::wstring&, const std::string&)> appendChatProcedureStatusLine;

	/// <summary>
	/// Get document context for config load/save operations.
	/// Returns nullptr if document is unavailable.
	/// </summary>
	std::function<::CBlazeClawMFCDoc*()> getDocument;

	/// <summary>
	/// Refresh gateway skills after config save.
	/// Returns error message on failure, empty string on success.
	/// </summary>
	std::function<std::string()> refreshGatewaySkills;

	/// <summary>
	/// Refresh skill view UI after config save.
	/// </summary>
	std::function<void()> refreshSkillView;
};

} // namespace config_bridge
} // namespace blazeclaw
