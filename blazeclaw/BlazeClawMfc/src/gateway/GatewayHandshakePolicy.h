#pragma once

#include <string>

namespace blazeclaw::gateway {

	class GatewayHandshakePolicy {
	public:
		struct Input {
			bool hasHostHeader = false;
			bool isAllowedRequestTarget = false;
			bool isAllowedOrigin = true;
			bool requestedPerMessageDeflate = false;
			bool subprotocolRequested = false;
			bool supportsRequestedSubprotocol = true;
			std::string requestedSubprotocol;
			std::string authMode = "none";
			bool authHookAllowed = true;
		};

		struct Output {
			bool accepted = false;
			bool extensionRejected = false;
			bool emitConnectChallenge = false;
			std::string reasonCode;
			std::string errorMessage;
			std::string selectedSubprotocol;
		};

		[[nodiscard]] Output Evaluate(const Input& input) const;
	};

} // namespace blazeclaw::gateway
