#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

class GatewayToolRegistry;

/// Shared wire-stable handlers for `gateway.tools.list` / `gateway.tools.catalog` (used by full registration and `StartLocalRuntimeDispatchOnly`).
namespace handlers::tools_shared {

struct ToolsSharedHandlers {
	static protocol::ResponseFrame HandleToolsList(
		const protocol::RequestFrame& request,
		GatewayToolRegistry& registry);
	static protocol::ResponseFrame HandleToolsCatalog(
		const protocol::RequestFrame& request,
		GatewayToolRegistry& registry);
};

} // namespace handlers::tools_shared

} // namespace blazeclaw::gateway
