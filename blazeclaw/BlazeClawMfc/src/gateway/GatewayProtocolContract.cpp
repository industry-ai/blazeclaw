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
      .payloadJson = "{\"methods\":[\"gateway.agents.activate\",\"gateway.agents.count\",\"gateway.agents.create\",\"gateway.agents.delete\",\"gateway.agents.exists\",\"gateway.agents.files.delete\",\"gateway.agents.files.exists\",\"gateway.agents.files.get\",\"gateway.agents.files.list\",\"gateway.agents.files.set\",\"gateway.agents.get\",\"gateway.agents.list\",\"gateway.agents.update\",\"gateway.channels.accounts\",\"gateway.channels.accounts.activate\",\"gateway.channels.accounts.clear\",\"gateway.channels.accounts.count\",\"gateway.channels.accounts.create\",\"gateway.channels.accounts.deactivate\",\"gateway.channels.accounts.delete\",\"gateway.channels.accounts.exists\",\"gateway.channels.accounts.get\",\"gateway.channels.accounts.reset\",\"gateway.channels.accounts.restore\",\"gateway.channels.accounts.update\",\"gateway.channels.logout\",\"gateway.channels.route.delete\",\"gateway.channels.route.exists\",\"gateway.channels.route.get\",\"gateway.channels.route.patch\",\"gateway.channels.route.resolve\",\"gateway.channels.route.reset\",\"gateway.channels.route.restore\",\"gateway.channels.route.set\",\"gateway.channels.routes\",\"gateway.channels.routes.clear\",\"gateway.channels.routes.count\",\"gateway.channels.routes.reset\",\"gateway.channels.routes.restore\",\"gateway.channels.status\",\"gateway.channels.status.count\",\"gateway.channels.status.exists\",\"gateway.channels.status.get\",\"gateway.config.audit\",\"gateway.config.backup\",\"gateway.config.bundle\",\"gateway.config.count\",\"gateway.config.diff\",\"gateway.config.exists\",\"gateway.config.get\",\"gateway.config.getKey\",\"gateway.config.getSection\",\"gateway.config.history\",\"gateway.config.keys\",\"gateway.config.profile\",\"gateway.config.revision\",\"gateway.config.rollback\",\"gateway.config.schema\",\"gateway.config.sections\",\"gateway.config.set\",\"gateway.config.snapshot\",\"gateway.config.template\",\"gateway.config.validate\",\"gateway.events.anchor\",\"gateway.events.batch\",\"gateway.events.catalog\",\"gateway.events.channels\",\"gateway.events.count\",\"gateway.events.cursor\",\"gateway.events.exists\",\"gateway.events.get\",\"gateway.events.last\",\"gateway.events.latestByType\",\"gateway.events.list\",\"gateway.events.offset\",\"gateway.events.recent\",\"gateway.events.sample\",\"gateway.events.search\",\"gateway.events.summary\",\"gateway.events.timeline\",\"gateway.events.types\",\"gateway.events.window\",\"gateway.features.list\",\"gateway.health\",\"gateway.health.details\",\"gateway.logs.count\",\"gateway.logs.levels\",\"gateway.logs.tail\",\"gateway.models.affinity\",\"gateway.models.catalog\",\"gateway.models.compatibility\",\"gateway.models.count\",\"gateway.models.default.get\",\"gateway.models.exists\",\"gateway.models.fallback\",\"gateway.models.get\",\"gateway.models.list\",\"gateway.models.listByProvider\",\"gateway.models.manifest\",\"gateway.models.pool\",\"gateway.models.preference\",\"gateway.models.priority\",\"gateway.models.providers\",\"gateway.models.recommended\",\"gateway.models.routing\",\"gateway.models.selection\",\"gateway.ping\",\"gateway.protocol.version\",\"gateway.session.list\",\"gateway.sessions.activate\",\"gateway.sessions.compact\",\"gateway.sessions.count\",\"gateway.sessions.create\",\"gateway.sessions.delete\",\"gateway.sessions.exists\",\"gateway.sessions.patch\",\"gateway.sessions.preview\",\"gateway.sessions.reset\",\"gateway.sessions.resolve\",\"gateway.sessions.usage\",\"gateway.tools.backlog\",\"gateway.tools.call.execute\",\"gateway.tools.call.preview\",\"gateway.tools.capacity\",\"gateway.tools.catalog\",\"gateway.tools.categories\",\"gateway.tools.count\",\"gateway.tools.errors\",\"gateway.tools.exists\",\"gateway.tools.failures\",\"gateway.tools.get\",\"gateway.tools.health\",\"gateway.tools.latency\",\"gateway.tools.list\",\"gateway.tools.metrics\",\"gateway.tools.queue\",\"gateway.tools.scheduler\",\"gateway.tools.stats\",\"gateway.tools.throughput\",\"gateway.tools.usage\",\"gateway.transport.connections.count\",\"gateway.transport.endpoint.exists\",\"gateway.transport.endpoint.get\",\"gateway.transport.endpoint.set\",\"gateway.transport.endpoints.list\",\"gateway.transport.policy.commit\",\"gateway.transport.policy.digest\",\"gateway.transport.policy.export\",\"gateway.transport.policy.get\",\"gateway.transport.policy.history\",\"gateway.transport.policy.import\",\"gateway.transport.policy.metrics\",\"gateway.transport.policy.preview\",\"gateway.transport.policy.reset\",\"gateway.transport.policy.set\",\"gateway.transport.policy.status\",\"gateway.transport.policy.validate\",\"gateway.transport.status\"],\"events\":[\"gateway.agent.update\",\"gateway.channels.accounts.update\",\"gateway.channels.update\",\"gateway.health\",\"gateway.session.reset\",\"gateway.shutdown\",\"gateway.tick\",\"gateway.tools.catalog.update\"]}",
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

		const ResponseFrame channelsRouteSetResponse{
			.id = "req-40",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"},\"saved\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRouteDeleteResponse{
			.id = "req-41",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"},\"deleted\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRouteExistsResponse{
			.id = "req-42",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsActivateResponse{
			.id = "req-43",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":true},\"activated\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsDeactivateResponse{
			.id = "req-44",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":false,\"connected\":false},\"deactivated\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsExistsResponse{
			.id = "req-45",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsUpdateResponse{
			.id = "req-46",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Renamed\",\"active\":true,\"connected\":true},\"updated\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsGetResponse{
			.id = "req-47",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":false}}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsCreateResponse{
			.id = "req-48",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.new\",\"label\":\"Telegram Account\",\"active\":true,\"connected\":false},\"created\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsDeleteResponse{
			.id = "req-49",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":false},\"deleted\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRoutesClearResponse{
			.id = "req-50",
			.ok = true,
			.payloadJson = "{\"cleared\":1,\"remaining\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRoutesRestoreResponse{
			.id = "req-51",
			.ok = true,
			.payloadJson = "{\"restored\":1,\"total\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRouteGetResponse{
			.id = "req-52",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"}}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRouteRestoreResponse{
			.id = "req-53",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"},\"restored\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsClearResponse{
			.id = "req-54",
			.ok = true,
			.payloadJson = "{\"cleared\":1,\"remaining\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsRestoreResponse{
			.id = "req-55",
			.ok = true,
			.payloadJson = "{\"restored\":1,\"total\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsCountResponse{
			.id = "req-56",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\",\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRoutesCountResponse{
			.id = "req-57",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\",\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRoutePatchResponse{
			.id = "req-58",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"assistant\",\"sessionId\":\"main\"},\"updated\":true}",
		   .error = std::nullopt,
		};

		const ResponseFrame channelsRoutesResetResponse{
			.id = "req-59",
			.ok = true,
			.payloadJson = "{\"cleared\":1,\"restored\":1,\"total\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsAccountsResetResponse{
			.id = "req-60",
			.ok = true,
			.payloadJson = "{\"cleared\":1,\"restored\":1,\"total\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsRouteResetResponse{
			.id = "req-61",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"},\"deleted\":true,\"restored\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsStatusGetResponse{
			.id = "req-62",
			.ok = true,
			.payloadJson = "{\"channel\":{\"id\":\"telegram\",\"label\":\"Telegram\",\"connected\":false,\"accounts\":1}}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsStatusExistsResponse{
			.id = "req-63",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame channelsStatusCountResponse{
			.id = "req-64",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\",\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsExistsResponse{
			.id = "req-65",
			.ok = true,
			.payloadJson = "{\"sessionId\":\"main\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsCountResponse{
			.id = "req-66",
			.ok = true,
			.payloadJson = "{\"scope\":\"default\",\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame sessionsActivateResponse{
			.id = "req-67",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true},\"activated\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsExistsResponse{
			.id = "req-68",
			.ok = true,
			.payloadJson = "{\"agentId\":\"default\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame agentsCountResponse{
			.id = "req-69",
			.ok = true,
			.payloadJson = "{\"active\":true,\"activeFilterApplied\":true,\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsExistsResponse{
			.id = "req-70",
			.ok = true,
			.payloadJson = "{\"event\":\"gateway.tick\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsCountResponse{
			.id = "req-71",
			.ok = true,
			.payloadJson = "{\"event\":\"gateway.tick\",\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsExistsResponse{
			.id = "req-72",
			.ok = true,
			.payloadJson = "{\"tool\":\"chat.send\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsCountResponse{
			.id = "req-73",
			.ok = true,
			.payloadJson = "{\"active\":true,\"activeFilterApplied\":true,\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsExistsResponse{
			.id = "req-74",
			.ok = true,
			.payloadJson = "{\"modelId\":\"default\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame configExistsResponse{
			.id = "req-75",
			.ok = true,
			.payloadJson = "{\"key\":\"gateway.bind\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame configKeysResponse{
			.id = "req-76",
			.ok = true,
			.payloadJson = "{\"keys\":[\"gateway.bind\",\"gateway.port\",\"agent.model\",\"agent.streaming\"],\"count\":4}",
			.error = std::nullopt,
		};

		const ResponseFrame transportConnectionsCountResponse{
			.id = "req-77",
			.ok = true,
			.payloadJson = "{\"count\":0}",
			.error = std::nullopt,
		};

		const ResponseFrame healthDetailsResponse{
			.id = "req-78",
			.ok = true,
			.payloadJson = "{\"status\":\"ok\",\"running\":true,\"transport\":{\"running\":true,\"endpoint\":\"ws://127.0.0.1:18789\",\"connections\":0}}",
			.error = std::nullopt,
		};

		const ResponseFrame logsCountResponse{
			.id = "req-79",
			.ok = true,
			.payloadJson = "{\"level\":\"info\",\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame configCountResponse{
			.id = "req-80",
			.ok = true,
			.payloadJson = "{\"section\":\"gateway\",\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsCountResponse{
			.id = "req-81",
			.ok = true,
			.payloadJson = "{\"provider\":\"seed\",\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsGetResponse{
			.id = "req-82",
			.ok = true,
			.payloadJson = "{\"event\":\"gateway.tick\"}",
			.error = std::nullopt,
		};

		const ResponseFrame transportEndpointGetResponse{
			.id = "req-83",
			.ok = true,
			.payloadJson = "{\"endpoint\":\"ws://127.0.0.1:18789\"}",
			.error = std::nullopt,
		};

		const ResponseFrame logsLevelsResponse{
			.id = "req-84",
			.ok = true,
			.payloadJson = "{\"levels\":[\"info\",\"debug\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsListResponse{
			.id = "req-85",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.agent.update\",\"gateway.channels.accounts.update\",\"gateway.channels.update\",\"gateway.health\",\"gateway.session.reset\",\"gateway.shutdown\",\"gateway.tick\",\"gateway.tools.catalog.update\"],\"count\":8}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsGetResponse{
			.id = "req-86",
			.ok = true,
			.payloadJson = "{\"tool\":{\"id\":\"chat.send\",\"label\":\"Chat Send\",\"category\":\"messaging\",\"enabled\":true}}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsGetResponse{
			.id = "req-87",
			.ok = true,
			.payloadJson = "{\"model\":{\"id\":\"default\",\"provider\":\"seed\",\"displayName\":\"Default Model\",\"streaming\":true}}",
			.error = std::nullopt,
		};

		const ResponseFrame configGetKeyResponse{
			.id = "req-88",
			.ok = true,
			.payloadJson = "{\"key\":\"gateway.bind\",\"value\":\"127.0.0.1\"}",
			.error = std::nullopt,
		};

		const ResponseFrame transportEndpointExistsResponse{
			.id = "req-89",
			.ok = true,
			.payloadJson = "{\"endpoint\":\"ws://127.0.0.1:18789\",\"exists\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsLastResponse{
			.id = "req-90",
			.ok = true,
			.payloadJson = "{\"event\":\"gateway.tools.catalog.update\"}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsListResponse{
			.id = "req-91",
			.ok = true,
			.payloadJson = "{\"tools\":[{\"id\":\"chat.send\",\"label\":\"Chat Send\",\"category\":\"messaging\",\"enabled\":true},{\"id\":\"memory.search\",\"label\":\"Memory Search\",\"category\":\"knowledge\",\"enabled\":true}],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsListByProviderResponse{
			.id = "req-92",
			.ok = true,
			.payloadJson = "{\"provider\":\"seed\",\"models\":[{\"id\":\"default\",\"provider\":\"seed\",\"displayName\":\"Default Model\",\"streaming\":true},{\"id\":\"reasoner\",\"provider\":\"seed\",\"displayName\":\"Reasoner Model\",\"streaming\":false}],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame configSectionsResponse{
			.id = "req-93",
			.ok = true,
			.payloadJson = "{\"sections\":[\"gateway\",\"agent\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame transportEndpointSetResponse{
			.id = "req-94",
			.ok = true,
			.payloadJson = "{\"endpoint\":\"ws://127.0.0.1:18789\",\"updated\":false}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsSearchResponse{
			.id = "req-95",
			.ok = true,
			.payloadJson = "{\"term\":\"gateway\",\"events\":[\"gateway.agent.update\",\"gateway.channels.accounts.update\",\"gateway.channels.update\",\"gateway.health\",\"gateway.session.reset\",\"gateway.shutdown\",\"gateway.tick\",\"gateway.tools.catalog.update\"],\"count\":8}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsCategoriesResponse{
			.id = "req-96",
			.ok = true,
			.payloadJson = "{\"categories\":[\"messaging\",\"knowledge\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsProvidersResponse{
			.id = "req-97",
			.ok = true,
			.payloadJson = "{\"providers\":[\"seed\"],\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame configGetSectionResponse{
			.id = "req-98",
			.ok = true,
			.payloadJson = "{\"section\":\"gateway\",\"config\":{\"bind\":\"127.0.0.1\",\"port\":18789}}",
			.error = std::nullopt,
		};

		const ResponseFrame transportEndpointsListResponse{
			.id = "req-99",
			.ok = true,
			.payloadJson = "{\"endpoints\":[\"ws://127.0.0.1:18789\"],\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsSummaryResponse{
			.id = "req-100",
			.ok = true,
			.payloadJson = "{\"total\":8,\"lifecycle\":3,\"updates\":5}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsStatsResponse{
			.id = "req-101",
			.ok = true,
			.payloadJson = "{\"enabled\":2,\"disabled\":0,\"total\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsDefaultGetResponse{
			.id = "req-102",
			.ok = true,
			.payloadJson = "{\"model\":{\"id\":\"default\",\"provider\":\"seed\",\"displayName\":\"Default Model\",\"streaming\":true}}",
			.error = std::nullopt,
		};

		const ResponseFrame configSchemaResponse{
			.id = "req-103",
			.ok = true,
			.payloadJson = "{\"gateway\":{\"bind\":\"string\",\"port\":\"number\"},\"agent\":{\"model\":\"string\",\"streaming\":\"boolean\"}}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyGetResponse{
			.id = "req-104",
			.ok = true,
			.payloadJson = "{\"exclusiveAddrUse\":true,\"keepAlive\":true,\"noDelay\":true,\"idleTimeoutMs\":120000,\"handshakeTimeoutMs\":5000}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsTypesResponse{
			.id = "req-105",
			.ok = true,
			.payloadJson = "{\"types\":[\"lifecycle\",\"update\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsHealthResponse{
			.id = "req-106",
			.ok = true,
			.payloadJson = "{\"healthy\":true,\"enabled\":2,\"total\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsCompatibilityResponse{
			.id = "req-107",
			.ok = true,
			.payloadJson = "{\"default\":\"full\",\"reasoner\":\"partial\",\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame configValidateResponse{
			.id = "req-108",
			.ok = true,
			.payloadJson = "{\"valid\":true,\"errors\":[],\"count\":0}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicySetResponse{
			.id = "req-109",
			.ok = true,
			.payloadJson = "{\"applied\":false,\"reason\":\"runtime_immutable\"}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsChannelsResponse{
			.id = "req-110",
			.ok = true,
			.payloadJson = "{\"channelEvents\":3,\"accountEvents\":1,\"count\":4}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsMetricsResponse{
			.id = "req-111",
			.ok = true,
			.payloadJson = "{\"invocations\":0,\"enabled\":2,\"disabled\":0,\"total\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsRecommendedResponse{
			.id = "req-112",
			.ok = true,
			.payloadJson = "{\"model\":{\"id\":\"default\",\"provider\":\"seed\",\"displayName\":\"Default Model\",\"streaming\":true},\"reason\":\"seed_default\"}",
			.error = std::nullopt,
		};

		const ResponseFrame configAuditResponse{
			.id = "req-113",
			.ok = true,
			.payloadJson = "{\"enabled\":true,\"source\":\"runtime\",\"lastUpdatedMs\":1735689600000}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyResetResponse{
			.id = "req-114",
			.ok = true,
			.payloadJson = "{\"reset\":true,\"applied\":false}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsTimelineResponse{
			.id = "req-115",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.shutdown\",\"gateway.session.reset\",\"gateway.agent.update\",\"gateway.tools.catalog.update\"],\"count\":4}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsFailuresResponse{
			.id = "req-116",
			.ok = true,
			.payloadJson = "{\"failed\":0,\"total\":2,\"rate\":0}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsFallbackResponse{
			.id = "req-117",
			.ok = true,
			.payloadJson = "{\"preferred\":\"default\",\"fallback\":\"reasoner\",\"configured\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame configRollbackResponse{
			.id = "req-118",
			.ok = true,
			.payloadJson = "{\"rolledBack\":false,\"version\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyStatusResponse{
			.id = "req-119",
			.ok = true,
			.payloadJson = "{\"mutable\":false,\"lastApplied\":\"runtime_immutable\",\"policyVersion\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsLatestByTypeResponse{
			.id = "req-120",
			.ok = true,
			.payloadJson = "{\"type\":\"update\",\"event\":\"gateway.tools.catalog.update\"}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsUsageResponse{
			.id = "req-121",
			.ok = true,
			.payloadJson = "{\"calls\":0,\"tools\":2,\"avgMs\":0}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsSelectionResponse{
			.id = "req-122",
			.ok = true,
			.payloadJson = "{\"selected\":\"default\",\"strategy\":\"seed_priority\"}",
			.error = std::nullopt,
		};

		const ResponseFrame configBackupResponse{
			.id = "req-123",
			.ok = true,
			.payloadJson = "{\"saved\":true,\"version\":1,\"path\":\"config/runtime.backup.json\"}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyValidateResponse{
			.id = "req-124",
			.ok = true,
			.payloadJson = "{\"valid\":true,\"errors\":[],\"count\":0}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsSampleResponse{
			.id = "req-125",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.health\",\"gateway.tools.catalog.update\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsLatencyResponse{
			.id = "req-126",
			.ok = true,
			.payloadJson = "{\"minMs\":0,\"maxMs\":0,\"avgMs\":0,\"samples\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsRoutingResponse{
			.id = "req-127",
			.ok = true,
			.payloadJson = "{\"primary\":\"default\",\"fallback\":\"reasoner\",\"strategy\":\"seed_priority\"}",
			.error = std::nullopt,
		};

		const ResponseFrame configDiffResponse{
			.id = "req-128",
			.ok = true,
			.payloadJson = "{\"changed\":[],\"count\":0}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyHistoryResponse{
			.id = "req-129",
			.ok = true,
			.payloadJson = "{\"entries\":[{\"version\":1,\"applied\":false,\"reason\":\"runtime_immutable\"}],\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsWindowResponse{
			.id = "req-130",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.session.reset\",\"gateway.agent.update\",\"gateway.tools.catalog.update\"],\"count\":3}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsErrorsResponse{
			.id = "req-131",
			.ok = true,
			.payloadJson = "{\"errors\":0,\"tools\":2,\"rate\":0}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsPreferenceResponse{
			.id = "req-132",
			.ok = true,
			.payloadJson = "{\"model\":\"default\",\"provider\":\"seed\",\"source\":\"runtime\"}",
			.error = std::nullopt,
		};

		const ResponseFrame configSnapshotResponse{
			.id = "req-133",
			.ok = true,
			.payloadJson = "{\"gateway\":{\"bind\":\"127.0.0.1\",\"port\":18789},\"agent\":{\"model\":\"default\",\"streaming\":true}}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyMetricsResponse{
			.id = "req-134",
			.ok = true,
			.payloadJson = "{\"validations\":0,\"resets\":0,\"sets\":0}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsRecentResponse{
			.id = "req-135",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.shutdown\",\"gateway.session.reset\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsThroughputResponse{
			.id = "req-136",
			.ok = true,
			.payloadJson = "{\"calls\":0,\"windowSec\":60,\"perMinute\":0,\"tools\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsPriorityResponse{
			.id = "req-137",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame configRevisionResponse{
			.id = "req-138",
			.ok = true,
			.payloadJson = "{\"revision\":1,\"source\":\"runtime\"}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyExportResponse{
			.id = "req-139",
			.ok = true,
			.payloadJson = "{\"path\":\"transport/policy-export.json\",\"version\":1,\"exported\":true}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsBatchResponse{
			.id = "req-140",
			.ok = true,
			.payloadJson = "{\"batches\":[\"lifecycle\",\"updates\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsCapacityResponse{
			.id = "req-141",
			.ok = true,
			.payloadJson = "{\"total\":2,\"used\":0,\"free\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsAffinityResponse{
			.id = "req-142",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"],\"affinity\":\"balanced\"}",
			.error = std::nullopt,
		};

		const ResponseFrame configHistoryResponse{
			.id = "req-143",
			.ok = true,
			.payloadJson = "{\"revisions\":[1],\"count\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyImportResponse{
			.id = "req-144",
			.ok = true,
			.payloadJson = "{\"imported\":true,\"version\":1,\"applied\":false}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsCursorResponse{
			.id = "req-145",
			.ok = true,
			.payloadJson = "{\"cursor\":\"evt-2\",\"event\":\"gateway.session.reset\"}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsQueueResponse{
			.id = "req-146",
			.ok = true,
			.payloadJson = "{\"queued\":0,\"running\":0,\"tools\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsPoolResponse{
			.id = "req-147",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame configProfileResponse{
			.id = "req-148",
			.ok = true,
			.payloadJson = "{\"name\":\"default\",\"source\":\"runtime\"}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyDigestResponse{
			.id = "req-149",
			.ok = true,
			.payloadJson = "{\"digest\":\"sha256:seed-policy-v1\",\"version\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsAnchorResponse{
			.id = "req-150",
			.ok = true,
			.payloadJson = "{\"anchor\":\"evt-1\",\"event\":\"gateway.shutdown\"}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsSchedulerResponse{
			.id = "req-151",
			.ok = true,
			.payloadJson = "{\"ticks\":0,\"queued\":0,\"tools\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsManifestResponse{
			.id = "req-152",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"],\"manifestVersion\":1}",
			.error = std::nullopt,
		};

		const ResponseFrame configTemplateResponse{
			.id = "req-153",
			.ok = true,
			.payloadJson = "{\"template\":\"default\",\"keys\":[\"gateway.bind\",\"gateway.port\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyPreviewResponse{
			.id = "req-154",
			.ok = true,
			.payloadJson = "{\"path\":\"transport/policy-preview.json\",\"applied\":false,\"notes\":\"runtime_immutable\"}",
			.error = std::nullopt,
		};

		const ResponseFrame eventsOffsetResponse{
			.id = "req-155",
			.ok = true,
			.payloadJson = "{\"offset\":1,\"event\":\"gateway.session.reset\"}",
			.error = std::nullopt,
		};

		const ResponseFrame toolsBacklogResponse{
			.id = "req-156",
			.ok = true,
			.payloadJson = "{\"pending\":0,\"capacity\":2,\"tools\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame modelsCatalogResponse{
			.id = "req-157",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame configBundleResponse{
			.id = "req-158",
			.ok = true,
			.payloadJson = "{\"name\":\"runtime\",\"sections\":[\"gateway\",\"agent\"],\"count\":2}",
			.error = std::nullopt,
		};

		const ResponseFrame transportPolicyCommitResponse{
			.id = "req-159",
			.ok = true,
			.payloadJson = "{\"committed\":false,\"version\":1,\"applied\":false}",
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

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.set",
			channelsRouteSetResponse,
			responseIssue)) {
			error = "Channels route set response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.delete",
			channelsRouteDeleteResponse,
			responseIssue)) {
			error = "Channels route delete response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.exists",
			channelsRouteExistsResponse,
			responseIssue)) {
			error = "Channels route exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.activate",
			channelsAccountsActivateResponse,
			responseIssue)) {
			error = "Channels accounts activate response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.deactivate",
			channelsAccountsDeactivateResponse,
			responseIssue)) {
			error = "Channels accounts deactivate response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.exists",
			channelsAccountsExistsResponse,
			responseIssue)) {
			error = "Channels accounts exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.update",
			channelsAccountsUpdateResponse,
			responseIssue)) {
			error = "Channels accounts update response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.get",
			channelsAccountsGetResponse,
			responseIssue)) {
			error = "Channels accounts get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.create",
			channelsAccountsCreateResponse,
			responseIssue)) {
			error = "Channels accounts create response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.delete",
			channelsAccountsDeleteResponse,
			responseIssue)) {
			error = "Channels accounts delete response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.routes.clear",
			channelsRoutesClearResponse,
			responseIssue)) {
			error = "Channels routes clear response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.routes.restore",
			channelsRoutesRestoreResponse,
			responseIssue)) {
			error = "Channels routes restore response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.get",
			channelsRouteGetResponse,
			responseIssue)) {
			error = "Channels route get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.restore",
			channelsRouteRestoreResponse,
			responseIssue)) {
			error = "Channels route restore response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.clear",
			channelsAccountsClearResponse,
			responseIssue)) {
			error = "Channels accounts clear response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.restore",
			channelsAccountsRestoreResponse,
			responseIssue)) {
			error = "Channels accounts restore response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.count",
			channelsAccountsCountResponse,
			responseIssue)) {
			error = "Channels accounts count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.routes.count",
			channelsRoutesCountResponse,
			responseIssue)) {
			error = "Channels routes count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.patch",
			channelsRoutePatchResponse,
			responseIssue)) {
			error = "Channels route patch response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.routes.reset",
			channelsRoutesResetResponse,
			responseIssue)) {
			error = "Channels routes reset response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.reset",
			channelsAccountsResetResponse,
			responseIssue)) {
			error = "Channels accounts reset response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.reset",
			channelsRouteResetResponse,
			responseIssue)) {
			error = "Channels route reset response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.status.get",
			channelsStatusGetResponse,
			responseIssue)) {
			error = "Channels status get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.status.exists",
			channelsStatusExistsResponse,
			responseIssue)) {
			error = "Channels status exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.status.count",
			channelsStatusCountResponse,
			responseIssue)) {
			error = "Channels status count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.exists",
			sessionsExistsResponse,
			responseIssue)) {
			error = "Sessions exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.count",
			sessionsCountResponse,
			responseIssue)) {
			error = "Sessions count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.activate",
			sessionsActivateResponse,
			responseIssue)) {
			error = "Sessions activate response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.exists",
			agentsExistsResponse,
			responseIssue)) {
			error = "Agents exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.count",
			agentsCountResponse,
			responseIssue)) {
			error = "Agents count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.exists",
			eventsExistsResponse,
			responseIssue)) {
			error = "Events exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.count",
			eventsCountResponse,
			responseIssue)) {
			error = "Events count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.exists",
			toolsExistsResponse,
			responseIssue)) {
			error = "Tools exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.count",
			toolsCountResponse,
			responseIssue)) {
			error = "Tools count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.exists",
			modelsExistsResponse,
			responseIssue)) {
			error = "Models exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.exists",
			configExistsResponse,
			responseIssue)) {
			error = "Config exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.keys",
			configKeysResponse,
			responseIssue)) {
			error = "Config keys response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.connections.count",
			transportConnectionsCountResponse,
			responseIssue)) {
			error = "Transport connections count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.health.details",
			healthDetailsResponse,
			responseIssue)) {
			error = "Health details response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.logs.count",
			logsCountResponse,
			responseIssue)) {
			error = "Logs count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.count",
			configCountResponse,
			responseIssue)) {
			error = "Config count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.count",
			modelsCountResponse,
			responseIssue)) {
			error = "Models count response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.get",
			eventsGetResponse,
			responseIssue)) {
			error = "Events get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.endpoint.get",
			transportEndpointGetResponse,
			responseIssue)) {
			error = "Transport endpoint get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.logs.levels",
			logsLevelsResponse,
			responseIssue)) {
			error = "Logs levels response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.list",
			eventsListResponse,
			responseIssue)) {
			error = "Events list response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.get",
			toolsGetResponse,
			responseIssue)) {
			error = "Tools get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.get",
			modelsGetResponse,
			responseIssue)) {
			error = "Models get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.getKey",
			configGetKeyResponse,
			responseIssue)) {
			error = "Config getKey response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.endpoint.exists",
			transportEndpointExistsResponse,
			responseIssue)) {
			error = "Transport endpoint exists response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.last",
			eventsLastResponse,
			responseIssue)) {
			error = "Events last response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.list",
			toolsListResponse,
			responseIssue)) {
			error = "Tools list response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.listByProvider",
			modelsListByProviderResponse,
			responseIssue)) {
			error = "Models listByProvider response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.sections",
			configSectionsResponse,
			responseIssue)) {
			error = "Config sections response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.endpoint.set",
			transportEndpointSetResponse,
			responseIssue)) {
			error = "Transport endpoint set response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.search",
			eventsSearchResponse,
			responseIssue)) {
			error = "Events search response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.categories",
			toolsCategoriesResponse,
			responseIssue)) {
			error = "Tools categories response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.providers",
			modelsProvidersResponse,
			responseIssue)) {
			error = "Models providers response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.getSection",
			configGetSectionResponse,
			responseIssue)) {
			error = "Config getSection response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.endpoints.list",
			transportEndpointsListResponse,
			responseIssue)) {
			error = "Transport endpoints list response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.summary",
			eventsSummaryResponse,
			responseIssue)) {
			error = "Events summary response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.stats",
			toolsStatsResponse,
			responseIssue)) {
			error = "Tools stats response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.default.get",
			modelsDefaultGetResponse,
			responseIssue)) {
			error = "Models default get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.schema",
			configSchemaResponse,
			responseIssue)) {
			error = "Config schema response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.get",
			transportPolicyGetResponse,
			responseIssue)) {
			error = "Transport policy get response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.types",
			eventsTypesResponse,
			responseIssue)) {
			error = "Events types response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.health",
			toolsHealthResponse,
			responseIssue)) {
			error = "Tools health response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.compatibility",
			modelsCompatibilityResponse,
			responseIssue)) {
			error = "Models compatibility response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.validate",
			configValidateResponse,
			responseIssue)) {
			error = "Config validate response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.set",
			transportPolicySetResponse,
			responseIssue)) {
			error = "Transport policy set response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.channels",
			eventsChannelsResponse,
			responseIssue)) {
			error = "Events channels response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.metrics",
			toolsMetricsResponse,
			responseIssue)) {
			error = "Tools metrics response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.recommended",
			modelsRecommendedResponse,
			responseIssue)) {
			error = "Models recommended response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.audit",
			configAuditResponse,
			responseIssue)) {
			error = "Config audit response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.reset",
			transportPolicyResetResponse,
			responseIssue)) {
			error = "Transport policy reset response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.timeline",
			eventsTimelineResponse,
			responseIssue)) {
			error = "Events timeline response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.failures",
			toolsFailuresResponse,
			responseIssue)) {
			error = "Tools failures response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.fallback",
			modelsFallbackResponse,
			responseIssue)) {
			error = "Models fallback response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.rollback",
			configRollbackResponse,
			responseIssue)) {
			error = "Config rollback response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.status",
			transportPolicyStatusResponse,
			responseIssue)) {
			error = "Transport policy status response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.latestByType",
			eventsLatestByTypeResponse,
			responseIssue)) {
			error = "Events latestByType response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.usage",
			toolsUsageResponse,
			responseIssue)) {
			error = "Tools usage response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.selection",
			modelsSelectionResponse,
			responseIssue)) {
			error = "Models selection response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.backup",
			configBackupResponse,
			responseIssue)) {
			error = "Config backup response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.validate",
			transportPolicyValidateResponse,
			responseIssue)) {
			error = "Transport policy validate response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.sample",
			eventsSampleResponse,
			responseIssue)) {
			error = "Events sample response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.latency",
			toolsLatencyResponse,
			responseIssue)) {
			error = "Tools latency response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.routing",
			modelsRoutingResponse,
			responseIssue)) {
			error = "Models routing response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.diff",
			configDiffResponse,
			responseIssue)) {
			error = "Config diff response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.history",
			transportPolicyHistoryResponse,
			responseIssue)) {
			error = "Transport policy history response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.window",
			eventsWindowResponse,
			responseIssue)) {
			error = "Events window response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.errors",
			toolsErrorsResponse,
			responseIssue)) {
			error = "Tools errors response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.preference",
			modelsPreferenceResponse,
			responseIssue)) {
			error = "Models preference response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.snapshot",
			configSnapshotResponse,
			responseIssue)) {
			error = "Config snapshot response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.metrics",
			transportPolicyMetricsResponse,
			responseIssue)) {
			error = "Transport policy metrics response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.recent",
			eventsRecentResponse,
			responseIssue)) {
			error = "Events recent response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.throughput",
			toolsThroughputResponse,
			responseIssue)) {
			error = "Tools throughput response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.priority",
			modelsPriorityResponse,
			responseIssue)) {
			error = "Models priority response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.revision",
			configRevisionResponse,
			responseIssue)) {
			error = "Config revision response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.export",
			transportPolicyExportResponse,
			responseIssue)) {
			error = "Transport policy export response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.batch",
			eventsBatchResponse,
			responseIssue)) {
			error = "Events batch response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.capacity",
			toolsCapacityResponse,
			responseIssue)) {
			error = "Tools capacity response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.affinity",
			modelsAffinityResponse,
			responseIssue)) {
			error = "Models affinity response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.history",
			configHistoryResponse,
			responseIssue)) {
			error = "Config history response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.import",
			transportPolicyImportResponse,
			responseIssue)) {
			error = "Transport policy import response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.cursor",
			eventsCursorResponse,
			responseIssue)) {
			error = "Events cursor response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.queue",
			toolsQueueResponse,
			responseIssue)) {
			error = "Tools queue response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.pool",
			modelsPoolResponse,
			responseIssue)) {
			error = "Models pool response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.profile",
			configProfileResponse,
			responseIssue)) {
			error = "Config profile response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.digest",
			transportPolicyDigestResponse,
			responseIssue)) {
			error = "Transport policy digest response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.anchor",
			eventsAnchorResponse,
			responseIssue)) {
			error = "Events anchor response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.scheduler",
			toolsSchedulerResponse,
			responseIssue)) {
			error = "Tools scheduler response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.manifest",
			modelsManifestResponse,
			responseIssue)) {
			error = "Models manifest response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.template",
			configTemplateResponse,
			responseIssue)) {
			error = "Config template response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.preview",
			transportPolicyPreviewResponse,
			responseIssue)) {
			error = "Transport policy preview response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.offset",
			eventsOffsetResponse,
			responseIssue)) {
			error = "Events offset response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.backlog",
			toolsBacklogResponse,
			responseIssue)) {
			error = "Tools backlog response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.catalog",
			modelsCatalogResponse,
			responseIssue)) {
			error = "Models catalog response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.bundle",
			configBundleResponse,
			responseIssue)) {
			error = "Config bundle response schema validation failed: " + responseIssue.message;
			return false;
		}

		if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.commit",
			transportPolicyCommitResponse,
			responseIssue)) {
			error = "Transport policy commit response schema validation failed: " + responseIssue.message;
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

		const ResponseFrame channelsRouteSetResponseNegative{
			.id = "req-schema-30",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.set",
			channelsRouteSetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.route.set missing `saved`.";
			return false;
		}

		const ResponseFrame channelsRouteDeleteResponseNegative{
			.id = "req-schema-31",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.delete",
			channelsRouteDeleteResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.route.delete missing `deleted`.";
			return false;
		}

		const ResponseFrame channelsRouteExistsResponseNegative{
			.id = "req-schema-32",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\",\"accountId\":\"telegram.default\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.exists",
			channelsRouteExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.route.exists missing `exists`.";
			return false;
		}

		const ResponseFrame channelsAccountsActivateResponseNegative{
			.id = "req-schema-33",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":true}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.activate",
			channelsAccountsActivateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.activate missing `activated`.";
			return false;
		}

		const ResponseFrame channelsAccountsDeactivateResponseNegative{
			.id = "req-schema-34",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":false,\"connected\":false}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.deactivate",
			channelsAccountsDeactivateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.deactivate missing `deactivated`.";
			return false;
		}

		const ResponseFrame channelsAccountsExistsResponseNegative{
			.id = "req-schema-35",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\",\"accountId\":\"telegram.default\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.exists",
			channelsAccountsExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.exists missing `exists`.";
			return false;
		}

		const ResponseFrame channelsAccountsUpdateResponseNegative{
			.id = "req-schema-36",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Renamed\",\"active\":true,\"connected\":true}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.update",
			channelsAccountsUpdateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.update missing `updated`.";
			return false;
		}

		const ResponseFrame channelsAccountsGetResponseNegative{
			.id = "req-schema-37",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":false}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.get",
			channelsAccountsGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.get missing `account` object envelope.";
			return false;
		}

		const ResponseFrame channelsAccountsCreateResponseNegative{
			.id = "req-schema-38",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.new\",\"label\":\"Telegram Account\",\"active\":true,\"connected\":false}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.create",
			channelsAccountsCreateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.create missing `created`.";
			return false;
		}

		const ResponseFrame channelsAccountsDeleteResponseNegative{
			.id = "req-schema-39",
			.ok = true,
			.payloadJson = "{\"account\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true,\"connected\":false}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.delete",
			channelsAccountsDeleteResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.delete missing `deleted`.";
			return false;
		}

		const ResponseFrame channelsRoutesClearResponseNegative{
			.id = "req-schema-40",
			.ok = true,
			.payloadJson = "{\"cleared\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.routes.clear",
			channelsRoutesClearResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.routes.clear missing `remaining`.";
			return false;
		}

		const ResponseFrame channelsRoutesRestoreResponseNegative{
			.id = "req-schema-41",
			.ok = true,
			.payloadJson = "{\"restored\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.routes.restore",
			channelsRoutesRestoreResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.routes.restore missing `total`.";
			return false;
		}

		const ResponseFrame channelsRouteGetResponseNegative{
			.id = "req-schema-42",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\"}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.get",
			channelsRouteGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.route.get missing `sessionId`.";
			return false;
		}

		const ResponseFrame channelsRouteRestoreResponseNegative{
			.id = "req-schema-43",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.restore",
			channelsRouteRestoreResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.route.restore missing `restored`.";
			return false;
		}

		const ResponseFrame channelsAccountsClearResponseNegative{
			.id = "req-schema-44",
			.ok = true,
			.payloadJson = "{\"cleared\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.clear",
			channelsAccountsClearResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.clear missing `remaining`.";
			return false;
		}

		const ResponseFrame channelsAccountsRestoreResponseNegative{
			.id = "req-schema-45",
			.ok = true,
			.payloadJson = "{\"restored\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.restore",
			channelsAccountsRestoreResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.restore missing `total`.";
			return false;
		}

		const ResponseFrame channelsAccountsCountResponseNegative{
			.id = "req-schema-46",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.count",
			channelsAccountsCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.count missing `count`.";
			return false;
		}

		const ResponseFrame channelsRoutesCountResponseNegative{
			.id = "req-schema-47",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.routes.count",
			channelsRoutesCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.routes.count missing `count`.";
			return false;
		}

		const ResponseFrame channelsRoutePatchResponseNegative{
			.id = "req-schema-48",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"assistant\",\"sessionId\":\"main\"}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.patch",
			channelsRoutePatchResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.route.patch missing `updated`.";
			return false;
		}

		const ResponseFrame channelsRoutesResetResponseNegative{
			.id = "req-schema-49",
			.ok = true,
			.payloadJson = "{\"cleared\":1,\"restored\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.routes.reset",
			channelsRoutesResetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.routes.reset missing `total`.";
			return false;
		}

		const ResponseFrame channelsAccountsResetResponseNegative{
			.id = "req-schema-50",
			.ok = true,
			.payloadJson = "{\"cleared\":1,\"restored\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.accounts.reset",
			channelsAccountsResetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.accounts.reset missing `total`.";
			return false;
		}

		const ResponseFrame channelsRouteResetResponseNegative{
			.id = "req-schema-51",
			.ok = true,
			.payloadJson = "{\"route\":{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"agentId\":\"default\",\"sessionId\":\"main\"},\"deleted\":true}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.route.reset",
			channelsRouteResetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.route.reset missing `restored`.";
			return false;
		}

		const ResponseFrame channelsStatusGetResponseNegative{
			.id = "req-schema-52",
			.ok = true,
			.payloadJson = "{\"channel\":{\"id\":\"telegram\",\"label\":\"Telegram\",\"connected\":false}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.status.get",
			channelsStatusGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.status.get missing `accounts`.";
			return false;
		}

		const ResponseFrame channelsStatusExistsResponseNegative{
			.id = "req-schema-53",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.status.exists",
			channelsStatusExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.status.exists missing `exists`.";
			return false;
		}

		const ResponseFrame channelsStatusCountResponseNegative{
			.id = "req-schema-54",
			.ok = true,
			.payloadJson = "{\"channel\":\"telegram\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.channels.status.count",
			channelsStatusCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.channels.status.count missing `count`.";
			return false;
		}

		const ResponseFrame sessionsExistsResponseNegative{
			.id = "req-schema-55",
			.ok = true,
			.payloadJson = "{\"sessionId\":\"main\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.exists",
			sessionsExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.sessions.exists missing `exists`.";
			return false;
		}

		const ResponseFrame sessionsCountResponseNegative{
			.id = "req-schema-56",
			.ok = true,
			.payloadJson = "{\"scope\":\"default\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.count",
			sessionsCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.sessions.count missing `count`.";
			return false;
		}

		const ResponseFrame sessionsActivateResponseNegative{
			.id = "req-schema-57",
			.ok = true,
			.payloadJson = "{\"session\":{\"id\":\"main\",\"scope\":\"default\",\"active\":true}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.sessions.activate",
			sessionsActivateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.sessions.activate missing `activated`.";
			return false;
		}

		const ResponseFrame agentsExistsResponseNegative{
			.id = "req-schema-58",
			.ok = true,
			.payloadJson = "{\"agentId\":\"default\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.exists",
			agentsExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.exists missing `exists`.";
			return false;
		}

		const ResponseFrame agentsCountResponseNegative{
			.id = "req-schema-59",
			.ok = true,
			.payloadJson = "{\"active\":true,\"activeFilterApplied\":true}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.agents.count",
			agentsCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.agents.count missing `count`.";
			return false;
		}

		const ResponseFrame eventsExistsResponseNegative{
			.id = "req-schema-60",
			.ok = true,
			.payloadJson = "{\"event\":\"gateway.tick\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.exists",
			eventsExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.exists missing `exists`.";
			return false;
		}

		const ResponseFrame eventsCountResponseNegative{
			.id = "req-schema-61",
			.ok = true,
			.payloadJson = "{\"event\":\"gateway.tick\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.count",
			eventsCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.count missing `count`.";
			return false;
		}

		const ResponseFrame toolsExistsResponseNegative{
			.id = "req-schema-62",
			.ok = true,
			.payloadJson = "{\"tool\":\"chat.send\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.exists",
			toolsExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.exists missing `exists`.";
			return false;
		}

		const ResponseFrame toolsCountResponseNegative{
			.id = "req-schema-63",
			.ok = true,
			.payloadJson = "{\"active\":true,\"activeFilterApplied\":true}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.count",
			toolsCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.count missing `count`.";
			return false;
		}

		const ResponseFrame modelsExistsResponseNegative{
			.id = "req-schema-64",
			.ok = true,
			.payloadJson = "{\"modelId\":\"default\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.exists",
			modelsExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.exists missing `exists`.";
			return false;
		}

		const ResponseFrame configExistsResponseNegative{
			.id = "req-schema-65",
			.ok = true,
			.payloadJson = "{\"key\":\"gateway.bind\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.exists",
			configExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.exists missing `exists`.";
			return false;
		}

		const ResponseFrame configKeysResponseNegative{
			.id = "req-schema-66",
			.ok = true,
			.payloadJson = "{\"keys\":[\"gateway.bind\",\"gateway.port\",\"agent.model\",\"agent.streaming\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.keys",
			configKeysResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.keys missing `count`.";
			return false;
		}

		const ResponseFrame transportConnectionsCountResponseNegative{
			.id = "req-schema-67",
			.ok = true,
			.payloadJson = "{}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.connections.count",
			transportConnectionsCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.connections.count missing `count`.";
			return false;
		}

		const ResponseFrame healthDetailsResponseNegative{
			.id = "req-schema-68",
			.ok = true,
			.payloadJson = "{\"status\":\"ok\",\"running\":true,\"transport\":{\"running\":true,\"endpoint\":\"ws://127.0.0.1:18789\"}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.health.details",
			healthDetailsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.health.details missing `transport.connections`.";
			return false;
		}

		const ResponseFrame logsCountResponseNegative{
			.id = "req-schema-69",
			.ok = true,
			.payloadJson = "{\"level\":\"info\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.logs.count",
			logsCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.logs.count missing `count`.";
			return false;
		}

		const ResponseFrame configCountResponseNegative{
			.id = "req-schema-70",
			.ok = true,
			.payloadJson = "{\"section\":\"gateway\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.count",
			configCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.count missing `count`.";
			return false;
		}

		const ResponseFrame modelsCountResponseNegative{
			.id = "req-schema-71",
			.ok = true,
			.payloadJson = "{\"provider\":\"seed\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.count",
			modelsCountResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.count missing `count`.";
			return false;
		}

		const ResponseFrame eventsGetResponseNegative{
			.id = "req-schema-72",
			.ok = true,
			.payloadJson = "{}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.get",
			eventsGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.get missing `event`.";
			return false;
		}

		const ResponseFrame transportEndpointGetResponseNegative{
			.id = "req-schema-73",
			.ok = true,
			.payloadJson = "{}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.endpoint.get",
			transportEndpointGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.endpoint.get missing `endpoint`.";
			return false;
		}

		const ResponseFrame logsLevelsResponseNegative{
			.id = "req-schema-74",
			.ok = true,
			.payloadJson = "{\"levels\":[\"info\",\"debug\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.logs.levels",
			logsLevelsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.logs.levels missing `count`.";
			return false;
		}

		const ResponseFrame eventsListResponseNegative{
			.id = "req-schema-75",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.tick\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.list",
			eventsListResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.list missing `count`.";
			return false;
		}

		const ResponseFrame toolsGetResponseNegative{
			.id = "req-schema-76",
			.ok = true,
			.payloadJson = "{}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.get",
			toolsGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.get missing `tool`.";
			return false;
		}

		const ResponseFrame modelsGetResponseNegative{
			.id = "req-schema-77",
			.ok = true,
			.payloadJson = "{}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.get",
			modelsGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.get missing `model`.";
			return false;
		}

		const ResponseFrame configGetKeyResponseNegative{
			.id = "req-schema-78",
			.ok = true,
			.payloadJson = "{\"key\":\"gateway.bind\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.getKey",
			configGetKeyResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.getKey missing `value`.";
			return false;
		}

		const ResponseFrame transportEndpointExistsResponseNegative{
			.id = "req-schema-79",
			.ok = true,
			.payloadJson = "{\"endpoint\":\"ws://127.0.0.1:18789\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.endpoint.exists",
			transportEndpointExistsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.endpoint.exists missing `exists`.";
			return false;
		}

		const ResponseFrame eventsLastResponseNegative{
			.id = "req-schema-80",
			.ok = true,
			.payloadJson = "{}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.last",
			eventsLastResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.last missing `event`.";
			return false;
		}

		const ResponseFrame toolsListResponseNegative{
			.id = "req-schema-81",
			.ok = true,
			.payloadJson = "{\"tools\":[]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.list",
			toolsListResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.list missing `count`.";
			return false;
		}

		const ResponseFrame modelsListByProviderResponseNegative{
			.id = "req-schema-82",
			.ok = true,
			.payloadJson = "{\"provider\":\"seed\",\"models\":[]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.listByProvider",
			modelsListByProviderResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.listByProvider missing `count`.";
			return false;
		}

		const ResponseFrame configSectionsResponseNegative{
			.id = "req-schema-83",
			.ok = true,
			.payloadJson = "{\"sections\":[\"gateway\",\"agent\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.sections",
			configSectionsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.sections missing `count`.";
			return false;
		}

		const ResponseFrame transportEndpointSetResponseNegative{
			.id = "req-schema-84",
			.ok = true,
			.payloadJson = "{\"endpoint\":\"ws://127.0.0.1:18789\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.endpoint.set",
			transportEndpointSetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.endpoint.set missing `updated`.";
			return false;
		}

		const ResponseFrame eventsSearchResponseNegative{
			.id = "req-schema-85",
			.ok = true,
			.payloadJson = "{\"term\":\"gateway\",\"events\":[]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.search",
			eventsSearchResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.search missing `count`.";
			return false;
		}

		const ResponseFrame toolsCategoriesResponseNegative{
			.id = "req-schema-86",
			.ok = true,
			.payloadJson = "{\"categories\":[\"messaging\",\"knowledge\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.categories",
			toolsCategoriesResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.categories missing `count`.";
			return false;
		}

		const ResponseFrame modelsProvidersResponseNegative{
			.id = "req-schema-87",
			.ok = true,
			.payloadJson = "{\"providers\":[\"seed\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.providers",
			modelsProvidersResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.providers missing `count`.";
			return false;
		}

		const ResponseFrame configGetSectionResponseNegative{
			.id = "req-schema-88",
			.ok = true,
			.payloadJson = "{\"section\":\"gateway\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.getSection",
			configGetSectionResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.getSection missing `config`.";
			return false;
		}

		const ResponseFrame transportEndpointsListResponseNegative{
			.id = "req-schema-89",
			.ok = true,
			.payloadJson = "{\"endpoints\":[\"ws://127.0.0.1:18789\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.endpoints.list",
			transportEndpointsListResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.endpoints.list missing `count`.";
			return false;
		}

		const ResponseFrame eventsSummaryResponseNegative{
			.id = "req-schema-90",
			.ok = true,
			.payloadJson = "{\"total\":8,\"lifecycle\":3}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.summary",
			eventsSummaryResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.summary missing `updates`.";
			return false;
		}

		const ResponseFrame toolsStatsResponseNegative{
			.id = "req-schema-91",
			.ok = true,
			.payloadJson = "{\"enabled\":2,\"total\":2}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.stats",
			toolsStatsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.stats missing `disabled`.";
			return false;
		}

		const ResponseFrame modelsDefaultGetResponseNegative{
			.id = "req-schema-92",
			.ok = true,
			.payloadJson = "{}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.default.get",
			modelsDefaultGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.default.get missing `model`.";
			return false;
		}

		const ResponseFrame configSchemaResponseNegative{
			.id = "req-schema-93",
			.ok = true,
			.payloadJson = "{\"gateway\":{\"bind\":\"string\",\"port\":\"number\"}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.schema",
			configSchemaResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.schema missing `agent` schema object.";
			return false;
		}

		const ResponseFrame transportPolicyGetResponseNegative{
			.id = "req-schema-94",
			.ok = true,
			.payloadJson = "{\"exclusiveAddrUse\":true,\"keepAlive\":true,\"noDelay\":true,\"idleTimeoutMs\":120000}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.get",
			transportPolicyGetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.get missing `handshakeTimeoutMs`.";
			return false;
		}

		const ResponseFrame eventsTypesResponseNegative{
			.id = "req-schema-95",
			.ok = true,
			.payloadJson = "{\"types\":[\"lifecycle\",\"update\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.types",
			eventsTypesResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.types missing `count`.";
			return false;
		}

		const ResponseFrame toolsHealthResponseNegative{
			.id = "req-schema-96",
			.ok = true,
			.payloadJson = "{\"healthy\":true,\"enabled\":2}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.health",
			toolsHealthResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.health missing `total`.";
			return false;
		}

		const ResponseFrame modelsCompatibilityResponseNegative{
			.id = "req-schema-97",
			.ok = true,
			.payloadJson = "{\"default\":\"full\",\"reasoner\":\"partial\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.compatibility",
			modelsCompatibilityResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.compatibility missing `count`.";
			return false;
		}

		const ResponseFrame configValidateResponseNegative{
			.id = "req-schema-98",
			.ok = true,
			.payloadJson = "{\"valid\":true,\"errors\":[]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.validate",
			configValidateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.validate missing `count`.";
			return false;
		}

		const ResponseFrame transportPolicySetResponseNegative{
			.id = "req-schema-99",
			.ok = true,
			.payloadJson = "{\"applied\":false}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.set",
			transportPolicySetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.set missing `reason`.";
			return false;
		}

		const ResponseFrame eventsChannelsResponseNegative{
			.id = "req-schema-100",
			.ok = true,
			.payloadJson = "{\"channelEvents\":3,\"accountEvents\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.channels",
			eventsChannelsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.channels missing `count`.";
			return false;
		}

		const ResponseFrame toolsMetricsResponseNegative{
			.id = "req-schema-101",
			.ok = true,
			.payloadJson = "{\"invocations\":0,\"enabled\":2,\"disabled\":0}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.metrics",
			toolsMetricsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.metrics missing `total`.";
			return false;
		}

		const ResponseFrame modelsRecommendedResponseNegative{
			.id = "req-schema-102",
			.ok = true,
			.payloadJson = "{\"reason\":\"seed_default\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.recommended",
			modelsRecommendedResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.recommended missing `model`.";
			return false;
		}

		const ResponseFrame configAuditResponseNegative{
			.id = "req-schema-103",
			.ok = true,
			.payloadJson = "{\"enabled\":true,\"source\":\"runtime\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.audit",
			configAuditResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.audit missing `lastUpdatedMs`.";
			return false;
		}

		const ResponseFrame transportPolicyResetResponseNegative{
			.id = "req-schema-104",
			.ok = true,
			.payloadJson = "{\"reset\":true}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.reset",
			transportPolicyResetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.reset missing `applied`.";
			return false;
		}

		const ResponseFrame eventsTimelineResponseNegative{
			.id = "req-schema-105",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.shutdown\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.timeline",
			eventsTimelineResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.timeline missing `count`.";
			return false;
		}

		const ResponseFrame toolsFailuresResponseNegative{
			.id = "req-schema-106",
			.ok = true,
			.payloadJson = "{\"failed\":0,\"total\":2}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.failures",
			toolsFailuresResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.failures missing `rate`.";
			return false;
		}

		const ResponseFrame modelsFallbackResponseNegative{
			.id = "req-schema-107",
			.ok = true,
			.payloadJson = "{\"preferred\":\"default\",\"fallback\":\"reasoner\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.fallback",
			modelsFallbackResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.fallback missing `configured`.";
			return false;
		}

		const ResponseFrame configRollbackResponseNegative{
			.id = "req-schema-108",
			.ok = true,
			.payloadJson = "{\"rolledBack\":false}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.rollback",
			configRollbackResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.rollback missing `version`.";
			return false;
		}

		const ResponseFrame transportPolicyStatusResponseNegative{
			.id = "req-schema-109",
			.ok = true,
			.payloadJson = "{\"mutable\":false,\"lastApplied\":\"runtime_immutable\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.status",
			transportPolicyStatusResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.status missing `policyVersion`.";
			return false;
		}

		const ResponseFrame eventsLatestByTypeResponseNegative{
			.id = "req-schema-110",
			.ok = true,
			.payloadJson = "{\"type\":\"update\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.latestByType",
			eventsLatestByTypeResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.latestByType missing `event`.";
			return false;
		}

		const ResponseFrame toolsUsageResponseNegative{
			.id = "req-schema-111",
			.ok = true,
			.payloadJson = "{\"calls\":0,\"tools\":2}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.usage",
			toolsUsageResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.usage missing `avgMs`.";
			return false;
		}

		const ResponseFrame modelsSelectionResponseNegative{
			.id = "req-schema-112",
			.ok = true,
			.payloadJson = "{\"selected\":\"default\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.selection",
			modelsSelectionResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.selection missing `strategy`.";
			return false;
		}

		const ResponseFrame configBackupResponseNegative{
			.id = "req-schema-113",
			.ok = true,
			.payloadJson = "{\"saved\":true,\"version\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.backup",
			configBackupResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.backup missing `path`.";
			return false;
		}

		const ResponseFrame transportPolicyValidateResponseNegative{
			.id = "req-schema-114",
			.ok = true,
			.payloadJson = "{\"valid\":true,\"errors\":[]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.validate",
			transportPolicyValidateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.validate missing `count`.";
			return false;
		}

		const ResponseFrame eventsSampleResponseNegative{
			.id = "req-schema-115",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.health\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.sample",
			eventsSampleResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.sample missing `count`.";
			return false;
		}

		const ResponseFrame toolsLatencyResponseNegative{
			.id = "req-schema-116",
			.ok = true,
			.payloadJson = "{\"minMs\":0,\"maxMs\":0,\"avgMs\":0}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.latency",
			toolsLatencyResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.latency missing `samples`.";
			return false;
		}

		const ResponseFrame modelsRoutingResponseNegative{
			.id = "req-schema-117",
			.ok = true,
			.payloadJson = "{\"primary\":\"default\",\"fallback\":\"reasoner\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.routing",
			modelsRoutingResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.routing missing `strategy`.";
			return false;
		}

		const ResponseFrame configDiffResponseNegative{
			.id = "req-schema-118",
			.ok = true,
			.payloadJson = "{\"changed\":[]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.diff",
			configDiffResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.diff missing `count`.";
			return false;
		}

		const ResponseFrame transportPolicyHistoryResponseNegative{
			.id = "req-schema-119",
			.ok = true,
			.payloadJson = "{\"entries\":[{\"version\":1,\"applied\":false,\"reason\":\"runtime_immutable\"}]}",
		   .error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.history",
			transportPolicyHistoryResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.history missing `count`.";
			return false;
		}

		const ResponseFrame eventsWindowResponseNegative{
			.id = "req-schema-120",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.session.reset\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.window",
			eventsWindowResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.window missing `count`.";
			return false;
		}

		const ResponseFrame toolsErrorsResponseNegative{
			.id = "req-schema-121",
			.ok = true,
			.payloadJson = "{\"errors\":0,\"tools\":2}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.errors",
			toolsErrorsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.errors missing `rate`.";
			return false;
		}

		const ResponseFrame modelsPreferenceResponseNegative{
			.id = "req-schema-122",
			.ok = true,
			.payloadJson = "{\"model\":\"default\",\"provider\":\"seed\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.preference",
			modelsPreferenceResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.preference missing `source`.";
			return false;
		}

		const ResponseFrame configSnapshotResponseNegative{
			.id = "req-schema-123",
			.ok = true,
			.payloadJson = "{\"gateway\":{\"bind\":\"127.0.0.1\",\"port\":18789}}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.snapshot",
			configSnapshotResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.snapshot missing `agent`.";
			return false;
		}

		const ResponseFrame transportPolicyMetricsResponseNegative{
			.id = "req-schema-124",
			.ok = true,
			.payloadJson = "{\"validations\":0,\"resets\":0}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.metrics",
			transportPolicyMetricsResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.metrics missing `sets`.";
			return false;
		}

		const ResponseFrame eventsRecentResponseNegative{
			.id = "req-schema-125",
			.ok = true,
			.payloadJson = "{\"events\":[\"gateway.shutdown\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.recent",
			eventsRecentResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.recent missing `count`.";
			return false;
		}

		const ResponseFrame toolsThroughputResponseNegative{
			.id = "req-schema-126",
			.ok = true,
			.payloadJson = "{\"calls\":0,\"windowSec\":60,\"perMinute\":0}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.throughput",
			toolsThroughputResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.throughput missing `tools`.";
			return false;
		}

		const ResponseFrame modelsPriorityResponseNegative{
			.id = "req-schema-127",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.priority",
			modelsPriorityResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.priority missing `count`.";
			return false;
		}

		const ResponseFrame configRevisionResponseNegative{
			.id = "req-schema-128",
			.ok = true,
			.payloadJson = "{\"revision\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.revision",
			configRevisionResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.revision missing `source`.";
			return false;
		}

		const ResponseFrame transportPolicyExportResponseNegative{
			.id = "req-schema-129",
			.ok = true,
			.payloadJson = "{\"path\":\"transport/policy-export.json\",\"version\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.export",
			transportPolicyExportResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.export missing `exported`.";
			return false;
		}

		const ResponseFrame eventsBatchResponseNegative{
			.id = "req-schema-130",
			.ok = true,
			.payloadJson = "{\"batches\":[\"lifecycle\",\"updates\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.batch",
			eventsBatchResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.batch missing `count`.";
			return false;
		}

		const ResponseFrame toolsCapacityResponseNegative{
			.id = "req-schema-131",
			.ok = true,
			.payloadJson = "{\"total\":2,\"used\":0}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.capacity",
			toolsCapacityResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.capacity missing `free`.";
			return false;
		}

		const ResponseFrame modelsAffinityResponseNegative{
			.id = "req-schema-132",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.affinity",
			modelsAffinityResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.affinity missing `affinity`.";
			return false;
		}

		const ResponseFrame configHistoryResponseNegative{
			.id = "req-schema-133",
			.ok = true,
			.payloadJson = "{\"revisions\":[1]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.history",
			configHistoryResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.history missing `count`.";
			return false;
		}

		const ResponseFrame transportPolicyImportResponseNegative{
			.id = "req-schema-134",
			.ok = true,
			.payloadJson = "{\"imported\":true,\"version\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.import",
			transportPolicyImportResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.import missing `applied`.";
			return false;
		}

		const ResponseFrame eventsCursorResponseNegative{
			.id = "req-schema-135",
			.ok = true,
			.payloadJson = "{\"cursor\":\"evt-2\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.cursor",
			eventsCursorResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.cursor missing `event`.";
			return false;
		}

		const ResponseFrame toolsQueueResponseNegative{
			.id = "req-schema-136",
			.ok = true,
			.payloadJson = "{\"queued\":0,\"running\":0}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.queue",
			toolsQueueResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.queue missing `tools`.";
			return false;
		}

		const ResponseFrame modelsPoolResponseNegative{
			.id = "req-schema-137",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.pool",
			modelsPoolResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.pool missing `count`.";
			return false;
		}

		const ResponseFrame configProfileResponseNegative{
			.id = "req-schema-138",
			.ok = true,
			.payloadJson = "{\"name\":\"default\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.profile",
			configProfileResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.profile missing `source`.";
			return false;
		}

		const ResponseFrame transportPolicyDigestResponseNegative{
			.id = "req-schema-139",
			.ok = true,
			.payloadJson = "{\"digest\":\"sha256:seed-policy-v1\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.digest",
			transportPolicyDigestResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.digest missing `version`.";
			return false;
		}

		const ResponseFrame eventsAnchorResponseNegative{
			.id = "req-schema-140",
			.ok = true,
			.payloadJson = "{\"anchor\":\"evt-1\"}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.anchor",
			eventsAnchorResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.anchor missing `event`.";
			return false;
		}

		const ResponseFrame toolsSchedulerResponseNegative{
			.id = "req-schema-141",
			.ok = true,
			.payloadJson = "{\"ticks\":0,\"queued\":0}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.scheduler",
			toolsSchedulerResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.scheduler missing `tools`.";
			return false;
		}

		const ResponseFrame modelsManifestResponseNegative{
			.id = "req-schema-142",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.manifest",
			modelsManifestResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.manifest missing `manifestVersion`.";
			return false;
		}

		const ResponseFrame configTemplateResponseNegative{
			.id = "req-schema-143",
			.ok = true,
			.payloadJson = "{\"template\":\"default\",\"keys\":[\"gateway.bind\",\"gateway.port\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.template",
			configTemplateResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.template missing `count`.";
			return false;
		}

		const ResponseFrame transportPolicyPreviewResponseNegative{
			.id = "req-schema-144",
			.ok = true,
			.payloadJson = "{\"path\":\"transport/policy-preview.json\",\"applied\":false}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.preview",
			transportPolicyPreviewResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.preview missing `notes`.";
			return false;
		}

		const ResponseFrame eventsOffsetResponseNegative{
			.id = "req-schema-145",
			.ok = true,
			.payloadJson = "{\"offset\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.events.offset",
			eventsOffsetResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.events.offset missing `event`.";
			return false;
		}

		const ResponseFrame toolsBacklogResponseNegative{
			.id = "req-schema-146",
			.ok = true,
			.payloadJson = "{\"pending\":0,\"capacity\":2}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.tools.backlog",
			toolsBacklogResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.tools.backlog missing `tools`.";
			return false;
		}

		const ResponseFrame modelsCatalogResponseNegative{
			.id = "req-schema-147",
			.ok = true,
			.payloadJson = "{\"models\":[\"default\",\"reasoner\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.models.catalog",
			modelsCatalogResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.models.catalog missing `count`.";
			return false;
		}

		const ResponseFrame configBundleResponseNegative{
			.id = "req-schema-148",
			.ok = true,
			.payloadJson = "{\"name\":\"runtime\",\"sections\":[\"gateway\",\"agent\"]}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.config.bundle",
			configBundleResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.config.bundle missing `count`.";
			return false;
		}

		const ResponseFrame transportPolicyCommitResponseNegative{
			.id = "req-schema-149",
			.ok = true,
			.payloadJson = "{\"committed\":false,\"version\":1}",
			.error = std::nullopt,
		};
		if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"gateway.transport.policy.commit",
			transportPolicyCommitResponseNegative,
			responseIssue)) {
			error = "Schema response negative case unexpectedly passed for gateway.transport.policy.commit missing `applied`.";
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
			root / "response_channels_route_set.json",
			SerializeResponseFrame(channelsRouteSetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_route_delete.json",
			SerializeResponseFrame(channelsRouteDeleteResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_route_exists.json",
			SerializeResponseFrame(channelsRouteExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_activate.json",
			SerializeResponseFrame(channelsAccountsActivateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_deactivate.json",
			SerializeResponseFrame(channelsAccountsDeactivateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_exists.json",
			SerializeResponseFrame(channelsAccountsExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_update.json",
			SerializeResponseFrame(channelsAccountsUpdateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_get.json",
			SerializeResponseFrame(channelsAccountsGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_create.json",
			SerializeResponseFrame(channelsAccountsCreateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_delete.json",
			SerializeResponseFrame(channelsAccountsDeleteResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_routes_clear.json",
			SerializeResponseFrame(channelsRoutesClearResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_routes_restore.json",
			SerializeResponseFrame(channelsRoutesRestoreResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_route_get.json",
			SerializeResponseFrame(channelsRouteGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_route_restore.json",
			SerializeResponseFrame(channelsRouteRestoreResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_clear.json",
			SerializeResponseFrame(channelsAccountsClearResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_restore.json",
			SerializeResponseFrame(channelsAccountsRestoreResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_count.json",
			SerializeResponseFrame(channelsAccountsCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_routes_count.json",
			SerializeResponseFrame(channelsRoutesCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_route_patch.json",
			SerializeResponseFrame(channelsRoutePatchResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_routes_reset.json",
			SerializeResponseFrame(channelsRoutesResetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_accounts_reset.json",
			SerializeResponseFrame(channelsAccountsResetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_route_reset.json",
			SerializeResponseFrame(channelsRouteResetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_status_get.json",
			SerializeResponseFrame(channelsStatusGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_status_exists.json",
			SerializeResponseFrame(channelsStatusExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_channels_status_count.json",
			SerializeResponseFrame(channelsStatusCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_exists.json",
			SerializeResponseFrame(sessionsExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_count.json",
			SerializeResponseFrame(sessionsCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_sessions_activate.json",
			SerializeResponseFrame(sessionsActivateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_exists.json",
			SerializeResponseFrame(agentsExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_agents_count.json",
			SerializeResponseFrame(agentsCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_exists.json",
			SerializeResponseFrame(eventsExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_count.json",
			SerializeResponseFrame(eventsCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_exists.json",
			SerializeResponseFrame(toolsExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_count.json",
			SerializeResponseFrame(toolsCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_exists.json",
			SerializeResponseFrame(modelsExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_exists.json",
			SerializeResponseFrame(configExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_keys.json",
			SerializeResponseFrame(configKeysResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_connections_count.json",
			SerializeResponseFrame(transportConnectionsCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_health_details.json",
			SerializeResponseFrame(healthDetailsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_logs_count.json",
			SerializeResponseFrame(logsCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_count.json",
			SerializeResponseFrame(configCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_count.json",
			SerializeResponseFrame(modelsCountResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_get.json",
			SerializeResponseFrame(eventsGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_endpoint_get.json",
			SerializeResponseFrame(transportEndpointGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_logs_levels.json",
			SerializeResponseFrame(logsLevelsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_list.json",
			SerializeResponseFrame(eventsListResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_get.json",
			SerializeResponseFrame(toolsGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_get.json",
			SerializeResponseFrame(modelsGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_getKey.json",
			SerializeResponseFrame(configGetKeyResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_endpoint_exists.json",
			SerializeResponseFrame(transportEndpointExistsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_last.json",
			SerializeResponseFrame(eventsLastResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_list.json",
			SerializeResponseFrame(toolsListResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_listByProvider.json",
			SerializeResponseFrame(modelsListByProviderResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_sections.json",
			SerializeResponseFrame(configSectionsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_endpoint_set.json",
			SerializeResponseFrame(transportEndpointSetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_search.json",
			SerializeResponseFrame(eventsSearchResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_categories.json",
			SerializeResponseFrame(toolsCategoriesResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_providers.json",
			SerializeResponseFrame(modelsProvidersResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_getSection.json",
			SerializeResponseFrame(configGetSectionResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_endpoints_list.json",
			SerializeResponseFrame(transportEndpointsListResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_summary.json",
			SerializeResponseFrame(eventsSummaryResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_stats.json",
			SerializeResponseFrame(toolsStatsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_default_get.json",
			SerializeResponseFrame(modelsDefaultGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_schema.json",
			SerializeResponseFrame(configSchemaResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_get.json",
			SerializeResponseFrame(transportPolicyGetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_types.json",
			SerializeResponseFrame(eventsTypesResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_health.json",
			SerializeResponseFrame(toolsHealthResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_compatibility.json",
			SerializeResponseFrame(modelsCompatibilityResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_validate.json",
			SerializeResponseFrame(configValidateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_set.json",
			SerializeResponseFrame(transportPolicySetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_channels.json",
			SerializeResponseFrame(eventsChannelsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_metrics.json",
			SerializeResponseFrame(toolsMetricsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_recommended.json",
			SerializeResponseFrame(modelsRecommendedResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_audit.json",
			SerializeResponseFrame(configAuditResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_reset.json",
			SerializeResponseFrame(transportPolicyResetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_timeline.json",
			SerializeResponseFrame(eventsTimelineResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_failures.json",
			SerializeResponseFrame(toolsFailuresResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_fallback.json",
			SerializeResponseFrame(modelsFallbackResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_rollback.json",
			SerializeResponseFrame(configRollbackResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_status.json",
			SerializeResponseFrame(transportPolicyStatusResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_latestByType.json",
			SerializeResponseFrame(eventsLatestByTypeResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_usage.json",
			SerializeResponseFrame(toolsUsageResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_selection.json",
			SerializeResponseFrame(modelsSelectionResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_backup.json",
			SerializeResponseFrame(configBackupResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_validate.json",
			SerializeResponseFrame(transportPolicyValidateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_sample.json",
			SerializeResponseFrame(eventsSampleResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_latency.json",
			SerializeResponseFrame(toolsLatencyResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_routing.json",
			SerializeResponseFrame(modelsRoutingResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_diff.json",
			SerializeResponseFrame(configDiffResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_history.json",
			SerializeResponseFrame(transportPolicyHistoryResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_window.json",
			SerializeResponseFrame(eventsWindowResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_errors.json",
			SerializeResponseFrame(toolsErrorsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_preference.json",
			SerializeResponseFrame(modelsPreferenceResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_snapshot.json",
			SerializeResponseFrame(configSnapshotResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_metrics.json",
			SerializeResponseFrame(transportPolicyMetricsResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_recent.json",
			SerializeResponseFrame(eventsRecentResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_throughput.json",
			SerializeResponseFrame(toolsThroughputResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_priority.json",
			SerializeResponseFrame(modelsPriorityResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_revision.json",
			SerializeResponseFrame(configRevisionResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_export.json",
			SerializeResponseFrame(transportPolicyExportResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_batch.json",
			SerializeResponseFrame(eventsBatchResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_capacity.json",
			SerializeResponseFrame(toolsCapacityResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_affinity.json",
			SerializeResponseFrame(modelsAffinityResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_history.json",
			SerializeResponseFrame(configHistoryResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_import.json",
			SerializeResponseFrame(transportPolicyImportResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_cursor.json",
			SerializeResponseFrame(eventsCursorResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_queue.json",
			SerializeResponseFrame(toolsQueueResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_pool.json",
			SerializeResponseFrame(modelsPoolResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_profile.json",
			SerializeResponseFrame(configProfileResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_digest.json",
			SerializeResponseFrame(transportPolicyDigestResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_anchor.json",
			SerializeResponseFrame(eventsAnchorResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_scheduler.json",
			SerializeResponseFrame(toolsSchedulerResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_manifest.json",
			SerializeResponseFrame(modelsManifestResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_template.json",
			SerializeResponseFrame(configTemplateResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_preview.json",
			SerializeResponseFrame(transportPolicyPreviewResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_events_offset.json",
			SerializeResponseFrame(eventsOffsetResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_tools_backlog.json",
			SerializeResponseFrame(toolsBacklogResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_models_catalog.json",
			SerializeResponseFrame(modelsCatalogResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_config_bundle.json",
			SerializeResponseFrame(configBundleResponse),
			error)) {
			return false;
		}

		if (!CompareFixture(
			root / "response_transport_policy_commit.json",
			SerializeResponseFrame(transportPolicyCommitResponse),
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
