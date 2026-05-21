#include "pch.h"

#include "CronTimerService.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include <winhttp.h>

#pragma comment(lib, "Winhttp.lib")

namespace {
	constexpr std::int64_t kSecondMs = 1'000;
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

		void QueueScheduleAutoDisableNotification(
			const CronJson& job,
			const std::string& scheduleAutoDisableContextKey,
			const std::string& notifyText,
			std::vector<CronScheduleNotificationEvent>* notifications) {
			if (notifications == nullptr || notifyText.empty()) {
				return;
			}

			CronScheduleNotificationEvent event;
			event.text = notifyText;
			event.contextKey = scheduleAutoDisableContextKey;
			event.heartbeatWakeReason = scheduleAutoDisableContextKey;
			if (job.contains("agentId") && job["agentId"].is_string()) {
				event.agentId = TrimCopy(job["agentId"].get<std::string>());
			}
			if (job.contains("sessionKey") && job["sessionKey"].is_string()) {
				event.sessionKey = TrimCopy(job["sessionKey"].get<std::string>());
			}
			notifications->push_back(std::move(event));
		}

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
				const std::string canonical =
					CanonicalizeHttpUrlForRouteCompare(target);
				const std::size_t schemePos = canonical.find("://");
				if (schemePos == std::string::npos) {
					return canonical;
				}

				const std::string scheme = canonical.substr(0, schemePos);
				const std::size_t authorityStart = schemePos + 3;
				const std::size_t authorityEnd =
					canonical.find_first_of("/?#", authorityStart);
				if (authorityEnd == std::string::npos) {
					return canonical;
				}

				const std::string authority =
					canonical.substr(authorityStart, authorityEnd - authorityStart);
				const std::size_t atPos = authority.rfind('@');
				const std::string userInfo = atPos == std::string::npos
					? std::string()
					: authority.substr(0, atPos + 1);
				const std::string hostPort = atPos == std::string::npos
					? authority
					: authority.substr(atPos + 1);

				if (hostPort.empty()) {
					return canonical;
				}

				std::string host = hostPort;
				std::string port;
				if (hostPort.front() == '[') {
					const std::size_t closeBracket = hostPort.find(']');
					if (closeBracket != std::string::npos) {
						host = hostPort.substr(0, closeBracket + 1);
						port = hostPort.substr(closeBracket + 1);
					}
				}
				else {
					const std::size_t colonPos = hostPort.rfind(':');
					if (colonPos != std::string::npos) {
						host = hostPort.substr(0, colonPos);
						port = hostPort.substr(colonPos);
					}
				}

				if ((scheme == "http" && port == ":80") ||
					(scheme == "https" && port == ":443")) {
					std::string normalized = canonical;
					normalized.replace(
						authorityStart,
						authorityEnd - authorityStart,
						userInfo + host);
					return normalized;
				}

				return canonical;
			}

			return TrimCopy(target);
		}

		std::string CanonicalizeDeliveryRouteTargetForCompare(
			const std::string& mode,
			const std::string& target) {
			if (ToLowerCopy(TrimCopy(mode)) == "webhook") {
				return CanonicalizeFailureAlertRouteTarget("webhook", target);
			}

			return TrimCopy(target);
		}

		bool IsTransportDispatchEnabled(const CronJson& node) {
			if (node.contains("transportDispatch") && node["transportDispatch"].is_boolean()) {
				return node["transportDispatch"].get<bool>();
			}

			char* envValueRaw = nullptr;
			size_t envValueLen = 0;
			if (_dupenv_s(
				&envValueRaw,
				&envValueLen,
				"BLAZECLAW_CRON_DEFAULT_WEBHOOK_TRANSPORT_DISPATCH") != 0 ||
				envValueRaw == nullptr) {
				return false;
			}

			const std::string normalized = ToLowerCopy(TrimCopy(envValueRaw));
			free(envValueRaw);
			return normalized == "1" ||
				normalized == "true" ||
				normalized == "yes" ||
				normalized == "on";
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
			std::string runtimeExecutionPath = "none";
			bool runtimeAdapterRegistered = false;
			bool runtimeAdapterInvoked = false;
			bool runtimeHandled = false;
			bool simulationFallbackUsed = false;
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
			std::string failureAlertStatus;
			bool failureAlertAttempted = false;
			std::int64_t failureAlertHttpStatus = 0;
			std::string failureAlertError;
			std::string errorCategory;
			bool timedOut = false;
			bool aborted = false;
			bool skipDeliverySimulation = false;
			bool skipPrimaryDeliverySimulation = false;
			bool skipFailureDestinationSimulation = false;
			bool runtimeProjectedTransport = false;
			bool runtimeProjectedPrimaryTransport = false;
			bool runtimeProjectedFailureDestinationTransport = false;
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
			bool projectedPrimaryTransportFields = false;
			bool projectedFailureTransportFields = false;
			bool hasDeliveredField = false;
			bool hasDeliveryStatusField = false;
			bool hasFailureDestinationStatusField = false;
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
			if (runtimeResult.contains("aborted") && runtimeResult["aborted"].is_boolean()) {
				outcome.aborted = runtimeResult["aborted"].get<bool>();
			}
			if (outcome.timedOut && outcome.errorCategory.empty()) {
				outcome.errorCategory = "timeout";
			}
			if (outcome.aborted && outcome.errorCategory.empty()) {
				outcome.errorCategory = "aborted";
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
				hasDeliveredField = true;
				projectedPrimaryTransportFields = true;
			}
			if (runtimeResult.contains("deliveryStatus") && runtimeResult["deliveryStatus"].is_string()) {
				outcome.deliveryStatus = ToLowerCopy(
					TrimCopy(runtimeResult["deliveryStatus"].get<std::string>()));
				hasDeliveryStatusField = true;
				projectedPrimaryTransportFields = true;
			}
			if (runtimeResult.contains("deliveryMode") && runtimeResult["deliveryMode"].is_string()) {
				outcome.deliveryMode = ToLowerCopy(
					TrimCopy(runtimeResult["deliveryMode"].get<std::string>()));
				projectedPrimaryTransportFields = true;
			}
			if (runtimeResult.contains("deliveryTarget") && runtimeResult["deliveryTarget"].is_string()) {
				outcome.deliveryTarget = TrimCopy(runtimeResult["deliveryTarget"].get<std::string>());
				projectedPrimaryTransportFields = true;
			}
			if (outcome.deliveryMode.empty() &&
				!outcome.deliveryTarget.empty() &&
				StartsWithHttpScheme(outcome.deliveryTarget)) {
				outcome.deliveryMode = "webhook";
				projectedPrimaryTransportFields = true;
			}
			if (runtimeResult.contains("deliveryChannel") && runtimeResult["deliveryChannel"].is_string()) {
				outcome.deliveryChannel = TrimCopy(runtimeResult["deliveryChannel"].get<std::string>());
				projectedPrimaryTransportFields = true;
			}
			if (runtimeResult.contains("deliveryAccountId") && runtimeResult["deliveryAccountId"].is_string()) {
				outcome.deliveryAccountId = TrimCopy(runtimeResult["deliveryAccountId"].get<std::string>());
				projectedPrimaryTransportFields = true;
			}
			if (runtimeResult.contains("deliveryAttempted") && runtimeResult["deliveryAttempted"].is_boolean()) {
				outcome.deliveryAttempted = runtimeResult["deliveryAttempted"].get<bool>();
				projectedPrimaryTransportFields = true;
			}
			if (runtimeResult.contains("deliveryHttpStatus")) {
				const auto maybeStatus = TryReadInt64Field(runtimeResult, "deliveryHttpStatus");
				if (maybeStatus.has_value()) {
					outcome.deliveryHttpStatus = maybeStatus.value();
					projectedPrimaryTransportFields = true;
				}
			}

			if (runtimeResult.contains("failureDestinationStatus") &&
				runtimeResult["failureDestinationStatus"].is_string()) {
				outcome.failureDestinationStatus = ToLowerCopy(
					TrimCopy(runtimeResult["failureDestinationStatus"].get<std::string>()));
				hasFailureDestinationStatusField = true;
				projectedFailureTransportFields = true;
			}
			if (runtimeResult.contains("failureDestinationMode") &&
				runtimeResult["failureDestinationMode"].is_string()) {
				outcome.failureDestinationMode = ToLowerCopy(
					TrimCopy(runtimeResult["failureDestinationMode"].get<std::string>()));
				projectedFailureTransportFields = true;
			}
			if (runtimeResult.contains("failureDestinationTarget") &&
				runtimeResult["failureDestinationTarget"].is_string()) {
				outcome.failureDestinationTarget =
					TrimCopy(runtimeResult["failureDestinationTarget"].get<std::string>());
				projectedFailureTransportFields = true;
			}
			if (outcome.failureDestinationMode.empty() &&
				!outcome.failureDestinationTarget.empty() &&
				StartsWithHttpScheme(outcome.failureDestinationTarget)) {
				outcome.failureDestinationMode = "webhook";
				projectedFailureTransportFields = true;
			}
			if (runtimeResult.contains("failureDestinationChannel") &&
				runtimeResult["failureDestinationChannel"].is_string()) {
				outcome.failureDestinationChannel =
					TrimCopy(runtimeResult["failureDestinationChannel"].get<std::string>());
				projectedFailureTransportFields = true;
			}
			if (runtimeResult.contains("failureDestinationAccountId") &&
				runtimeResult["failureDestinationAccountId"].is_string()) {
				outcome.failureDestinationAccountId =
					TrimCopy(runtimeResult["failureDestinationAccountId"].get<std::string>());
				projectedFailureTransportFields = true;
			}
			if (runtimeResult.contains("failureDestinationAttempted") &&
				runtimeResult["failureDestinationAttempted"].is_boolean()) {
				outcome.failureDestinationAttempted =
					runtimeResult["failureDestinationAttempted"].get<bool>();
				projectedFailureTransportFields = true;
			}
			if (runtimeResult.contains("failureDestinationHttpStatus")) {
				const auto maybeFailureStatus =
					TryReadInt64Field(runtimeResult, "failureDestinationHttpStatus");
				if (maybeFailureStatus.has_value()) {
					outcome.failureDestinationHttpStatus = maybeFailureStatus.value();
					projectedFailureTransportFields = true;
				}
			}
			if (runtimeResult.contains("failureDestinationError") &&
				runtimeResult["failureDestinationError"].is_string()) {
				outcome.failureDestinationError =
					TrimCopy(runtimeResult["failureDestinationError"].get<std::string>());
				projectedFailureTransportFields = true;
			}

			outcome.runtimeProjectedPrimaryTransport = projectedPrimaryTransportFields;
			outcome.runtimeProjectedFailureDestinationTransport = projectedFailureTransportFields;
			outcome.runtimeProjectedTransport =
				projectedPrimaryTransportFields || projectedFailureTransportFields;

			if (projectedPrimaryTransportFields) {
				if (hasDeliveryStatusField) {
					if (!hasDeliveredField) {
						outcome.delivered = outcome.deliveryStatus == "delivered";
					}
				}
				else {
					if (hasDeliveredField) {
						if (outcome.delivered) {
							outcome.deliveryStatus = "delivered";
						}
						else if (outcome.deliveryAttempted) {
							outcome.deliveryStatus = "not-delivered";
						}
						else if (!outcome.deliveryTarget.empty() ||
							!outcome.deliveryMode.empty()) {
							outcome.deliveryStatus = "unknown";
						}
					}
					else if (outcome.deliveryAttempted ||
						!outcome.deliveryTarget.empty() ||
						!outcome.deliveryMode.empty()) {
						outcome.deliveryStatus = "unknown";
					}
				}
			}

			if (projectedFailureTransportFields &&
				!hasFailureDestinationStatusField) {
				if (outcome.failureDestinationAttempted ||
					!outcome.failureDestinationTarget.empty() ||
					!outcome.failureDestinationMode.empty()) {
					outcome.failureDestinationStatus = "unknown";
				}
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

		bool HasScheduledNextRunAtMs(const std::optional<std::int64_t>& nextRunAtMs) {
			return nextRunAtMs.has_value() && nextRunAtMs.value() > 0;
		}

		bool NormalizeJobTickState(CronJson& job, const std::int64_t nowMs) {
			bool changed = false;
			CronJson& state = EnsureStateObject(job);
			if (job.contains("schedule") && job["schedule"].is_object()) {
				CronJson& schedule = job["schedule"];
				const std::string scheduleKind =
					ToLowerCopy(TrimCopy(schedule.value("kind", std::string())));
				if (scheduleKind == "every") {
					const auto anchorMs = TryReadInt64Field(schedule, "anchorMs");
					if (!anchorMs.has_value() || anchorMs.value() < 0) {
						const auto createdAtMs = TryReadInt64Field(job, "createdAtMs");
						schedule["anchorMs"] = createdAtMs.has_value() && createdAtMs.value() >= 0
							? CronJson(createdAtMs.value())
							: CronJson(nowMs);
						changed = true;
					}
				}
			}

			if (!job.value("enabled", true)) {
				if (state.contains("nextRunAtMs") && !state["nextRunAtMs"].is_null()) {
					state["nextRunAtMs"] = nullptr;
					changed = true;
				}
				if (TryReadInt64Field(state, "runningAtMs").has_value()) {
					state["runningAtMs"] = nullptr;
					changed = true;
				}
				return changed;
			}

			const auto runningAtMs = TryReadInt64Field(state, "runningAtMs");
			if (runningAtMs.has_value() &&
				nowMs - runningAtMs.value() > kCronStuckRunMs) {
				state["runningAtMs"] = nullptr;
				changed = true;
			}

			return changed;
		}

		std::string ReadScheduleKind(const CronJson& job) {
			if (!job.contains("schedule") || !job["schedule"].is_object()) {
				return std::string();
			}

			return ToLowerCopy(TrimCopy(job["schedule"].value("kind", std::string())));
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

		bool IsWildcardCronToken(const std::string& tokenRaw) {
			const std::string token = TrimCopy(tokenRaw);
			if (token.empty() || token == "*" || token == "?") {
				return true;
			}

			if (token.rfind("*/", 0) == 0) {
				try {
					return std::stoi(token.substr(2)) == 1;
				}
				catch (...) {
					return false;
				}
			}

			return false;
		}

		bool TryParseStrictNonNegativeInt(const std::string& raw, int& valueOut) {
			const std::string token = TrimCopy(raw);
			if (token.empty()) {
				return false;
			}

			for (const char ch : token) {
				if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
					return false;
				}
			}

			try {
				valueOut = std::stoi(token);
			}
			catch (...) {
				return false;
			}

			return true;
		}

		std::optional<int> TryResolveCronMonthAlias(const std::string& raw) {
			const std::string token = ToLowerCopy(TrimCopy(raw));
			if (token.empty()) {
				return std::nullopt;
			}

			static const std::unordered_map<std::string, int> kMonthAliases = {
				{ "jan", 1 }, { "january", 1 },
				{ "feb", 2 }, { "february", 2 },
				{ "mar", 3 }, { "march", 3 },
				{ "apr", 4 }, { "april", 4 },
				{ "may", 5 },
				{ "jun", 6 }, { "june", 6 },
				{ "jul", 7 }, { "july", 7 },
				{ "aug", 8 }, { "august", 8 },
				{ "sep", 9 }, { "sept", 9 }, { "september", 9 },
				{ "oct", 10 }, { "october", 10 },
				{ "nov", 11 }, { "november", 11 },
				{ "dec", 12 }, { "december", 12 }
			};

			const auto it = kMonthAliases.find(token);
			if (it == kMonthAliases.end()) {
				return std::nullopt;
			}

			return it->second;
		}

		std::optional<int> TryResolveCronDayOfWeekAlias(const std::string& raw) {
			const std::string token = ToLowerCopy(TrimCopy(raw));
			if (token.empty()) {
				return std::nullopt;
			}

			static const std::unordered_map<std::string, int> kDayAliases = {
				{ "sun", 0 }, { "sunday", 0 },
				{ "mon", 1 }, { "monday", 1 },
				{ "tue", 2 }, { "tues", 2 }, { "tuesday", 2 },
				{ "wed", 3 }, { "wednesday", 3 },
				{ "thu", 4 }, { "thur", 4 }, { "thurs", 4 }, { "thursday", 4 },
				{ "fri", 5 }, { "friday", 5 },
				{ "sat", 6 }, { "saturday", 6 }
			};

			const auto it = kDayAliases.find(token);
			if (it == kDayAliases.end()) {
				return std::nullopt;
			}

			return it->second;
		}

		bool TryParseCronFieldValue(
			const std::string& raw,
			int& valueOut,
			const std::function<std::optional<int>(const std::string&)>& aliasResolver) {
			if (aliasResolver) {
				const auto aliasValue = aliasResolver(raw);
				if (aliasValue.has_value()) {
					valueOut = aliasValue.value();
					return true;
				}
			}

			return TryParseStrictNonNegativeInt(raw, valueOut);
		}

		bool MatchCronTokenSegment(
			const std::string& segmentRaw,
			const int value,
			const int minValue,
			const int maxValue,
			const bool normalizeSevenToZero,
			const std::function<std::optional<int>(const std::string&)>& aliasResolver = {}) {
			std::string segment = TrimCopy(segmentRaw);
			if (segment.empty()) {
				return false;
			}

			int step = 1;
			const std::size_t slashPos = segment.find('/');
			if (slashPos != std::string::npos) {
				if (segment.find('/', slashPos + 1) != std::string::npos) {
					return false;
				}

				const std::string stepPart = TrimCopy(segment.substr(slashPos + 1));
				if (!TryParseStrictNonNegativeInt(stepPart, step) || step <= 0) {
					return false;
				}

				segment = TrimCopy(segment.substr(0, slashPos));
				if (segment.empty()) {
					return false;
				}
			}

			auto normalizeValue = [&](int parsed) {
				if (normalizeSevenToZero && parsed == 7) {
					return 0;
				}
				return parsed;
			};

			const int normalizedCurrent = normalizeValue(value);
			int rangeStart = minValue;
			int rangeEnd = maxValue;

			if (segment != "*" && segment != "?") {
				const std::size_t dashPos = segment.find('-');
				if (dashPos != std::string::npos) {
					if (segment.find('-', dashPos + 1) != std::string::npos) {
						return false;
					}

					int parsedStart = 0;
					int parsedEnd = 0;
					if (!TryParseCronFieldValue(segment.substr(0, dashPos), parsedStart, aliasResolver) ||
						!TryParseCronFieldValue(segment.substr(dashPos + 1), parsedEnd, aliasResolver)) {
						return false;
					}

					rangeStart = normalizeValue(parsedStart);
					rangeEnd = normalizeValue(parsedEnd);
				}
				else {
					int parsedValue = 0;
					if (!TryParseCronFieldValue(segment, parsedValue, aliasResolver)) {
						return false;
					}

					rangeStart = normalizeValue(parsedValue);
					rangeEnd = rangeStart;
				}
			}

			if (rangeStart < minValue ||
				rangeStart > maxValue ||
				rangeEnd < minValue ||
				rangeEnd > maxValue) {
				return false;
			}

			if (rangeStart <= rangeEnd) {
				if (normalizedCurrent < rangeStart || normalizedCurrent > rangeEnd) {
					return false;
				}

				return ((normalizedCurrent - rangeStart) % step) == 0;
			}

			const int span = maxValue - minValue + 1;
			if (span <= 0) {
				return false;
			}

			const int rotatedCurrent = normalizedCurrent >= rangeStart
				? normalizedCurrent - rangeStart
				: normalizedCurrent + span - rangeStart;
			const int rotatedEnd = rangeEnd + span - rangeStart;

			if (rotatedCurrent < 0 || rotatedCurrent > rotatedEnd) {
				return false;
			}

			return (rotatedCurrent % step) == 0;
		}

		bool MatchCronFieldToken(
			const std::string& tokenRaw,
			const int value,
			const int minValue,
			const int maxValue,
			const bool normalizeSevenToZero,
			const std::function<std::optional<int>(const std::string&)>& aliasResolver = {}) {
			const std::string token = TrimCopy(tokenRaw);
			if (token.empty()) {
				return true;
			}

			std::istringstream parts(token);
			std::string item;
			bool sawToken = false;
			while (std::getline(parts, item, ',')) {
				const std::string trimmed = TrimCopy(item);
				if (trimmed.empty()) {
					continue;
				}

				sawToken = true;
				const bool matched = MatchCronTokenSegment(
					trimmed,
					value,
					minValue,
					maxValue,
					normalizeSevenToZero,
					aliasResolver);
				if (matched) {
					return true;
				}

				const bool isWildcardSegment =
					trimmed == "*" ||
					(trimmed.rfind("*/", 0) == 0);
				if (!isWildcardSegment) {
					const std::size_t slashPos = trimmed.find('/');
					const std::size_t dashPos = trimmed.find('-');
					if (slashPos == std::string::npos && dashPos == std::string::npos) {
						int parsedValue = 0;
						if (TryParseCronFieldValue(trimmed, parsedValue, aliasResolver)) {
							if (normalizeSevenToZero && parsedValue == 7) {
								parsedValue = 0;
							}
							if (parsedValue >= minValue && parsedValue <= maxValue) {
								continue;
							}
						}
					}
				}

				if (!matched) {
					const bool validSegment =
						MatchCronTokenSegment(
							trimmed,
							minValue,
							minValue,
							maxValue,
							normalizeSevenToZero,
							aliasResolver) ||
						MatchCronTokenSegment(
							trimmed,
							maxValue,
							minValue,
							maxValue,
							normalizeSevenToZero,
							aliasResolver);
					if (!validSegment) {
						return false;
					}
				}
			}

			return !sawToken ? true : false;
		}

		bool MatchCronDayOfWeekToken(const std::string& tokenRaw, const int dayOfWeek) {
			return MatchCronFieldToken(
				tokenRaw,
				dayOfWeek,
				0,
				6,
				true,
				TryResolveCronDayOfWeekAlias);
		}

		bool MatchCronMonthToken(const std::string& tokenRaw, const int month) {
			return MatchCronFieldToken(
				tokenRaw,
				month,
				1,
				12,
				false,
				TryResolveCronMonthAlias);
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
			std::string payloadKind =
				ToLowerCopy(TrimCopy(payload.value("kind", std::string())));
			if (payloadKind.empty()) {
				if (payload.contains("message") && payload["message"].is_string()) {
					payloadKind = "agentturn";
				}
				else if (payload.contains("text") && payload["text"].is_string()) {
					payloadKind = "systemevent";
				}
			}
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

			const bool isolatedLikeTarget =
				sessionTarget == "isolated" ||
				sessionTarget.rfind("session:", 0) == 0 ||
				(sessionTarget == "current" && !resolvedSessionKey.empty());

			if (sessionTarget == "main" && payloadKind == "agentturn") {
				const std::string message =
					TrimCopy(payload.value("message", std::string()));
				if (!message.empty()) {
					payloadKind = "systemevent";
				}
			}

			if (isolatedLikeTarget && payloadKind == "systemevent") {
				const std::string text =
					TrimCopy(payload.value("text", std::string()));
				if (!text.empty()) {
					payloadKind = "agentturn";
				}
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
			bool runtimeAdapterRegistered = false;
			if (payloadKind == "systemevent") {
				runtimeAdapter = &adapters.mainSession;
				runtimeAdapterRegistered = static_cast<bool>(adapters.mainSession);
			}
			else if (payloadKind == "agentturn") {
				runtimeAdapter = &adapters.isolatedSession;
				runtimeAdapterRegistered = static_cast<bool>(adapters.isolatedSession);
			}
			outcome.runtimeAdapterRegistered = runtimeAdapterRegistered;
			bool explicitRuntimeHandledFalse = false;
			if (runtimeAdapter != nullptr && runtimeAdapterRegistered) {
				outcome.runtimeAdapterInvoked = true;
				const std::optional<CronJson> runtimeResult =
					(*runtimeAdapter)(job, nowMs);
				if (runtimeResult.has_value() && runtimeResult.value().is_object()) {
					const CronJson& runtimeNode = runtimeResult.value();
					ApplyRuntimeExecutionResult(outcome, runtimeResult.value());
					const bool hasExplicitHandledFlag =
						runtimeNode.contains("handled") &&
						runtimeNode["handled"].is_boolean();
					if (runtimeNode.contains("handled") &&
						runtimeNode["handled"].is_boolean()) {
						runtimeHandled = runtimeNode["handled"].get<bool>();
						explicitRuntimeHandledFalse =
							hasExplicitHandledFlag && !runtimeHandled;
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

					if (runtimeHandled &&
						!runtimeNode.contains("skipDelivery") &&
						outcome.runtimeProjectedTransport) {
						if (outcome.runtimeProjectedPrimaryTransport &&
							outcome.runtimeProjectedFailureDestinationTransport) {
							outcome.skipDeliverySimulation = true;
						}
						else {
							outcome.skipPrimaryDeliverySimulation =
								outcome.runtimeProjectedPrimaryTransport;
							outcome.skipFailureDestinationSimulation =
								outcome.runtimeProjectedFailureDestinationTransport;
						}
					}

					if (hasExplicitHandledFlag &&
						!runtimeHandled) {
						outcome.skipPrimaryDeliverySimulation = false;
						outcome.skipFailureDestinationSimulation = false;
						outcome.skipDeliverySimulation = false;
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
						const std::int64_t resolvedBusyDelayMs =
							outcome.hasRetryDelayOverride
							? (std::max)(static_cast<std::int64_t>(1),
								outcome.retryDelayOverrideMs)
							: busyDelayMs;

						if (heartbeatBusyAttempts < maxBusyAttempts) {
							outcome.status = "error";
							outcome.error = "main heartbeat busy";
							outcome.errorCategory = "heartbeat_busy";
							outcome.summary = "Main heartbeat busy; retry scheduled";
							outcome.retryable = true;
							outcome.hasRetryDelayOverride = true;
							outcome.retryDelayOverrideMs = resolvedBusyDelayMs;
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
				else if (adapters.preferRuntimeExecution) {
					runtimeHandled = true;
					outcome.status = "error";
					outcome.error = "cron runtime adapter returned no result";
					outcome.errorCategory = "runtime_unavailable";
					outcome.summary = "Cron runtime execution is unavailable";
					outcome.retryable = true;
				}
			}
			else if (runtimeAdapter != nullptr && adapters.preferRuntimeExecution) {
				runtimeHandled = true;
				outcome.status = "error";
				outcome.error = "cron runtime adapter is not registered";
				outcome.errorCategory = "runtime_unavailable";
				outcome.summary = "Cron runtime execution is unavailable";
				outcome.retryable = true;
			}

			const bool allowSimulationFallback =
				!runtimeHandled &&
				(!adapters.preferRuntimeExecution ||
					explicitRuntimeHandledFalse);
			outcome.runtimeHandled = runtimeHandled;

			if (allowSimulationFallback && payloadKind == "systemevent") {
				outcome.simulationFallbackUsed = true;
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
			if (allowSimulationFallback && payloadKind == "agentturn") {
				outcome.simulationFallbackUsed = true;
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

			if (runtimeHandled) {
				outcome.runtimeExecutionPath = "runtime";
			}
			else if (outcome.simulationFallbackUsed) {
				outcome.runtimeExecutionPath = "simulation";
			}
			else {
				outcome.runtimeExecutionPath = "none";
			}

			const bool heartbeatBusyDeliverySuppressed =
				outcome.errorCategory == "heartbeat_busy" ||
				outcome.errorCategory == "heartbeat_busy_fallback";
			if (heartbeatBusyDeliverySuppressed) {
				outcome.deliveryStatus = "not-requested";
				outcome.delivered = false;
				outcome.deliveryAttempted = false;
				outcome.failureDestinationStatus = "not-requested";
				outcome.failureDestinationAttempted = false;
				outcome.failureDestinationError.clear();
			}

			const bool skipPrimaryDeliverySimulation =
				runtimeHandled &&
				(outcome.skipDeliverySimulation ||
					outcome.skipPrimaryDeliverySimulation);
			const bool skipFailureDestinationSimulation =
				runtimeHandled &&
				(outcome.skipDeliverySimulation ||
					outcome.skipFailureDestinationSimulation);

			if (!heartbeatBusyDeliverySuppressed &&
				job.contains("delivery") &&
				job["delivery"].is_object() &&
				(!skipPrimaryDeliverySimulation || !skipFailureDestinationSimulation)) {
				const CronJson& delivery = job["delivery"];
				const bool simulateTransientFailure =
					delivery.contains("simulateTransientFailure") &&
					delivery["simulateTransientFailure"].is_boolean() &&
					delivery["simulateTransientFailure"].get<bool>();
				const std::string mode = ToLowerCopy(
					TrimCopy(delivery.value("mode", std::string("announce"))));
				if (!skipPrimaryDeliverySimulation) {
					outcome.deliveryMode = mode;
					outcome.deliveryTarget = TrimCopy(delivery.value("to", std::string()));
					outcome.deliveryChannel = TrimCopy(
						delivery.value("channel", std::string("last")));
					if (outcome.deliveryChannel.empty()) {
						outcome.deliveryChannel = "last";
					}
					outcome.deliveryAccountId = TrimCopy(
						delivery.value("accountId", std::string()));
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
				}

				if (!skipFailureDestinationSimulation &&
					outcome.status == "error" &&
					delivery.contains("failureDestination") &&
					delivery["failureDestination"].is_object()) {
					const CronJson& failureDestination = delivery["failureDestination"];
					const bool failureHasExplicitMode =
						failureDestination.contains("mode") &&
						failureDestination["mode"].is_string() &&
						!TrimCopy(failureDestination["mode"].get<std::string>()).empty();
					std::string failureMode = ToLowerCopy(
						TrimCopy(failureDestination.value("mode", std::string("announce"))));
					if (failureMode != "announce" && failureMode != "webhook") {
						failureMode = "announce";
					}

					const std::string primaryMode =
						!outcome.deliveryMode.empty()
						? outcome.deliveryMode
						: mode;
					std::string resolvedPrimaryMode = primaryMode;
					if (resolvedPrimaryMode != "webhook" &&
						outcome.deliveryMode.empty() &&
						!outcome.deliveryTarget.empty() &&
						StartsWithHttpScheme(outcome.deliveryTarget)) {
						resolvedPrimaryMode = "webhook";
					}
					std::string primaryTo = !outcome.deliveryTarget.empty()
						? outcome.deliveryTarget
						: TrimCopy(delivery.value("to", std::string()));
					const bool primaryHasExplicitTo =
						delivery.contains("to") &&
						delivery["to"].is_string();
					if (primaryTo.empty() &&
						delivery.contains("url") &&
						delivery["url"].is_string()) {
						primaryTo = TrimCopy(delivery["url"].get<std::string>());
					}
					if (primaryTo.empty() &&
						!primaryHasExplicitTo &&
						resolvedPrimaryMode == "announce") {
						primaryTo = outcome.sessionId.empty()
							? (sessionTarget == "main"
								? std::string("main")
								: std::string("isolated"))
							: outcome.sessionId;
					}
					const std::string primaryChannel = !outcome.deliveryChannel.empty()
						? outcome.deliveryChannel
						: TrimCopy(delivery.value("channel", std::string("last")));
					const std::string primaryAccountId =
						!outcome.deliveryAccountId.empty()
						? outcome.deliveryAccountId
						: TrimCopy(delivery.value("accountId", std::string()));

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
					if (failureMode != "webhook" &&
						!failureHasExplicitMode &&
						!failureTo.empty() &&
						StartsWithHttpScheme(failureTo)) {
						failureMode = "webhook";
					}
					outcome.failureDestinationMode = failureMode;
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
						resolvedPrimaryMode == "webhook" &&
						CanonicalizeDeliveryRouteTargetForCompare(
							failureMode,
							resolvedFailureTo) ==
						CanonicalizeDeliveryRouteTargetForCompare(
							resolvedPrimaryMode,
							primaryTo);
					const bool sameAnnounceTarget =
						failureMode == "announce" &&
						resolvedPrimaryMode != "none" &&
						!primaryTo.empty() &&
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
						const bool failureHasExplicitEmptyTo =
							failureHasExplicitTo && failureTo.empty();
						if ((failureHasExplicitTo &&
							failureTo.empty()) ||
							resolvedFailureTo.empty()) {
							outcome.failureDestinationStatus = "not-delivered";
							outcome.failureDestinationError = failureHasExplicitEmptyTo
								? std::string("announce failure destination target is empty")
								: std::string("announce failure destination target is unresolved");
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
			if (tz.empty() ||
				tz == "utc" ||
				tz == "gmt" ||
				tz == "z" ||
				tz == "ut" ||
				tz == "utc0" ||
				tz == "gmt0") {
				return 0;
			}

			if (tz == "etc/utc" ||
				tz == "etc/utc0" ||
				tz == "etc/ut" ||
				tz == "etc/uct" ||
				tz == "etc/zulu" ||
				tz == "etc/universal" ||
				tz == "etc/greenwich") {
				return 0;
			}

			if (tz.rfind("etc/gmt", 0) == 0) {
				const std::string suffix = tz.substr(7);
				if (suffix.empty()) {
					return 0;
				}
				if (suffix == "0") {
					return 0;
				}
				if (suffix[0] != '+' && suffix[0] != '-') {
					return std::nullopt;
				}

				const int sign = suffix[0] == '-' ? 1 : -1; // POSIX sign semantics
				std::string digits = suffix.substr(1);
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
			return MatchCronFieldToken(tokenRaw, value, 0, maxValue, false);
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
			if (anchorMs > nowMs) {
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
				return std::nullopt;
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
			if (!schedule.contains("expr") || !schedule["expr"].is_string()) {
				throw std::invalid_argument("invalid cron expression field count");
			}

			const std::string expr =
				TrimCopy(schedule.value("expr", std::string()));
			std::istringstream stream(expr);
			std::vector<std::string> parts;
			std::string token;
			while (stream >> token) {
				parts.push_back(token);
			}
			if (parts.size() != 5 && parts.size() != 6) {
				throw std::invalid_argument("invalid cron expression field count");
			}

			const bool hasSecondsField = parts.size() >= 6;
			const std::string secondToken =
				hasSecondsField ? parts[0] : std::string("0");
			const std::string minuteToken =
				hasSecondsField ? parts[1] : parts[0];
			const std::string hourToken =
				hasSecondsField ? parts[2] : parts[1];
			const std::string dayOfMonthToken =
				hasSecondsField
				? (parts.size() >= 4 ? parts[3] : std::string("*"))
				: (parts.size() >= 3 ? parts[2] : std::string("*"));
			const std::string monthToken =
				hasSecondsField
				? (parts.size() >= 5 ? parts[4] : std::string("*"))
				: (parts.size() >= 4 ? parts[3] : std::string("*"));
			const std::string dayOfWeekToken =
				hasSecondsField
				? (parts.size() >= 6 ? parts[5] : std::string("*"))
				: (parts.size() >= 5 ? parts[4] : std::string("*"));

			auto HasAnyFieldMatch = [](
				const std::string& tokenRaw,
				const int minValue,
				const int maxValue,
				const bool normalizeSevenToZero,
				const std::function<std::optional<int>(const std::string&)>& aliasResolver = nullptr) {
				for (int value = minValue; value <= maxValue; ++value) {
					if (MatchCronFieldToken(
						tokenRaw,
						value,
						minValue,
						maxValue,
						normalizeSevenToZero,
						aliasResolver)) {
						return true;
					}
				}
				return false;
			};

			if (hasSecondsField && !HasAnyFieldMatch(secondToken, 0, 59, false)) {
				throw std::invalid_argument("invalid cron second field");
			}
			if (!HasAnyFieldMatch(minuteToken, 0, 59, false)) {
				throw std::invalid_argument("invalid cron minute field");
			}
			if (!HasAnyFieldMatch(hourToken, 0, 23, false)) {
				throw std::invalid_argument("invalid cron hour field");
			}
			if (!HasAnyFieldMatch(dayOfMonthToken, 1, 31, false)) {
				throw std::invalid_argument("invalid cron day-of-month field");
			}
			if (!HasAnyFieldMatch(monthToken, 1, 12, false, TryResolveCronMonthAlias)) {
				throw std::invalid_argument("invalid cron month field");
			}
			if (!HasAnyFieldMatch(dayOfWeekToken, 0, 6, true, TryResolveCronDayOfWeekAlias)) {
				throw std::invalid_argument("invalid cron day-of-week field");
			}

			const auto timezoneOffsetMinutes =
				ParseTimezoneOffsetMinutes(schedule.value("tz", std::string()));
			if (!timezoneOffsetMinutes.has_value() &&
				schedule.contains("tz") &&
				schedule["tz"].is_string() &&
				!TrimCopy(schedule["tz"].get<std::string>()).empty()) {
				throw std::invalid_argument("invalid cron timezone offset");
			}
			const std::int64_t timezoneOffsetMs =
				static_cast<std::int64_t>(timezoneOffsetMinutes.value_or(0)) * 60 * 1000;
			std::int64_t localNowMs = nowMs + timezoneOffsetMs;
			const std::int64_t stepMs = hasSecondsField ? kSecondMs : kMinuteMs;

			const auto staggerMs = TryReadInt64Field(schedule, "staggerMs");
			const std::int64_t offsetMs =
				(staggerMs.has_value() && staggerMs.value() > 0)
				? ResolveStableCronOffsetMs(job, staggerMs.value())
				: 0;

			std::int64_t cursorMs = nowMs;
			if (!hasSecondsField && offsetMs > 0) {
				cursorMs = (std::max)(static_cast<std::int64_t>(0), nowMs - offsetMs);
			}

			std::int64_t candidateLocalMs =
				hasSecondsField
				? (((cursorMs + timezoneOffsetMs) / kSecondMs) + 1) * kSecondMs
				: (((cursorMs + timezoneOffsetMs) / kMinuteMs) + 1) * kMinuteMs;
			const int maxAttempts =
				hasSecondsField ? (60 * 60 * 24 * 8) : (60 * 24 * 366);

			for (int attempt = 0; attempt < maxAttempts; ++attempt) {
				const std::time_t candidateSeconds =
					static_cast<std::time_t>(candidateLocalMs / 1000);
				std::tm candidateTm{};
				gmtime_s(&candidateTm, &candidateSeconds);

				const int second = candidateTm.tm_sec;
				const int minute = candidateTm.tm_min;
				const int hour = candidateTm.tm_hour;
				const int dayOfMonth = candidateTm.tm_mday;
				const int month = candidateTm.tm_mon + 1;
				const int dayOfWeek = candidateTm.tm_wday;

				const bool domWildcard = IsWildcardCronToken(dayOfMonthToken);
				const bool dowWildcard = IsWildcardCronToken(dayOfWeekToken);
				const bool dayOfMonthMatch =
					MatchCronToken(dayOfMonthToken, dayOfMonth, 31);
				const bool dayOfWeekMatch =
					MatchCronDayOfWeekToken(dayOfWeekToken, dayOfWeek);
				const bool dayMatch =
					(domWildcard && dowWildcard) ||
					(domWildcard && dayOfWeekMatch) ||
					(dowWildcard && dayOfMonthMatch) ||
					(!domWildcard && !dowWildcard &&
						(dayOfMonthMatch || dayOfWeekMatch));

				if ((!hasSecondsField || MatchCronToken(secondToken, second, 59)) &&
					MatchCronToken(minuteToken, minute, 59) &&
					MatchCronToken(hourToken, hour, 23) &&
					MatchCronMonthToken(monthToken, month) &&
					dayMatch) {
					std::int64_t candidate = candidateLocalMs - timezoneOffsetMs;
					if (offsetMs > 0) {
						candidate += offsetMs;
					}
					if (candidate > nowMs) {
						return candidate;
					}
				}

				candidateLocalMs += stepMs;
			}

			return std::nullopt;
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
			const bool allowCronMissedRunByLastRun =
				schedule.contains("allowCronMissedRunByLastRun") &&
				schedule["allowCronMissedRunByLastRun"].is_boolean() &&
				schedule["allowCronMissedRunByLastRun"].get<bool>();
			if (allowCronMissedRunByLastRun &&
				job.contains("state") &&
				job["state"].is_object()) {
				auto replayAnchor = TryReadInt64Field(job["state"], "lastRunAtMs");
				if (!replayAnchor.has_value()) {
					replayAnchor = TryReadInt64Field(job["state"], "lastScheduledForMs");
				}
				if (replayAnchor.has_value() &&
					replayAnchor.value() >= 0 &&
					replayAnchor.value() < nowMs) {
					const std::int64_t replayLimit = (std::max)(
						static_cast<std::int64_t>(1),
						(std::min)(
							static_cast<std::int64_t>(60),
							TryReadInt64Field(schedule, "missedRunReplayLimit")
							.value_or(1)));
					std::int64_t replayCursor = replayAnchor.value();
					for (std::int64_t replayAttempt = 0;
						replayAttempt < replayLimit;
						++replayAttempt) {
						const auto replayCandidate =
							ComputeNextCron(schedule, job, replayCursor);
						if (!replayCandidate.has_value()) {
							break;
						}
						if (replayCandidate.value() <= nowMs) {
							return replayCandidate.value();
						}
						replayCursor = replayCandidate.value();
					}
				}
			}

			const auto next = ComputeNextCron(schedule, job, nowMs);
			if (next.has_value()) {
				return next;
			}

			const std::int64_t nextSecondMs = ((nowMs / kSecondMs) * kSecondMs) + kSecondMs;
			return ComputeNextCron(schedule, job, nextSecondMs);
		}

		return std::nullopt;
	}

	bool CronTimerService::RecomputeSchedules(
		CronJson& jobs,
		const std::int64_t nowMs,
		const CronRecomputeOptions& opts,
		std::vector<CronScheduleNotificationEvent>* notifications) const {
		bool changed = false;
		for (auto& job : jobs) {
			CronJson& state = EnsureStateObject(job);
			if (NormalizeJobTickState(job, nowMs)) {
				changed = true;
			}

			const std::string jobId = TrimCopy(job.value("id", std::string()));
			const std::string jobName = TrimCopy(job.value("name", std::string()));
			const std::string scheduleAutoDisableContextKey =
				"cron:" + (jobId.empty() ? std::string("unknown") : jobId) +
				":auto-disabled";
			const auto currentNextRunAtMs = TryReadInt64Field(state, "nextRunAtMs");
			const bool hasRunningMarker = TryReadInt64Field(state, "runningAtMs").has_value();
			if (opts.maintenanceOnly) {
				if (!HasScheduledNextRunAtMs(currentNextRunAtMs)) {
					// Recompute missing schedules below.
				}
				else if (opts.recomputeExpired &&
					currentNextRunAtMs.value() <= nowMs &&
					!hasRunningMarker) {
					const auto lastRunAtMs = TryReadInt64Field(state, "lastRunAtMs");
					const bool alreadyExecutedSlot =
						lastRunAtMs.has_value() &&
						lastRunAtMs.value() >= currentNextRunAtMs.value();
					if (!alreadyExecutedSlot) {
						continue;
					}
				}
				else {
					continue;
				}
			}
			else if (opts.preserveDueSlots &&
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
				if (state.contains("scheduleAutoDisabled") && !state["scheduleAutoDisabled"].is_null()) {
					state["scheduleAutoDisabled"] = nullptr;
					changed = true;
				}
				if (state.contains("scheduleAutoDisabledAtMs") && !state["scheduleAutoDisabledAtMs"].is_null()) {
					state["scheduleAutoDisabledAtMs"] = nullptr;
					changed = true;
				}
				if (state.contains("scheduleAutoDisabledReason") && !state["scheduleAutoDisabledReason"].is_null()) {
					state["scheduleAutoDisabledReason"] = nullptr;
					changed = true;
				}
				if (state.contains("scheduleAutoDisableNotificationText") &&
					!state["scheduleAutoDisableNotificationText"].is_null()) {
					state["scheduleAutoDisableNotificationText"] = nullptr;
					changed = true;
				}
				if (state.contains("scheduleAutoDisableNotificationContextKey") &&
					!state["scheduleAutoDisableNotificationContextKey"].is_null()) {
					state["scheduleAutoDisableNotificationContextKey"] = nullptr;
					changed = true;
				}
				if (state.contains("scheduleAutoDisableNotificationAgentId") &&
					!state["scheduleAutoDisableNotificationAgentId"].is_null()) {
					state["scheduleAutoDisableNotificationAgentId"] = nullptr;
					changed = true;
				}
				if (state.contains("scheduleAutoDisableNotificationSessionKey") &&
					!state["scheduleAutoDisableNotificationSessionKey"].is_null()) {
					state["scheduleAutoDisableNotificationSessionKey"] = nullptr;
					changed = true;
				}
				if (state.contains("scheduleAutoDisableHeartbeatWakeRequested") &&
					!state["scheduleAutoDisableHeartbeatWakeRequested"].is_null()) {
					state["scheduleAutoDisableHeartbeatWakeRequested"] = nullptr;
					changed = true;
				}
				if (state.contains("scheduleAutoDisableHeartbeatWakeRequestedAtMs") &&
					!state["scheduleAutoDisableHeartbeatWakeRequestedAtMs"].is_null()) {
					state["scheduleAutoDisableHeartbeatWakeRequestedAtMs"] = nullptr;
					changed = true;
				}
				if (state.contains("scheduleAutoDisableHeartbeatWakeReason") &&
					!state["scheduleAutoDisableHeartbeatWakeReason"].is_null()) {
					state["scheduleAutoDisableHeartbeatWakeReason"] = nullptr;
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
					const std::string notifyName =
						jobName.empty()
						? (jobId.empty() ? std::string("(unknown cron job)") : jobId)
						: jobName;
					const std::string notifyText =
						"⚠️ Cron job \"" + notifyName +
						"\" has been auto-disabled after " +
						std::to_string(errorCount) +
						" consecutive schedule errors. Last error: " +
						std::string(ex.what());
					state["scheduleAutoDisableNotificationText"] = notifyText;
					state["scheduleAutoDisableNotificationContextKey"] =
						scheduleAutoDisableContextKey;
					state["scheduleAutoDisableNotificationAgentId"] =
						job.contains("agentId")
						? job["agentId"]
						: CronJson(nullptr);
					state["scheduleAutoDisableNotificationSessionKey"] =
						job.contains("sessionKey")
						? job["sessionKey"]
						: CronJson(nullptr);
					state["scheduleAutoDisableHeartbeatWakeRequested"] = true;
					state["scheduleAutoDisableHeartbeatWakeRequestedAtMs"] = nowMs;
					state["scheduleAutoDisableHeartbeatWakeReason"] =
						scheduleAutoDisableContextKey;
					state["scheduleAutoDisabled"] = true;
					state["scheduleAutoDisabledAtMs"] = nowMs;
					state["scheduleAutoDisabledReason"] = "schedule_error_threshold";
					job["enabled"] = false;
					if (errorCount == kMaxScheduleErrors) {
						QueueScheduleAutoDisableNotification(
							job,
							scheduleAutoDisableContextKey,
							notifyText,
							notifications);
					}
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
					const std::string notifyName =
						jobName.empty()
						? (jobId.empty() ? std::string("(unknown cron job)") : jobId)
						: jobName;
					const std::string notifyText =
						"⚠️ Cron job \"" + notifyName +
						"\" has been auto-disabled after " +
						std::to_string(errorCount) +
						" consecutive schedule errors. Last error: unknown";
					state["scheduleAutoDisableNotificationText"] = notifyText;
					state["scheduleAutoDisableNotificationContextKey"] =
						scheduleAutoDisableContextKey;
					state["scheduleAutoDisableNotificationAgentId"] =
						job.contains("agentId")
						? job["agentId"]
						: CronJson(nullptr);
					state["scheduleAutoDisableNotificationSessionKey"] =
						job.contains("sessionKey")
						? job["sessionKey"]
						: CronJson(nullptr);
					state["scheduleAutoDisableHeartbeatWakeRequested"] = true;
					state["scheduleAutoDisableHeartbeatWakeRequestedAtMs"] = nowMs;
					state["scheduleAutoDisableHeartbeatWakeReason"] =
						scheduleAutoDisableContextKey;
					state["scheduleAutoDisabled"] = true;
					state["scheduleAutoDisabledAtMs"] = nowMs;
					state["scheduleAutoDisabledReason"] = "schedule_error_threshold";
					job["enabled"] = false;
					if (errorCount == kMaxScheduleErrors) {
						QueueScheduleAutoDisableNotification(
							job,
							scheduleAutoDisableContextKey,
							notifyText,
							notifications);
					}
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
		const bool forceRunDue,
		const CronPumpOptions& pumpOptions,
		const CronPumpCallbacks* pumpCallbacks) const {
		std::size_t executed = 0;
		for (auto it = jobs.begin(); it != jobs.end();) {
			if (pumpOptions.maxExecutionsPerPump > 0 &&
				executed >= pumpOptions.maxExecutionsPerPump) {
				break;
			}

			if (!(*it).is_object() || !(*it).value("enabled", true)) {
				++it;
				continue;
			}

			const std::string id = (*it).value("id", std::string());
			if (id.empty()) {
				++it;
				continue;
			}

			CronJson& state = EnsureStateObject(*it);
			if (TryReadInt64Field(state, "runningAtMs").has_value()) {
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

			if (pumpCallbacks != nullptr &&
				static_cast<bool>(pumpCallbacks->onStarted)) {
				pumpCallbacks->onStarted(*it, nowMs);
			}

			RunOutcome outcome =
				EvaluateRunOutcome(*it, nowMs, m_runtimeAdapters);
			state["runningAtMs"] = nowMs;
			state["startedAtMs"] = nowMs;
			state["lastRunAtMs"] = nowMs;
			state["lastRuntimeExecutionPath"] = outcome.runtimeExecutionPath;
			state["lastRuntimeAdapterRegistered"] = outcome.runtimeAdapterRegistered;
			state["lastRuntimeAdapterInvoked"] = outcome.runtimeAdapterInvoked;
			state["lastRuntimeHandled"] = outcome.runtimeHandled;
			state["lastSimulationFallbackUsed"] = outcome.simulationFallbackUsed;
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
			const std::int64_t heartbeatBusyAttemptsSnapshot =
				TryReadInt64Field(state, "heartbeatBusyAttempts").value_or(0);
			const bool heartbeatFallbackWakeRequestedSnapshot =
				state.contains("heartbeatFallbackWakeRequested") &&
				state["heartbeatFallbackWakeRequested"].is_boolean() &&
				state["heartbeatFallbackWakeRequested"].get<bool>();
			const std::optional<std::int64_t> heartbeatFallbackWakeRequestedAtMsSnapshot =
				TryReadInt64Field(state, "heartbeatFallbackWakeRequestedAtMs");
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
				state["lastFailureAlertTarget"] = CronJson(nullptr);
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
						outcome.failureAlertStatus = "not-requested";
					}
					else {
						std::string failureAlertMode = kFailureAlertModeAnnounce;
						std::string failureAlertChannel = "last";
						std::string failureAlertAccountId;
						std::string failureAlertTarget;
						std::string deliveryTargetFallback = outcome.deliveryTarget;
						std::string deliveryChannelFallback = "last";
						std::string deliveryAccountIdFallback;
						if ((*it).contains("delivery") && (*it)["delivery"].is_object()) {
							if (deliveryTargetFallback.empty()) {
								deliveryTargetFallback =
									TrimCopy((*it)["delivery"].value("to", std::string()));
							}
							if (deliveryTargetFallback.empty() &&
								(*it)["delivery"].contains("url") &&
								(*it)["delivery"]["url"].is_string()) {
								deliveryTargetFallback =
									TrimCopy((*it)["delivery"]["url"].get<std::string>());
							}
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
							failureAlertAccountId.clear();
						}

						const std::string normalizedFailureAlertChannel =
							ToLowerCopy(TrimCopy(failureAlertChannel));
						const bool routeChannelChanged =
							failureAlertMode == kFailureAlertModeAnnounce &&
							previousFailureAlertChannel != normalizedFailureAlertChannel;
						const bool routeAccountChanged =
							failureAlertMode == kFailureAlertModeAnnounce &&
							previousFailureAlertAccountId != failureAlertAccountId;

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
						routeChannelChanged ||
						routeAccountChanged;
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
							outcome.failureAlertStatus = "not-delivered";
							outcome.failureAlertError = "invalid failure alert webhook target";
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
							if (scheduledRetry) {
								state["failureAlertSuppressed"] = true;
								state["failureAlertSuppressedReason"] = "retry_pending";
							}
							else {
								failureAlertTriggered = true;
								failureAlertAtMs = nowMs;
								state["lastFailureAlertAtMs"] = nowMs;
							}
						}
						else if (consecutiveErrors < alertAfter) {
							state["failureAlertSuppressed"] = true;
							state["failureAlertSuppressedReason"] = "threshold_not_met";
						}
						else if (!cooldownOpen) {
							state["failureAlertSuppressed"] = true;
							state["failureAlertSuppressedReason"] = "cooldown_active";
						}

						if (failureAlertMode == kFailureAlertModeWebhook) {
							const bool alertTransportDispatch =
								(*it).contains("failureAlert") &&
								(*it)["failureAlert"].is_object() &&
								IsTransportDispatchEnabled((*it)["failureAlert"]);
							const auto alertHttpStatus =
								(*it).contains("failureAlert") &&
								(*it)["failureAlert"].is_object()
								? TryReadInt64Field((*it)["failureAlert"], "simulateHttpStatus")
								: std::nullopt;

							if (alertHttpStatus.has_value()) {
								outcome.failureAlertAttempted = true;
								outcome.failureAlertHttpStatus = alertHttpStatus.value();
								if (alertHttpStatus.value() >= 200 && alertHttpStatus.value() < 300) {
									outcome.failureAlertStatus = "delivered";
								}
								else {
									outcome.failureAlertStatus = "not-delivered";
									outcome.failureAlertError =
										"failure alert webhook returned HTTP " + std::to_string(alertHttpStatus.value());
								}
							}
							else if (alertTransportDispatch && StartsWithHttpScheme(failureAlertTarget)) {
								const WebhookDispatchResult dispatch =
									DispatchWebhookPostWinHttp(failureAlertTarget);
								outcome.failureAlertAttempted = dispatch.attempted;
								if (dispatch.httpStatus.has_value()) {
									outcome.failureAlertHttpStatus = dispatch.httpStatus.value();
									if (dispatch.httpStatus.value() >= 200 && dispatch.httpStatus.value() < 300) {
										outcome.failureAlertStatus = "delivered";
									}
									else {
										outcome.failureAlertStatus = "not-delivered";
										outcome.failureAlertError =
											"failure alert webhook returned HTTP " + std::to_string(dispatch.httpStatus.value());
									}
								}
								else {
									outcome.failureAlertStatus = "not-delivered";
									outcome.failureAlertError = dispatch.error.empty()
										? std::string("failure alert webhook transport dispatch failed")
										: dispatch.error;
								}
							}
							else if (StartsWithHttpScheme(failureAlertTarget)) {
								outcome.failureAlertAttempted = true;
								outcome.failureAlertStatus = "delivered";
							}
							else if (outcome.failureAlertStatus.empty()) {
								outcome.failureAlertStatus = "not-delivered";
								outcome.failureAlertError = "invalid failure alert webhook target";
							}
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
				state["lastFailureAlertMode"] = nullptr;
				state["lastFailureAlertTarget"] = CronJson(nullptr);
				state["lastFailureAlertChannel"] = nullptr;
				state["lastFailureAlertAccountId"] = nullptr;
			}

			if (!deleteAfterRun && !scheduledRetry) {
				nextAfterRun = ComputeNextRunAtMs(*it, nowMs);
				if (nextAfterRun.has_value() && ReadScheduleKind(*it) == "cron") {
					const std::int64_t minNext = nowMs + kCronMinRefireGapMs;
					if (nextAfterRun.value() < minNext) {
						nextAfterRun = minNext;
					}
				}
				state["nextRunAtMs"] =
					nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr);
				(*it)["updatedAtMs"] = nowMs;
			}
			state["lastRunId"] = runId;
			state["lastScheduledForMs"] = nextRunAtMs.value();
			state["lastFinishedAtMs"] = nowMs;
			state["endedAtMs"] = nowMs;
			state["lastRunTimedOut"] = outcome.timedOut;
			state["lastRunAborted"] = outcome.aborted;
			state["lastTaskLedgerRuntime"] = "cron";
			state["lastTaskLedgerPhase"] = "terminal";
			state["lastTaskLedgerStatus"] = outcome.status;
			state["lastTaskLedgerDisposition"] = "scheduled";
			state["lastTaskLedgerTerminal"] = true;
			state["lastHeartbeatBusyAttempts"] = heartbeatBusyAttemptsSnapshot;
			state["lastHeartbeatFallbackWakeRequested"] = heartbeatFallbackWakeRequestedSnapshot;
			state["lastHeartbeatFallbackWakeRequestedAtMs"] =
				heartbeatFallbackWakeRequestedAtMsSnapshot.has_value()
				? CronJson(heartbeatFallbackWakeRequestedAtMsSnapshot.value())
				: CronJson(nullptr);

			runs.push_back({
				{ "ts", nowMs },
				{ "jobId", id },
				{ "action", "finished" },
				{ "runtimeExecutionPath", outcome.runtimeExecutionPath },
				{ "runtimeAdapterRegistered", outcome.runtimeAdapterRegistered },
				{ "runtimeAdapterInvoked", outcome.runtimeAdapterInvoked },
				{ "runtimeHandled", outcome.runtimeHandled },
				{ "simulationFallbackUsed", outcome.simulationFallbackUsed },
				{ "lifecycleState", "terminal" },
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
				{ "failureAlertStatus", outcome.failureAlertStatus.empty()
					? CronJson(nullptr)
					: CronJson(outcome.failureAlertStatus) },
				{ "failureAlertAttempted", outcome.failureAlertAttempted },
				{ "failureAlertHttpStatus", outcome.failureAlertHttpStatus > 0
					? CronJson(outcome.failureAlertHttpStatus)
					: CronJson(nullptr) },
				{ "failureAlertError", outcome.failureAlertError.empty()
					? CronJson(nullptr)
					: CronJson(outcome.failureAlertError) },
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
				{ "heartbeatBusyAttempts", heartbeatBusyAttemptsSnapshot },
				{ "heartbeatFallbackWakeRequested", heartbeatFallbackWakeRequestedSnapshot },
				{ "heartbeatFallbackWakeRequestedAtMs",
					heartbeatFallbackWakeRequestedAtMsSnapshot.has_value()
					? CronJson(heartbeatFallbackWakeRequestedAtMsSnapshot.value())
					: CronJson(nullptr) },
				{ "retryAttempt", retryAttempt },
				{ "retryScheduled", scheduledRetry },
				{ "retryScheduledAtMs",
					scheduledRetry && nextAfterRun.has_value()
					? CronJson(nextAfterRun.value())
					: (outcome.hasRetryDelayOverride
						? CronJson(nowMs + (std::max)(static_cast<std::int64_t>(0), outcome.retryDelayOverrideMs))
						: CronJson(nullptr)) },
				{ "durationMs", 0 },
				{ "timedOut", outcome.timedOut },
				{ "aborted", outcome.aborted },
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
				{ "jobName", jobName },
				{ "runId", runId }
			});
			if (outcome.usageAvailable) {
				runs.back()["usage"] = CronJson({
					{ "promptTokens", outcome.usagePromptTokens },
					{ "completionTokens", outcome.usageCompletionTokens },
					{ "totalTokens", outcome.usageTotalTokens }
				});
			}
			if (pumpCallbacks != nullptr &&
				static_cast<bool>(pumpCallbacks->onFinished) &&
				!runs.empty()) {
				pumpCallbacks->onFinished(*it, runs.back());
			}
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
