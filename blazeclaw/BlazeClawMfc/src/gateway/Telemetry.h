#pragma once

#include <string>
#include <windows.h>

namespace blazeclaw::gateway {
    // Emit a structured telemetry event. payloadJson should be a JSON object string.
    void EmitTelemetryEvent(const std::string& eventName, const std::string& payloadJson);

    // Helper to produce a quoted JSON string with escaping
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

    // Default inline implementation writes telemetry to debug output. Can be replaced by
    // a more advanced telemetry sink by providing a non-inline implementation elsewhere.
    inline void EmitTelemetryEvent(const std::string& eventName, const std::string& payloadJson) {
        const std::string frame = "{\"type\":\"event\",\"event\":\"gateway.telemetry\",\"payload\":{\"event\":" + JsonString(eventName) + ",\"payload\":" + payloadJson + "}}";
        OutputDebugStringA((std::string("[Telemetry] ") + frame + "\n").c_str());
    }
}
