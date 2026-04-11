#include "pch.h"
#include "GatewayHandshakePolicy.h"

#include <algorithm>

namespace blazeclaw::gateway {
	namespace {
		std::string LowerTrim(std::string value) {
			value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) == 0;
				}));
			value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
				return std::isspace(ch) == 0;
				}).base(), value.end());
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
				});
			return value;
		}
	}

	GatewayHandshakePolicy::Output GatewayHandshakePolicy::Evaluate(const Input& input) const {
		if (!input.hasHostHeader) {
			return Output{
				.accepted = false,
				.reasonCode = "missing_host_header",
				.errorMessage = "Missing `Host` header.",
			};
		}

		if (!input.isAllowedRequestTarget) {
			return Output{
				.accepted = false,
				.reasonCode = "request_target_not_allowed",
				.errorMessage = "Handshake request target is not allowed for gateway endpoint.",
			};
		}

		if (!input.isAllowedOrigin) {
			return Output{
				.accepted = false,
				.reasonCode = "origin_not_allowed",
				.errorMessage = "Origin is not permitted by endpoint policy.",
			};
		}

		if (input.requestedPerMessageDeflate) {
			return Output{
				.accepted = false,
				.extensionRejected = true,
				.reasonCode = "extension_rejected",
				.errorMessage = "Unsupported websocket extension requested: permessage-deflate.",
			};
		}

		if (input.subprotocolRequested && !input.supportsRequestedSubprotocol) {
			return Output{
				.accepted = false,
				.reasonCode = "subprotocol_not_supported",
				.errorMessage = "Requested websocket subprotocol is not supported.",
			};
		}

		if (!input.authHookAllowed) {
			return Output{
				.accepted = false,
				.reasonCode = "auth_policy_rejected",
				.errorMessage = "Handshake rejected by transport auth policy.",
			};
		}

		const std::string normalizedAuthMode = LowerTrim(input.authMode);
		const bool challengeEnabled =
			normalizedAuthMode.empty() ||
			normalizedAuthMode == "none" ||
			normalizedAuthMode == "token" ||
			normalizedAuthMode == "password" ||
			normalizedAuthMode == "trusted-proxy";

		Output output;
		output.accepted = true;
		output.reasonCode = "accepted";
		output.emitConnectChallenge = challengeEnabled;
		output.selectedSubprotocol =
			input.subprotocolRequested && input.supportsRequestedSubprotocol
			? input.requestedSubprotocol
			: std::string{};
		return output;
	}

} // namespace blazeclaw::gateway
