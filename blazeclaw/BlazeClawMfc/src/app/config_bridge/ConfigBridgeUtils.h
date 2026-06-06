#pragma once

#include <string>
#include <unordered_map>

namespace blazeclaw
{
namespace config_bridge
{

/// <summary>
/// Shared utility functions for config bridge handlers.
/// </summary>
class ConfigBridgeUtils
{
public:
	/// <summary>
	/// Escape string for JSON encoding.
	/// </summary>
	static std::string EscapeJson(const std::string& value);

	/// <summary>
	/// Wrap string in JSON quotes with escaping.
	/// </summary>
	static std::string JsonString(const std::string& value);

	/// <summary>
	/// Trim ASCII whitespace from string.
	/// </summary>
	static std::string TrimAscii(const std::string& value);

	/// <summary>
	/// Convert string to lowercase ASCII.
	/// </summary>
	static std::string ToLowerAscii(const std::string& value);

	/// <summary>
	/// Parse .env format content into key-value pairs.
	/// </summary>
	static std::unordered_map<std::string, std::string> ParseDotEnvPairs(
		const std::string& envContent);

	/// <summary>
	/// Try to parse port number from string.
	/// Returns true if valid port (1-65535), false otherwise.
	/// </summary>
	static bool TryParsePort(const std::string& value, int& parsed);

	/// <summary>
	/// Check if string looks like an email address.
	/// </summary>
	static bool IsLikelyEmailAddress(const std::string& value);

	/// <summary>
	/// Convert narrow string to wide string.
	/// </summary>
	static std::wstring ToWide(const std::string& value);

	/// <summary>
	/// Convert wide string to narrow string.
	/// </summary>
	static std::string ToNarrow(const std::wstring& value);

	/// <summary>
	/// Truncate string for diagnostics display.
	/// </summary>
	static std::string TruncateForDiagnostics(const std::string& value, std::size_t maxLength);

	/// <summary>
	/// Redact sensitive fields from JSON payload for diagnostics.
	/// </summary>
	static std::string RedactSensitiveJsonPayload(const std::string& payloadJson);

	/// <summary>
	/// Parse environment variable boolean value with fallback.
	/// </summary>
	static bool ParseEnvBool(const std::string& value, bool fallback);

	/// <summary>
	/// Resolve preferred email server preset from SMTP host.
	/// </summary>
	static std::string ResolvePreferredServerPreset(const std::string& smtpHost);
};

} // namespace config_bridge
} // namespace blazeclaw
