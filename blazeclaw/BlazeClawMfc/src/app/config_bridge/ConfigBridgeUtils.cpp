#include "pch.h"
#include "ConfigBridgeUtils.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace blazeclaw
{
namespace config_bridge
{

namespace
{
	bool IsSensitiveConfigKey(const std::string& key)
	{
		const std::string lowered = ConfigBridgeUtils::ToLowerAscii(key);
		return lowered.find("pass") != std::string::npos ||
			lowered.find("secret") != std::string::npos ||
			lowered.find("token") != std::string::npos ||
			lowered.find("apikey") != std::string::npos ||
			lowered.find("api_key") != std::string::npos;
	}
}

std::string ConfigBridgeUtils::EscapeJson(const std::string& value)
{
	std::string escaped;
	escaped.reserve(value.size() + 8);
	for (const char ch : value)
	{
		switch (ch)
		{
		case '"':
			escaped += "\\\"";
			break;
		case '\\':
			escaped += "\\\\";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\r':
			escaped += "\\r";
			break;
		case '\t':
			escaped += "\\t";
			break;
		default:
			escaped.push_back(ch);
			break;
		}
	}
	return escaped;
}

std::string ConfigBridgeUtils::JsonString(const std::string& value)
{
	return std::string("\"") + EscapeJson(value) + "\"";
}

std::string ConfigBridgeUtils::TrimAscii(const std::string& value)
{
	const std::size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
	{
		return {};
	}

	const std::size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::string ConfigBridgeUtils::ToLowerAscii(const std::string& value)
{
	std::string lowered = value;
	std::transform(
		lowered.begin(),
		lowered.end(),
		lowered.begin(),
		[](const unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
	return lowered;
}

std::unordered_map<std::string, std::string> ConfigBridgeUtils::ParseDotEnvPairs(
	const std::string& envContent)
{
	std::unordered_map<std::string, std::string> values;
	std::istringstream stream(envContent);
	std::string line;
	while (std::getline(stream, line))
	{
		std::string trimmed = TrimAscii(line);
		if (trimmed.empty() || trimmed[0] == '#')
		{
			continue;
		}

		if (!trimmed.empty() && trimmed.back() == '\r')
		{
			trimmed.pop_back();
			trimmed = TrimAscii(trimmed);
		}

		const std::size_t eqPos = trimmed.find('=');
		if (eqPos == std::string::npos || eqPos == 0)
		{
			continue;
		}

		std::string key = TrimAscii(trimmed.substr(0, eqPos));
		std::string value = TrimAscii(trimmed.substr(eqPos + 1));
		if (!value.empty() &&
			value.size() >= 2 &&
			((value.front() == '"' && value.back() == '"') ||
				(value.front() == '\'' && value.back() == '\'')))
		{
			value = value.substr(1, value.size() - 2);
		}

		if (!key.empty())
		{
			values.insert_or_assign(key, value);
		}
	}

	return values;
}

bool ConfigBridgeUtils::TryParsePort(const std::string& value, int& parsed)
{
	if (value.empty())
	{
		return false;
	}

	for (const char ch : value)
	{
		if (!std::isdigit(static_cast<unsigned char>(ch)))
		{
			return false;
		}
	}

	try
	{
		parsed = std::stoi(value);
	}
	catch (...)
	{
		return false;
	}

	return parsed > 0 && parsed <= 65535;
}

bool ConfigBridgeUtils::IsLikelyEmailAddress(const std::string& value)
{
	const auto atPos = value.find('@');
	if (atPos == std::string::npos || atPos == 0 || atPos + 1 >= value.size())
	{
		return false;
	}

	return value.find('.', atPos + 1) != std::string::npos;
}

std::wstring ConfigBridgeUtils::ToWide(const std::string& value)
{
	if (value.empty())
	{
		return {};
	}

	const int required = MultiByteToWideChar(
		CP_UTF8,
		0,
		value.data(),
		static_cast<int>(value.size()),
		nullptr,
		0);
	if (required <= 0)
	{
		return {};
	}

	std::wstring wide(required, L'\0');
	MultiByteToWideChar(
		CP_UTF8,
		0,
		value.data(),
		static_cast<int>(value.size()),
		wide.data(),
		required);
	return wide;
}

std::string ConfigBridgeUtils::ToNarrow(const std::wstring& value)
{
	if (value.empty())
	{
		return {};
	}

	const int required = WideCharToMultiByte(
		CP_UTF8,
		0,
		value.data(),
		static_cast<int>(value.size()),
		nullptr,
		0,
		nullptr,
		nullptr);
	if (required <= 0)
	{
		return {};
	}

	std::string narrow(required, '\0');
	WideCharToMultiByte(
		CP_UTF8,
		0,
		value.data(),
		static_cast<int>(value.size()),
		narrow.data(),
		required,
		nullptr,
		nullptr);
	return narrow;
}

std::string ConfigBridgeUtils::TruncateForDiagnostics(const std::string& value, const std::size_t maxLength)
{
	if (value.size() <= maxLength)
	{
		return value;
	}

	return value.substr(0, maxLength) + "...";
}

std::string ConfigBridgeUtils::RedactSensitiveJsonPayload(const std::string& payloadJson)
{
	const auto pairs = ParseDotEnvPairs(payloadJson);
	if (pairs.empty())
	{
		return payloadJson;
	}

	std::string json = "{";
	bool first = true;
	for (const auto& pair : pairs)
	{
		if (!first)
		{
			json += ",";
		}

		json += JsonString(pair.first);
		json += ":";
		json += JsonString(
			IsSensitiveConfigKey(pair.first)
			? std::string("***REDACTED***")
			: pair.second);
		first = false;
	}
	json += "}";
	return json;
}

bool ConfigBridgeUtils::ParseEnvBool(const std::string& value, const bool fallback)
{
	const std::string lowered = ToLowerAscii(TrimAscii(value));
	if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
	{
		return true;
	}

	if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
	{
		return false;
	}

	return fallback;
}

std::string ConfigBridgeUtils::ResolvePreferredServerPreset(const std::string& smtpHost)
{
	const std::string lowered = ToLowerAscii(TrimAscii(smtpHost));
	if (lowered == "smtp.163.com") return "163.com";
	if (lowered == "smtp.vip.163.com") return "vip.163.com";
	if (lowered == "smtp.126.com") return "126.com";
	if (lowered == "smtp.vip.126.com") return "vip.126.com";
	if (lowered == "smtp.188.com") return "188.com";
	if (lowered == "smtp.vip.188.com") return "vip.188.com";
	if (lowered == "smtp.yeah.net") return "yeah.net";
	if (lowered == "smtp.gmail.com") return "gmail.com";
	if (lowered == "smtp.office365.com") return "outlook.com";
	if (lowered == "smtp.qq.com") return "qq.com";
	return {};
}

} // namespace config_bridge
} // namespace blazeclaw
