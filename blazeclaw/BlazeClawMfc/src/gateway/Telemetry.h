#pragma once

#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace blazeclaw::gateway {
	inline std::string JsonString(const std::string& value) {
		std::string escaped;
		escaped.reserve(value.size() + 8);
		for (const char ch : value) {
			switch (ch) {
			case '\\':
				escaped += "\\\\";
				break;
			case '"':
				escaped += "\\\"";
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

		return std::string("\"") + escaped + "\"";
	}

	inline std::string NormalizePayloadObject(const std::string& payloadJson) {
		const auto first = payloadJson.find_first_not_of(" \t\r\n");
		const auto last = payloadJson.find_last_not_of(" \t\r\n");
		if (first == std::string::npos || last == std::string::npos) {
			return "{}";
		}

		if (payloadJson[first] == '{' && payloadJson[last] == '}') {
			return payloadJson.substr(first, last - first + 1);
		}

		return std::string("{\"raw\":") + JsonString(payloadJson) + "}";
	}

	inline std::string BuildGatewayHostRouteDecisionPayload(
		const std::string& method,
		const std::string& target,
		const std::string& reason,
		const std::string& cohort,
		const bool fallback) {
		return std::string("{\"method\":") +
			JsonString(method) +
			",\"target\":" +
			JsonString(target) +
			",\"reason\":" +
			JsonString(reason) +
			",\"cohort\":" +
			JsonString(cohort) +
			",\"fallback\":" +
			std::string(fallback ? "true" : "false") +
			"}";
	}

	// Default telemetry sink writes structured telemetry JSON to debug output.
	inline void EmitTelemetryEvent(const std::string& eventName, const std::string& payloadJson) {
		const std::string normalizedPayload = NormalizePayloadObject(payloadJson);
		const std::string frame =
			"{\"type\":\"event\",\"event\":\"gateway.telemetry\",\"payload\":{\"event\":" +
			JsonString(eventName) +
			",\"payload\":" +
			normalizedPayload +
			"}}";
#if defined(_WIN32)
		OutputDebugStringA((std::string("[Telemetry] ") + frame + "\n").c_str());
#else
		(void)frame;
#endif
	}
}
