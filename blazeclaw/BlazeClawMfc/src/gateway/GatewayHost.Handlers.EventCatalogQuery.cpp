#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersEventCatalogQuery.h"
#include "GatewayHostCatalogHelpers.h"
#include "GatewayJsonSerializers.h"
#include "GatewayRequestParams.h"

#include <algorithm>

namespace blazeclaw::gateway::handlers::event_catalog_query {

protocol::ResponseFrame EventCatalogQueryHandlers::HandleLatestByType(const protocol::RequestFrame& request) {
	const std::string type = RequestParamsView(request.paramsJson).GetString("type");
	const bool lifecycle = type == "lifecycle";
	const std::string event = lifecycle ? "gateway.shutdown" : "gateway.tools.catalog.update";
	return protocol::OkResponse(
		request,
		"{\"type\":\"" + EscapeJsonString(type.empty() ? "update" : type) + "\",\"event\":\"" +
			EscapeJsonString(event) + "\"}");
}

protocol::ResponseFrame EventCatalogQueryHandlers::HandleConfigSnapshot(
	const protocol::RequestFrame& request,
	GatewayHost& host) {
	return protocol::OkResponse(
		request,
		"{\"gateway\":{\"bind\":\"" + EscapeJsonString(host.m_runtimeGatewayBind) +
			"\",\"port\":" + std::to_string(host.m_runtimeGatewayPort) + "},\"agent\":{\"model\":\"" +
			EscapeJsonString(host.m_runtimeAgentModel) + "\",\"streaming\":" +
			std::string(host.m_runtimeAgentStreaming ? "true" : "false") + "},\"emailFallback\":{\"preflightEnabled\":" +
			std::string(host.m_runtimeEmailPreflightEnabled ? "true" : "false") +
			",\"policyProfilesEnabled\":" +
			std::string(host.m_runtimeEmailPolicyProfilesEnabled ? "true" : "false") +
			",\"policyProfilesEnforce\":" +
			std::string(host.m_runtimeEmailPolicyProfilesEnforce ? "true" : "false") +
			"},\"deepseek\":" +
			BuildGatewayDeepSeekConfigJson(
				host.m_runtimeDeepSeekApiKey,
				host.m_runtimeDeepSeekBaseUrl,
				host.m_runtimeDeepSeekDefaultModel) +
			"}");
}

protocol::ResponseFrame EventCatalogQueryHandlers::HandleSummary(const protocol::RequestFrame& request) {
	const auto& events = GatewayEventCatalogNames();
	const std::size_t lifecycle = static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [](const std::string& item) {
		return item == "gateway.tick" || item == "gateway.health" || item == "gateway.shutdown";
		}));
	const std::size_t updates = events.size() - lifecycle;

	return protocol::OkResponse(
		request,
		"{\"total\":" + std::to_string(events.size()) + ",\"lifecycle\":" + std::to_string(lifecycle) +
			",\"updates\":" + std::to_string(updates) + "}");
}

protocol::ResponseFrame EventCatalogQueryHandlers::HandleSearch(const protocol::RequestFrame& request) {
	const std::string term = RequestParamsView(request.paramsJson).GetString("term");
	const auto& events = GatewayEventCatalogNames();
	std::string eventsJson = "[";
	std::size_t count = 0;
	for (std::size_t i = 0; i < events.size(); ++i) {
		if (!term.empty() && events[i].find(term) == std::string::npos) {
			continue;
		}
		if (count > 0) {
			eventsJson += ",";
		}
		eventsJson += "\"" + EscapeJsonString(events[i]) + "\"";
		++count;
	}
	eventsJson += "]";

	return protocol::OkResponse(
		request,
		"{\"term\":\"" + EscapeJsonString(term.empty() ? "*" : term) + "\",\"events\":" + eventsJson +
			",\"count\":" + std::to_string(count) + "}");
}

protocol::ResponseFrame EventCatalogQueryHandlers::HandleLast(const protocol::RequestFrame& request) {
	const auto& events = GatewayEventCatalogNames();
	const std::string last = events.empty() ? "none" : events.back();
	return protocol::OkResponse(request, "{\"event\":\"" + EscapeJsonString(last) + "\"}");
}

protocol::ResponseFrame EventCatalogQueryHandlers::HandleToolsCategories(
	const protocol::RequestFrame& request,
	GatewayToolRegistry& registry) {
	const auto tools = registry.List();
	std::vector<std::string> categories;
	for (std::size_t i = 0; i < tools.size(); ++i) {
		if (std::find(categories.begin(), categories.end(), tools[i].category) == categories.end()) {
			categories.push_back(tools[i].category);
		}
	}

	return protocol::OkResponse(
		request,
		"{\"categories\":" + SerializeStringArray(categories) + ",\"count\":" + std::to_string(categories.size()) + "}");
}

protocol::ResponseFrame EventCatalogQueryHandlers::HandleEventsList(const protocol::RequestFrame& request) {
	const auto& events = GatewayEventCatalogNames();
	return protocol::OkResponse(
		request,
		"{\"events\":" + SerializeStringArray(events) + ",\"count\":" + std::to_string(events.size()) + "}");
}

} // namespace blazeclaw::gateway::handlers::event_catalog_query

namespace blazeclaw::gateway {

void GatewayHost::RegisterGatewayEventCatalogQueryHandlers() {
	using handlers::event_catalog_query::EventCatalogQueryHandlers;
	m_dispatcher.Register("gateway.events.latestByType", [](const protocol::RequestFrame& request) {
		return EventCatalogQueryHandlers::HandleLatestByType(request);
		});

	m_dispatcher.Register("gateway.config.snapshot", [this](const protocol::RequestFrame& request) {
		return EventCatalogQueryHandlers::HandleConfigSnapshot(request, *this);
		});

	m_dispatcher.Register("gateway.events.summary", [](const protocol::RequestFrame& request) {
		return EventCatalogQueryHandlers::HandleSummary(request);
		});

	m_dispatcher.Register("gateway.events.search", [](const protocol::RequestFrame& request) {
		return EventCatalogQueryHandlers::HandleSearch(request);
		});

	m_dispatcher.Register("gateway.events.last", [](const protocol::RequestFrame& request) {
		return EventCatalogQueryHandlers::HandleLast(request);
		});

	m_dispatcher.Register("gateway.tools.categories", [this](const protocol::RequestFrame& request) {
		return EventCatalogQueryHandlers::HandleToolsCategories(request, m_toolRegistry);
		});

	m_dispatcher.Register("gateway.events.list", [](const protocol::RequestFrame& request) {
		return EventCatalogQueryHandlers::HandleEventsList(request);
		});
}

} // namespace blazeclaw::gateway
