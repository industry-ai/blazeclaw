#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

class GatewayHost;
class GatewayToolRegistry;

/// Named handlers for `gateway.events.*` query surface and related catalog helpers (`RegisterGatewayEventCatalogQueryHandlers`).
namespace handlers::event_catalog_query {

struct EventCatalogQueryHandlers {
	static protocol::ResponseFrame HandleLatestByType(const protocol::RequestFrame& request);
	static protocol::ResponseFrame HandleConfigSnapshot(const protocol::RequestFrame& request, GatewayHost& host);
	static protocol::ResponseFrame HandleSummary(const protocol::RequestFrame& request);
	static protocol::ResponseFrame HandleSearch(const protocol::RequestFrame& request);
	static protocol::ResponseFrame HandleLast(const protocol::RequestFrame& request);
	static protocol::ResponseFrame HandleToolsCategories(const protocol::RequestFrame& request, GatewayToolRegistry& registry);
	static protocol::ResponseFrame HandleEventsList(const protocol::RequestFrame& request);
};

} // namespace handlers::event_catalog_query

} // namespace blazeclaw::gateway
