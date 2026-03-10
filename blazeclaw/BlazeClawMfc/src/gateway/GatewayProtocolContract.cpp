#include "pch.h"
#include "GatewayProtocolContract.h"

#include "GatewayProtocolCodec.h"
#include "GatewayProtocolSchemaValidator.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace blazeclaw::gateway::protocol {
	namespace {

		std::string ReadFileText(const std::filesystem::path& path) {
			std::ifstream input(path, std::ios::in | std::ios::binary);
			if (!input.is_open()) {
				return {};
			}

			std::ostringstream buffer;
			buffer << input.rdbuf();
			return buffer.str();
		}

		std::string TrimBoundaryWhitespace(const std::string& value) {
			std::size_t start = 0;
			std::size_t end = value.size();

			while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}

			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}

			return value.substr(start, end - start);
		}

		bool CompareFixture(const std::filesystem::path& path, const std::string& actual, std::string& error) {
			const std::string expected = TrimBoundaryWhitespace(ReadFileText(path));
			if (expected.empty()) {
				error = "Missing or empty fixture: " + path.string();
				return false;
			}

			const std::string normalizedActual = TrimBoundaryWhitespace(actual);
			if (expected != normalizedActual) {
				error = "Fixture mismatch: " + path.string();
				return false;
			}

			return true;
		}

	} // namespace

	bool GatewayProtocolContract::ValidateFixtureParity(const std::string& fixtureRoot, std::string& error) {
		const std::filesystem::path root(fixtureRoot);
		const std::string requestFixture = TrimBoundaryWhitespace(ReadFileText(root / "request_ping.json"));
		const std::string invalidProtocolParamsRequestFixture =
			TrimBoundaryWhitespace(ReadFileText(root / "request_invalid_protocol_params.json"));

		RequestFrame decodedRequest;
		std::string decodeError;
		if (!TryDecodeRequestFrame(requestFixture, decodedRequest, decodeError)) {
			error = "Request frame decode failed: " + decodeError;
			return false;
		}

		if (decodedRequest.id != "req-1" || decodedRequest.method != "gateway.ping") {
			error = "Decoded request frame does not match canonical request fixture.";
			return false;
		}

		if (!decodedRequest.paramsJson.has_value() || decodedRequest.paramsJson.value() != "{\"echo\":\"hello\"}") {
			error = "Decoded request params do not match canonical request fixture.";
			return false;
		}

		RequestFrame invalidProtocolParamsRequest;
		std::string invalidDecodeError;
		if (!TryDecodeRequestFrame(
			invalidProtocolParamsRequestFixture,
			invalidProtocolParamsRequest,
			invalidDecodeError)) {
			error = "Invalid protocol params request decode failed: " + invalidDecodeError;
			return false;
		}

		SchemaValidationIssue validationIssue;
		if (GatewayProtocolSchemaValidator::ValidateRequest(invalidProtocolParamsRequest, validationIssue)) {
			error = "Invalid protocol params request unexpectedly passed schema validation.";
			return false;
		}

		const RequestFrame request{
			.id = "req-1",
			.method = "gateway.ping",
			.paramsJson = "{\"echo\":\"hello\"}",
		};

		const ResponseFrame response{
			.id = "req-1",
			.ok = true,
			.payloadJson = "{\"pong\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame protocolVersionResponse{
			.id = "req-2",
			.ok = true,
			.payloadJson = "{\"minProtocol\":1,\"maxProtocol\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame featuresListResponse{
			.id = "req-3",
			.ok = true,
	  .payloadJson = "{\"methods\":[\"gateway.agents.activate\",\"gateway.agents.create\",\"gateway.agents.delete\",\"gateway.agents.files.delete\",\"gateway.agents.files.exists\",\"gateway.agents.files.get\",\"gateway.agents.files.list\",\"gateway.agents.files.set\",\"gateway.agents.get\",\"gateway.agents.list\",\"gateway.agents.update\",\"gateway.channels.accounts\",\"gateway.channels.logout\",\"gateway.channels.route.resolve\",\"gateway.channels.routes\",\"gateway.channels.status\",\"gateway.config.get\",\"gateway.config.set\",\"gateway.events.catalog\",\"gateway.features.list\",\"gateway.health\",\"gateway.logs.tail\",\"gateway.models.list\",\"gateway.ping\",\"gateway.protocol.version\",\"gateway.session.list\",\"gateway.sessions.compact\",\"gateway.sessions.create\",\"gateway.sessions.delete\",\"gateway.sessions.patch\",\"gateway.sessions.preview\",\"gateway.sessions.reset\",\"gateway.sessions.resolve\",\"gateway.sessions.usage\",\"gateway.tools.call.execute\",\"gateway.tools.call.preview\",\"gateway.tools.catalog\",\"gateway.transport.status\"],\"events\":[\"gateway.agent.update\",\"gateway.channels.accounts.update\",\"gateway.channels.update\",\"gateway.health\",\"gateway.session.reset\",\"gateway.shutdown\",\"gateway.tick\",\"gateway.tools.catalog.update\"]}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsListResponse{
			.id = "req-16",
			.ok = true,
		   .payloadJson = "{\"agents\":[{\"id\":\"default\",\"name\":\"Default Agent\",\"active\":true}],\"count\":1,\"activeAgentId\":\"default\"}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsGetResponse{
			.id = "req-17",
			.ok = true,
			.payloadJson = "{\"agent\":{\"id\":\"default\",\"name\":\"Default Agent\",\"active\":true}}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsActivateResponse{
			.id = "req-18",
			.ok = true,
			.payloadJson = "{\"agent\":{\"id\":\"default\",\"name\":\"Default Agent\",\"active\":true},\"event\":\"gateway.agent.update\"}",
			.error = std::nullopt,
		};

		const ResponseFrame configGetResponse{
			.id = "req-9",
			.ok = true,
			.payloadJson = "{\"gateway\":{\"bind\":\"127.0.0.1\",\"port\":18789},\"agent\":{\"model\":\"default\",\"streaming\":true}}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsStatusResponse{
			.id = "req-10",
			.ok = true,
			.payloadJson = "{\"channels\":[{\"id\":\"telegram\",\"label\":\"Telegram\",\"connected\":false,\"accounts\":1},{\"id\":\"discord\",\"label\":\"Discord\",\"connected\":false,\"accounts\":1}]}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsResponse{
			.id = "req-19",
			.ok = true,
			.payloadJson = "{\"accounts\":[{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":false},{\"channel\":\"discord\",\"accountId\":\"discord.default\",\"label\":\"Discord Default\",\"active\":true,\"connected\":false}]}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRoutesResponse{
			.id = "req-13",
			.ok = true,
			.payloadJson = "{\"routes\":[{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"},{\"channel\":\"discord\",\"accountId\":\"discord.default\",\"agentId\":\"default\",\"sessionId\":\"main\"}]}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRouteResolveResponse{
			.id = "req-20",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"}}",
			.error = std::nullopt,
		};

		const ResponseFrame logsTailResponse{
			.id = "req-11",
			.ok = true,
			.payloadJson = "{\"entries\":[{\"ts\":1735689600000,\"level\":\"info\",\"source\":\"gateway\",\"message\":\"Gateway host started\"},{\"ts\":1735689600100,\"level\":\"info\",\"source\":\"transport\",\"message\":\"WebSocket listener active\"},{\"ts\":1735689600200,\"level\":\"debug\",\"source\":\"dispatcher\",\"message\":\"Method handlers registered\"}]}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsCatalogResponse{
			.id = "req-21",
			.ok = true,
			.payloadJson = "{\"tools\":[{\"id\":\"chat.send\",\"label\":\"Chat Send\",\"category\":\"messaging\",\"enabled\":true},{\"id\":\"memory.search\",\"label\":\"Memory Search\",\"category\":\"knowledge\",\"enabled\":true}]}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsCallPreviewResponse{
			.id = "req-22",
			.ok = true,
			.payloadJson = "{\"tool\":\"none\",\"allowed\":false,\"reason\":\"missing_tool\",\"argsProvided\":false,\"policy\":\"seeded_preview_v1\"}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsResolveResponse{
			.id = "req-12",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true}}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsCreateResponse{
			.id = "req-14",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true}}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsResetResponse{
			.id = "req-15",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true},\"event\":\"gateway.session.reset\"}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsDeleteResponse{
			.id = "req-23",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"thread-1\",\"scope\":\"thread\",\"active\":false},\"deleted\":true,\"remaining\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsUsageResponse{
			.id = "req-24",
			.ok = true,
			.payloadJson = "{\"sessionId\":\"main\",\"messages\":42,\"tokens\":{\"input\":1024,\"output\":512,\"total\":1536},\"lastActiveMs\":1735689600200}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsCompactResponse{
			.id = "req-25",
			.ok = true,
			.payloadJson = "{\"compacted\":1,\"remaining\":1,\"dryRun\":false}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsPreviewResponse{
			.id = "req-26",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true},\"title\":\"Session main\",\"hasMessages\":true,\"unread\":0}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsPatchResponse{
			.id = "req-27",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true},\"patched\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsCreateResponse{
			.id = "req-28",
			.ok = true,
			.payloadJson = "{\"agent\":{\"id\":\"builder\",\"name\":\"Agent builder\",\"active\":false},\"created\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsDeleteResponse{
			.id = "req-29",
			.ok = true,
			.payloadJson = "{\"agent\":{\"id\":\"builder\",\"name\":\"Agent builder\",\"active\":false},\"deleted\":true,\"remaining\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsUpdateResponse{
			.id = "req-30",
			.ok = true,
			.payloadJson = "{\"agent\":{\"id\":\"builder\",\"name\":\"Builder Prime\",\"active\":true},\"updated\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsLogoutResponse{
			.id = "req-31",
			.ok = true,
			.payloadJson = "{\"loggedOut\":true,\"affected\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsCallExecuteResponse{
			.id = "req-32",
			.ok = true,
			.payloadJson = "{\"tool\":\"chat.send\",\"executed\":true,\"status\":\"ok\",\"output\":\"seeded_execution_v1\",\"argsProvided\":false}",
			.error = std::nullopt,
		};

		const ResponseFrame configSetResponse{
			.id = "req-33",
			.ok = true,
			.payloadJson = "{\"gateway\":{\"bind\":\"127.0.0.1\",\"port\":18789},\"agent\":{\"model\":\"default\",\"streaming\":true},\"updated\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsListResponse{
			.id = "req-34",
			.ok = true,
			.payloadJson = "{\"models\":[{\"id\":\"default\",\"provider\":\"seed\",\"displayName\":\"Default Model\",\"streaming\":true},{\"id\":\"reasoner\",\"provider\":\"seed\",\"displayName\":\"Reasoner Model\",\"streaming\":false}]}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsFilesListResponse{
			.id = "req-35",
			.ok = true,
			.payloadJson = "{\"files\":[{\"path\":\"agents/default/profile.json\",\"size\":512,\"updatedMs\":1735689600000},{\"path\":\"agents/default/memory.txt\",\"size\":2048,\"updatedMs\":1735689605000}],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsFilesGetResponse{
			.id = "req-36",
			.ok = true,
			.payloadJson = "{\"file\":{\"path\":\"agents/default/profile.json\",\"size\":512,\"updatedMs\":1735689600000,\"content\":\"seeded_content_for_agents/default/profile.json\"}}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsFilesSetResponse{
			.id = "req-37",
			.ok = true,
			.payloadJson = "{\"file\":{\"path\":\"agents/default/profile.json\",\"size\":5,\"updatedMs\":1735689620000,\"content\":\"hello\"},\"saved\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsFilesDeleteResponse{
			.id = "req-38",
			.ok = true,
			.payloadJson = "{\"file\":{\"path\":\"agents/default/profile.json\",\"size\":0,\"updatedMs\":1735689630000,\"content\":\"\"},\"deleted\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsFilesExistsResponse{
			.id = "req-39",
			.ok = true,
			.payloadJson = "{\"path\":\"agents/default/profile.json\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame healthResponse{
			.id = "req-5",
			.ok = true,
			.payloadJson = "{\"status\":\"ok\",\"running\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionListResponse{
			.id = "req-6",
			.ok = true,
			.payloadJson = "{\"sessions\":[{\"id\":\"main\",\"scope\":\"default\",\"active\":true}],\"count\":1,\"activeSessionId\":\"main\"}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsCatalogResponse{
			.id = "req-7",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.agent.update\",\"gateway.channels.accounts.update\",\"gateway.channels.update\",\"gateway.health\",\"gateway.session.reset\",\"gateway.shutdown\",\"gateway.tick\",\"gateway.tools.catalog.update\"]}",
			.error = std::nullopt,
		};

		const ResponseFrame transportStatusResponse{
			.id = "req-8",
			.ok = true,
			.payloadJson = "{\"running\":true,\"endpoint\":\"ws://127.0.0.1:18789\",\"connections\":0,\"timeouts\":{\"handshake\":0,\"idle\":0},\"closes\":{\"invalidUtf8\":0,\"messageTooBig\":0,\"extensionRejected\":0}}",
			.error = std::nullopt,
		};

		const EventFrame event{
			.eventName = "gateway.tick",
			.payloadJson = "{\"ts\":1735689600000,\"running\":true,\"connections\":0}",
			.seq = 1,
			.stateVersion = 1,
		};

		const EventFrame healthEvent{
			.eventName = "gateway.health",
			.payloadJson = "{\"status\":\"ok\",\"running\":true,\"endpoint\":\"ws://127.0.0.1:18789\",\"connections\":0,\"timeouts\":{\"handshake\":0,\"idle\":0},\"closes\":{\"invalidUtf8\":0,\"messageTooBig\":0,\"extensionRejected\":0}}",
			.seq = 2,
			.stateVersion = 2,
		};

		const EventFrame shutdownEvent{
			.eventName = "gateway.shutdown",
			.payloadJson = "{\"reason\":\"maintenance\",\"graceful\":true,\"seq\":3}",
			.seq = 3,
			.stateVersion = 3,
		};

		const EventFrame channelsUpdateEvent{
			.eventName = "gateway.channels.update",
			.payloadJson = "{\"channels\":[{\"id\":\"telegram\",\"label\":\"Telegram\",\"connected\":false,\"accounts\":1},{\"id\":\"discord\",\"label\":\"Discord\",\"connected\":false,\"accounts\":1}]}",
			.seq = 4,
			.stateVersion = 4,
		};

		const EventFrame sessionResetEvent{
			.eventName = "gateway.session.reset",
			.payloadJson = "{\"sessionId\":\"main\",\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true}}",
			.seq = 5,
			.stateVersion = 5,
		};

		const EventFrame agentUpdateEvent{
			.eventName = "gateway.agent.update",
			.payloadJson = "{\"agentId\":\"default\",\"agent\":{\"id\":\"default\",\"name\":\"Default Agent\",\"active\":true}}",
			.seq = 6,
			.stateVersion = 6,
		};

		const EventFrame channelsAccountsUpdateEvent{
			.eventName = "gateway.channels.accounts.update",
			.payloadJson = "{\"accounts\":[{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":false},{\"channel\":\"discord\",\"accountId\":\"discord.default\",\"label\":\"Discord Default\",\"active\":true,\"connected\":false}]}",
			.seq = 7,
			.stateVersion = 7,
		};

		const EventFrame toolsCatalogUpdateEvent{
			.eventName = "gateway.tools.catalog.update",
			.payloadJson = "{\"tools\":[{\"id\":\"chat.send\",\"label\":\"Chat Send\",\"category\":\"messaging\",\"enabled\":true},{\"id\":\"memory.search\",\"label\":\"Memory Search\",\"category\":\"knowledge\",\"enabled\":true}]}",
			.seq = 8,
			.stateVersion = 8,
		};

		const ResponseFrame invalidProtocolParamsResponse{
			.id = invalidProtocolParamsRequest.id,
			.ok = false,
			.payloadJson = std::nullopt,
			.error = ErrorShape{
				.code = validationIssue.code.empty() ? "schema_validation_failed" : validationIssue.code,
				.message = validationIssue.message.empty() ? "Request failed schema validation." : validationIssue.message,
				.detailsJson = "{\"method\":\"" + invalidProtocolParamsRequest.method + "\"}",
				.retryable = false,
				.retryAfterMs = std::nullopt,
			},
		};

		SchemaValidationIssue responseIssue;
		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod("gateway.ping", response, responseIssue)) {
			error = "Ping response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.protocol.version",
			protocolVersionResponse,
			responseIssue)) {
			error = "Protocol version response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.features.list",
			featuresListResponse,
			responseIssue)) {
			error = "Features list response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod("gateway.health", healthResponse, responseIssue)) {
			error = "Health response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.status",
			transportStatusResponse,
			responseIssue)) {
			error = "Transport status response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.session.list",
			sessionListResponse,
			responseIssue)) {
			error = "Session list response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.catalog",
			eventsCatalogResponse,
			responseIssue)) {
			error = "Events catalog response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.get",
			configGetResponse,
			responseIssue)) {
			error = "Config get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.status",
			channelsStatusResponse,
			responseIssue)) {
			error = "Channels status response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts",
			channelsAccountsResponse,
			responseIssue)) {
			error = "Channels accounts response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.routes",
			channelsRoutesResponse,
			responseIssue)) {
			error = "Channels routes response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.resolve",
			channelsRouteResolveResponse,
			responseIssue)) {
			error = "Channels route resolve response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.logs.tail",
			logsTailResponse,
			responseIssue)) {
			error = "Logs tail response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.catalog",
			toolsCatalogResponse,
			responseIssue)) {
			error = "Tools catalog response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.call.preview",
			toolsCallPreviewResponse,
			responseIssue)) {
			error = "Tools call preview response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.resolve",
			sessionsResolveResponse,
			responseIssue)) {
			error = "Sessions resolve response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.create",
			sessionsCreateResponse,
			responseIssue)) {
			error = "Sessions create response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.reset",
			sessionsResetResponse,
			responseIssue)) {
			error = "Sessions reset response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.delete",
			sessionsDeleteResponse,
			responseIssue)) {
			error = "Sessions delete response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.usage",
			sessionsUsageResponse,
			responseIssue)) {
			error = "Sessions usage response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.compact",
			sessionsCompactResponse,
			responseIssue)) {
			error = "Sessions compact response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.preview",
			sessionsPreviewResponse,
			responseIssue)) {
			error = "Sessions preview response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.patch",
			sessionsPatchResponse,
			responseIssue)) {
			error = "Sessions patch response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.create",
			agentsCreateResponse,
			responseIssue)) {
			error = "Agents create response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.delete",
			agentsDeleteResponse,
			responseIssue)) {
			error = "Agents delete response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.update",
			agentsUpdateResponse,
			responseIssue)) {
			error = "Agents update response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.logout",
			channelsLogoutResponse,
			responseIssue)) {
			error = "Channels logout response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.call.execute",
			toolsCallExecuteResponse,
			responseIssue)) {
			error = "Tools call execute response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.set",
			configSetResponse,
			responseIssue)) {
			error = "Config set response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.list",
			modelsListResponse,
			responseIssue)) {
			error = "Models list response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.list",
			agentsFilesListResponse,
			responseIssue)) {
			error = "Agents files list response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.get",
			agentsFilesGetResponse,
			responseIssue)) {
			error = "Agents files get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.set",
			agentsFilesSetResponse,
			responseIssue)) {
			error = "Agents files set response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.delete",
			agentsFilesDeleteResponse,
			responseIssue)) {
			error = "Agents files delete response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.exists",
			agentsFilesExistsResponse,
			responseIssue)) {
			error = "Agents files exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		SchemaValidationIssue eventIssue;
		if (!GatewayProtocolSchemaValidator::ValidateEvent(event, eventIssue)) {
			error = "Tick event schema validation failed: " + eventIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateEvent(healthEvent, eventIssue)) {
			error = "Health event schema validation failed: " + eventIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateEvent(shutdownEvent, eventIssue)) {
			error = "Shutdown event schema validation failed: " + eventIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateEvent(channelsUpdateEvent, eventIssue)) {
			error = "Channels update event schema validation failed: " + eventIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateEvent(sessionResetEvent, eventIssue)) {
			error = "Session reset event schema validation failed: " + eventIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateEvent(agentUpdateEvent, eventIssue)) {
			error = "Agent update event schema validation failed: " + eventIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateEvent(channelsAccountsUpdateEvent, eventIssue)) {
			error = "Channels accounts update event schema validation failed: " + eventIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateEvent(toolsCatalogUpdateEvent, eventIssue)) {
			error = "Tools catalog update event schema validation failed: " + eventIssue.message;
			return false;
		}

		const RequestFrame toolsPreviewRequestPositive{
			.id = "req-schema-1",
			.method = "gateway.tools.call.preview",
			.paramsJson = "{\"tool\":\"chat.send\",\"args\":{}}",
		};
		SchemaValidationIssue requestIssue;
		if (!GatewayProtocolSchemaValidator::ValidateRequest(toolsPreviewRequestPositive, requestIssue)) {
			error = "Schema request positive case failed for gateway.tools.call.preview: " + requestIssue.message;
			return false;
		}

		const RequestFrame toolsPreviewRequestNegative{
			.id = "req-schema-2",
			.method = "gateway.tools.call.preview",
			.paramsJson = "{\"tool\":\"chat.send\",\"args\":[]}",
		};
		if (GatewayProtocolSchemaValidator::ValidateRequest(toolsPreviewRequestNegative, requestIssue)) {
			error = "Schema request negative case unexpectedly passed for gateway.tools.call.preview args type.";
			return false;
		}

		const ResponseFrame channelsAccountsResponseNegative{
			.id = "req-schema-3",
			.ok = true,
			.payloadJson = "{\"accounts\":[{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true}]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts",
			channelsAccountsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts missing `connected`.";
			return false;
		}

		const ResponseFrame sessionsDeleteResponseNegative{
			.id = "req-schema-13",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"thread-1\",\"scope\":\"thread\",\"active\":false},\"deleted\":true}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.delete",
			sessionsDeleteResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.sessions.delete missing `remaining`.";
			return false;
		}

		const ResponseFrame sessionsUsageResponseNegative{
			.id = "req-schema-14",
			.ok = true,
			.payloadJson = "{\"sessionId\":\"main\",\"messages\":42,\"tokens\":{\"input\":1024,\"output\":512},\"lastActiveMs\":1735689600200}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.usage",
			sessionsUsageResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.sessions.usage missing `tokens.total`.";
			return false;
		}

		const ResponseFrame sessionsCompactResponseNegative{
			.id = "req-schema-15",
			.ok = true,
			.payloadJson = "{\"compacted\":1,\"remaining\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.compact",
			sessionsCompactResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.sessions.compact missing `dryRun`.";
			return false;
		}

		const ResponseFrame sessionsPreviewResponseNegative{
			.id = "req-schema-16",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true},\"title\":\"Session main\",\"hasMessages\":true}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.preview",
			sessionsPreviewResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.sessions.preview missing `unread`.";
			return false;
		}

		const ResponseFrame sessionsPatchResponseNegative{
			.id = "req-schema-17",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.patch",
			sessionsPatchResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.sessions.patch missing `patched`.";
			return false;
		}

		const ResponseFrame agentsCreateResponseNegative{
			.id = "req-schema-18",
			.ok = true,
			.payloadJson = "{\"agent\":{\"id\":\"builder\",\"name\":\"Agent builder\",\"active\":false}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.create",
			agentsCreateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.create missing `created`.";
			return false;
		}

		const ResponseFrame agentsDeleteResponseNegative{
			.id = "req-schema-19",
			.ok = true,
			.payloadJson = "{\"agent\":{\"id\":\"builder\",\"name\":\"Agent builder\",\"active\":false},\"deleted\":true}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.delete",
			agentsDeleteResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.delete missing `remaining`.";
			return false;
		}

		const ResponseFrame agentsUpdateResponseNegative{
			.id = "req-schema-20",
			.ok = true,
			.payloadJson = "{\"agent\":{\"id\":\"builder\",\"name\":\"Builder Prime\",\"active\":true}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.update",
			agentsUpdateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.update missing `updated`.";
			return false;
		}

		const ResponseFrame channelsLogoutResponseNegative{
			.id = "req-schema-21",
			.ok = true,
			.payloadJson = "{\"loggedOut\":true}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.logout",
			channelsLogoutResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.logout missing `affected`.";
			return false;
		}

		const ResponseFrame toolsCallExecuteResponseNegative{
			.id = "req-schema-22",
			.ok = true,
			.payloadJson = "{\"tool\":\"chat.send\",\"executed\":true,\"status\":\"ok\",\"argsProvided\":false}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.call.execute",
			toolsCallExecuteResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.call.execute missing `output`.";
			return false;
		}

		const ResponseFrame configSetResponseNegative{
			.id = "req-schema-23",
			.ok = true,
			.payloadJson = "{\"gateway\":{\"bind\":\"127.0.0.1\",\"port\":18789},\"agent\":{\"model\":\"default\",\"streaming\":true}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.set",
			configSetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.set missing `updated`.";
			return false;
		}

		const ResponseFrame modelsListResponseNegative{
			.id = "req-schema-24",
			.ok = true,
			.payloadJson = "{\"models\":[{\"id\":\"default\",\"provider\":\"seed\",\"displayName\":\"Default Model\"}]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.list",
			modelsListResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.list missing `streaming`.";
			return false;
		}

		const ResponseFrame agentsFilesListResponseNegative{
			.id = "req-schema-25",
			.ok = true,
			.payloadJson = "{\"files\":[{\"path\":\"agents/default/profile.json\",\"size\":512}],\"count\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.list",
			agentsFilesListResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.files.list missing `updatedMs`.";
			return false;
		}

		const ResponseFrame agentsFilesGetResponseNegative{
			.id = "req-schema-26",
			.ok = true,
			.payloadJson = "{\"file\":{\"path\":\"agents/default/profile.json\",\"size\":512,\"updatedMs\":1735689600000}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.get",
			agentsFilesGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.files.get missing `content`.";
			return false;
		}

		const ResponseFrame agentsFilesSetResponseNegative{
			.id = "req-schema-27",
			.ok = true,
			.payloadJson = "{\"file\":{\"path\":\"agents/default/profile.json\",\"size\":5,\"updatedMs\":1735689620000,\"content\":\"hello\"}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.set",
			agentsFilesSetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.files.set missing `saved`.";
			return false;
		}

		const ResponseFrame agentsFilesDeleteResponseNegative{
			.id = "req-schema-28",
			.ok = true,
			.payloadJson = "{\"file\":{\"path\":\"agents/default/profile.json\",\"size\":0,\"updatedMs\":1735689630000,\"content\":\"\"}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.delete",
			agentsFilesDeleteResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.files.delete missing `deleted`.";
			return false;
		}

		const ResponseFrame agentsFilesExistsResponseNegative{
			.id = "req-schema-29",
			.ok = true,
			.payloadJson = "{\"path\":\"agents/default/profile.json\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.files.exists",
			agentsFilesExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.files.exists missing `exists`.";
			return false;
		}

		const EventFrame channelsAccountsUpdateEventPositive{
			.eventName = "gateway.channels.accounts.update",
			.payloadJson = "{\"accounts\":[{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":false}]}",
			.seq = 9,
			.stateVersion = 9,
		};
		if (!GatewayProtocolSchemaValidator::ValidateEvent(channelsAccountsUpdateEventPositive, eventIssue)) {
			error = "Schema event positive case failed for gateway.channels.accounts.update: " + eventIssue.message;
			return false;
		}

		const EventFrame channelsAccountsUpdateEventNegative{
			.eventName = "gateway.channels.accounts.update",
			.payloadJson = "{\"accounts\":[{\"channel\":\"telegram\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":false}]}",
			.seq = 10,
			.stateVersion = 10,
		};
		if (GatewayProtocolSchemaValidator::ValidateEvent(channelsAccountsUpdateEventNegative, eventIssue)) {
			error = "Schema event negative case unexpectedly passed for gateway.channels.accounts.update missing `accountId`.";
			return false;
		}

		const EventFrame toolsCatalogUpdateEventPositive{
			.eventName = "gateway.tools.catalog.update",
			.payloadJson = "{\"tools\":[{\"id\":\"chat.send\",\"label\":\"Chat Send\",\"category\":\"messaging\",\"enabled\":true}]}",
			.seq = 11,
			.stateVersion = 11,
		};
		if (!GatewayProtocolSchemaValidator::ValidateEvent(toolsCatalogUpdateEventPositive, eventIssue)) {
			error = "Schema event positive case failed for gateway.tools.catalog.update: " + eventIssue.message;
			return false;
		}

		const EventFrame toolsCatalogUpdateEventNegative{
			.eventName = "gateway.tools.catalog.update",
			.payloadJson = "{\"tools\":[{\"id\":\"chat.send\",\"label\":\"Chat Send\",\"enabled\":true}]}",
			.seq = 12,
			.stateVersion = 12,
		};
		if (GatewayProtocolSchemaValidator::ValidateEvent(toolsCatalogUpdateEventNegative, eventIssue)) {
			error = "Schema event negative case unexpectedly passed for gateway.tools.catalog.update missing `category`.";
			return false;
		}

		if (!CompareFixture(root / "request_ping.json", SerializeRequestFrame(request), error)) {
			return false;
		}

		if (!CompareFixture(root / "response_pong.json", SerializeResponseFrame(response), error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_protocol_version.json", SerializeResponseFrame(protocolVersionResponse), error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_features_list.json", SerializeResponseFrame(featuresListResponse), error)) {
			return false;
		}

		if (!CompareFixture(root / "response_agents_list.json", SerializeResponseFrame(agentsListResponse), error)) {
			return false;
		}

		if (!CompareFixture(root / "response_agents_get.json", SerializeResponseFrame(agentsGetResponse), error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_activate.json",
			SerializeResponseFrame(agentsActivateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(root / "response_health.json", SerializeResponseFrame(healthResponse), error)) {
			return false;
		}

		if (!CompareFixture(root / "response_config_get.json", SerializeResponseFrame(configGetResponse), error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_status.json",
			SerializeResponseFrame(channelsStatusResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts.json",
			SerializeResponseFrame(channelsAccountsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_routes.json",
			SerializeResponseFrame(channelsRoutesResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_route_resolve.json",
			SerializeResponseFrame(channelsRouteResolveResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(root / "response_logs_tail.json", SerializeResponseFrame(logsTailResponse), error)) {
			return false;
		}

		if (!CompareFixture(root / "response_tools_catalog.json", SerializeResponseFrame(toolsCatalogResponse), error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_call_preview.json",
			SerializeResponseFrame(toolsCallPreviewResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_resolve.json",
			SerializeResponseFrame(sessionsResolveResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_create.json",
			SerializeResponseFrame(sessionsCreateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_reset.json",
			SerializeResponseFrame(sessionsResetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_delete.json",
			SerializeResponseFrame(sessionsDeleteResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_usage.json",
			SerializeResponseFrame(sessionsUsageResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_compact.json",
			SerializeResponseFrame(sessionsCompactResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_preview.json",
			SerializeResponseFrame(sessionsPreviewResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_patch.json",
			SerializeResponseFrame(sessionsPatchResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_create.json",
			SerializeResponseFrame(agentsCreateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_delete.json",
			SerializeResponseFrame(agentsDeleteResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_update.json",
			SerializeResponseFrame(agentsUpdateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_logout.json",
			SerializeResponseFrame(channelsLogoutResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_call_execute.json",
			SerializeResponseFrame(toolsCallExecuteResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_set.json",
			SerializeResponseFrame(configSetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_list.json",
			SerializeResponseFrame(modelsListResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_files_list.json",
			SerializeResponseFrame(agentsFilesListResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_files_get.json",
			SerializeResponseFrame(agentsFilesGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_files_set.json",
			SerializeResponseFrame(agentsFilesSetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_files_delete.json",
			SerializeResponseFrame(agentsFilesDeleteResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_files_exists.json",
			SerializeResponseFrame(agentsFilesExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_session_list.json", SerializeResponseFrame(sessionListResponse), error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_catalog.json", SerializeResponseFrame(eventsCatalogResponse), error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_status.json", SerializeResponseFrame(transportStatusResponse), error)) {
			return false;
		}

		if (!CompareFixture(root / "event_tick.json", SerializeEventFrame(event), error)) {
			return false;
		}

		if (!CompareFixture(root / "event_health.json", SerializeEventFrame(healthEvent), error)) {
			return false;
		}

		if (!CompareFixture(root / "event_shutdown.json", SerializeEventFrame(shutdownEvent), error)) {
			return false;
		}

		if (!CompareFixture(
			root / "event_channels_update.json",
			SerializeEventFrame(channelsUpdateEvent),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "event_session_reset.json",
			SerializeEventFrame(sessionResetEvent),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "event_agent_update.json",
			SerializeEventFrame(agentUpdateEvent),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "event_channels_accounts_update.json",
			SerializeEventFrame(channelsAccountsUpdateEvent),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "event_tools_catalog_update.json",
			SerializeEventFrame(toolsCatalogUpdateEvent),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_invalid_protocol_params.json",
			SerializeResponseFrame(invalidProtocolParamsResponse),
			error)) {
			return false;
		}

		return true;
	}

} // namespace blazeclaw::gateway::protocol
