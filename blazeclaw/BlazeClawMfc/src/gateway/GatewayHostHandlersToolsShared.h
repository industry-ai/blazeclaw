#pragma once

#include "GatewayMethodDispatcher.h"
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
	/// Single `Register("gateway.tools.list", …)` site for dispatcher hygiene (Phase B).
	static void RegisterToolsList(GatewayMethodDispatcher& dispatcher, GatewayToolRegistry& registry);
	/// Single `Register("gateway.tools.catalog", …)` site for dispatcher hygiene (Phase B).
	static void RegisterToolsCatalog(GatewayMethodDispatcher& dispatcher, GatewayToolRegistry& registry);
};

} // namespace handlers::tools_shared

} // namespace blazeclaw::gateway
