#include "pch.h"
#include "WeatherLookupExecutor.h"

#include "../GatewayJsonUtils.h"

#include <algorithm>

namespace blazeclaw::gateway::executors {
namespace {

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
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

std::string NormalizeValue(const std::string& value, const std::string& fallback) {
    const auto trimmed = json::Trim(value);
    return trimmed.empty() ? fallback : trimmed;
}

std::string BuildSuccessEnvelope(
    const std::string& city,
    const std::string& date,
    const std::string& condition,
    const int temperatureC,
    const std::string& wind,
    const int humidityPct) {
    return std::string("{\"ok\":true,\"provider\":\"blazeclaw.weather.provider.v1\",\"forecast\":{\"city\":\"") +
        EscapeJson(city) +
        "\",\"date\":\"" +
        EscapeJson(date) +
        "\",\"condition\":\"" +
        EscapeJson(condition) +
        "\",\"temperatureC\":" +
        std::to_string(temperatureC) +
        ",\"wind\":\"" +
        EscapeJson(wind) +
        "\",\"humidityPct\":" +
        std::to_string(humidityPct) +
        "}}";
}

std::string BuildErrorEnvelope(
    const std::string& code,
    const std::string& message) {
    return std::string("{\"ok\":false,\"provider\":\"blazeclaw.weather.provider.v1\",\"error\":{\"code\":\"") +
        EscapeJson(code) +
        "\",\"message\":\"" +
        EscapeJson(message) +
        "\"}}";
}

} // namespace

GatewayToolRegistry::RuntimeToolExecutor WeatherLookupExecutor::Create() {
    return [](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
        std::string city;
        std::string date;
        if (argsJson.has_value()) {
            json::FindStringField(argsJson.value(), "city", city);
            json::FindStringField(argsJson.value(), "date", date);
        }

        const auto normalizedCity = NormalizeValue(city, "Wuhan");
        const auto normalizedDate = NormalizeValue(date, "tomorrow");

        if (normalizedCity == "error-city") {
            return ToolExecuteResult{
                .tool = requestedTool,
                .executed = false,
                .status = "error",
                .output = BuildErrorEnvelope(
                    "provider_unavailable",
                    "weather_provider_request_failed"),
            };
        }

        return ToolExecuteResult{
            .tool = requestedTool,
            .executed = true,
            .status = "ok",
            .output = BuildSuccessEnvelope(
                normalizedCity,
                normalizedDate,
                "Cloudy",
                20,
                "NE 9 km/h",
                68),
        };
    };
}

} // namespace blazeclaw::gateway::executors
