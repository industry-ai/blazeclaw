#include "pch.h"
#include "GatewayLifecycleEventEmitter.h"

#include "Telemetry.h"

namespace blazeclaw::gateway {

	std::string GatewayLifecycleEventEmitter::BuildLifecyclePayload(
		const std::string& runId,
		const std::string& sessionKey,
		const std::string& state,
		const std::uint64_t timestampMs,
		const std::optional<std::string>& reason) {
		std::string payload =
			std::string("{\"runId\":") + JsonString(runId) +
			",\"sessionKey\":" + JsonString(sessionKey) +
			",\"state\":" + JsonString(state) +
			",\"timestamp\":" + std::to_string(timestampMs);
		if (reason.has_value()) {
			payload += ",\"reason\":" + JsonString(reason.value());
		}
		payload += "}";
		return payload;
	}

	void GatewayLifecycleEventEmitter::EmitLifecycle(
		const std::string& state,
		const std::string& runId,
		const std::string& sessionKey,
		const std::uint64_t timestampMs,
		const std::optional<std::string>& reason) {
		EmitTelemetryEvent(
			"gateway.chat.lifecycle",
			BuildLifecyclePayload(
				runId,
				sessionKey,
				state,
				timestampMs,
				reason));
	}

} // namespace blazeclaw::gateway
