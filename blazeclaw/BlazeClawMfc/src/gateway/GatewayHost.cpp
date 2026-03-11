#include "pch.h"
#include "GatewayHost.h"

#include "GatewayProtocolCodec.h"
#include "GatewayProtocolSchemaValidator.h"

namespace blazeclaw::gateway {
	namespace {

		std::string ToNarrow(const std::wstring& value) {
			std::string result;
			result.reserve(value.size());

			for (const wchar_t ch : value) {
				result.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return result;
		}

		std::string EscapeJson(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);

			for (const char ch : value) {
				switch (ch) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}

			return escaped;
		}

		std::optional<bool> ExtractBooleanParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return std::nullopt;
			}

			const std::string token = "\"" + fieldName + "\"";
			const std::string& params = paramsJson.value();
			const std::size_t keyPos = params.find(token);
			if (keyPos == std::string::npos) {
				return std::nullopt;
			}

			std::size_t valuePos = params.find(':', keyPos + token.size());
			if (valuePos == std::string::npos) {
				return std::nullopt;
			}

			++valuePos;
			while (valuePos < params.size() && std::isspace(static_cast<unsigned char>(params[valuePos])) != 0) {
				++valuePos;
			}

			if (params.compare(valuePos, 4, "true") == 0) {
				return true;
			}

			if (params.compare(valuePos, 5, "false") == 0) {
				return false;
			}

			return std::nullopt;
		}

		std::string SerializeSession(const SessionEntry& session) {
			return "{\"id\":\"" + EscapeJson(session.id) + "\",\"scope\":\"" + EscapeJson(session.scope) +
				"\",\"active\":" + std::string(session.active ? "true" : "false") + "}";
		}

		std::string SerializeAgent(const AgentEntry& agent) {
			return "{\"id\":\"" + EscapeJson(agent.id) + "\",\"name\":\"" + EscapeJson(agent.name) +
				"\",\"active\":" + std::string(agent.active ? "true" : "false") + "}";
		}

		std::string SerializeAgentFile(const AgentFileEntry& file) {
			return "{\"path\":\"" + EscapeJson(file.path) + "\",\"size\":" + std::to_string(file.size) +
				",\"updatedMs\":" + std::to_string(file.updatedMs) + "}";
		}

		std::string SerializeAgentFileContent(const AgentFileContentEntry& file) {
			return "{\"path\":\"" + EscapeJson(file.path) + "\",\"size\":" + std::to_string(file.size) +
				",\"updatedMs\":" + std::to_string(file.updatedMs) +
				",\"content\":\"" + EscapeJson(file.content) + "\"}";
		}

		std::string SerializeChannelStatus(const ChannelStatusEntry& channel) {
			return "{\"id\":\"" + EscapeJson(channel.id) + "\",\"label\":\"" + EscapeJson(channel.label) +
				"\",\"connected\":" + std::string(channel.connected ? "true" : "false") +
				",\"accounts\":" + std::to_string(channel.accountCount) + "}";
		}

		std::string SerializeChannelAccount(const ChannelAccountEntry& account) {
			return "{\"channel\":\"" + EscapeJson(account.channel) + "\",\"accountId\":\"" +
				EscapeJson(account.accountId) + "\",\"label\":\"" + EscapeJson(account.label) +
				"\",\"active\":" + std::string(account.active ? "true" : "false") +
				",\"connected\":" + std::string(account.connected ? "true" : "false") + "}";
		}

		std::string SerializeChannelRoute(const ChannelRouteEntry& route) {
			return "{\"channel\":\"" + EscapeJson(route.channel) + "\",\"accountId\":\"" +
				EscapeJson(route.accountId) + "\",\"agentId\":\"" + EscapeJson(route.agentId) +
				"\",\"sessionId\":\"" + EscapeJson(route.sessionId) + "\"}";
		}

		std::string SerializeTool(const ToolCatalogEntry& tool) {
			return "{\"id\":\"" + EscapeJson(tool.id) + "\",\"label\":\"" + EscapeJson(tool.label) +
				"\",\"category\":\"" + EscapeJson(tool.category) + "\",\"enabled\":" +
				std::string(tool.enabled ? "true" : "false") + "}";
		}

		std::string SerializeStringArray(const std::vector<std::string>& values) {
			std::string json = "[";
			for (std::size_t i = 0; i < values.size(); ++i) {
				if (i > 0) {
					json += ",";
				}

				json += "\"" + EscapeJson(values[i]) + "\"";
			}

			json += "]";
			return json;
		}

		const std::vector<std::string>& EventCatalogNames() {
			static const std::vector<std::string> events = {
				"gateway.agent.update",
				"gateway.channels.accounts.update",
				"gateway.channels.update",
				"gateway.health",
				"gateway.session.reset",
				"gateway.shutdown",
				"gateway.tick",
				"gateway.tools.catalog.update",
			};

			return events;
		}

		std::string ExtractStringParam(const std::optional<std::string>& paramsJson, const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return {};
			}

			const std::string token = "\"" + fieldName + "\"";
			const std::string& params = paramsJson.value();
			const std::size_t keyPos = params.find(token);
			if (keyPos == std::string::npos) {
				return {};
			}

			std::size_t valuePos = params.find(':', keyPos + token.size());
			if (valuePos == std::string::npos) {
				return {};
			}

			valuePos = params.find('"', valuePos);
			if (valuePos == std::string::npos) {
				return {};
			}

			const std::size_t endQuote = params.find('"', valuePos + 1);
			if (endQuote == std::string::npos) {
				return {};
			}

			return params.substr(valuePos + 1, endQuote - valuePos - 1);
		}

		std::optional<std::size_t> ExtractNumericParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return std::nullopt;
			}

			const std::string token = "\"" + fieldName + "\"";
			const std::string& params = paramsJson.value();
			const std::size_t keyPos = params.find(token);
			if (keyPos == std::string::npos) {
				return std::nullopt;
			}

			std::size_t valuePos = params.find(':', keyPos + token.size());
			if (valuePos == std::string::npos) {
				return std::nullopt;
			}

			++valuePos;
			while (valuePos < params.size() && std::isspace(static_cast<unsigned char>(params[valuePos])) != 0) {
				++valuePos;
			}

			if (valuePos >= params.size() || std::isdigit(static_cast<unsigned char>(params[valuePos])) == 0) {
				return std::nullopt;
			}

			std::size_t endPos = valuePos;
			while (endPos < params.size() && std::isdigit(static_cast<unsigned char>(params[endPos])) != 0) {
				++endPos;
			}

			try {
				return static_cast<std::size_t>(std::stoull(params.substr(valuePos, endPos - valuePos)));
			}
			catch (...) {
				return std::nullopt;
			}
		}

	} // namespace

	bool GatewayHost::Start(const blazeclaw::config::GatewayConfig& config) {
		if (m_running) {
			return true;
		}

		if (config.bindAddress.empty() || config.port == 0) {
			m_lastWarning = "Invalid gateway bind configuration.";
			return false;
		}

		m_bindAddress = ToNarrow(config.bindAddress);
		m_port = config.port;
		m_runtimeGatewayBind = m_bindAddress;
		m_runtimeGatewayPort = m_port;
		RegisterDefaultHandlers();

		std::string fixtureError;
		if (!protocol::GatewayProtocolContract::ValidateFixtureParity("blazeclaw/fixtures/gateway", fixtureError)) {
			m_lastWarning = fixtureError;
		}
		else {
			m_lastWarning.clear();
		}

		std::string transportError;
		if (!m_transport.Start(
			m_bindAddress,
			m_port,
			[this](const std::string& inboundFrame) {
				return HandleInboundText(inboundFrame);
			},
			transportError)) {
			m_lastWarning = transportError;
			return false;
		}

		m_running = true;
		return true;
	}

	void GatewayHost::Stop() {
		m_transport.Stop();
		m_running = false;
		m_bindAddress.clear();
		m_port = 0;
	}

	bool GatewayHost::IsRunning() const noexcept {
		return m_running;
	}

	std::string GatewayHost::LastWarning() const {
		return m_lastWarning;
	}

	bool GatewayHost::AcceptConnection(const std::string& connectionId, std::string& error) {
		return m_transport.AcceptConnection(connectionId, error);
	}

	bool GatewayHost::PumpInboundFrame(
		const std::string& connectionId,
		const std::string& inboundFrame,
		std::string& error) {
		return m_transport.ProcessInboundFrame(connectionId, inboundFrame, error);
	}

	std::vector<std::string> GatewayHost::DrainOutboundFrames(
		const std::string& connectionId,
		std::string& error) {
		return m_transport.DrainOutboundFrames(connectionId, error);
	}

	bool GatewayHost::PumpNetworkOnce(std::string& error) {
		return m_transport.PumpNetworkOnce(error);
	}

	std::string GatewayHost::BuildTickEventFrame(std::uint64_t timestampMs, std::uint64_t seq) const {
		protocol::EventFrame frame{
			.eventName = "gateway.tick",
			.payloadJson = "{\"ts\":" + std::to_string(timestampMs) +
				",\"running\":" + std::string(IsRunning() ? "true" : "false") +
				",\"connections\":" + std::to_string(m_transport.ConnectionCount()) + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"tick\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildChannelsAccountsUpdateEventFrame(std::uint64_t seq) const {
		const auto accounts = m_channelRegistry.ListAccounts();
		std::string accountsJson = "[";
		for (std::size_t i = 0; i < accounts.size(); ++i) {
			if (i > 0) {
				accountsJson += ",";
			}

			accountsJson += SerializeChannelAccount(accounts[i]);
		}

		accountsJson += "]";

		protocol::EventFrame frame{
			.eventName = "gateway.channels.accounts.update",
			.payloadJson = "{\"accounts\":" + accountsJson + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"channels.accounts.update\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildAgentUpdateEventFrame(const std::string& agentId, std::uint64_t seq) const {
		const AgentEntry agent = m_agentRegistry.Get(agentId);

		protocol::EventFrame frame{
			.eventName = "gateway.agent.update",
			.payloadJson = "{\"agentId\":\"" + agent.id + "\",\"agent\":" + SerializeAgent(agent) + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"agent.update\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildToolsCatalogUpdateEventFrame(std::uint64_t seq) const {
		const auto tools = m_toolRegistry.List();
		std::string toolsJson = "[";
		for (std::size_t i = 0; i < tools.size(); ++i) {
			if (i > 0) {
				toolsJson += ",";
			}

			toolsJson += SerializeTool(tools[i]);
		}

		toolsJson += "]";

		protocol::EventFrame frame{
			.eventName = "gateway.tools.catalog.update",
			.payloadJson = "{\"tools\":" + toolsJson + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"tools.catalog.update\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildChannelsUpdateEventFrame(std::uint64_t seq) const {
		const auto channels = m_channelRegistry.ListStatus();
		std::string channelsJson = "[";
		for (std::size_t i = 0; i < channels.size(); ++i) {
			if (i > 0) {
				channelsJson += ",";
			}

			channelsJson += SerializeChannelStatus(channels[i]);
		}

		channelsJson += "]";

		protocol::EventFrame frame{
			.eventName = "gateway.channels.update",
			.payloadJson = "{\"channels\":" + channelsJson + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"channels.update\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildSessionResetEventFrame(const std::string& sessionId, std::uint64_t seq) const {
		const SessionEntry session = m_sessionRegistry.Resolve(sessionId);

		protocol::EventFrame frame{
			.eventName = "gateway.session.reset",
			.payloadJson = "{\"sessionId\":\"" + session.id + "\",\"session\":" + SerializeSession(session) + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"session.reset\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildHealthEventFrame(std::uint64_t seq) const {
		protocol::EventFrame frame{
			.eventName = "gateway.health",
			.payloadJson = "{\"status\":\"ok\",\"running\":true}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"health\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildShutdownEventFrame(const std::string& reason, std::uint64_t seq) const {
		protocol::EventFrame frame{
			.eventName = "gateway.shutdown",
			.payloadJson = "{\"reason\":\"" + reason + "\",\"graceful\":true,\"seq\":" + std::to_string(seq) + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"shutdown\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::HandleInboundText(const std::string& inboundJson) const {
		protocol::RequestFrame request;
		std::string decodeError;
		if (!protocol::TryDecodeRequestFrame(inboundJson, request, decodeError)) {
			const protocol::ResponseFrame errorResponse{
				.id = "",
				.ok = false,
				.payloadJson = std::nullopt,
				.error = protocol::ErrorShape{
					.code = "invalid_frame",
					.message = decodeError,
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};

			return protocol::EncodeResponseFrame(errorResponse);
		}

		protocol::SchemaValidationIssue validationIssue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateRequest(request, validationIssue)) {
			const protocol::ResponseFrame schemaErrorResponse{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = protocol::ErrorShape{
					.code = validationIssue.code.empty() ? "schema_validation_failed" : validationIssue.code,
					.message = validationIssue.message.empty() ? "Request failed schema validation." : validationIssue.message,
					.detailsJson = "{\"method\":\"" + request.method + "\"}",
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};

			return protocol::EncodeResponseFrame(schemaErrorResponse);
		}

		const protocol::ResponseFrame routedResponse = RouteRequest(request);
		if (!protocol::GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			request.method,
			routedResponse,
			validationIssue)) {
			const protocol::ResponseFrame schemaErrorResponse{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = protocol::ErrorShape{
					.code = validationIssue.code.empty() ? "schema_invalid_response" : validationIssue.code,
					.message = validationIssue.message.empty()
						? "Handler response failed schema validation."
						: validationIssue.message,
					.detailsJson = "{\"method\":\"" + request.method + "\"}",
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};

			return protocol::EncodeResponseFrame(schemaErrorResponse);
		}

		return protocol::EncodeResponseFrame(routedResponse);
	}

	protocol::ResponseFrame GatewayHost::RouteRequest(const protocol::RequestFrame& request) const {
		return m_dispatcher.Dispatch(request);
	}

	void GatewayHost::RegisterDefaultHandlers() {
		m_dispatcher.Register("gateway.ping", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"pong\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.exists", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const auto agents = m_agentRegistry.List();
			const bool exists = std::any_of(agents.begin(), agents.end(), [&](const AgentEntry& agent) {
				return requestedId.empty() || agent.id == requestedId;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"agentId\":\"" + EscapeJson(requestedId.empty() ? "*" : requestedId) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.count", [this](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = ExtractBooleanParam(request.paramsJson, "active");
			const auto agents = m_agentRegistry.List();
			const std::size_t count = static_cast<std::size_t>(std::count_if(agents.begin(), agents.end(), [&](const AgentEntry& agent) {
				return !activeFilter.has_value() || agent.active == activeFilter.value();
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"active\":" + std::string(activeFilter.value_or(false) ? "true" : "false") +
					",\"activeFilterApplied\":" + std::string(activeFilter.has_value() ? "true" : "false") +
					",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.exists", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const auto sessions = m_sessionRegistry.List();
			const bool exists = std::any_of(sessions.begin(), sessions.end(), [&](const SessionEntry& session) {
				return requestedId.empty() || session.id == requestedId;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"sessionId\":\"" + EscapeJson(requestedId.empty() ? "*" : requestedId) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.count", [this](const protocol::RequestFrame& request) {
			const std::string scope = ExtractStringParam(request.paramsJson, "scope");
			const std::optional<bool> active = ExtractBooleanParam(request.paramsJson, "active");
			const auto sessions = m_sessionRegistry.List();
			const std::size_t count = static_cast<std::size_t>(std::count_if(sessions.begin(), sessions.end(), [&](const SessionEntry& session) {
				if (!scope.empty() && session.scope != scope) {
					return false;
				}
				if (active.has_value() && session.active != active.value()) {
					return false;
				}
				return true;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"scope\":\"" + EscapeJson(scope.empty() ? "*" : scope) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.activate", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const auto sessions = m_sessionRegistry.List();
			const bool exists = std::any_of(sessions.begin(), sessions.end(), [&](const SessionEntry& session) {
				return requestedId.empty() || session.id == requestedId;
				});
			const SessionEntry activated = m_sessionRegistry.Patch(requestedId, std::nullopt, true);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(activated) +
					",\"activated\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.reset", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::size_t cleared = m_channelRegistry.ClearAccounts(channel);
			const std::size_t restored = m_channelRegistry.RestoreAccounts(channel);
			const std::size_t total = m_channelRegistry.ListAccounts().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"cleared\":" + std::to_string(cleared) +
					",\"restored\":" + std::to_string(restored) +
					",\"total\":" + std::to_string(total) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.restore", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::size_t restored = m_channelRegistry.RestoreAccounts(channel);
			const std::size_t total = m_channelRegistry.ListAccounts().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"restored\":" + std::to_string(restored) +
					",\"total\":" + std::to_string(total) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.route.reset", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			ChannelRouteEntry removedRoute;
			const bool deleted = m_channelRegistry.DeleteRoute(channel, accountId, removedRoute);
			bool restored = false;
			const ChannelRouteEntry route = m_channelRegistry.RestoreRoute(channel, accountId, restored);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"route\":" + SerializeChannelRoute(route) +
					",\"deleted\":" + std::string(deleted ? "true" : "false") +
					",\"restored\":" + std::string(restored ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.count", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const auto accounts = m_channelRegistry.ListAccounts();
			const std::size_t count = static_cast<std::size_t>(std::count_if(
				accounts.begin(),
				accounts.end(),
				[&](const ChannelAccountEntry& account) {
					return channel.empty() || account.channel == channel;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":\"" + EscapeJson(channel.empty() ? "*" : channel) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.clear", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::size_t cleared = m_channelRegistry.ClearAccounts(channel);
			const std::size_t remaining = m_channelRegistry.ListAccounts().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"cleared\":" + std::to_string(cleared) +
					",\"remaining\":" + std::to_string(remaining) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.routes.reset", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const RouteResetResult result = m_channelRegistry.ResetRoutes(channel);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"cleared\":" + std::to_string(result.cleared) +
					",\"restored\":" + std::to_string(result.restored) +
					",\"total\":" + std::to_string(result.total) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.routes.count", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const auto routes = m_channelRegistry.ListRoutes();
			const std::size_t count = static_cast<std::size_t>(std::count_if(
				routes.begin(),
				routes.end(),
				[&](const ChannelRouteEntry& route) {
					return channel.empty() || route.channel == channel;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":\"" + EscapeJson(channel.empty() ? "*" : channel) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.route.restore", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			bool restored = false;
			const ChannelRouteEntry route = m_channelRegistry.RestoreRoute(channel, accountId, restored);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"route\":" + SerializeChannelRoute(route) +
					",\"restored\":" + std::string(restored ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.route.patch", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			const bool hasAgentId = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"agentId\"") != std::string::npos;
			const bool hasSessionId = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"sessionId\"") != std::string::npos;
			const std::optional<std::string> agentId = hasAgentId
				? std::optional<std::string>(ExtractStringParam(request.paramsJson, "agentId"))
				: std::nullopt;
			const std::optional<std::string> sessionId = hasSessionId
				? std::optional<std::string>(ExtractStringParam(request.paramsJson, "sessionId"))
				: std::nullopt;
			bool updated = false;
			const ChannelRouteEntry route = m_channelRegistry.PatchRoute(channel, accountId, agentId, sessionId, updated);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"route\":" + SerializeChannelRoute(route) +
					",\"updated\":" + std::string(updated ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.route.get", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			const ChannelRouteEntry route = m_channelRegistry.GetRoute(channel, accountId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"route\":" + SerializeChannelRoute(route) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.routes.restore", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::size_t restored = m_channelRegistry.RestoreRoutes(channel);
			const std::size_t total = m_channelRegistry.ListRoutes().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"restored\":" + std::to_string(restored) +
					",\"total\":" + std::to_string(total) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.routes.clear", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::size_t cleared = m_channelRegistry.ClearRoutes(channel);
			const std::size_t remaining = m_channelRegistry.ListRoutes().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"cleared\":" + std::to_string(cleared) +
					",\"remaining\":" + std::to_string(remaining) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.delete", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			bool deleted = false;
			const ChannelAccountEntry account = m_channelRegistry.DeleteAccount(channel, accountId, deleted);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"account\":" + SerializeChannelAccount(account) +
					",\"deleted\":" + std::string(deleted ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.create", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			const bool hasLabel = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"label\"") != std::string::npos;
			const std::optional<std::string> label = hasLabel
				? std::optional<std::string>(ExtractStringParam(request.paramsJson, "label"))
				: std::nullopt;
			const std::optional<bool> active = ExtractBooleanParam(request.paramsJson, "active");
			const std::optional<bool> connected = ExtractBooleanParam(request.paramsJson, "connected");
			bool created = false;
			const ChannelAccountEntry account = m_channelRegistry.CreateAccount(
				channel,
				accountId,
				label,
				active,
				connected,
				created);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"account\":" + SerializeChannelAccount(account) +
					",\"created\":" + std::string(created ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.get", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			const ChannelAccountEntry account = m_channelRegistry.GetAccount(channel, accountId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"account\":" + SerializeChannelAccount(account) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.update", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			const bool hasLabel = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"label\"") != std::string::npos;
			const std::optional<std::string> label = hasLabel
				? std::optional<std::string>(ExtractStringParam(request.paramsJson, "label"))
				: std::nullopt;
			const std::optional<bool> active = ExtractBooleanParam(request.paramsJson, "active");
			const std::optional<bool> connected = ExtractBooleanParam(request.paramsJson, "connected");
			bool updated = false;
			const ChannelAccountEntry account = m_channelRegistry.UpdateAccount(
				channel,
				accountId,
				label,
				active,
				connected,
				updated);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"account\":" + SerializeChannelAccount(account) +
					",\"updated\":" + std::string(updated ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.exists", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			const bool exists = m_channelRegistry.AccountExists(channel, accountId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":\"" + EscapeJson(channel.empty() ? "*" : channel) +
					"\",\"accountId\":\"" + EscapeJson(accountId.empty() ? "*" : accountId) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.deactivate", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			bool deactivated = false;
			const ChannelAccountEntry account = m_channelRegistry.DeactivateAccount(channel, accountId, deactivated);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"account\":" + SerializeChannelAccount(account) +
					",\"deactivated\":" + std::string(deactivated ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts.activate", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			bool activated = false;
			const ChannelAccountEntry account = m_channelRegistry.ActivateAccount(channel, accountId, activated);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"account\":" + SerializeChannelAccount(account) +
					",\"activated\":" + std::string(activated ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.route.exists", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			const bool exists = m_channelRegistry.RouteExists(channel, accountId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":\"" + EscapeJson(channel.empty() ? "*" : channel) +
					"\",\"accountId\":\"" + EscapeJson(accountId.empty() ? "*" : accountId) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.route.delete", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			ChannelRouteEntry removedRoute;
			const bool deleted = m_channelRegistry.DeleteRoute(channel, accountId, removedRoute);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"route\":" + SerializeChannelRoute(removedRoute) +
					",\"deleted\":" + std::string(deleted ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.route.set", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			const std::string agentId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string sessionId = ExtractStringParam(request.paramsJson, "sessionId");
			const ChannelRouteEntry route = m_channelRegistry.SetRoute(channel, accountId, agentId, sessionId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"route\":" + SerializeChannelRoute(route) + ",\"saved\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.files.exists", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedPath = ExtractStringParam(request.paramsJson, "path");
			const AgentFileExistsResult result = m_agentRegistry.ExistsFile(requestedId, requestedPath);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"path\":\"" + EscapeJson(result.path) +
					"\",\"exists\":" + std::string(result.exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.files.delete", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedPath = ExtractStringParam(request.paramsJson, "path");
			const AgentFileDeleteResult result = m_agentRegistry.DeleteFile(requestedId, requestedPath);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"file\":" + SerializeAgentFileContent(result.file) +
					",\"deleted\":" + std::string(result.deleted ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.files.set", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedPath = ExtractStringParam(request.paramsJson, "path");
			const std::string content = ExtractStringParam(request.paramsJson, "content");
			const AgentFileContentEntry file = m_agentRegistry.SetFile(requestedId, requestedPath, content);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"file\":" + SerializeAgentFileContent(file) + ",\"saved\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.files.get", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedPath = ExtractStringParam(request.paramsJson, "path");
			const AgentFileContentEntry file = m_agentRegistry.GetFile(requestedId, requestedPath);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"file\":" + SerializeAgentFileContent(file) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.files.list", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const auto files = m_agentRegistry.ListFiles(requestedId);
			std::string filesJson = "[";
			for (std::size_t i = 0; i < files.size(); ++i) {
				if (i > 0) {
					filesJson += ",";
				}

				filesJson += SerializeAgentFile(files[i]);
			}

			filesJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"files\":" + filesJson + ",\"count\":" + std::to_string(files.size()) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.list", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"models\":[{\"id\":\"default\",\"provider\":\"seed\",\"displayName\":\"Default Model\",\"streaming\":true},{\"id\":\"reasoner\",\"provider\":\"seed\",\"displayName\":\"Reasoner Model\",\"streaming\":false}]}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.call.execute", [this](const protocol::RequestFrame& request) {
			const std::string requestedTool = ExtractStringParam(request.paramsJson, "tool");
			const ToolExecuteResult execution = m_toolRegistry.Execute(requestedTool);
			const bool argsProvided = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"args\"") != std::string::npos;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tool\":\"" + EscapeJson(execution.tool) +
					"\",\"executed\":" + std::string(execution.executed ? "true" : "false") +
					",\"status\":\"" + EscapeJson(execution.status) +
					"\",\"output\":\"" + EscapeJson(execution.output) +
					"\",\"argsProvided\":" + std::string(argsProvided ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.logout", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string accountId = ExtractStringParam(request.paramsJson, "accountId");
			const ChannelLogoutResult result = m_channelRegistry.Logout(channel, accountId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"loggedOut\":" + std::string(result.loggedOut ? "true" : "false") +
					",\"affected\":" + std::to_string(result.affected) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.update", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedName = ExtractStringParam(request.paramsJson, "name");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");
			const AgentEntry updated = m_agentRegistry.Update(
				requestedId,
				requestedName.empty() ? std::nullopt : std::optional<std::string>(requestedName),
				requestedActive);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"agent\":" + SerializeAgent(updated) + ",\"updated\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.delete", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			AgentEntry removedAgent;
			const bool deleted = m_agentRegistry.Delete(requestedId, removedAgent);
			const std::size_t remaining = m_agentRegistry.List().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"agent\":" + SerializeAgent(removedAgent) +
					",\"deleted\":" + std::string(deleted ? "true" : "false") +
					",\"remaining\":" + std::to_string(remaining) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.create", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedName = ExtractStringParam(request.paramsJson, "name");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");
			const AgentEntry created = m_agentRegistry.Create(
				requestedId,
				requestedName.empty() ? std::nullopt : std::optional<std::string>(requestedName),
				requestedActive);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"agent\":" + SerializeAgent(created) + ",\"created\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.patch", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const std::string requestedScope = ExtractStringParam(request.paramsJson, "scope");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");
			const SessionEntry patched = m_sessionRegistry.Patch(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(patched) + ",\"patched\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.preview", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const SessionEntry session = m_sessionRegistry.Resolve(requestedId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(session) +
					",\"title\":\"Session " + EscapeJson(session.id) +
					"\",\"hasMessages\":true,\"unread\":0}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.compact", [this](const protocol::RequestFrame& request) {
			const bool dryRun = ExtractBooleanParam(request.paramsJson, "dryRun").value_or(false);
			const std::size_t compacted = dryRun ? m_sessionRegistry.CountCompactCandidates() : m_sessionRegistry.CompactInactive();
			const std::size_t remaining = m_sessionRegistry.List().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"compacted\":" + std::to_string(compacted) +
					",\"remaining\":" + std::to_string(remaining) +
					",\"dryRun\":" + std::string(dryRun ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.usage", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const SessionEntry session = m_sessionRegistry.Resolve(requestedId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"sessionId\":\"" + EscapeJson(session.id) +
					"\",\"messages\":42,\"tokens\":{\"input\":1024,\"output\":512,\"total\":1536},\"lastActiveMs\":1735689600200}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.delete", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			SessionEntry removedSession;
			const bool deleted = m_sessionRegistry.Delete(requestedId, removedSession);
			const std::size_t remaining = m_sessionRegistry.List().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(removedSession) +
					",\"deleted\":" + std::string(deleted ? "true" : "false") +
					",\"remaining\":" + std::to_string(remaining) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.protocol.version", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"minProtocol\":1,\"maxProtocol\":1}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.features.list", [this](const protocol::RequestFrame& request) {
			const std::string methodsJson = SerializeStringArray(m_dispatcher.RegisteredMethods());
			const std::string eventsJson = SerializeStringArray(EventCatalogNames());

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"methods\":" + methodsJson + ",\"events\":" + eventsJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.list", [this](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = ExtractBooleanParam(request.paramsJson, "active");
			const auto agents = m_agentRegistry.List();
			std::string payload = "{\"agents\":[";
			bool first = true;
			std::string activeAgentId = "none";
			std::size_t count = 0;
			for (std::size_t i = 0; i < agents.size(); ++i) {
				if (activeFilter.has_value() && agents[i].active != activeFilter.value()) {
					continue;
				}

				if (!first) {
					payload += ",";
				}

				payload += SerializeAgent(agents[i]);
				if (activeAgentId == "none" && agents[i].active) {
					activeAgentId = agents[i].id;
				}

				first = false;
				++count;
			}

			payload += "],\"count\":" + std::to_string(count) + ",\"activeAgentId\":\"" + EscapeJson(activeAgentId) + "\"}";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts", [this](const protocol::RequestFrame& request) {
			const std::string channelFilter = ExtractStringParam(request.paramsJson, "channel");
			const auto accounts = m_channelRegistry.ListAccounts();
			std::string accountsJson = "[";
			bool first = true;
			for (std::size_t i = 0; i < accounts.size(); ++i) {
				if (!channelFilter.empty() && accounts[i].channel != channelFilter) {
					continue;
				}

				if (!first) {
					accountsJson += ",";
				}

				accountsJson += SerializeChannelAccount(accounts[i]);
				first = false;
			}

			accountsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"accounts\":" + accountsJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.status.get", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const auto statuses = m_channelRegistry.ListStatus();
			ChannelStatusEntry selected{};
			bool found = false;
			for (const auto& status : statuses) {
				if (!channel.empty() && status.id != channel) {
					continue;
				}
				selected = status;
				found = true;
				break;
			}
			if (!found) {
				if (!statuses.empty()) {
					selected = statuses.front();
				}
				else {
					selected = ChannelStatusEntry{ .id = channel.empty() ? "unknown" : channel, .label = "Unknown", .connected = false, .accountCount = 0 };
				}
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":" + SerializeChannelStatus(selected) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.status.exists", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const auto statuses = m_channelRegistry.ListStatus();
			const bool exists = std::any_of(statuses.begin(), statuses.end(), [&](const ChannelStatusEntry& status) {
				return channel.empty() || status.id == channel;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":\"" + EscapeJson(channel.empty() ? "*" : channel) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.status.count", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const auto statuses = m_channelRegistry.ListStatus();
			const std::size_t count = static_cast<std::size_t>(std::count_if(statuses.begin(), statuses.end(), [&](const ChannelStatusEntry& status) {
				return channel.empty() || status.id == channel;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":\"" + EscapeJson(channel.empty() ? "*" : channel) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.route.resolve", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string account = ExtractStringParam(request.paramsJson, "accountId");
			const std::string sessionId = ExtractStringParam(request.paramsJson, "sessionId");
			const std::string agentId = ExtractStringParam(request.paramsJson, "agentId");
			const ChannelRouteEntry route = m_channelRegistry.ResolveRoute(channel, account);
			const SessionEntry session = m_sessionRegistry.Resolve(sessionId.empty() ? route.sessionId : sessionId);
			const AgentEntry agent = m_agentRegistry.Get(agentId.empty() ? route.agentId : agentId);
			const ChannelRouteEntry resolvedRoute{
				.channel = route.channel,
				.accountId = route.accountId,
				.agentId = agent.id,
				.sessionId = session.id,
			};

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"route\":" + SerializeChannelRoute(resolvedRoute) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.get", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const AgentEntry agent = m_agentRegistry.Get(requestedId);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"agent\":" + SerializeAgent(agent) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.catalog", [this](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			std::string toolsJson = "[";
			for (std::size_t i = 0; i < tools.size(); ++i) {
				if (i > 0) {
					toolsJson += ",";
				}

				toolsJson += SerializeTool(tools[i]);
			}

			toolsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tools\":" + toolsJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.call.preview", [this](const protocol::RequestFrame& request) {
			const std::string requestedTool = ExtractStringParam(request.paramsJson, "tool");
			const ToolPreviewResult preview = m_toolRegistry.Preview(requestedTool);
			const bool argsProvided = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"args\"") != std::string::npos;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tool\":\"" + EscapeJson(preview.tool) + "\",\"allowed\":" +
					std::string(preview.allowed ? "true" : "false") + ",\"reason\":\"" +
					EscapeJson(preview.reason) + "\",\"argsProvided\":" +
					std::string(argsProvided ? "true" : "false") +
					",\"policy\":\"seeded_preview_v1\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.activate", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const AgentEntry agent = m_agentRegistry.Activate(requestedId);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"agent\":" + SerializeAgent(agent) + ",\"event\":\"gateway.agent.update\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.get", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
		.payloadJson = "{\"gateway\":{\"bind\":\"" + EscapeJson(m_runtimeGatewayBind) +
			"\",\"port\":" + std::to_string(m_runtimeGatewayPort) +
			"},\"agent\":{\"model\":\"" + EscapeJson(m_runtimeAgentModel) +
			"\",\"streaming\":" + std::string(m_runtimeAgentStreaming ? "true" : "false") + "}}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.set", [this](const protocol::RequestFrame& request) {
			const std::string bind = ExtractStringParam(request.paramsJson, "bind");
			const std::optional<std::size_t> port = ExtractNumericParam(request.paramsJson, "port");
			const std::string model = ExtractStringParam(request.paramsJson, "model");
			const std::optional<bool> streaming = ExtractBooleanParam(request.paramsJson, "streaming");

			if (!bind.empty()) {
				m_runtimeGatewayBind = bind;
			}

			if (port.has_value() && port.value() > 0 && port.value() <= 65535) {
				m_runtimeGatewayPort = static_cast<std::uint16_t>(port.value());
			}

			if (!model.empty()) {
				m_runtimeAgentModel = model;
			}

			if (streaming.has_value()) {
				m_runtimeAgentStreaming = streaming.value();
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"gateway\":{\"bind\":\"" + EscapeJson(m_runtimeGatewayBind) +
					"\",\"port\":" + std::to_string(m_runtimeGatewayPort) +
					"},\"agent\":{\"model\":\"" + EscapeJson(m_runtimeAgentModel) +
					"\",\"streaming\":" + std::string(m_runtimeAgentStreaming ? "true" : "false") +
					"},\"updated\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.routes", [this](const protocol::RequestFrame& request) {
			const std::string channelFilter = ExtractStringParam(request.paramsJson, "channel");
			const auto routes = m_channelRegistry.ListRoutes();
			std::string routesJson = "[";
			bool first = true;
			for (std::size_t i = 0; i < routes.size(); ++i) {
				if (!channelFilter.empty() && routes[i].channel != channelFilter) {
					continue;
				}

				if (!first) {
					routesJson += ",";
				}

				routesJson += SerializeChannelRoute(routes[i]);
				first = false;
			}

			routesJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"routes\":" + routesJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.status", [this](const protocol::RequestFrame& request) {
			const std::string channelFilter = ExtractStringParam(request.paramsJson, "channel");
			const auto channels = m_channelRegistry.ListStatus();
			std::string channelsJson = "[";
			bool first = true;
			for (std::size_t i = 0; i < channels.size(); ++i) {
				if (!channelFilter.empty() && channels[i].id != channelFilter) {
					continue;
				}

				if (!first) {
					channelsJson += ",";
				}

				channelsJson += SerializeChannelStatus(channels[i]);
				first = false;
			}

			channelsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channels\":" + channelsJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.logs.tail", [](const protocol::RequestFrame& request) {
			const std::vector<std::string> seededEntries = {
				"{\"ts\":1735689600000,\"level\":\"info\",\"source\":\"gateway\",\"message\":\"Gateway host started\"}",
				"{\"ts\":1735689600100,\"level\":\"info\",\"source\":\"transport\",\"message\":\"WebSocket listener active\"}",
				"{\"ts\":1735689600200,\"level\":\"debug\",\"source\":\"dispatcher\",\"message\":\"Method handlers registered\"}",
			};

			const std::size_t requestedLimit = ExtractNumericParam(request.paramsJson, "limit").value_or(50);
			const std::size_t cappedLimit = std::max<std::size_t>(1, std::min<std::size_t>(requestedLimit, 200));
			const std::size_t emitCount = std::min<std::size_t>(cappedLimit, seededEntries.size());
			const std::size_t begin = seededEntries.size() - emitCount;

			std::string entriesJson = "[";
			for (std::size_t i = begin; i < seededEntries.size(); ++i) {
				if (i > begin) {
					entriesJson += ",";
				}

				entriesJson += seededEntries[i];
			}

			entriesJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"entries\":" + entriesJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.resolve", [this](const protocol::RequestFrame& request) {
			const std::string sessionId = ExtractStringParam(request.paramsJson, "sessionId");

			const SessionEntry resolved = m_sessionRegistry.Resolve(sessionId);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(resolved) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.create", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const std::string requestedScope = ExtractStringParam(request.paramsJson, "scope");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");

			const SessionEntry created = m_sessionRegistry.Create(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(created) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.reset", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const std::string requestedScope = ExtractStringParam(request.paramsJson, "scope");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");

			const SessionEntry reset = m_sessionRegistry.Reset(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(reset) + ",\"event\":\"gateway.session.reset\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.health", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"status\":\"ok\",\"running\":" + std::string(IsRunning() ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.transport.status", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"running\":" + std::string(m_transport.IsRunning() ? "true" : "false") +
					",\"endpoint\":\"" + m_transport.Endpoint() + "\",\"connections\":" +
					std::to_string(m_transport.ConnectionCount()) +
					",\"timeouts\":{\"handshake\":" + std::to_string(m_transport.HandshakeTimeoutCount()) +
					",\"idle\":" + std::to_string(m_transport.IdleTimeoutCloseCount()) +
					"},\"closes\":{\"invalidUtf8\":" + std::to_string(m_transport.InvalidUtf8CloseCount()) +
					",\"messageTooBig\":" + std::to_string(m_transport.MessageTooBigCloseCount()) +
					",\"extensionRejected\":" + std::to_string(m_transport.ExtensionRejectCount()) + "}}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.session.list", [this](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = ExtractBooleanParam(request.paramsJson, "active");
			const std::string scopeFilter = ExtractStringParam(request.paramsJson, "scope");
			const auto sessions = m_sessionRegistry.List();
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

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"sessions\":" + sessionArray + ",\"count\":" + std::to_string(count) +
					",\"activeSessionId\":\"" + EscapeJson(activeSessionId) + "\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.catalog", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"events\":" + SerializeStringArray(EventCatalogNames()) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.exists", [](const protocol::RequestFrame& request) {
			const std::string eventName = ExtractStringParam(request.paramsJson, "event");
			const auto& events = EventCatalogNames();
			const bool exists = std::any_of(events.begin(), events.end(), [&](const std::string& item) {
				return eventName.empty() || item == eventName;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"event\":\"" + EscapeJson(eventName.empty() ? "*" : eventName) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.count", [](const protocol::RequestFrame& request) {
			const std::string eventName = ExtractStringParam(request.paramsJson, "event");
			const auto& events = EventCatalogNames();
			const std::size_t count = static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const std::string& item) {
				return eventName.empty() || item == eventName;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"event\":\"" + EscapeJson(eventName.empty() ? "*" : eventName) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.exists", [this](const protocol::RequestFrame& request) {
			const std::string requestedTool = ExtractStringParam(request.paramsJson, "tool");
			const auto tools = m_toolRegistry.List();
			const bool exists = std::any_of(tools.begin(), tools.end(), [&](const ToolCatalogEntry& tool) {
				return requestedTool.empty() || tool.id == requestedTool;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tool\":\"" + EscapeJson(requestedTool.empty() ? "*" : requestedTool) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.count", [this](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = ExtractBooleanParam(request.paramsJson, "active");
			const auto tools = m_toolRegistry.List();
			const std::size_t count = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [&](const ToolCatalogEntry& tool) {
				return !activeFilter.has_value() || tool.enabled == activeFilter.value();
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"active\":" + std::string(activeFilter.value_or(false) ? "true" : "false") +
					",\"activeFilterApplied\":" + std::string(activeFilter.has_value() ? "true" : "false") +
					",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.exists", [](const protocol::RequestFrame& request) {
			const std::string modelId = ExtractStringParam(request.paramsJson, "modelId");
			const bool exists = modelId.empty() || modelId == "default" || modelId == "reasoner";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"modelId\":\"" + EscapeJson(modelId.empty() ? "*" : modelId) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.exists", [this](const protocol::RequestFrame& request) {
			const std::string key = ExtractStringParam(request.paramsJson, "key");
			const bool exists = key.empty() ||
				key == "gateway.bind" ||
				key == "gateway.port" ||
				key == "agent.model" ||
				key == "agent.streaming";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"key\":\"" + EscapeJson(key.empty() ? "*" : key) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.keys", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"keys\":[\"gateway.bind\",\"gateway.port\",\"agent.model\",\"agent.streaming\"],\"count\":4}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.transport.connections.count", [this](const protocol::RequestFrame& request) {
			const std::size_t count = m_transport.ConnectionCount();
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.health.details", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"status\":\"ok\",\"running\":" + std::string(IsRunning() ? "true" : "false") +
					",\"transport\":{\"running\":" + std::string(m_transport.IsRunning() ? "true" : "false") +
					",\"endpoint\":\"" + EscapeJson(m_transport.Endpoint()) + "\",\"connections\":" + std::to_string(m_transport.ConnectionCount()) + "}}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.logs.count", [](const protocol::RequestFrame& request) {
			const std::string level = ExtractStringParam(request.paramsJson, "level");
			const std::vector<std::string> levels = { "info", "info", "debug" };
			const std::size_t count = static_cast<std::size_t>(std::count_if(levels.begin(), levels.end(), [&](const std::string& item) {
				return level.empty() || item == level;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"level\":\"" + EscapeJson(level.empty() ? "*" : level) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.count", [](const protocol::RequestFrame& request) {
			const std::string section = ExtractStringParam(request.paramsJson, "section");
			const std::size_t count = section == "gateway" || section == "agent"
				? 2
				: 4;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"section\":\"" + EscapeJson(section.empty() ? "*" : section) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.count", [](const protocol::RequestFrame& request) {
			const std::string provider = ExtractStringParam(request.paramsJson, "provider");
			const std::size_t count = provider.empty() || provider == "seed" ? 2 : 0;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"provider\":\"" + EscapeJson(provider.empty() ? "*" : provider) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.get", [](const protocol::RequestFrame& request) {
			const std::string eventName = ExtractStringParam(request.paramsJson, "event");
			const auto& events = EventCatalogNames();
			std::string selected = events.empty() ? "unknown" : events.front();
			for (const auto& item : events) {
				if (!eventName.empty() && item != eventName) {
					continue;
				}
				selected = item;
				break;
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"event\":\"" + EscapeJson(selected) + "\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.transport.endpoint.get", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"endpoint\":\"" + EscapeJson(m_transport.Endpoint()) + "\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.logs.levels", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"levels\":[\"info\",\"debug\"],\"count\":2}",
				.error = std::nullopt,
			};
			});
	}

} // namespace blazeclaw::gateway
