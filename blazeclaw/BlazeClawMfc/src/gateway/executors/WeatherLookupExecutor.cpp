#include "pch.h"
#include "WeatherLookupExecutor.h"

#include "../GatewayJsonUtils.h"

#include <algorithm>
#include <cmath>
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

		bool IsTransientProviderFailure(const std::string& error) {
			std::string lowered = error;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			if (lowered.rfind("http_status_5", 0) == 0) {
				return true;
			}

			return lowered.find("curl_perform_failed") != std::string::npos ||
				lowered.find("empty_weather_response") != std::string::npos;
		}

		bool IsRecoverableProviderPayloadFailure(const std::string& error) {
			return error == "weather_payload_parse_failed" ||
				error == "weather_payload_missing_current_condition" ||
				error == "weather_payload_missing_temperature" ||
				error == "weather_payload_missing_humidity";
		}

		WeatherSnapshot BuildFallbackSnapshot(const std::string& requestedDate) {
			WeatherSnapshot snapshot;
			snapshot.date = requestedDate.empty() ? "tomorrow" : requestedDate;
			snapshot.condition = "Provider unavailable (fallback estimate)";
			snapshot.temperatureC = 22;
			snapshot.wind = "Unknown";
			snapshot.humidityPct = 65;
			return snapshot;
		}

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

		bool FetchJsonByUrl(
			const std::string& url,
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

			const std::string trimmed = json::Trim(outResponse);
			if (trimmed.empty()) {
				outError = "empty_weather_response";
				return false;
			}

			return true;
		}

		bool FetchWttrJson(
			const std::string& city,
			std::string& outResponse,
			std::string& outError) {
            if (!EnsureCurlInitialized()) {
				outError = "curl_global_init_failed";
				outResponse.clear();
				return false;
			}

			CURL* curl = curl_easy_init();
			if (curl == nullptr) {
				outError = "curl_easy_init_failed";
				outResponse.clear();
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
			curl_easy_cleanup(curl);

			return FetchJsonByUrl(url, outResponse, outError);
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

		bool TryReadDouble(const nlohmann::json& value, double& out) {
			if (value.is_number()) {
				out = value.get<double>();
				return true;
			}

			if (!value.is_string()) {
				return false;
			}

			try {
				out = std::stod(value.get<std::string>());
				return true;
			}
			catch (...) {
			}

			return false;
		}

		std::string WindDirectionFromDegrees(const double degrees) {
			static const char* kDirections[16] = {
				"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
				"S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
			};

			double normalized = std::fmod(degrees, 360.0);
			if (normalized < 0.0) {
				normalized += 360.0;
			}

			const int index = static_cast<int>(std::round(normalized / 22.5)) % 16;
			return kDirections[index];
		}

		std::string WmoCodeToCondition(const int code) {
			switch (code) {
			case 0: return "Clear";
			case 1: return "Mainly clear";
			case 2: return "Partly cloudy";
			case 3: return "Overcast";
			case 45:
			case 48: return "Fog";
			case 51:
			case 53:
			case 55: return "Drizzle";
			case 61:
			case 63:
			case 65: return "Rain";
			case 71:
			case 73:
			case 75: return "Snow";
			case 80:
			case 81:
			case 82: return "Rain showers";
			case 95:
			case 96:
			case 99: return "Thunderstorm";
			default: return "Unknown";
			}
		}

		bool ParseOpenMeteoJson(
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
				outError = "openmeteo_payload_parse_failed";
				return false;
			}

			if (!payload.is_object() || !payload.contains("daily") || !payload["daily"].is_object()) {
				outError = "openmeteo_payload_missing_daily";
				return false;
			}

			const auto& daily = payload["daily"];
			if (!daily.contains("time") || !daily["time"].is_array() || daily["time"].empty()) {
				outError = "openmeteo_payload_missing_dates";
				return false;
			}

			std::size_t index = 0;
			if (ToLowerCopy(requestedDate) == "tomorrow" && daily["time"].size() > 1) {
				index = 1;
			}

			if (!daily["time"][index].is_string()) {
				outError = "openmeteo_payload_invalid_date";
				return false;
			}

			outSnapshot.date = daily["time"][index].get<std::string>();

			int weatherCode = 0;
			if (daily.contains("weather_code") &&
				daily["weather_code"].is_array() &&
				index < daily["weather_code"].size()) {
				TryReadInt(daily["weather_code"][index], weatherCode);
			}
			outSnapshot.condition = WmoCodeToCondition(weatherCode);

			double maxTemp = 0.0;
			double minTemp = 0.0;
			if (!daily.contains("temperature_2m_max") ||
				!daily["temperature_2m_max"].is_array() ||
				index >= daily["temperature_2m_max"].size() ||
				!TryReadDouble(daily["temperature_2m_max"][index], maxTemp)) {
				outError = "openmeteo_payload_missing_temperature_max";
				return false;
			}
			if (!daily.contains("temperature_2m_min") ||
				!daily["temperature_2m_min"].is_array() ||
				index >= daily["temperature_2m_min"].size() ||
				!TryReadDouble(daily["temperature_2m_min"][index], minTemp)) {
				outError = "openmeteo_payload_missing_temperature_min";
				return false;
			}
			outSnapshot.temperatureC = static_cast<int>(std::round((maxTemp + minTemp) / 2.0));

			double maxHumidity = 0.0;
			double minHumidity = 0.0;
			if (daily.contains("relative_humidity_2m_max") &&
				daily["relative_humidity_2m_max"].is_array() &&
				index < daily["relative_humidity_2m_max"].size() &&
				TryReadDouble(daily["relative_humidity_2m_max"][index], maxHumidity) &&
				daily.contains("relative_humidity_2m_min") &&
				daily["relative_humidity_2m_min"].is_array() &&
				index < daily["relative_humidity_2m_min"].size() &&
				TryReadDouble(daily["relative_humidity_2m_min"][index], minHumidity)) {
				outSnapshot.humidityPct = static_cast<int>(std::round((maxHumidity + minHumidity) / 2.0));
			}
			else if (payload.contains("current") && payload["current"].is_object() &&
				payload["current"].contains("relative_humidity_2m") &&
				TryReadDouble(payload["current"]["relative_humidity_2m"], maxHumidity)) {
				outSnapshot.humidityPct = static_cast<int>(std::round(maxHumidity));
			}
			else {
				outSnapshot.humidityPct = 60;
			}

			double windSpeed = 0.0;
			double windDirection = 0.0;
			const bool hasDailyWind =
				daily.contains("wind_speed_10m_max") &&
				daily["wind_speed_10m_max"].is_array() &&
				index < daily["wind_speed_10m_max"].size() &&
				TryReadDouble(daily["wind_speed_10m_max"][index], windSpeed) &&
				daily.contains("wind_direction_10m_dominant") &&
				daily["wind_direction_10m_dominant"].is_array() &&
				index < daily["wind_direction_10m_dominant"].size() &&
				TryReadDouble(daily["wind_direction_10m_dominant"][index], windDirection);

			if (!hasDailyWind && payload.contains("current") && payload["current"].is_object()) {
				const auto& current = payload["current"];
				if (current.contains("wind_speed_10m")) {
					TryReadDouble(current["wind_speed_10m"], windSpeed);
				}
				if (current.contains("wind_direction_10m")) {
					TryReadDouble(current["wind_direction_10m"], windDirection);
				}
			}

			outSnapshot.wind = WindDirectionFromDegrees(windDirection) +
				" " + std::to_string(static_cast<int>(std::round(windSpeed))) + " km/h";

			return true;
		}

		bool FetchOpenMeteoSnapshot(
			const std::string& city,
			const std::string& requestedDate,
			WeatherSnapshot& outSnapshot,
			std::string& outError) {
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
			curl_easy_cleanup(curl);

			std::string geoResponse;
			if (!FetchJsonByUrl(
				"https://geocoding-api.open-meteo.com/v1/search?name=" + encodedCity + "&count=1&language=en&format=json",
				geoResponse,
				outError)) {
				return false;
			}

			nlohmann::json geoPayload;
			try {
				geoPayload = nlohmann::json::parse(geoResponse);
			}
			catch (...) {
				outError = "openmeteo_geocode_parse_failed";
				return false;
			}

			if (!geoPayload.contains("results") ||
				!geoPayload["results"].is_array() ||
				geoPayload["results"].empty() ||
				!geoPayload["results"][0].is_object()) {
				outError = "openmeteo_geocode_missing_results";
				return false;
			}

			const auto& location = geoPayload["results"][0];
			double latitude = 0.0;
			double longitude = 0.0;
			if (!location.contains("latitude") || !TryReadDouble(location["latitude"], latitude) ||
				!location.contains("longitude") || !TryReadDouble(location["longitude"], longitude)) {
				outError = "openmeteo_geocode_missing_coordinates";
				return false;
			}

			std::ostringstream forecastUrl;
			forecastUrl.setf(std::ios::fixed);
			forecastUrl.precision(4);
			forecastUrl
				<< "https://api.open-meteo.com/v1/forecast?latitude=" << latitude
				<< "&longitude=" << longitude
				<< "&daily=weather_code,temperature_2m_max,temperature_2m_min,relative_humidity_2m_max,relative_humidity_2m_min,wind_speed_10m_max,wind_direction_10m_dominant"
				<< "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,wind_direction_10m,weather_code"
				<< "&timezone=auto";

			std::string forecastResponse;
			if (!FetchJsonByUrl(forecastUrl.str(), forecastResponse, outError)) {
				return false;
			}

			return ParseOpenMeteoJson(
				forecastResponse,
				requestedDate,
				outSnapshot,
				outError);
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

		const nlohmann::json* ResolveForecastDayNode(
			const nlohmann::json& payload,
			const std::string& requestedDate) {
			if (!payload.contains("weather") || !payload["weather"].is_array()) {
				return nullptr;
			}

			const auto& weather = payload["weather"];
			if (weather.empty()) {
				return nullptr;
			}

			const std::string normalizedDate = ToLowerCopy(requestedDate);
			std::size_t dayIndex = 0;
			if (normalizedDate == "tomorrow" && weather.size() > 1) {
				dayIndex = 1;
			}

			if (dayIndex < weather.size() && weather[dayIndex].is_object()) {
				return &weather[dayIndex];
			}

			for (const auto& dayNode : weather) {
				if (!dayNode.is_object()) {
					continue;
				}

				if (!requestedDate.empty() && dayNode.contains("date") &&
					dayNode["date"].is_string() &&
					dayNode["date"].get<std::string>() == requestedDate) {
					return &dayNode;
				}
			}

			return nullptr;
		}

		const nlohmann::json* ResolveObservationNode(
			const nlohmann::json& payload,
			const nlohmann::json* forecastDayNode) {
			if (payload.contains("current_condition") &&
				payload["current_condition"].is_array() &&
				!payload["current_condition"].empty() &&
				payload["current_condition"][0].is_object()) {
				return &payload["current_condition"][0];
			}

			if (forecastDayNode != nullptr &&
				forecastDayNode->contains("hourly") &&
				(*forecastDayNode)["hourly"].is_array() &&
				!(*forecastDayNode)["hourly"].empty() &&
				(*forecastDayNode)["hourly"][0].is_object()) {
				return &(*forecastDayNode)["hourly"][0];
			}

			return nullptr;
		}

		bool TryReadForecastTemperature(
			const nlohmann::json& observation,
			const nlohmann::json* forecastDayNode,
			int& outTemperatureC) {
			if (observation.contains("temp_C") &&
				TryReadInt(observation["temp_C"], outTemperatureC)) {
				return true;
			}

			if (observation.contains("tempC") &&
				TryReadInt(observation["tempC"], outTemperatureC)) {
				return true;
			}

			if (forecastDayNode == nullptr) {
				return false;
			}

			if (forecastDayNode->contains("avgtempC") &&
				TryReadInt((*forecastDayNode)["avgtempC"], outTemperatureC)) {
				return true;
			}

			int maxTemp = 0;
			int minTemp = 0;
			if (forecastDayNode->contains("maxtempC") &&
				forecastDayNode->contains("mintempC") &&
				TryReadInt((*forecastDayNode)["maxtempC"], maxTemp) &&
				TryReadInt((*forecastDayNode)["mintempC"], minTemp)) {
				outTemperatureC = (maxTemp + minTemp) / 2;
				return true;
			}

			return false;
		}

		bool TryReadForecastHumidity(
			const nlohmann::json& observation,
			const nlohmann::json* forecastDayNode,
			int& outHumidityPct) {
			if (observation.contains("humidity") &&
				TryReadInt(observation["humidity"], outHumidityPct)) {
				return true;
			}

			if (forecastDayNode != nullptr &&
				forecastDayNode->contains("avghumidity") &&
				TryReadInt((*forecastDayNode)["avghumidity"], outHumidityPct)) {
				return true;
			}

			return false;
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

           const nlohmann::json* forecastDayNode =
				ResolveForecastDayNode(payload, requestedDate);
			const nlohmann::json* observationNode =
				ResolveObservationNode(payload, forecastDayNode);

			if (observationNode == nullptr) {
				outError = "weather_payload_missing_current_condition";
				return false;
			}

          const auto& current = *observationNode;
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

          if (!TryReadForecastTemperature(current, forecastDayNode, outSnapshot.temperatureC)) {
				outError = "weather_payload_missing_temperature";
				return false;
			}

           if (!TryReadForecastHumidity(current, forecastDayNode, outSnapshot.humidityPct)) {
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
			const int humidityPct,
			const bool fallback = false,
			const std::string& providerError = {}) {
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
				"},\"fallback\":" +
				std::string(fallback ? "true" : "false") +
				",\"providerError\":\"" +
				EscapeJson(providerError) +
				"\"}";
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
				if (IsTransientProviderFailure(fetchError)) {
                    WeatherSnapshot openMeteoSnapshot;
					std::string openMeteoError;
					if (FetchOpenMeteoSnapshot(
						normalizedCity,
						normalizedDate,
						openMeteoSnapshot,
						openMeteoError)) {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = BuildSuccessEnvelope(
								normalizedCity,
								openMeteoSnapshot.date,
								openMeteoSnapshot.condition,
								openMeteoSnapshot.temperatureC,
								openMeteoSnapshot.wind,
								openMeteoSnapshot.humidityPct,
								false,
								fetchError),
						};
					}

					const auto fallback = BuildFallbackSnapshot(normalizedDate);
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = BuildSuccessEnvelope(
							normalizedCity,
							fallback.date,
							fallback.condition,
							fallback.temperatureC,
							fallback.wind,
						  fallback.humidityPct,
							true,
							fetchError),
					};
				}

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
               if (IsRecoverableProviderPayloadFailure(parseError)) {
                    WeatherSnapshot openMeteoSnapshot;
					std::string openMeteoError;
					if (FetchOpenMeteoSnapshot(
						normalizedCity,
						normalizedDate,
						openMeteoSnapshot,
						openMeteoError)) {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = true,
							.status = "ok",
							.output = BuildSuccessEnvelope(
								normalizedCity,
								openMeteoSnapshot.date,
								openMeteoSnapshot.condition,
								openMeteoSnapshot.temperatureC,
								openMeteoSnapshot.wind,
								openMeteoSnapshot.humidityPct,
								false,
								parseError),
						};
					}

					const auto fallback = BuildFallbackSnapshot(normalizedDate);
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = BuildSuccessEnvelope(
							normalizedCity,
							fallback.date,
							fallback.condition,
							fallback.temperatureC,
							fallback.wind,
							fallback.humidityPct,
							true,
							parseError),
					};
				}

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
				  snapshot.humidityPct,
					false,
					""),
			};
			};
	}

} // namespace blazeclaw::gateway::executors
