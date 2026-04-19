#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

class GatewayToolRegistry;

/// Named handlers for `gateway.tools.executions.*` (wire-stable payloads; used by `RegisterToolExecutionHistoryHandlers`).
namespace handlers::tool_execution {

struct ToolExecutionHistoryHandlers {
	static protocol::ResponseFrame HandleExecutionsList(
		const protocol::RequestFrame& request,
		GatewayToolRegistry& registry);
	static protocol::ResponseFrame HandleExecutionsCount(
		const protocol::RequestFrame& request,
		GatewayToolRegistry& registry);
	static protocol::ResponseFrame HandleExecutionsLatest(
		const protocol::RequestFrame& request,
		GatewayToolRegistry& registry);
	static protocol::ResponseFrame HandleExecutionsClear(
		const protocol::RequestFrame& request,
		GatewayToolRegistry& registry);
};

} // namespace handlers::tool_execution

} // namespace blazeclaw::gateway
