#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersSupplementaryCatalog.h"
#include "GatewayHostCatalogHelpers.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewayJsonSerializers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayRequestParams.h"
#include "Telemetry.h"
#include "GatewayJsonUtils.h"

#include <algorithm>

namespace blazeclaw::gateway::handlers::supplementary_catalog {

void SupplementaryCatalogHandlers::RegisterAll(GatewayHost& host) {
host.m_dispatcher.Register("gateway.session.list", [&host](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = RequestParamsView(request.paramsJson).GetBool("active");
			const std::string scopeFilter = RequestParamsView(request.paramsJson).GetString("scope");
			const auto sessions = host.m_sessionRegistry.List();
			std::string sessionArray = "[";
			bool first = true;
			std::size_t count = 0;
			std::string activeSessionId = "none";
			for (std::size_t i = 0; i < sessions.size(); ++i) {
				if (activeFilter.has_value() && sessions[i].active != activeFilter.value()) {
					continue;
				}

				if (!scopeFilter.empty() && sessions[i].scope != scopeFilter) {
					continue;
				}

				if (!first) {
					sessionArray += ",";
				}

				sessionArray += SerializeSession(sessions[i]);
				if (activeSessionId == "none" && sessions[i].active) {
					activeSessionId = sessions[i].id;
				}

				first = false;
				++count;
			}

			sessionArray += "]";

			return protocol::OkResponse(request, "{\"sessions\":" + sessionArray + ",\"count\":" + std::to_string(count) +
					",\"activeSessionId\":\"" + EscapeJsonString(activeSessionId) + "\"}");
			});

		host.m_dispatcher.Register("gateway.events.catalog", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"events\":" + SerializeStringArray(GatewayEventCatalogNames()) + "}");
			});

		host.m_dispatcher.Register("gateway.events.exists", [](const protocol::RequestFrame& request) {
			const std::string eventName = RequestParamsView(request.paramsJson).GetString("event");
			const auto& events = GatewayEventCatalogNames();
			const bool exists = std::any_of(events.begin(), events.end(), [&](const std::string& item) {
				return eventName.empty() || item == eventName;
				});

			return protocol::OkResponse(request, "{\"event\":\"" + EscapeJsonString(eventName.empty() ? "*" : eventName) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("gateway.events.count", [](const protocol::RequestFrame& request) {
			const std::string eventName = RequestParamsView(request.paramsJson).GetString("event");
			const auto& events = GatewayEventCatalogNames();
			const std::size_t count = static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const std::string& item) {
				return eventName.empty() || item == eventName;
				}));

			return protocol::OkResponse(request, "{\"event\":\"" + EscapeJsonString(eventName.empty() ? "*" : eventName) +
					"\",\"count\":" + std::to_string(count) + "}");
			});

		host.m_dispatcher.Register("gateway.tools.exists", [&host](const protocol::RequestFrame& request) {
			const std::string requestedTool = RequestParamsView(request.paramsJson).GetString("tool");
			const auto tools = host.m_toolRegistry.List();
			const bool exists = std::any_of(tools.begin(), tools.end(), [&](const ToolCatalogEntry& tool) {
				return requestedTool.empty() || tool.id == requestedTool;
				});

			return protocol::OkResponse(request, "{\"tool\":\"" + EscapeJsonString(requestedTool.empty() ? "*" : requestedTool) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("gateway.tools.count", [&host](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = RequestParamsView(request.paramsJson).GetBool("active");
			const auto tools = host.m_toolRegistry.List();
			const std::size_t count = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [&](const ToolCatalogEntry& tool) {
				return !activeFilter.has_value() || tool.enabled == activeFilter.value();
				}));

			return protocol::OkResponse(request, "{\"active\":" + std::string(activeFilter.value_or(false) ? "true" : "false") +
					",\"activeFilterApplied\":" + std::string(activeFilter.has_value() ? "true" : "false") +
					",\"count\":" + std::to_string(count) + "}");
			});

		host.m_dispatcher.Register("gateway.models.exists", [](const protocol::RequestFrame& request) {
			const std::string modelId = RequestParamsView(request.paramsJson).GetString("modelId");
			const bool exists = modelId.empty() || modelId == "default" || modelId == "reasoner";

			return protocol::OkResponse(request, "{\"modelId\":\"" + EscapeJsonString(modelId.empty() ? "*" : modelId) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("gateway.config.exists", [&host](const protocol::RequestFrame& request) {
			const std::string key = RequestParamsView(request.paramsJson).GetString("key");
			const bool exists = key.empty() ||
				key == "gateway.bind" ||
				key == "gateway.port" ||
				key == "agent.model" ||
				key == "agent.streaming" ||
				key == "embeddings.enabled" ||
				key == "embeddings.provider" ||
				key == "embeddings.model_path" ||
				key == "embeddings.tokenizer_path" ||
				key == "embeddings.dimension" ||
				key == "embeddings.max_sequence_length" ||
				key == "embeddings.normalize" ||
				key == "embeddings.intra_threads" ||
				key == "embeddings.inter_threads" ||
				key == "embeddings.execution_mode";

			return protocol::OkResponse(request, "{\"key\":\"" + EscapeJsonString(key.empty() ? "*" : key) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("gateway.health.details", [&host](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"status\":\"ok\",\"running\":" + std::string(host.IsRunning() ? "true" : "false") +
					",\"transport\":{\"running\":" + std::string(host.m_transport.IsRunning() ? "true" : "false") +
					",\"endpoint\":\"" + EscapeJsonString(host.m_transport.Endpoint()) + "\",\"connections\":" + std::to_string(host.m_transport.ConnectionCount()) + "}}");
			});

		host.m_dispatcher.Register("gateway.logs.count", [](const protocol::RequestFrame& request) {
			const std::string level = RequestParamsView(request.paramsJson).GetString("level");
			const std::vector<std::string> levels = { "info", "info", "debug" };
			const std::size_t count = static_cast<std::size_t>(std::count_if(levels.begin(), levels.end(), [&](const std::string& item) {
				return level.empty() || item == level;
				}));

			return protocol::OkResponse(request, "{\"level\":\"" + EscapeJsonString(level.empty() ? "*" : level) +
					"\",\"count\":" + std::to_string(count) + "}");
			});

		host.m_dispatcher.Register("gateway.config.count", [](const protocol::RequestFrame& request) {
			const std::string section = RequestParamsView(request.paramsJson).GetString("section");
			const std::size_t count = section == "gateway" || section == "agent"
				? 2
				: 4;

			return protocol::OkResponse(request, "{\"section\":\"" + EscapeJsonString(section.empty() ? "*" : section) +
					"\",\"count\":" + std::to_string(count) + "}");
			});

		host.m_dispatcher.Register("gateway.models.count", [](const protocol::RequestFrame& request) {
			const std::string provider = RequestParamsView(request.paramsJson).GetString("provider");
			const std::size_t count = provider.empty() || provider == "seed" ? 2 : 0;

			return protocol::OkResponse(request, "{\"provider\":\"" + EscapeJsonString(provider.empty() ? "*" : provider) +
					"\",\"count\":" + std::to_string(count) + "}");
			});

		host.m_dispatcher.Register("gateway.events.get", [](const protocol::RequestFrame& request) {
			const std::string eventName = RequestParamsView(request.paramsJson).GetString("event");
			const auto& events = GatewayEventCatalogNames();
			std::string selected = events.empty() ? "unknown" : events.front();
			for (const auto& item : events) {
				if (!eventName.empty() && item != eventName) {
					continue;
				}
				selected = item;
				break;
			}

			return protocol::OkResponse(request, "{\"event\":\"" + EscapeJsonString(selected) + "\"}");
			});


		host.m_dispatcher.Register("gateway.logs.levels", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"levels\":[\"info\",\"debug\"],\"count\":2}");
			});

		host.m_dispatcher.Register("gateway.tools.list", [&host](const protocol::RequestFrame& request) {
			const std::string category = RequestParamsView(request.paramsJson).GetString("category");
			const auto tools = host.m_toolRegistry.List();
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

			return protocol::OkResponse(request, "{\"tools\":" + toolsJson + ",\"count\":" + std::to_string(count) + "}");
			});

		host.m_dispatcher.Register("gateway.models.listByProvider", [](const protocol::RequestFrame& request) {
			const std::string provider = RequestParamsView(request.paramsJson).GetString("provider");
			const bool includeAll = provider.empty();
			const bool includeSeed = includeAll || provider == "seed";
			const bool includeDeepSeek = includeAll || provider == "deepseek";

			std::string modelsJson = "[";
			std::size_t count = 0;
			if (includeSeed) {
				modelsJson += GatewayModel::BuildModelJson(GatewayModel::kDefaultModelId);
				modelsJson += "," + GatewayModel::BuildModelJson(GatewayModel::kReasonerModelId);
				count += 2;
			}

			if (includeDeepSeek) {
				if (count > 0) {
					modelsJson += ",";
				}

				modelsJson += GatewayModel::BuildModelJson(GatewayModel::kDeepSeekChatModelId);
				modelsJson += "," + GatewayModel::BuildModelJson(GatewayModel::kDeepSeekReasonerModelId);
				count += 2;
			}

			modelsJson += "]";

			return protocol::OkResponse(request, "{\"provider\":\"" + EscapeJsonString(provider.empty() ? "*" : provider) + "\",\"models\":" + modelsJson + ",\"count\":" + std::to_string(count) + "}");
			});
}

} // namespace blazeclaw::gateway::handlers::supplementary_catalog

namespace blazeclaw::gateway {

void GatewayHost::RegisterGatewaySupplementaryCatalogHandlers() {
	handlers::supplementary_catalog::SupplementaryCatalogHandlers::RegisterAll(*this);
}

} // namespace blazeclaw::gateway
