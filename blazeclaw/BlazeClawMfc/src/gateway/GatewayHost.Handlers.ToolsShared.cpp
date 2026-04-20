#include "pch.h"
#include "GatewayHostHandlersToolsShared.h"

#include "GatewayJsonSerializers.h"
#include "GatewayRequestParams.h"
#include "GatewayToolRegistry.h"

namespace blazeclaw::gateway::handlers::tools_shared {

void ToolsSharedHandlers::RegisterToolsList(
	GatewayMethodDispatcher& dispatcher,
	GatewayToolRegistry& registry) {
	dispatcher.Register("gateway.tools.list", [&registry](const protocol::RequestFrame& request) {
		return HandleToolsList(request, registry);
	});
}

void ToolsSharedHandlers::RegisterToolsCatalog(
	GatewayMethodDispatcher& dispatcher,
	GatewayToolRegistry& registry) {
	dispatcher.Register("gateway.tools.catalog", [&registry](const protocol::RequestFrame& request) {
		return HandleToolsCatalog(request, registry);
	});
}

protocol::ResponseFrame ToolsSharedHandlers::HandleToolsList(
	const protocol::RequestFrame& request,
	GatewayToolRegistry& registry) {
	const std::string category = RequestParamsView(request.paramsJson).GetString("category");
	const auto tools = registry.List();
	std::string toolsJson = "[";
	std::size_t count = 0;
	for (std::size_t i = 0; i < tools.size(); ++i) {
		if (!category.empty() && tools[i].category != category) {
			continue;
		}
		if (count > 0) {
			toolsJson += ",";
		}
		toolsJson += SerializeTool(tools[i]);
		++count;
	}
	toolsJson += "]";

	return protocol::OkResponse(
		request,
		"{\"tools\":" + toolsJson + ",\"count\":" + std::to_string(count) + "}");
}

protocol::ResponseFrame ToolsSharedHandlers::HandleToolsCatalog(
	const protocol::RequestFrame& request,
	GatewayToolRegistry& registry) {
	const auto tools = registry.List();
	std::string toolsJson = "[";
	for (std::size_t i = 0; i < tools.size(); ++i) {
		if (i > 0) {
			toolsJson += ",";
		}

		toolsJson += SerializeTool(tools[i]);
	}

	toolsJson += "]";

	return protocol::OkResponse(request, "{\"tools\":" + toolsJson + "}");
}

} // namespace blazeclaw::gateway::handlers::tools_shared
