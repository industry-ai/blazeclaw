#include "pch.h"
#include "WeatherLookupExecutor.h"

#include "../GatewayJsonUtils.h"

#include <algorithm>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway::executors {
namespace {

struct WeatherSnapshot {
    std::string date;
    std::string condition;
    int temperatureC = 0;
    std::string wind;
    int humidityPct = 0;
};

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

std::string ToLowerCopy(const std::string& value) {
    std::string lowered = value;
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return lowered;
}

bool EnsureCurlInitialized() {
    static const bool initialized =
        curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return initialized;
}

size_t CurlWriteCallback(
    char* ptr,
    size_t size,
    size_t nmemb,
    void* userdata) {
    if (ptr == nullptr || userdata == nullptr) {
        return 0;
    }

    const size_t total = size * nmemb;
    auto* response = static_cast<std::string*>(userdata);
    response->append(ptr, total);
    return total;
}

bool FetchWttrJson(
    const std::string& city,
    std::string& outResponse,
    std::string& outError) {
    outResponse.clear();
    outError.clear();

    if (!EnsureCurlInitialized()) {
        outError = "curl_global_init_failed";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        outError = "curl_easy_init_failed";
        return false;
    }

    char* escapedCity = curl_easy_escape(
        curl,
        city.c_str(),
        static_cast<int>(city.size()));
    const std::string encodedCity = escapedCity == nullptr
        ? city
        : std::string(escapedCity);
    if (escapedCity != nullptr) {
        curl_free(escapedCity);
    }

    const std::string url =
        "https://wttr.in/" + encodedCity + "?format=j1";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "blazeclaw-weather-skill/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outResponse);

    const CURLcode code = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        outError = std::string("curl_perform_failed:") + curl_easy_strerror(code);
        return false;
    }

    if (statusCode >= 400) {
        outError = "http_status_" + std::to_string(statusCode);
        return false;
    }

    if (json::Trim(outResponse).empty()) {
        outError = "empty_weather_response";
        return false;
    }

    return true;
}

bool TryReadInt(const nlohmann::json& value, int& out) {
    if (value.is_number_integer()) {
        out = value.get<int>();
        return true;
    }

    if (!value.is_string()) {
        return false;
    }

    try {
        out = std::stoi(value.get<std::string>());
        return true;
    }
    catch (...) {
    }

    return false;
}

std::string ResolveForecastDate(
    const nlohmann::json& payload,
    const std::string& requestedDate) {
    const auto normalizedDate = ToLowerCopy(requestedDate);
    if (!payload.contains("weather") || !payload["weather"].is_array()) {
        return requestedDate;
    }

    const auto& weather = payload["weather"];
    if (normalizedDate == "today" && !weather.empty() &&
        weather[0].contains("date") && weather[0]["date"].is_string()) {
        return weather[0]["date"].get<std::string>();
    }

    if (normalizedDate == "tomorrow" && weather.size() > 1 &&
        weather[1].contains("date") && weather[1]["date"].is_string()) {
        return weather[1]["date"].get<std::string>();
    }

    if (requestedDate.empty()) {
        return "today";
    }

    return requestedDate;
}

bool ParseWttrJson(
    const std::string& response,
    const std::string& requestedDate,
    WeatherSnapshot& outSnapshot,
    std::string& outError) {
    outError.clear();
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(response);
    }
    catch (...) {
        outError = "weather_payload_parse_failed";
        return false;
    }

    if (!payload.contains("current_condition") ||
        !payload["current_condition"].is_array() ||
        payload["current_condition"].empty() ||
        !payload["current_condition"][0].is_object()) {
        outError = "weather_payload_missing_current_condition";
        return false;
    }

    const auto& current = payload["current_condition"][0];
    outSnapshot.date = ResolveForecastDate(payload, requestedDate);

    outSnapshot.condition = "Unknown";
    if (current.contains("weatherDesc") &&
        current["weatherDesc"].is_array() &&
        !current["weatherDesc"].empty() &&
        current["weatherDesc"][0].is_object() &&
        current["weatherDesc"][0].contains("value") &&
        current["weatherDesc"][0]["value"].is_string()) {
        outSnapshot.condition =
            current["weatherDesc"][0]["value"].get<std::string>();
    }

    if (!current.contains("temp_C") || !TryReadInt(current["temp_C"], outSnapshot.temperatureC)) {
        outError = "weather_payload_missing_temperature";
        return false;
    }

    if (!current.contains("humidity") || !TryReadInt(current["humidity"], outSnapshot.humidityPct)) {
        outError = "weather_payload_missing_humidity";
        return false;
    }

    std::string windSpeed = "";
    if (current.contains("windspeedKmph") && current["windspeedKmph"].is_string()) {
        windSpeed = current["windspeedKmph"].get<std::string>();
    }

    std::string windDirection = "";
    if (current.contains("winddir16Point") && current["winddir16Point"].is_string()) {
        windDirection = current["winddir16Point"].get<std::string>();
    }

    const std::string windHead = json::Trim(windDirection);
    const std::string windTail = json::Trim(windSpeed);
    if (!windHead.empty() && !windTail.empty()) {
        outSnapshot.wind = windHead + " " + windTail + " km/h";
    }
    else if (!windTail.empty()) {
        outSnapshot.wind = windTail + " km/h";
    }
    else {
        outSnapshot.wind = "Unknown";
    }

    return true;
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

        std::string response;
        std::string fetchError;
        if (!FetchWttrJson(normalizedCity, response, fetchError)) {
            return ToolExecuteResult{
                .tool = requestedTool,
                .executed = false,
                .status = "error",
                .output = BuildErrorEnvelope(
                    "provider_unavailable",
                    fetchError),
            };
        }

        WeatherSnapshot snapshot;
        std::string parseError;
        if (!ParseWttrJson(response, normalizedDate, snapshot, parseError)) {
            return ToolExecuteResult{
                .tool = requestedTool,
                .executed = false,
                .status = "error",
                .output = BuildErrorEnvelope(
                    "provider_invalid_payload",
                    parseError),
            };
        }

        return ToolExecuteResult{
            .tool = requestedTool,
            .executed = true,
            .status = "ok",
            .output = BuildSuccessEnvelope(
                normalizedCity,
                snapshot.date,
                snapshot.condition,
                snapshot.temperatureC,
                snapshot.wind,
                snapshot.humidityPct),
        };
    };
}

} // namespace blazeclaw::gateway::executors
