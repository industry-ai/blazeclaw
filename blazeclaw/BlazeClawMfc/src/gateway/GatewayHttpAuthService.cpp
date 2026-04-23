#include "pch.h"
#include "GatewayHttpAuthService.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::gateway {
	namespace {

		constexpr std::string_view kA2uiPath = "/__openclaw__/a2ui";
		constexpr std::string_view kCanvasHostPath = "/__openclaw__/canvas";
		constexpr std::string_view kCanvasWsPath = "/__openclaw__/ws";

		std::string ToLowerAsciiCopy(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		std::string TrimCopy(const std::string& value) {
			std::size_t start = 0;
			std::size_t end = value.size();
			while (start < end &&
				std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}
			while (end > start &&
				std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}
			return value.substr(start, end - start);
		}

		bool StartsWithPathPrefix(
			const std::string_view value,
			const std::string_view prefix) {
			if (value.size() < prefix.size()) {
				return false;
			}
			return value.substr(0, prefix.size()) == prefix;
		}

		std::optional<std::string> FindHeaderCaseInsensitive(
			const std::unordered_map<std::string, std::string>& headers,
			const std::string& headerName) {
			const std::string loweredTarget = ToLowerAsciiCopy(headerName);
			for (const auto& [name, value] : headers) {
				if (ToLowerAsciiCopy(name) == loweredTarget) {
					return value;
				}
			}
			return std::nullopt;
		}

	} // namespace

	bool GatewayHttpAuthService::IsCanvasPath(std::string_view pathname) {
		return
			pathname == kA2uiPath ||
			StartsWithPathPrefix(pathname, std::string(kA2uiPath) + "/") ||
			pathname == kCanvasHostPath ||
			StartsWithPathPrefix(pathname, std::string(kCanvasHostPath) + "/") ||
			pathname == kCanvasWsPath;
	}

	std::optional<std::string> GatewayHttpAuthService::GetBearerToken(
		const std::unordered_map<std::string, std::string>& headers) {
		const auto authorization = FindHeaderCaseInsensitive(
			headers,
			"authorization");
		if (!authorization.has_value()) {
			return std::nullopt;
		}

		const std::string trimmed = TrimCopy(authorization.value());
		if (trimmed.size() < 7) {
			return std::nullopt;
		}

		const std::string prefix = ToLowerAsciiCopy(trimmed.substr(0, 7));
		if (prefix != "bearer ") {
			return std::nullopt;
		}

		const std::string token = TrimCopy(trimmed.substr(7));
		if (token.empty()) {
			return std::nullopt;
		}

		return token;
	}

	GatewayHttpAuthDecisionResult GatewayHttpAuthService::AuthorizeCanvasRequest(
		const GatewayHttpAuthRequestContext& context,
		const GatewayHttpAuthPolicyCallbacks& callbacks) const {
		auto emitDecision = [&](const GatewayHttpAuthDecisionResult& result) {
			if (callbacks.observeDecision) {
				callbacks.observeDecision(context, result);
			}
			};

		if (context.malformedScopedPath) {
			const GatewayHttpAuthDecisionResult result{
				.ok = false,
				.reason = "unauthorized",
				.branch = "malformed_path",
			};
			emitDecision(result);
			return result;
		}

		std::string lastAuthFailure = "unauthorized";
		const auto token = GetBearerToken(context.headers);
		if (token.has_value()) {
			if (callbacks.checkRateLimit) {
				std::string limitReason;
				if (!callbacks.checkRateLimit(context, limitReason)) {
					const GatewayHttpAuthDecisionResult result{
						.ok = false,
						.reason = limitReason.empty() ? "rate_limited" : limitReason,
						.branch = "rate_limited",
					};
					emitDecision(result);
					return result;
				}
			}

			if (callbacks.authorizeBearer) {
				std::string authFailureReason;
				if (callbacks.authorizeBearer(token.value(), context, authFailureReason)) {
					const GatewayHttpAuthDecisionResult result{
						.ok = true,
						.reason = "",
						.branch = "bearer_ok",
					};
					emitDecision(result);
					return result;
				}
				if (!authFailureReason.empty()) {
					lastAuthFailure = authFailureReason;
				}
			}
			else {
				lastAuthFailure = "authorization_handler_missing";
			}

			const GatewayHttpAuthDecisionResult result{
				.ok = false,
				.reason = lastAuthFailure,
				.branch = "bearer_fail",
			};
			emitDecision(result);
			return result;
		}

		if (!context.canvasCapability.empty() && callbacks.authorizeCanvasCapability) {
			std::string capabilityFailureReason;
			if (callbacks.authorizeCanvasCapability(
				context.canvasCapability,
				context,
				capabilityFailureReason)) {
				const GatewayHttpAuthDecisionResult result{
					.ok = true,
					.reason = "",
					.branch = "capability_ok",
				};
				emitDecision(result);
				return result;
			}
			if (!capabilityFailureReason.empty()) {
				lastAuthFailure = capabilityFailureReason;
			}
		}

		const GatewayHttpAuthDecisionResult result{
			.ok = false,
			.reason = lastAuthFailure,
			.branch = "unauthorized",
		};
		emitDecision(result);
		return result;
	}

} // namespace blazeclaw::gateway
