#include "pch.h"
#include "GatewayStaticRegistration.h"

#include "GatewayProtocolModels.h"

#include <string>

namespace blazeclaw::gateway {

	void RegisterStaticPayloadHandlers(
		GatewayMethodDispatcher& dispatcher,
		const StaticPayloadHandlerEntry* entries,
		std::size_t count) {
		for (std::size_t i = 0; i < count; ++i) {
			dispatcher.Register(
				entries[i].method,
				[payload = std::string(entries[i].payloadJson)](const protocol::RequestFrame& request) {
					return protocol::OkResponse(request, std::move(payload));
				});
		}
	}

} // namespace blazeclaw::gateway
