#include "pch.h"

#include "CronTimerService.h"

#include <chrono>
#include <cmath>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include <winhttp.h>

#pragma comment(lib, "Winhttp.lib")

namespace {
	constexpr std::int64_t kMinuteMs = 60'000;
	constexpr std::int64_t kHourMs = 60 * kMinuteMs;
	constexpr std::int64_t kDayMs = 24 * kHourMs;
}

namespace blazeclaw::cron {

	namespace {
		inline constexpr std::int64_t kMaxScheduleErrors = 3;

		inline constexpr std::int64_t kDefaultRetryDelayMs = 60'000;
		inline constexpr std::int64_t kDefaultHeartbeatBusyMaxAttempts = 2;
		inline constexpr std::int64_t kDefaultHeartbeatBusyDelayMs = 1500;
		inline constexpr std::int64_t kDefaultFailureAlertAfter = 2;
		inline constexpr std::int64_t kDefaultFailureAlertCooldownMs = 60 * 60'000;
			inline constexpr const char* kFailureAlertModeAnnounce = "announce";
			inline constexpr const char* kFailureAlertModeWebhook = "webhook";

		bool StartsWithHttpScheme(const std::string& value) {
			const std::string lowered = ToLowerCopy(TrimCopy(value));
			return lowered.rfind("http://", 0) == 0 || lowered.rfind("https://", 0) == 0;
		}

		std::string CanonicalizeHttpUrlForRouteCompare(const std::string& value) {
			const std::string trimmed = TrimCopy(value);
			if (trimmed.empty()) {
				return std::string();
			}

			std::string lowered = ToLowerCopy(trimmed);
			std::size_t schemeLength = 0;
			if (lowered.rfind("http://", 0) == 0) {
				schemeLength = 7;
			}
			else if (lowered.rfind("https://", 0) == 0) {
				schemeLength = 8;
			}
			if (schemeLength > 0) {
				std::string canonical =
					lowered.substr(0, schemeLength) + trimmed.substr(schemeLength);

				const std::size_t authorityStart = schemeLength;
				const std::size_t authorityEnd = canonical.find_first_of("/?#", authorityStart);
				const std::size_t authorityLength = authorityEnd == std::string::npos
					? canonical.size() - authorityStart
					: authorityEnd - authorityStart;

				if (authorityLength > 0) {
					const std::string authority = canonical.substr(authorityStart, authorityLength);
					const std::size_t atPos = authority.rfind('@');
					const std::string userInfo = atPos == std::string::npos
						? std::string()
						: authority.substr(0, atPos + 1);
					const std::string hostPort = atPos == std::string::npos
						? authority
						: authority.substr(atPos + 1);

					std::string canonicalHostPort = hostPort;
					if (!hostPort.empty()) {
						if (hostPort.front() == '[') {
							const std::size_t closeBracket = hostPort.find(']');
							if (closeBracket != std::string::npos) {
								const std::string host = hostPort.substr(0, closeBracket + 1);
								const std::string port = hostPort.substr(closeBracket + 1);
								canonicalHostPort = ToLowerCopy(host) + port;
							}
						}
						else {
							const std::size_t colonPos = hostPort.rfind(':');
							if (colonPos != std::string::npos) {
								const std::string host = hostPort.substr(0, colonPos);
								const std::string port = hostPort.substr(colonPos);
								canonicalHostPort = ToLowerCopy(host) + port;
							}
							else {
								canonicalHostPort = ToLowerCopy(hostPort);
							}
						}
					}

					canonical.replace(
						authorityStart,
						authorityLength,
						userInfo + canonicalHostPort);
				}

				return canonical;
			}

			return trimmed;
		}

		std::string CanonicalizeFailureAlertRouteTarget(
			const std::string& mode,
			const std::string& target) {
			const std::string normalizedMode = ToLowerCopy(TrimCopy(mode));
			if (normalizedMode == kFailureAlertModeWebhook) {
				return CanonicalizeHttpUrlForRouteCompare(target);
			}

			return TrimCopy(target);
		}

		bool IsTransportDispatchEnabled(const CronJson& node) {
			return node.contains("transportDispatch") &&
				node["transportDispatch"].is_boolean() &&
				node["transportDispatch"].get<bool>();
		}

		std::wstring Utf8ToWide(const std::string& value) {
			if (value.empty()) {
				return std::wstring();
			}

			const int required = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				-1,
				nullptr,
				0);
			if (required <= 0) {
				return std::wstring();
			}

			std::wstring converted(static_cast<std::size_t>(required), L'\0');
			const int convertedCount = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				-1,
				converted.data(),
				required);
			if (convertedCount <= 0) {
				return std::wstring();
			}

			if (!converted.empty() && converted.back() == L'\0') {
				converted.pop_back();
			}

			return converted;
		}

		struct WebhookDispatchResult {
			bool attempted = false;
			std::optional<std::int64_t> httpStatus;
			std::string error;
		};

		WebhookDispatchResult DispatchWebhookPostWinHttp(
			const std::string& url,
			const std::int64_t timeoutMs = 5000) {
			WebhookDispatchResult result;

			const std::wstring urlW = Utf8ToWide(url);
			if (urlW.empty()) {
				result.error = "invalid webhook URL encoding";
				return result;
			}

			URL_COMPONENTS components{};
			components.dwStructSize = sizeof(components);
			components.dwSchemeLength = static_cast<DWORD>(-1);
			components.dwHostNameLength = static_cast<DWORD>(-1);
			components.dwUrlPathLength = static_cast<DWORD>(-1);
			components.dwExtraInfoLength = static_cast<DWORD>(-1);

			if (!WinHttpCrackUrl(urlW.c_str(), 0, 0, &components)) {
				result.error = "failed to parse webhook URL";
				return result;
			}

			const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
			if (!secure && components.nScheme != INTERNET_SCHEME_HTTP) {
				result.error = "unsupported webhook URL scheme";
				return result;
			}

			const std::wstring host(
				components.lpszHostName,
				components.dwHostNameLength);
			std::wstring path(
				components.lpszUrlPath,
				components.dwUrlPathLength > 0
				? components.dwUrlPathLength
				: 1);
			if (path.empty()) {
				path = L"/";
			}
			if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo != nullptr) {
				path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
			}

			const DWORD timeoutMsDword = static_cast<DWORD>((std::max)(
				static_cast<std::int64_t>(1),
				timeoutMs));

			HINTERNET session = WinHttpOpen(
				L"BlazeClawCron/1.0",
				WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
				WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS,
				0);
			if (!session) {
				result.error = "WinHttpOpen failed";
				return result;
			}

			HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
			if (!connection) {
				WinHttpCloseHandle(session);
				result.error = "WinHttpConnect failed";
				return result;
			}

			HINTERNET request = WinHttpOpenRequest(
				connection,
				L"POST",
				path.c_str(),
				nullptr,
				WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES,
				secure ? WINHTTP_FLAG_SECURE : 0);
			if (!request) {
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				result.error = "WinHttpOpenRequest failed";
				return result;
			}

			WinHttpSetTimeouts(
				request,
				static_cast<int>(timeoutMsDword),
				static_cast<int>(timeoutMsDword),
				static_cast<int>(timeoutMsDword),
				static_cast<int>(timeoutMsDword));

			result.attempted = true;
			if (!WinHttpSendRequest(
				request,
				WINHTTP_NO_ADDITIONAL_HEADERS,
				0,
				WINHTTP_NO_REQUEST_DATA,
				0,
				0,
				0)) {
				result.error = "WinHttpSendRequest failed";
				WinHttpCloseHandle(request);
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				return result;
			}

			if (!WinHttpReceiveResponse(request, nullptr)) {
				result.error = "WinHttpReceiveResponse failed";
				WinHttpCloseHandle(request);
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				return result;
			}

			DWORD statusCode = 0;
			DWORD statusCodeSize = sizeof(statusCode);
			if (!WinHttpQueryHeaders(
				request,
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX,
				&statusCode,
				&statusCodeSize,
				WINHTTP_NO_HEADER_INDEX)) {
				result.error = "WinHttpQueryHeaders failed";
			}
			else {
				result.httpStatus = static_cast<std::int64_t>(statusCode);
			}

			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return result;
		}

		std::int64_t ResolveRetryDelayMs(const CronJson& retry, const std::int64_t attemptIndex) {
			auto parseDelay = [](const CronJson& value) -> std::optional<std::int64_t> {
				if (value.is_number_integer()) {
					return value.get<std::int64_t>();
				}
				if (value.is_number_unsigned()) {
					return static_cast<std::int64_t>(value.get<std::uint64_t>());
				}
				if (value.is_number_float()) {
					return static_cast<std::int64_t>(value.get<double>());
				}
				if (value.is_string()) {
					try {
						return std::stoll(TrimCopy(value.get<std::string>()));
					}
					catch (...) {
						return std::nullopt;
					}
				}
				return std::nullopt;
			};

			if (retry.contains("backoffMs") && retry["backoffMs"].is_array()) {
				const auto& backoff = retry["backoffMs"];
				if (!backoff.empty()) {
					const std::size_t index = static_cast<std::size_t>((std::max)(
						static_cast<std::int64_t>(0),
						attemptIndex - 1));
					const std::size_t bounded = (std::min)(index, backoff.size() - 1);
					const auto delay = parseDelay(backoff[bounded]);
					if (delay.has_value() && delay.value() >= 0) {
						return delay.value();
					}
				}
			}

			return kDefaultRetryDelayMs;
		}

		std::int64_t ResolveFailureAlertAfter(const CronJson& job) {
			if (!job.contains("failureAlert")) {
				return 0;
			}

			if (job["failureAlert"].is_boolean()) {
				return job["failureAlert"].get<bool>()
					? kDefaultFailureAlertAfter
					: 0;
			}

			if (!job.contains("failureAlert") || !job["failureAlert"].is_object()) {
				return kDefaultFailureAlertAfter;
			}
			return (std::max)(
				static_cast<std::int64_t>(1),
				TryReadInt64Field(job["failureAlert"], "after")
				.value_or(kDefaultFailureAlertAfter));
		}

		std::int64_t ResolveFailureAlertCooldownMs(const CronJson& job) {
			if (!job.contains("failureAlert")) {
				return 0;
			}

			if (job["failureAlert"].is_boolean()) {
				return job["failureAlert"].get<bool>()
					? kDefaultFailureAlertCooldownMs
					: 0;
			}

			if (!job.contains("failureAlert") || !job["failureAlert"].is_object()) {
				return kDefaultFailureAlertCooldownMs;
			}
			return (std::max)(
				static_cast<std::int64_t>(0),
				TryReadInt64Field(job["failureAlert"], "cooldownMs")
				.value_or(kDefaultFailureAlertCooldownMs));
		}

		struct RunOutcome {
			std::string status = "ok";
			std::string summary;
			std::string error;
			std::string sessionId;
			std::string sessionKey;
			std::string model;
			std::string provider;
			std::int64_t usagePromptTokens = 0;
			std::int64_t usageCompletionTokens = 0;
			std::int64_t usageTotalTokens = 0;
			bool usageAvailable = false;
			std::string deliveryStatus = "not-requested";
			std::string deliveryMode;
			std::string deliveryTarget;
			std::string deliveryChannel;
			std::string deliveryAccountId;
			bool delivered = false;
			bool deliveryAttempted = false;
			std::int64_t deliveryHttpStatus = 0;
			bool retryable = false;
			std::string failureDestinationStatus = "not-requested";
			std::string failureDestinationTarget;
			std::string failureDestinationChannel;
			std::string failureDestinationAccountId;
			bool failureDestinationAttempted = false;
			std::int64_t failureDestinationHttpStatus = 0;
			std::string failureDestinationError;
			std::string failureDestinationMode;
			std::string errorCategory;
			bool timedOut = false;
			bool skipDeliverySimulation = false;
			bool hasRetryDelayOverride = false;
			std::int64_t retryDelayOverrideMs = 0;
		};

		void ApplyWebhookHttpStatusToPrimaryOutcome(
			RunOutcome& outcome,
			const std::int64_t statusCode) {
			outcome.deliveryHttpStatus = statusCode;
			if (statusCode >= 200 && statusCode < 300) {
				outcome.deliveryStatus = "delivered";
				outcome.delivered = true;
				return;
			}

			outcome.status = "error";
			outcome.deliveryStatus = "not-delivered";
			outcome.error =
				"webhook delivery returned HTTP " + std::to_string(statusCode);
			if (statusCode == 429) {
				outcome.errorCategory = "rate_limit";
				outcome.summary = "Webhook delivery rate limited";
				outcome.retryable = true;
			}
			else if (statusCode >= 500) {
				outcome.errorCategory = "network";
				outcome.summary = "Webhook delivery server error";
				outcome.retryable = true;
			}
			else {
				outcome.errorCategory = "delivery_http_error";
				outcome.summary = "Webhook delivery rejected";
				outcome.retryable = false;
			}
		}

		void ApplyWebhookHttpStatusToFailureDestination(
			RunOutcome& outcome,
			const std::int64_t statusCode) {
			outcome.failureDestinationHttpStatus = statusCode;
			if (statusCode >= 200 && statusCode < 300) {
				outcome.failureDestinationStatus = "delivered";
				return;
			}

			outcome.failureDestinationStatus = "not-delivered";
			outcome.failureDestinationError =
				"failure destination webhook returned HTTP " + std::to_string(statusCode);
		}

		void ApplyRuntimeExecutionResult(
			RunOutcome& outcome,
			const CronJson& runtimeResult) {
			if (runtimeResult.contains("status") && runtimeResult["status"].is_string()) {
				outcome.status =
					ToLowerCopy(TrimCopy(runtimeResult["status"].get<std::string>()));
			}
			if (runtimeResult.contains("summary") && runtimeResult["summary"].is_string()) {
				outcome.summary = TrimCopy(runtimeResult["summary"].get<std::string>());
			}
			if (runtimeResult.contains("error") && runtimeResult["error"].is_string()) {
				outcome.error = TrimCopy(runtimeResult["error"].get<std::string>());
			}
			if (runtimeResult.contains("errorCategory") &&
				runtimeResult["errorCategory"].is_string()) {
				outcome.errorCategory =
					ToLowerCopy(TrimCopy(runtimeResult["errorCategory"].get<std::string>()));
			}
			if (runtimeResult.contains("retryable") && runtimeResult["retryable"].is_boolean()) {
				outcome.retryable = runtimeResult["retryable"].get<bool>();
			}
			if (runtimeResult.contains("retryAfterMs")) {
				const auto maybeRetryAfter = TryReadInt64Field(runtimeResult, "retryAfterMs");
				if (maybeRetryAfter.has_value() && maybeRetryAfter.value() >= 0) {
					outcome.hasRetryDelayOverride = true;
					outcome.retryDelayOverrideMs = maybeRetryAfter.value();
				}
			}
			if (runtimeResult.contains("skipDelivery") &&
				runtimeResult["skipDelivery"].is_boolean()) {
				outcome.skipDeliverySimulation = runtimeResult["skipDelivery"].get<bool>();
			}
			if (runtimeResult.contains("timedOut") && runtimeResult["timedOut"].is_boolean()) {
				outcome.timedOut = runtimeResult["timedOut"].get<bool>();
			}
			if (outcome.timedOut && outcome.errorCategory.empty()) {
				outcome.errorCategory = "timeout";
			}
			if (runtimeResult.contains("sessionId") && runtimeResult["sessionId"].is_string()) {
				outcome.sessionId = TrimCopy(runtimeResult["sessionId"].get<std::string>());
			}
			if (runtimeResult.contains("sessionKey") && runtimeResult["sessionKey"].is_string()) {
				outcome.sessionKey = TrimCopy(runtimeResult["sessionKey"].get<std::string>());
			}
			if (runtimeResult.contains("model") && runtimeResult["model"].is_string()) {
				outcome.model = TrimCopy(runtimeResult["model"].get<std::string>());
			}
			if (runtimeResult.contains("provider") && runtimeResult["provider"].is_string()) {
				outcome.provider = TrimCopy(runtimeResult["provider"].get<std::string>());
			}

			if (runtimeResult.contains("delivered") && runtimeResult["delivered"].is_boolean()) {
				outcome.delivered = runtimeResult["delivered"].get<bool>();
			}
			if (runtimeResult.contains("deliveryStatus") && runtimeResult["deliveryStatus"].is_string()) {
				outcome.deliveryStatus = ToLowerCopy(
					TrimCopy(runtimeResult["deliveryStatus"].get<std::string>()));
			}
			if (runtimeResult.contains("deliveryMode") && runtimeResult["deliveryMode"].is_string()) {
				outcome.deliveryMode = ToLowerCopy(
					TrimCopy(runtimeResult["deliveryMode"].get<std::string>()));
			}
			if (runtimeResult.contains("deliveryTarget") && runtimeResult["deliveryTarget"].is_string()) {
				outcome.deliveryTarget = TrimCopy(runtimeResult["deliveryTarget"].get<std::string>());
			}
			if (runtimeResult.contains("deliveryChannel") && runtimeResult["deliveryChannel"].is_string()) {
				outcome.deliveryChannel = TrimCopy(runtimeResult["deliveryChannel"].get<std::string>());
			}
			if (runtimeResult.contains("deliveryAccountId") && runtimeResult["deliveryAccountId"].is_string()) {
				outcome.deliveryAccountId = TrimCopy(runtimeResult["deliveryAccountId"].get<std::string>());
			}
			if (runtimeResult.contains("deliveryAttempted") && runtimeResult["deliveryAttempted"].is_boolean()) {
				outcome.deliveryAttempted = runtimeResult["deliveryAttempted"].get<bool>();
			}
			if (runtimeResult.contains("deliveryHttpStatus")) {
				const auto maybeStatus = TryReadInt64Field(runtimeResult, "deliveryHttpStatus");
				if (maybeStatus.has_value()) {
					outcome.deliveryHttpStatus = maybeStatus.value();
				}
			}

			if (runtimeResult.contains("failureDestinationStatus") &&
				runtimeResult["failureDestinationStatus"].is_string()) {
				outcome.failureDestinationStatus = ToLowerCopy(
					TrimCopy(runtimeResult["failureDestinationStatus"].get<std::string>()));
			}
			if (runtimeResult.contains("failureDestinationMode") &&
				runtimeResult["failureDestinationMode"].is_string()) {
				outcome.failureDestinationMode = ToLowerCopy(
					TrimCopy(runtimeResult["failureDestinationMode"].get<std::string>()));
			}
			if (runtimeResult.contains("failureDestinationTarget") &&
				runtimeResult["failureDestinationTarget"].is_string()) {
				outcome.failureDestinationTarget =
					TrimCopy(runtimeResult["failureDestinationTarget"].get<std::string>());
			}
			if (runtimeResult.contains("failureDestinationChannel") &&
				runtimeResult["failureDestinationChannel"].is_string()) {
				outcome.failureDestinationChannel =
					TrimCopy(runtimeResult["failureDestinationChannel"].get<std::string>());
			}
			if (runtimeResult.contains("failureDestinationAccountId") &&
				runtimeResult["failureDestinationAccountId"].is_string()) {
				outcome.failureDestinationAccountId =
					TrimCopy(runtimeResult["failureDestinationAccountId"].get<std::string>());
			}
			if (runtimeResult.contains("failureDestinationAttempted") &&
				runtimeResult["failureDestinationAttempted"].is_boolean()) {
				outcome.failureDestinationAttempted =
					runtimeResult["failureDestinationAttempted"].get<bool>();
			}
			if (runtimeResult.contains("failureDestinationHttpStatus")) {
				const auto maybeFailureStatus =
					TryReadInt64Field(runtimeResult, "failureDestinationHttpStatus");
				if (maybeFailureStatus.has_value()) {
					outcome.failureDestinationHttpStatus = maybeFailureStatus.value();
				}
			}
			if (runtimeResult.contains("failureDestinationError") &&
				runtimeResult["failureDestinationError"].is_string()) {
				outcome.failureDestinationError =
					TrimCopy(runtimeResult["failureDestinationError"].get<std::string>());
			}

			if (runtimeResult.contains("usage") && runtimeResult["usage"].is_object()) {
				const CronJson& usage = runtimeResult["usage"];
				const auto promptTokens = TryReadInt64Field(usage, "promptTokens");
				const auto completionTokens = TryReadInt64Field(usage, "completionTokens");
				const auto totalTokens = TryReadInt64Field(usage, "totalTokens");
				if (promptTokens.has_value() ||
					completionTokens.has_value() ||
					totalTokens.has_value()) {
					outcome.usagePromptTokens = promptTokens.value_or(0);
					outcome.usageCompletionTokens = completionTokens.value_or(0);
					outcome.usageTotalTokens = totalTokens.has_value()
						? totalTokens.value()
						: (outcome.usagePromptTokens + outcome.usageCompletionTokens);
					outcome.usageAvailable = true;
				}
			}
		}

		bool IsTransientErrorCategory(const std::string& errorText) {
			const std::string lowered = ToLowerCopy(errorText);
			return lowered.find("timeout") != std::string::npos ||
				lowered.find("network") != std::string::npos ||
				lowered.find("429") != std::string::npos ||
				lowered.find("rate") != std::string::npos;
		}

		CronJson& EnsureStateObject(CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				job["state"] = CronJson::object();
			}
			return job["state"];
		}

		std::optional<std::int64_t> ReadRetryPendingUntilMs(const CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				return std::nullopt;
			}
			return TryReadInt64Field(job["state"], "retryPendingUntilMs");
		}

		std::optional<std::int64_t> ReadNextRunAtMs(const CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				return std::nullopt;
			}
			return TryReadInt64Field(job["state"], "nextRunAtMs");
		}

		bool IsBestEffortDelivery(const CronJson& job) {
			if (!job.contains("delivery") || !job["delivery"].is_object()) {
				return false;
			}
			if (!job["delivery"].contains("bestEffort") ||
				!job["delivery"]["bestEffort"].is_boolean()) {
				return false;
			}

			return job["delivery"]["bestEffort"].get<bool>();
		}

		RunOutcome EvaluateRunOutcome(
			const CronJson& job,
			const std::int64_t nowMs,
			const CronRuntimeExecutionAdapters& adapters) {
			RunOutcome outcome;
			bool runtimeHandled = false;
			const std::string sessionTargetRaw =
				TrimCopy(job.value("sessionTarget", std::string("main")));
			const std::string sessionTarget = ToLowerCopy(sessionTargetRaw);

			if (!job.contains("payload") || !job["payload"].is_object()) {
				outcome.status = "error";
				outcome.error = "invalid payload";
				outcome.errorCategory = "invalid_payload";
				outcome.summary = "Payload missing or invalid";
				outcome.retryable = false;
				return outcome;
			}

			const CronJson& payload = job["payload"];
			const std::string payloadKind =
				ToLowerCopy(TrimCopy(payload.value("kind", std::string())));
			std::string resolvedSessionKey = TrimCopy(job.value("sessionKey", std::string()));
			if (resolvedSessionKey.empty() &&
				payload.contains("sessionKey") &&
				payload["sessionKey"].is_string()) {
				resolvedSessionKey = TrimCopy(payload["sessionKey"].get<std::string>());
			}
			if (resolvedSessionKey.empty() &&
				sessionTarget.rfind("session:", 0) == 0 &&
				sessionTargetRaw.size() > std::string("session:").size()) {
				resolvedSessionKey = TrimCopy(
					sessionTargetRaw.substr(std::string("session:").size()));
			}

			if (sessionTarget == "current") {
				if (!resolvedSessionKey.empty()) {
					outcome.sessionId = std::string("session:") + resolvedSessionKey;
				}
				else if (payload.contains("sessionId") && payload["sessionId"].is_string()) {
					outcome.sessionId = TrimCopy(payload["sessionId"].get<std::string>());
				}
				else {
					outcome.sessionId = payloadKind == "systemevent"
						? std::string("main")
						: std::string("isolated");
				}
			}
			else if (sessionTarget == "main") {
				outcome.sessionId = "main";
			}
			else if (sessionTarget == "isolated") {
				outcome.sessionId = "isolated";
			}
			else if (sessionTarget.rfind("session:", 0) == 0) {
				outcome.sessionId = sessionTargetRaw;
			}
			else {
				outcome.sessionId = payloadKind == "agentturn"
					? std::string("isolated")
					: std::string("main");
			}
			outcome.sessionKey = resolvedSessionKey;

			const bool isolatedLikeTarget =
				sessionTarget == "isolated" ||
				sessionTarget.rfind("session:", 0) == 0 ||
				(sessionTarget == "current" && !resolvedSessionKey.empty());
			if (payload.contains("model") && payload["model"].is_string()) {
				outcome.model = TrimCopy(payload["model"].get<std::string>());
			}
			if (payload.contains("provider") && payload["provider"].is_string()) {
				outcome.provider = TrimCopy(payload["provider"].get<std::string>());
			}
			if (outcome.model.empty() && job.contains("model") && job["model"].is_string()) {
				outcome.model = TrimCopy(job["model"].get<std::string>());
			}
			if (outcome.provider.empty() && job.contains("provider") && job["provider"].is_string()) {
				outcome.provider = TrimCopy(job["provider"].get<std::string>());
			}

			if (sessionTarget == "main" && payloadKind != "systemevent") {
				outcome.status = "skipped";
				outcome.summary =
					"Skipped: main session target requires systemEvent payload";
				return outcome;
			}
			if (isolatedLikeTarget && payloadKind != "agentturn") {
				outcome.status = "skipped";
				outcome.summary =
					"Skipped: isolated/session target requires agentTurn payload";
				return outcome;
			}

			const CronRuntimeExecutionAdapter* runtimeAdapter = nullptr;
			if (payloadKind == "systemevent") {
				runtimeAdapter = &adapters.mainSession;
			}
			else if (payloadKind == "agentturn") {
				runtimeAdapter = &adapters.isolatedSession;
			}
			if (runtimeAdapter != nullptr && static_cast<bool>(*runtimeAdapter)) {
				const std::optional<CronJson> runtimeResult =
					(*runtimeAdapter)(job, nowMs);
				if (runtimeResult.has_value() && runtimeResult.value().is_object()) {
					const CronJson& runtimeNode = runtimeResult.value();
					ApplyRuntimeExecutionResult(outcome, runtimeResult.value());
					if (runtimeNode.contains("handled") &&
						runtimeNode["handled"].is_boolean()) {
						runtimeHandled = runtimeNode["handled"].get<bool>();
					}
					else {
						const bool hasExplicitRuntimeOutcome =
							runtimeNode.contains("status") ||
							runtimeNode.contains("summary") ||
							runtimeNode.contains("error") ||
							runtimeNode.contains("timedOut") ||
							runtimeNode.contains("aborted");
						if (hasExplicitRuntimeOutcome) {
							runtimeHandled = true;
						}
					}

					const bool heartbeatBusy =
						runtimeNode.contains("busy") &&
						runtimeNode["busy"].is_boolean() &&
						runtimeNode["busy"].get<bool>();
					if (heartbeatBusy &&
						payloadKind == "systemevent" &&
						sessionTarget == "main") {
						const std::int64_t heartbeatBusyAttempts = (std::max)(
							static_cast<std::int64_t>(0),
							TryReadInt64Field(job.value("state", CronJson::object()),
								"heartbeatBusyAttempts")
							.value_or(0));
						const std::int64_t maxBusyAttempts = (std::max)(
							static_cast<std::int64_t>(1),
							TryReadInt64Field(payload, "heartbeatBusyMaxAttempts")
							.value_or(kDefaultHeartbeatBusyMaxAttempts));
						const std::int64_t busyDelayMs = (std::max)(
							static_cast<std::int64_t>(1),
							TryReadInt64Field(payload, "heartbeatBusyDelayMs")
							.value_or(kDefaultHeartbeatBusyDelayMs));

						if (heartbeatBusyAttempts < maxBusyAttempts) {
							outcome.status = "error";
							outcome.error = "main heartbeat busy";
							outcome.errorCategory = "heartbeat_busy";
							outcome.summary = "Main heartbeat busy; retry scheduled";
							outcome.retryable = true;
							outcome.hasRetryDelayOverride = true;
							outcome.retryDelayOverrideMs = busyDelayMs;
						}
						else {
							outcome.status = "error";
							outcome.error = "main heartbeat busy fallback wake requested";
							outcome.errorCategory = "heartbeat_busy_fallback";
							outcome.summary =
								"Main heartbeat busy after retries; fallback wake requested";
							outcome.retryable = false;
						}
					}
				}
			}

			if (!runtimeHandled && payloadKind == "systemevent") {
				const std::string text =
					TrimCopy(payload.value("text", std::string()));
				if (text.empty()) {
					outcome.status = "skipped";
					outcome.summary = "Skipped: empty systemEvent text";
					return outcome;
				}

				if (outcome.sessionId.empty()) {
					outcome.sessionId = "isolated";
				}

				if (!outcome.usageAvailable) {
					outcome.usagePromptTokens =
						(std::max)(static_cast<std::int64_t>(1),
							static_cast<std::int64_t>(text.size() / 4));
					outcome.usageCompletionTokens = 0;
					outcome.usageTotalTokens = outcome.usagePromptTokens;
					outcome.usageAvailable = true;
				}
			}
			if (!runtimeHandled && payloadKind == "agentturn") {
				const std::string message =
					TrimCopy(payload.value("message", std::string()));
				if (message.empty()) {
					outcome.status = "skipped";
					outcome.summary = "Skipped: empty agentTurn message";
					return outcome;
				}

				const auto timeoutSeconds = TryReadInt64Field(payload, "timeoutSeconds");
				if (timeoutSeconds.has_value() && timeoutSeconds.value() > 0 &&
					timeoutSeconds.value() <= 1) {
					outcome.status = "error";
					outcome.error = "cron: job execution timed out";
					outcome.errorCategory = "timeout";
					outcome.summary = "Agent turn timed out";
					outcome.retryable = true;
					outcome.timedOut = true;
					return outcome;
				}

				if (!outcome.usageAvailable) {
					outcome.usagePromptTokens =
						(std::max)(static_cast<std::int64_t>(1),
							static_cast<std::int64_t>(message.size() / 4));
					outcome.usageCompletionTokens =
						(std::max)(static_cast<std::int64_t>(1),
							outcome.usagePromptTokens / 2);
					outcome.usageTotalTokens =
						outcome.usagePromptTokens + outcome.usageCompletionTokens;
					outcome.usageAvailable = true;
				}
			}

			if (outcome.status == "ok" &&
				!outcome.error.empty()) {
				outcome.error.clear();
			}

			if (!(runtimeHandled && outcome.skipDeliverySimulation) &&
				job.contains("delivery") &&
				job["delivery"].is_object()) {
				const CronJson& delivery = job["delivery"];
				const bool simulateTransientFailure =
					delivery.contains("simulateTransientFailure") &&
					delivery["simulateTransientFailure"].is_boolean() &&
					delivery["simulateTransientFailure"].get<bool>();
				const std::string mode = ToLowerCopy(
					TrimCopy(delivery.value("mode", std::string("announce"))));
				outcome.deliveryMode = mode;
				outcome.deliveryTarget = TrimCopy(delivery.value("to", std::string()));
				outcome.deliveryChannel = TrimCopy(delivery.value("channel", std::string("last")));
				if (outcome.deliveryChannel.empty()) {
					outcome.deliveryChannel = "last";
				}
				outcome.deliveryAccountId = TrimCopy(delivery.value("accountId", std::string()));
				if (mode == "none") {
					outcome.deliveryStatus = "not-requested";
					outcome.delivered = false;
				}
				else if (mode == "announce") {
					outcome.deliveryAttempted = true;
					if (outcome.deliveryTarget.empty()) {
						outcome.deliveryTarget = outcome.sessionId.empty()
							? (sessionTarget == "main"
								? std::string("main")
								: std::string("isolated"))
							: outcome.sessionId;
					}

					if (delivery.contains("to") &&
						delivery["to"].is_string() &&
						TrimCopy(delivery["to"].get<std::string>()).empty()) {
						outcome.status = "error";
						outcome.deliveryStatus = "not-delivered";
						outcome.error = "announce delivery target is empty";
						outcome.errorCategory = "delivery_target_invalid";
						outcome.summary = "Announce delivery target is invalid";
						outcome.retryable = false;
					}
					else if (outcome.deliveryTarget.empty()) {
						outcome.status = "error";
						outcome.deliveryStatus = "not-delivered";
						outcome.error = "announce delivery target is unresolved";
						outcome.errorCategory = "delivery_target_invalid";
						outcome.summary = "Announce delivery target could not be resolved";
						outcome.retryable = false;
					}
					else {
						outcome.deliveryStatus = "delivered";
						outcome.delivered = true;
					}
				}
				else if (mode == "webhook") {
					outcome.deliveryAttempted = true;
					std::string to =
						TrimCopy(delivery.value("to", std::string()));
					if (to.empty() &&
						delivery.contains("url") &&
						delivery["url"].is_string()) {
						to = TrimCopy(delivery["url"].get<std::string>());
					}
					outcome.deliveryTarget = to;
					const bool transportDispatch = IsTransportDispatchEnabled(delivery);
					const auto simulatedHttpStatus =
						TryReadInt64Field(delivery, "simulateHttpStatus");
					if (simulateTransientFailure) {
						outcome.status = "error";
						outcome.deliveryStatus = "not-delivered";
						outcome.error = "webhook delivery transient failure";
						outcome.errorCategory = "network";
						outcome.summary = "Webhook delivery transient failure";
						outcome.retryable = true;
					}
					else if (simulatedHttpStatus.has_value()) {
						ApplyWebhookHttpStatusToPrimaryOutcome(
							outcome,
							simulatedHttpStatus.value());
					}
					else if (transportDispatch && StartsWithHttpScheme(to)) {
						const WebhookDispatchResult dispatch =
							DispatchWebhookPostWinHttp(to);
						outcome.deliveryAttempted = dispatch.attempted;
						if (dispatch.httpStatus.has_value()) {
							ApplyWebhookHttpStatusToPrimaryOutcome(
								outcome,
								dispatch.httpStatus.value());
						}
						else {
							outcome.status = "error";
							outcome.deliveryStatus = "not-delivered";
							outcome.error = dispatch.error.empty()
								? std::string("webhook delivery transport dispatch failed")
								: dispatch.error;
							outcome.errorCategory = "network";
							outcome.summary = "Webhook delivery transport dispatch failed";
							outcome.retryable = true;
						}
					}
					else if (StartsWithHttpScheme(to)) {
						outcome.deliveryStatus = "delivered";
						outcome.delivered = true;
					}
					else {
						outcome.status = "error";
						outcome.deliveryStatus = "not-delivered";
						outcome.error = "invalid webhook delivery target";
						outcome.errorCategory = "delivery_target_invalid";
						outcome.summary = "Webhook delivery target is invalid";
						outcome.retryable = false;
					}
				}
				else {
					outcome.status = "error";
					outcome.deliveryStatus = "not-delivered";
					outcome.error = "unsupported delivery mode";
					outcome.errorCategory = "delivery_mode_invalid";
					outcome.summary = "Delivery mode is invalid";
					outcome.retryable = false;
				}

				if (outcome.status == "error" &&
					delivery.contains("failureDestination") &&
					delivery["failureDestination"].is_object()) {
					const CronJson& failureDestination = delivery["failureDestination"];
					std::string failureMode = ToLowerCopy(
						TrimCopy(failureDestination.value("mode", std::string("announce"))));
					if (failureMode != "announce" && failureMode != "webhook") {
						failureMode = "announce";
					}
					outcome.failureDestinationMode = failureMode;

					const std::string primaryMode = mode;
					const std::string primaryTo = !outcome.deliveryTarget.empty()
						? outcome.deliveryTarget
						: TrimCopy(delivery.value("to", std::string()));
					const std::string primaryChannel = !outcome.deliveryChannel.empty()
						? outcome.deliveryChannel
						: TrimCopy(delivery.value("channel", std::string("last")));
					const std::string primaryAccountId =
						TrimCopy(delivery.value("accountId", std::string()));

					const bool failureHasExplicitTo =
						failureDestination.contains("to") &&
						failureDestination["to"].is_string();
					std::string failureTo = failureHasExplicitTo
						? TrimCopy(failureDestination["to"].get<std::string>())
						: std::string();
					if (failureTo.empty() &&
						failureDestination.contains("url") &&
						failureDestination["url"].is_string()) {
						failureTo = TrimCopy(failureDestination["url"].get<std::string>());
					}
					const std::string resolvedFailureTo =
						(failureMode == "announce" &&
							!failureHasExplicitTo &&
							!primaryTo.empty())
						? primaryTo
						: failureTo;
					outcome.failureDestinationTarget = resolvedFailureTo;
					const bool failureHasExplicitChannel =
						failureDestination.contains("channel") &&
						failureDestination["channel"].is_string();
					const std::string failureChannel = failureHasExplicitChannel
						? TrimCopy(failureDestination["channel"].get<std::string>())
						: std::string();
					const std::string failureAccountId =
						TrimCopy(failureDestination.value("accountId", std::string()));
					const std::string normalizedPrimaryChannel =
						primaryChannel.empty() ? std::string("last") : primaryChannel;
					const std::string normalizedFailureChannel =
						failureChannel.empty() ? std::string("last") : failureChannel;
					const std::string resolvedFailureChannel =
						failureChannel.empty() ? normalizedPrimaryChannel : failureChannel;
					const std::string resolvedFailureAccountId =
						failureAccountId.empty() ? primaryAccountId : failureAccountId;
					outcome.failureDestinationChannel = resolvedFailureChannel;
					outcome.failureDestinationAccountId = resolvedFailureAccountId;

					const bool sameWebhookTarget =
						failureMode == "webhook" &&
						primaryMode == "webhook" &&
						resolvedFailureTo == primaryTo;
					const bool sameAnnounceTarget =
						failureMode == "announce" &&
						primaryMode != "none" &&
						resolvedFailureTo == primaryTo &&
						normalizedFailureChannel == normalizedPrimaryChannel &&
						resolvedFailureAccountId == primaryAccountId;
					const bool sameTarget = sameWebhookTarget || sameAnnounceTarget;
					if (sameTarget) {
						outcome.failureDestinationStatus = "suppressed";
						outcome.failureDestinationError =
							"failure destination matches primary delivery target";
						return outcome;
					}

					if (failureMode == "webhook") {
						outcome.failureDestinationAttempted = true;
						const bool failureTransportDispatch =
							IsTransportDispatchEnabled(failureDestination);
						const auto failureDestinationHttpStatus =
							TryReadInt64Field(failureDestination, "simulateHttpStatus");
						if (failureDestinationHttpStatus.has_value()) {
							ApplyWebhookHttpStatusToFailureDestination(
								outcome,
								failureDestinationHttpStatus.value());
						}
						else if (failureTransportDispatch && StartsWithHttpScheme(resolvedFailureTo)) {
							const WebhookDispatchResult dispatch =
								DispatchWebhookPostWinHttp(resolvedFailureTo);
							outcome.failureDestinationAttempted = dispatch.attempted;
							if (dispatch.httpStatus.has_value()) {
								ApplyWebhookHttpStatusToFailureDestination(
									outcome,
									dispatch.httpStatus.value());
							}
							else {
								outcome.failureDestinationStatus = "not-delivered";
								outcome.failureDestinationError = dispatch.error.empty()
									? std::string("failure destination webhook transport dispatch failed")
									: dispatch.error;
							}
						}
						else if (StartsWithHttpScheme(resolvedFailureTo)) {
							outcome.failureDestinationStatus = "delivered";
						}
						else {
							outcome.failureDestinationStatus = "not-delivered";
							outcome.failureDestinationError =
								"invalid failure destination webhook target";
						}
					}
					else {
						outcome.failureDestinationAttempted = true;
						if (failureHasExplicitTo &&
							failureTo.empty()) {
							outcome.failureDestinationStatus = "not-delivered";
							outcome.failureDestinationError =
								"announce failure destination target is empty";
						}
						else {
							outcome.failureDestinationStatus = "delivered";
						}
					}
				}
			}

			if (outcome.status == "error" && outcome.errorCategory.empty()) {
				outcome.errorCategory = IsTransientErrorCategory(outcome.error)
					? "transient"
					: "runtime";
				outcome.retryable = outcome.errorCategory == "transient";
			}

			if (outcome.status == "ok" && outcome.summary.empty()) {
				outcome.summary = "Run completed";
			}

			return outcome;
		}

		std::optional<int> ParseTimezoneOffsetMinutes(const std::string& tzRaw) {
			const std::string tz = ToLowerCopy(TrimCopy(tzRaw));
			if (tz.empty() || tz == "utc" || tz == "gmt" || tz == "z") {
				return 0;
			}

			std::size_t offsetPos = std::string::npos;
			if (tz.rfind("utc", 0) == 0 || tz.rfind("gmt", 0) == 0) {
				offsetPos = 3;
			}
			else if (tz[0] == '+' || tz[0] == '-') {
				offsetPos = 0;
			}

			if (offsetPos == std::string::npos || offsetPos >= tz.size()) {
				return std::nullopt;
			}

			const char signCh = tz[offsetPos];
			if (signCh != '+' && signCh != '-') {
				return std::nullopt;
			}

			const int sign = signCh == '+' ? 1 : -1;
			std::string digits = tz.substr(offsetPos + 1);
			digits.erase(std::remove(digits.begin(), digits.end(), ':'), digits.end());
			if (digits.empty() || digits.size() > 4) {
				return std::nullopt;
			}

			for (const char ch : digits) {
				if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
					return std::nullopt;
				}
			}

			int hours = 0;
			int minutes = 0;
			try {
				if (digits.size() <= 2) {
					hours = std::stoi(digits);
				}
				else {
					hours = std::stoi(digits.substr(0, digits.size() - 2));
					minutes = std::stoi(digits.substr(digits.size() - 2));
				}
			}
			catch (...) {
				return std::nullopt;
			}

			if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
				return std::nullopt;
			}

			return sign * (hours * 60 + minutes);
		}

		bool MatchCronToken(const std::string& tokenRaw, const int value, const int maxValue) {
			const std::string token = TrimCopy(tokenRaw);
			if (token.empty() || token == "*") {
				return true;
			}

			if (token.rfind("*/", 0) == 0) {
				try {
					const int step = std::stoi(token.substr(2));
					return step > 0 && value % step == 0;
				}
				catch (...) {
					return false;
				}
			}

			std::istringstream parts(token);
			std::string item;
			while (std::getline(parts, item, ',')) {
				const std::string trimmed = TrimCopy(item);
				if (trimmed.empty()) {
					continue;
				}

				try {
					const int parsed = std::stoi(trimmed);
					if (parsed >= 0 && parsed <= maxValue && parsed == value) {
						return true;
					}
				}
				catch (...) {
					return false;
				}
			}

			return false;
		}

		std::int64_t ResolveStableCronOffsetMs(const CronJson& job, const std::int64_t staggerMs) {
			if (staggerMs <= 1) {
				return 0;
			}

			const std::string jobId = job.value("id", std::string());
			if (jobId.empty()) {
				return 0;
			}

			const std::uint64_t hashValue = std::hash<std::string>{}(jobId);
			return static_cast<std::int64_t>(hashValue % static_cast<std::uint64_t>(staggerMs));
		}

		std::int64_t ResolveEveryAnchorMs(const CronJson& schedule, const CronJson& job) {
			const auto explicitAnchor = TryReadInt64Field(schedule, "anchorMs");
			if (explicitAnchor.has_value() && explicitAnchor.value() >= 0) {
				return explicitAnchor.value();
			}

			const auto createdAtMs = TryReadInt64Field(job, "createdAtMs");
			if (createdAtMs.has_value() && createdAtMs.value() >= 0) {
				return createdAtMs.value();
			}

			return 0;
		}

		std::int64_t ComputeNextEvery(
			const CronJson& schedule,
			const CronJson& job,
			const std::int64_t nowMs) {
			const std::int64_t everyMs =
				(std::max)(static_cast<std::int64_t>(1000),
					TryReadInt64Field(schedule, "everyMs").value_or(60'000));

			const auto lastRunAtMs =
				job.contains("state") && job["state"].is_object()
				? TryReadInt64Field(job["state"], "lastRunAtMs")
				: std::nullopt;
			if (lastRunAtMs.has_value() && lastRunAtMs.value() + everyMs > nowMs) {
				return lastRunAtMs.value() + everyMs;
			}

			const std::int64_t anchorMs = ResolveEveryAnchorMs(schedule, job);
			if (anchorMs >= nowMs) {
				return anchorMs;
			}

			const std::int64_t elapsed = nowMs - anchorMs;
			const std::int64_t ticks = elapsed / everyMs;
			return anchorMs + (ticks + 1) * everyMs;
		}

		std::optional<std::int64_t> ComputeNextAt(
			const CronJson& schedule,
			const CronJson& job,
			const std::int64_t nowMs) {
			const auto atMs =
				TryReadInt64Field(schedule, "atMs").has_value()
				? TryReadInt64Field(schedule, "atMs")
				: TryReadInt64Field(schedule, "at");
			if (!atMs.has_value()) {
				return nowMs + 5 * 60 * 1000;
			}

			const auto lastStatus =
				job.contains("state") && job["state"].is_object()
				? ToLowerCopy(job["state"].value("lastStatus", std::string()))
				: std::string();
			const auto lastRunAtMs =
				job.contains("state") && job["state"].is_object()
				? TryReadInt64Field(job["state"], "lastRunAtMs")
				: std::nullopt;

			if (lastStatus == "ok" && lastRunAtMs.has_value() && atMs.value() <= lastRunAtMs.value()) {
				return std::nullopt;
			}

			return atMs.value();
		}

		std::optional<std::int64_t> ComputeNextCron(
			const CronJson& schedule,
			const CronJson& job,
			const std::int64_t nowMs) {
			const std::string expr =
				TrimCopy(schedule.value("expr", std::string("* * * * *")));
			std::istringstream stream(expr);
			std::vector<std::string> parts;
			std::string token;
			while (stream >> token) {
				parts.push_back(token);
			}
			if (parts.size() < 2) {
				return nowMs + kMinuteMs;
			}

			const auto timezoneOffsetMinutes =
				ParseTimezoneOffsetMinutes(schedule.value("tz", std::string()));
			const std::int64_t timezoneOffsetMs =
				static_cast<std::int64_t>(timezoneOffsetMinutes.value_or(0)) * 60 * 1000;
			std::int64_t localNowMs = nowMs + timezoneOffsetMs;
			std::int64_t candidateLocalMs = ((localNowMs / kMinuteMs) + 1) * kMinuteMs;

			for (int attempt = 0; attempt < 60 * 24 * 7; ++attempt) {
				const std::int64_t minuteOfDay =
					((candidateLocalMs / kMinuteMs) % (24 * 60) + (24 * 60)) % (24 * 60);
				const int hour = static_cast<int>(minuteOfDay / 60);
				const int minute = static_cast<int>(minuteOfDay % 60);
				if (MatchCronToken(parts[0], minute, 59) &&
					MatchCronToken(parts[1], hour, 23)) {
					std::int64_t candidate = candidateLocalMs - timezoneOffsetMs;
					const auto staggerMs = TryReadInt64Field(schedule, "staggerMs");
					if (staggerMs.has_value() && staggerMs.value() > 0) {
						candidate += ResolveStableCronOffsetMs(job, staggerMs.value());
					}
					if (candidate > nowMs) {
						return candidate;
					}
				}

				candidateLocalMs += kMinuteMs;
			}

			return nowMs + kMinuteMs;
		}
	}

	void CronTimerService::SetRuntimeExecutionAdapters(
		CronRuntimeExecutionAdapters adapters) {
		m_runtimeAdapters = std::move(adapters);
	}

	std::optional<std::int64_t> CronTimerService::ComputeNextRunAtMs(
		const CronJson& job,
		const std::int64_t nowMs) const {
		if (!job.value("enabled", true)) {
			return std::nullopt;
		}
		if (!job.contains("schedule") || !job["schedule"].is_object()) {
			return std::nullopt;
		}

		const auto retryPendingUntilMs = ReadRetryPendingUntilMs(job);
		if (retryPendingUntilMs.has_value() && retryPendingUntilMs.value() > nowMs) {
			return retryPendingUntilMs.value();
		}

		const CronJson& schedule = job["schedule"];
		const std::string kind =
			ToLowerCopy(TrimCopy(schedule.value("kind", std::string())));

		if (kind == "every") {
			return ComputeNextEvery(schedule, job, nowMs);
		}

		if (kind == "at") {
			return ComputeNextAt(schedule, job, nowMs);
		}

		if (kind == "cron") {
			return ComputeNextCron(schedule, job, nowMs);
		}

		return std::nullopt;
	}

	bool CronTimerService::RecomputeSchedules(
		CronJson& jobs,
		const std::int64_t nowMs,
		const CronRecomputeOptions& opts) const {
		bool changed = false;
		for (auto& job : jobs) {
			CronJson& state = EnsureStateObject(job);
			const auto currentNextRunAtMs = TryReadInt64Field(state, "nextRunAtMs");
			const bool hasRunningMarker = TryReadInt64Field(state, "runningAtMs").has_value();
			if (opts.preserveDueSlots &&
				currentNextRunAtMs.has_value() &&
				currentNextRunAtMs.value() > 0 &&
				currentNextRunAtMs.value() <= nowMs &&
				!hasRunningMarker) {
				continue;
			}

			try {
				const std::optional<std::int64_t> nextRunAtMs =
					ComputeNextRunAtMs(job, nowMs);
				const CronJson nextJson =
					nextRunAtMs.has_value() ? CronJson(nextRunAtMs.value()) : CronJson(nullptr);
				if (!state.contains("nextRunAtMs") || state["nextRunAtMs"] != nextJson) {
					state["nextRunAtMs"] = nextJson;
					changed = true;
				}

				if (state.contains("scheduleErrorCount") && !state["scheduleErrorCount"].is_null()) {
					state["scheduleErrorCount"] = nullptr;
					changed = true;
				}
			}
			catch (const std::exception& ex) {
				const std::int64_t errorCount =
					TryReadInt64Field(state, "scheduleErrorCount").value_or(0) + 1;
				state["scheduleErrorCount"] = errorCount;
				state["nextRunAtMs"] = nullptr;
				state["lastError"] = std::string("schedule error: ") + ex.what();
				changed = true;
				if (errorCount >= kMaxScheduleErrors) {
					job["enabled"] = false;
				}
			}
			catch (...) {
				const std::int64_t errorCount =
					TryReadInt64Field(state, "scheduleErrorCount").value_or(0) + 1;
				state["scheduleErrorCount"] = errorCount;
				state["nextRunAtMs"] = nullptr;
				state["lastError"] = "schedule error: unknown";
				changed = true;
				if (errorCount >= kMaxScheduleErrors) {
					job["enabled"] = false;
				}
			}
		}

		return changed;
	}

	std::int64_t CronTimerService::ComputeNextWakeAtMs(const CronJson& jobs) const {
		std::int64_t nextWakeAtMs = 0;
		for (const auto& job : jobs) {
			if (!job.value("enabled", true)) {
				continue;
			}
			if (!job.contains("state") || !job["state"].is_object()) {
				continue;
			}
			const auto maybeNext = TryReadInt64Field(job["state"], "nextRunAtMs");
			if (!maybeNext.has_value() || maybeNext.value() <= 0) {
				continue;
			}
			if (nextWakeAtMs <= 0 || maybeNext.value() < nextWakeAtMs) {
				nextWakeAtMs = maybeNext.value();
			}
		}
		return nextWakeAtMs;
	}

	std::size_t CronTimerService::PumpDueRuns(
		CronJson& jobs,
		CronJson& runs,
		const std::int64_t nowMs,
		const bool forceRunDue) const {
		std::size_t executed = 0;
		for (auto it = jobs.begin(); it != jobs.end();) {
			if (!(*it).is_object() || !(*it).value("enabled", true)) {
				++it;
				continue;
			}

			const std::string id = (*it).value("id", std::string());
			if (id.empty()) {
				++it;
				continue;
			}

			const std::optional<std::int64_t> nextRunAtMs = ReadNextRunAtMs(*it);
			if (!nextRunAtMs.has_value()) {
				++it;
				continue;
			}

			if (!forceRunDue && nextRunAtMs.value() > nowMs) {
				++it;
				continue;
			}

			CronJson& state = EnsureStateObject(*it);
			const RunOutcome outcome =
				EvaluateRunOutcome(*it, nowMs, m_runtimeAdapters);
			state["runningAtMs"] = nowMs;
			state["startedAtMs"] = nowMs;
			state["lastRunAtMs"] = nowMs;
			state["lastStatus"] = outcome.status;
			state["lastRunStatus"] = outcome.status;
			state["lastError"] = outcome.error.empty() ? CronJson(nullptr) : CronJson(outcome.error);
			state["lastErrorCategory"] = outcome.errorCategory.empty() ? CronJson(nullptr) : CronJson(outcome.errorCategory);
			state["lastDelivered"] = outcome.delivered;
			state["lastDeliveryStatus"] = outcome.deliveryStatus;
			state["lastDeliveryMode"] = outcome.deliveryMode.empty()
				? CronJson(nullptr)
				: CronJson(outcome.deliveryMode);
			state["lastDeliveryTarget"] = outcome.deliveryTarget.empty()
				? CronJson(nullptr)
				: CronJson(outcome.deliveryTarget);
			state["lastDeliveryAttempted"] = outcome.deliveryAttempted;
			state["lastDeliveryHttpStatus"] = outcome.deliveryHttpStatus > 0
				? CronJson(outcome.deliveryHttpStatus)
				: CronJson(nullptr);
			state["lastDeliveryError"] =
				outcome.deliveryStatus == "not-delivered"
				? CronJson(outcome.error)
				: CronJson(nullptr);
			state["lastFailureDestinationStatus"] = outcome.failureDestinationStatus;
			state["lastFailureDestinationTarget"] = outcome.failureDestinationTarget.empty()
				? CronJson(nullptr)
				: CronJson(outcome.failureDestinationTarget);
			state["lastFailureDestinationChannel"] = outcome.failureDestinationChannel.empty()
				? CronJson(nullptr)
				: CronJson(outcome.failureDestinationChannel);
			state["lastFailureDestinationAccountId"] = outcome.failureDestinationAccountId.empty()
				? CronJson(nullptr)
				: CronJson(outcome.failureDestinationAccountId);
			state["lastFailureDestinationAttempted"] = outcome.failureDestinationAttempted;
			state["lastFailureDestinationHttpStatus"] =
				outcome.failureDestinationHttpStatus > 0
				? CronJson(outcome.failureDestinationHttpStatus)
				: CronJson(nullptr);
			state["lastFailureDestinationError"] =
				outcome.failureDestinationError.empty()
				? CronJson(nullptr)
				: CronJson(outcome.failureDestinationError);
			state["lastDurationMs"] = 0;
			state["lastModel"] = outcome.model.empty()
				? CronJson(nullptr)
				: CronJson(outcome.model);
			state["lastProvider"] = outcome.provider.empty()
				? CronJson(nullptr)
				: CronJson(outcome.provider);
			state["runningAtMs"] = nullptr;

			const bool deleteAfterRun = (*it).value("deleteAfterRun", false);
			const std::string jobName = (*it).value("name", std::string());
			const std::string jobSessionKey = TrimCopy((*it).value("sessionKey", std::string()));
			const std::string jobSessionId =
				(*it).contains("sessionTarget") && (*it)["sessionTarget"].is_string()
				? TrimCopy((*it)["sessionTarget"].get<std::string>())
				: std::string();
			const std::string effectiveSessionKey =
				outcome.sessionKey.empty() ? jobSessionKey : outcome.sessionKey;
			const std::string effectiveSessionId =
				outcome.sessionId.empty() ? jobSessionId : outcome.sessionId;
			const std::string runId = BuildCronRunId(nowMs);
			const CronJson retry = (*it).value("retry", CronJson::object());
			const std::int64_t maxAttempts = (std::max)(
				static_cast<std::int64_t>(0),
				TryReadInt64Field(retry, "maxAttempts").value_or(0));
			const std::int64_t previousAttempt =
				TryReadInt64Field(state, "retryAttempt").value_or(0);
			const std::int64_t previousConsecutiveErrors =
				TryReadInt64Field(state, "consecutiveErrors").value_or(0);
			bool scheduledRetry = false;
			bool failureAlertTriggered = false;
			std::string failureAlertTargetSnapshot;
			std::string failureAlertChannelSnapshot;
			std::string failureAlertAccountIdSnapshot;
			std::int64_t failureAlertAtMs = 0;
			std::int64_t consecutiveErrors = 0;
			std::int64_t retryAttempt = previousAttempt;
			std::optional<std::int64_t> nextAfterRun;
			const bool heartbeatBusyRun =
				outcome.errorCategory == "heartbeat_busy" ||
				outcome.errorCategory == "heartbeat_busy_fallback";
			if (heartbeatBusyRun) {
				const std::int64_t previousBusyAttempts =
					TryReadInt64Field(state, "heartbeatBusyAttempts").value_or(0);
				if (outcome.errorCategory == "heartbeat_busy") {
					state["heartbeatBusyAttempts"] = previousBusyAttempts + 1;
					state["heartbeatFallbackWakeRequested"] = false;
					state["heartbeatFallbackWakeRequestedAtMs"] = CronJson(nullptr);
				}
				else {
					state["heartbeatBusyAttempts"] = 0;
					state["heartbeatFallbackWakeRequested"] = true;
					state["heartbeatFallbackWakeRequestedAtMs"] = nowMs;
				}
			}
			else {
				state["heartbeatBusyAttempts"] = 0;
				state["heartbeatFallbackWakeRequested"] = false;
				state["heartbeatFallbackWakeRequestedAtMs"] = CronJson(nullptr);
			}
			if (outcome.status == "error" && outcome.retryable && previousAttempt < maxAttempts) {
				retryAttempt = previousAttempt + 1;
				const std::int64_t delayMs = outcome.hasRetryDelayOverride
					? outcome.retryDelayOverrideMs
					: ResolveRetryDelayMs(retry, retryAttempt);
				const std::int64_t retryAtMs = nowMs + (std::max)(static_cast<std::int64_t>(0), delayMs);
				state["retryAttempt"] = retryAttempt;
				state["retryPendingUntilMs"] = retryAtMs;
				state["nextRunAtMs"] = retryAtMs;
				nextAfterRun = retryAtMs;
				scheduledRetry = true;
			}
			else {
				state["retryAttempt"] = 0;
				state["retryPendingUntilMs"] = CronJson(nullptr);
			}

			if (outcome.status == "error") {
				consecutiveErrors = previousConsecutiveErrors + 1;
				state["consecutiveErrors"] = consecutiveErrors;
				auto readOptionalStateString = [](const CronJson& node, const char* key)
					-> std::string {
					if (!node.contains(key) || node[key].is_null() || !node[key].is_string()) {
						return std::string();
					}
					return TrimCopy(node[key].get<std::string>());
				};
				const std::string previousFailureAlertMode = ToLowerCopy(
					readOptionalStateString(state, "lastFailureAlertMode"));
				const std::string previousFailureAlertTarget =
					readOptionalStateString(state, "lastFailureAlertTarget");
				const std::string previousFailureAlertChannel = ToLowerCopy(
					readOptionalStateString(state, "lastFailureAlertChannel"));
				const std::string previousFailureAlertAccountId =
					readOptionalStateString(state, "lastFailureAlertAccountId");
				state["failureAlertSuppressed"] = false;
				state["failureAlertSuppressedReason"] = nullptr;
				state["lastFailureAlertMode"] = nullptr;
				state["lastFailureAlertChannel"] = nullptr;
				state["lastFailureAlertAccountId"] = nullptr;
				const bool bestEffortDelivery = IsBestEffortDelivery(*it);

				if ((*it).contains("failureAlert") && (*it)["failureAlert"].is_boolean() && !(*it)["failureAlert"].get<bool>()) {
					state["lastFailureAlertAtMs"] = CronJson(nullptr);
					state["failureAlertSuppressed"] = true;
					state["failureAlertSuppressedReason"] = "disabled";
				}
				else if (bestEffortDelivery) {
					state["lastFailureAlertAtMs"] = CronJson(nullptr);
					state["failureAlertSuppressed"] = true;
					state["failureAlertSuppressedReason"] = "best_effort_delivery";
				}
				else {
					const std::int64_t alertAfter = ResolveFailureAlertAfter(*it);
					const std::int64_t cooldownMs = ResolveFailureAlertCooldownMs(*it);
					if (alertAfter <= 0) {
						state["lastFailureAlertAtMs"] = CronJson(nullptr);
						state["failureAlertSuppressed"] = true;
						state["failureAlertSuppressedReason"] = "not_configured";
						state["lastFailureAlertMode"] = nullptr;
						state["lastFailureAlertTarget"] = CronJson(nullptr);
					}
					else {
						std::string failureAlertMode = kFailureAlertModeAnnounce;
						std::string failureAlertChannel = "last";
						std::string failureAlertAccountId;
						std::string failureAlertTarget;
						std::string deliveryTargetFallback;
						std::string deliveryChannelFallback = "last";
						std::string deliveryAccountIdFallback;
						if ((*it).contains("delivery") && (*it)["delivery"].is_object()) {
							deliveryTargetFallback =
								TrimCopy((*it)["delivery"].value("to", std::string()));
							deliveryChannelFallback =
								TrimCopy((*it)["delivery"].value("channel", std::string("last")));
							if (deliveryChannelFallback.empty()) {
								deliveryChannelFallback = "last";
							}
							deliveryAccountIdFallback =
								TrimCopy((*it)["delivery"].value("accountId", std::string()));
						}

						if ((*it).contains("failureAlert") && (*it)["failureAlert"].is_object()) {
							failureAlertMode = ToLowerCopy(
								TrimCopy((*it)["failureAlert"].value("mode", std::string(kFailureAlertModeAnnounce))));
							if (failureAlertMode != kFailureAlertModeAnnounce &&
								failureAlertMode != kFailureAlertModeWebhook) {
								failureAlertMode = kFailureAlertModeAnnounce;
							}
							failureAlertTarget =
								TrimCopy((*it)["failureAlert"].value("to", std::string()));
							failureAlertChannel =
								TrimCopy((*it)["failureAlert"].value("channel", std::string()));
							failureAlertAccountId =
								TrimCopy((*it)["failureAlert"].value("accountId", std::string()));
						}

						if (failureAlertTarget.empty()) {
							failureAlertTarget = deliveryTargetFallback;
						}

						if (failureAlertMode == kFailureAlertModeAnnounce) {
							if (failureAlertChannel.empty()) {
								failureAlertChannel = deliveryChannelFallback;
							}
							if (failureAlertChannel.empty()) {
								failureAlertChannel = "last";
							}
							if (failureAlertAccountId.empty()) {
								failureAlertAccountId = deliveryAccountIdFallback;
							}
						}
						else {
							failureAlertChannel.clear();
						}

					const std::string previousFailureAlertRouteTarget =
						CanonicalizeFailureAlertRouteTarget(
							previousFailureAlertMode,
							previousFailureAlertTarget);
					const std::string currentFailureAlertRouteTarget =
						CanonicalizeFailureAlertRouteTarget(
							failureAlertMode,
							failureAlertTarget);
					const bool failureAlertRouteChanged =
						previousFailureAlertMode != failureAlertMode ||
						previousFailureAlertRouteTarget != currentFailureAlertRouteTarget ||
						previousFailureAlertChannel != ToLowerCopy(TrimCopy(failureAlertChannel)) ||
						previousFailureAlertAccountId != failureAlertAccountId;
					failureAlertTargetSnapshot = failureAlertTarget;
					failureAlertChannelSnapshot = failureAlertChannel;
					failureAlertAccountIdSnapshot = failureAlertAccountId;
					state["lastFailureAlertTarget"] = failureAlertTarget.empty()
						? CronJson(nullptr)
						: CronJson(failureAlertTarget);
					state["lastFailureAlertChannel"] = failureAlertChannel.empty()
						? CronJson(nullptr)
						: CronJson(failureAlertChannel);
					state["lastFailureAlertAccountId"] = failureAlertAccountId.empty()
						? CronJson(nullptr)
						: CronJson(failureAlertAccountId);

					if (failureAlertMode == kFailureAlertModeWebhook &&
						!StartsWithHttpScheme(failureAlertTarget)) {
						state["failureAlertSuppressed"] = true;
						state["failureAlertSuppressedReason"] = "invalid_webhook_target";
						state["lastFailureAlertAtMs"] = CronJson(nullptr);
						state["lastFailureAlertMode"] = failureAlertMode;
						state["lastFailureAlertTarget"] = failureAlertTarget.empty()
							? CronJson(nullptr)
							: CronJson(failureAlertTarget);
						state["lastFailureAlertChannel"] = failureAlertChannel.empty()
							? CronJson(nullptr)
							: CronJson(failureAlertChannel);
						state["lastFailureAlertAccountId"] = failureAlertAccountId.empty()
							? CronJson(nullptr)
							: CronJson(failureAlertAccountId);
					}
					else {
						state["lastFailureAlertMode"] = failureAlertMode;
						const std::int64_t lastAlertAtMs =
							TryReadInt64Field(state, "lastFailureAlertAtMs").value_or(0);
						const bool cooldownOpen =
							failureAlertRouteChanged ||
							lastAlertAtMs <= 0 ||
							(nowMs - lastAlertAtMs) >= cooldownMs;
						if (consecutiveErrors >= alertAfter && cooldownOpen) {
							failureAlertTriggered = true;
							failureAlertAtMs = nowMs;
							state["lastFailureAlertAtMs"] = nowMs;
						}
						else if (consecutiveErrors < alertAfter) {
							state["failureAlertSuppressed"] = true;
							state["failureAlertSuppressedReason"] = "threshold_not_met";
						}
						else if (!cooldownOpen) {
							state["failureAlertSuppressed"] = true;
							state["failureAlertSuppressedReason"] = "cooldown_active";
						}
					}
				}
				}
			}
			else {
				state["consecutiveErrors"] = 0;
				state["lastFailureAlertAtMs"] = CronJson(nullptr);
				state["failureAlertSuppressed"] = false;
				state["failureAlertSuppressedReason"] = nullptr;
			}

			if (!deleteAfterRun && !scheduledRetry) {
				nextAfterRun = ComputeNextRunAtMs(*it, nowMs);
				state["nextRunAtMs"] =
					nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr);
				(*it)["updatedAtMs"] = nowMs;
			}
			state["lastRunId"] = runId;
			state["lastScheduledForMs"] = nextRunAtMs.value();
			state["lastFinishedAtMs"] = nowMs;
			state["endedAtMs"] = nowMs;
			state["lastRunTimedOut"] = outcome.timedOut;
			state["lastRunAborted"] = false;
			state["lastTaskLedgerRuntime"] = "cron";
			state["lastTaskLedgerPhase"] = "terminal";
			state["lastTaskLedgerStatus"] = outcome.status;
			state["lastTaskLedgerDisposition"] = "scheduled";
			state["lastTaskLedgerTerminal"] = true;

			runs.push_back({
				{ "ts", nowMs },
				{ "jobId", id },
				{ "action", "finished" },
				{ "status", outcome.status },
				{ "summary", outcome.summary },
				{ "error", outcome.error.empty() ? CronJson(nullptr) : CronJson(outcome.error) },
				{ "errorCategory", outcome.errorCategory.empty() ? CronJson(nullptr) : CronJson(outcome.errorCategory) },
				{ "deliveryStatus", outcome.deliveryStatus },
				{ "deliveryMode", outcome.deliveryMode.empty() ? CronJson(nullptr) : CronJson(outcome.deliveryMode) },
				{ "deliveryTarget", outcome.deliveryTarget.empty() ? CronJson(nullptr) : CronJson(outcome.deliveryTarget) },
				{ "deliveryChannel", outcome.deliveryChannel.empty() ? CronJson(nullptr) : CronJson(outcome.deliveryChannel) },
				{ "deliveryAccountId", outcome.deliveryAccountId.empty() ? CronJson(nullptr) : CronJson(outcome.deliveryAccountId) },
				{ "deliveryAttempted", outcome.deliveryAttempted },
				{ "deliveryHttpStatus", outcome.deliveryHttpStatus > 0
					? CronJson(outcome.deliveryHttpStatus)
					: CronJson(nullptr) },
				{ "deliveryError", outcome.deliveryStatus == "not-delivered"
					? (outcome.error.empty() ? CronJson(nullptr) : CronJson(outcome.error))
					: CronJson(nullptr) },
				{ "failureDestinationStatus", outcome.failureDestinationStatus },
				{ "failureDestinationTarget", outcome.failureDestinationTarget.empty()
					? CronJson(nullptr)
					: CronJson(outcome.failureDestinationTarget) },
				{ "failureDestinationChannel", outcome.failureDestinationChannel.empty()
					? CronJson(nullptr)
					: CronJson(outcome.failureDestinationChannel) },
				{ "failureDestinationAccountId", outcome.failureDestinationAccountId.empty()
					? CronJson(nullptr)
					: CronJson(outcome.failureDestinationAccountId) },
				{ "failureDestinationAttempted", outcome.failureDestinationAttempted },
				{ "failureDestinationHttpStatus",
					outcome.failureDestinationHttpStatus > 0
					? CronJson(outcome.failureDestinationHttpStatus)
					: CronJson(nullptr) },
				{ "failureDestinationMode", outcome.failureDestinationMode.empty()
					? CronJson(nullptr)
					: CronJson(outcome.failureDestinationMode) },
				{ "failureDestinationError", outcome.failureDestinationError.empty()
					? CronJson(nullptr)
					: CronJson(outcome.failureDestinationError) },
				{ "delivered", outcome.delivered },
				{ "consecutiveErrors", consecutiveErrors },
				{ "failureAlertTriggered", failureAlertTriggered },
				{ "failureAlertSuppressed", state.value("failureAlertSuppressed", false) },
				{ "failureAlertSuppressedReason",
					state.contains("failureAlertSuppressedReason")
					? state["failureAlertSuppressedReason"]
					: CronJson(nullptr) },
				{ "failureAlertMode",
					state.contains("lastFailureAlertMode")
					? state["lastFailureAlertMode"]
					: CronJson(nullptr) },
				{ "failureAlertTarget",
					failureAlertTargetSnapshot.empty()
					? CronJson(nullptr)
					: CronJson(failureAlertTargetSnapshot) },
				{ "failureAlertChannel",
					failureAlertChannelSnapshot.empty()
					? CronJson(nullptr)
					: CronJson(failureAlertChannelSnapshot) },
				{ "failureAlertAccountId",
					failureAlertAccountIdSnapshot.empty()
					? CronJson(nullptr)
					: CronJson(failureAlertAccountIdSnapshot) },
				{ "failureAlertAtMs", failureAlertTriggered ? CronJson(failureAlertAtMs) : CronJson(nullptr) },
				{ "retryAttempt", retryAttempt },
				{ "retryScheduled", scheduledRetry },
				{ "retryScheduledAtMs", scheduledRetry && nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr) },
				{ "durationMs", 0 },
				{ "timedOut", outcome.timedOut },
				{ "aborted", false },
				{ "taskLedgerRuntime", "cron" },
				{ "taskLedgerPhase", "terminal" },
				{ "taskLedgerStatus", outcome.status },
				{ "taskLedgerDisposition", "scheduled" },
				{ "taskLedgerTerminal", true },
				{ "startedAtMs", nowMs },
				{ "endedAtMs", nowMs },
				{ "scheduledForMs", nextRunAtMs.value() },
				{ "runAtMs", nowMs },
				{ "nextRunAtMs", deleteAfterRun ? CronJson(nullptr) : (nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr)) },
				{ "sessionKey", effectiveSessionKey.empty() ? CronJson(nullptr) : CronJson(effectiveSessionKey) },
				{ "sessionId", effectiveSessionId.empty() ? CronJson(nullptr) : CronJson(effectiveSessionId) },
				{ "model", outcome.model.empty() ? CronJson(nullptr) : CronJson(outcome.model) },
				{ "provider", outcome.provider.empty() ? CronJson(nullptr) : CronJson(outcome.provider) },
				{ "usage", outcome.usageAvailable
					? CronJson({
						{ "promptTokens", outcome.usagePromptTokens },
						{ "completionTokens", outcome.usageCompletionTokens },
						{ "totalTokens", outcome.usageTotalTokens }
					})
					: CronJson(nullptr) },
				{ "jobName", jobName },
				{ "runId", runId }
			});
			++executed;

			if (deleteAfterRun && !scheduledRetry) {
				it = jobs.erase(it);
				continue;
			}

			++it;
		}

		return executed;
	}

} // namespace blazeclaw::cron
