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
		dispatcher.Register("commands.list", [&registry](const protocol::RequestFrame& request) {
			return HandleToolsList(request, registry);
			});
		dispatcher.Register("tools.effective", [&registry](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string sessionId = params.GetString("sessionId");
			const std::string agentId = params.GetString("agentId");
			const std::string provider = params.GetString("provider");
			const std::string category = params.GetString("category");
			if (sessionId.empty()) {
				return protocol::ErrorResponse(request, "invalid_request", "sessionId required");
			}
			if (agentId.empty()) {
				return protocol::ErrorResponse(request, "invalid_request", "agentId required");
			}

			auto tools = registry.List();
			std::vector<ToolCatalogEntry> filtered;
			filtered.reserve(tools.size());
			for (const auto& tool : tools) {
				if (!category.empty() && tool.category != category) {
					continue;
				}
				if (sessionId != "main" && tool.category == "system") {
					continue;
				}
				filtered.push_back(tool);
			}

			std::string toolsJson = "[";
			for (std::size_t i = 0; i < filtered.size(); ++i) {
				if (i > 0) {
					toolsJson += ",";
				}
				toolsJson += SerializeTool(filtered[i]);
			}
			toolsJson += "]";

			return protocol::OkResponse(
				request,
				"{\"tools\":" + toolsJson +
				",\"count\":" + std::to_string(filtered.size()) +
				",\"context\":{\"sessionId\":\"" + sessionId +
				"\",\"agentId\":\"" + agentId + "\"}}"
			);
			});
	}

	void ToolsSharedHandlers::RegisterToolsCatalog(
		GatewayMethodDispatcher& dispatcher,
		GatewayToolRegistry& registry) {
		auto registerToolsCatalog = [&dispatcher, &registry](const std::string& methodName) {
			dispatcher.Register(methodName, [&registry](const protocol::RequestFrame& request) {
				return HandleToolsCatalog(request, registry);
				});
			};
		registerToolsCatalog("gateway.tools.catalog");
		registerToolsCatalog("tools.catalog");
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
