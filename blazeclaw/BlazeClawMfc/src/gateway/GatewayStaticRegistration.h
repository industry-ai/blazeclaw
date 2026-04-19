#pragma once

#include <cstddef>

#include "GatewayMethodDispatcher.h"

namespace blazeclaw::gateway {

	/// One row for RegisterStaticPayloadHandlers: fixed method name and JSON payload string.
	struct StaticPayloadHandlerEntry {
		const char* method;
		const char* payloadJson;
	};

	/// Registers each entry as a handler that returns protocol::OkResponse with the fixed payload (copied per registration).
	void RegisterStaticPayloadHandlers(
		GatewayMethodDispatcher& dispatcher,
		const StaticPayloadHandlerEntry* entries,
		std::size_t count);

} // namespace blazeclaw::gateway
