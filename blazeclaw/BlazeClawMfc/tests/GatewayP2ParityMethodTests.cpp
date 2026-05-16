#include <catch2/catch_all.hpp>

#include "gateway/GatewayHost.h"

#include <nlohmann/json.hpp>

using blazeclaw::gateway::GatewayHost;
using blazeclaw::gateway::protocol::RequestFrame;

namespace {

	blazeclaw::gateway::protocol::ResponseFrame Route(
		GatewayHost& host,
		const std::string& id,
		const std::string& method,
		std::optional<std::string> paramsJson = std::nullopt)
	{
		return host.RouteRequest(
			RequestFrame{
				.id = id,
				.method = method,
				.paramsJson = std::move(paramsJson),
			});
	}

} // namespace

TEST_CASE("P2 parity methods: classic agent aliases are routable", "[gateway][parity][p2]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto agentIdentity = Route(host, "p2-agent-identity", "agent.identity.get");
	REQUIRE(agentIdentity.ok);
	REQUIRE(agentIdentity.payloadJson.has_value());
	REQUIRE(agentIdentity.payloadJson.value().find("\"agent\"") != std::string::npos);

	const auto agentWait = Route(host, "p2-agent-wait", "agent.wait");
	REQUIRE(agentWait.ok);
	REQUIRE(agentWait.payloadJson.has_value());
	REQUIRE(agentWait.payloadJson.value().find("\"status\":\"idle\"") != std::string::npos);

	const auto wake = Route(host, "p2-wake", "wake");
	REQUIRE(wake.ok);
	REQUIRE(wake.payloadJson.has_value());
	REQUIRE(wake.payloadJson.value().find("\"wake\":true") != std::string::npos);
}

TEST_CASE("P2 parity methods: heartbeat and system aliases are routable", "[gateway][parity][p2]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto heartbeat = Route(host, "p2-heartbeat", "last-heartbeat");
	REQUIRE(heartbeat.ok);
	REQUIRE(heartbeat.payloadJson.has_value());
	REQUIRE(heartbeat.payloadJson.value().find("\"lastHeartbeatMs\"") != std::string::npos);

	const auto presence = Route(host, "p2-presence", "system-presence");
	REQUIRE(presence.ok);
	REQUIRE(presence.payloadJson.has_value());
	REQUIRE(presence.payloadJson.value().find("\"mode\":") != std::string::npos);

	const auto identity = Route(host, "p2-gateway-identity", "gateway.identity.get");
	REQUIRE(identity.ok);
	REQUIRE(identity.payloadJson.has_value());
	REQUIRE(identity.payloadJson.value().find("\"blazeclaw.gateway\"") != std::string::npos);
}

TEST_CASE("P2 parity methods: commands and tool-effective aliases are routable", "[gateway][parity][p2]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto commandsList = Route(host, "p2-commands-list", "commands.list");
	REQUIRE(commandsList.ok);
	REQUIRE(commandsList.payloadJson.has_value());
	REQUIRE(commandsList.payloadJson.value().find("\"tools\"") != std::string::npos);

	const auto toolsEffective = Route(
		host,
		"p2-tools-effective",
		"tools.effective",
		R"({"sessionId":"main","agentId":"default"})");
	REQUIRE(toolsEffective.ok);
	REQUIRE(toolsEffective.payloadJson.has_value());
	REQUIRE(toolsEffective.payloadJson.value().find("\"tools\"") != std::string::npos);
}

TEST_CASE("P2 parity methods: skills, web login, update, and doctor memory methods are routable", "[gateway][parity][p2]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto skillsSearch = Route(host, "p2-skills-search", "skills.search");
	REQUIRE(skillsSearch.ok);
	REQUIRE(skillsSearch.payloadJson.has_value());
	REQUIRE(skillsSearch.payloadJson.value().find("\"skills\"") != std::string::npos);

	const auto webLoginStart = Route(host, "p2-web-login-start", "web.login.start");
	REQUIRE(webLoginStart.ok);
	REQUIRE(webLoginStart.payloadJson.has_value());
	REQUIRE(webLoginStart.payloadJson.value().find("\"status\":") != std::string::npos);

	const auto ttsStatus = Route(host, "p2-tts-status", "tts.status");
	REQUIRE(ttsStatus.ok);
	REQUIRE(ttsStatus.payloadJson.has_value());
	REQUIRE(ttsStatus.payloadJson.value().find("\"provider\":\"default\"") != std::string::npos);

	const auto ttsEnable = Route(host, "p2-tts-enable", "tts.enable");
	REQUIRE(ttsEnable.ok);
	REQUIRE(ttsEnable.payloadJson.has_value());
	REQUIRE(ttsEnable.payloadJson.value().find("\"enabled\":true") != std::string::npos);

	const auto ttsConvert = Route(host, "p2-tts-convert", "tts.convert", std::string("{\"text\":\"hello\"}"));
	REQUIRE(ttsConvert.ok);
	REQUIRE(ttsConvert.payloadJson.has_value());
	REQUIRE(ttsConvert.payloadJson.value().find("\"audioPath\":\"artifacts/tts/tts-") != std::string::npos);

	const auto secretsReload = Route(host, "p2-secrets-reload", "secrets.reload");
	REQUIRE(secretsReload.ok);
	REQUIRE(secretsReload.payloadJson.has_value());
	REQUIRE(secretsReload.payloadJson.value().find("\"ok\":true") != std::string::npos);

	const auto secretsResolve = Route(
		host,
		"p2-secrets-resolve",
		"secrets.resolve",
		std::string("{\"commandName\":\"email.schedule\",\"targetIds\":[\"runtime\"]}"));
	REQUIRE(secretsResolve.ok);
	REQUIRE(secretsResolve.payloadJson.has_value());
	REQUIRE(secretsResolve.payloadJson.value().find("\"commandName\":\"email.schedule\"") != std::string::npos);

	const auto updateRun = Route(host, "p2-update-run", "update.run");
	REQUIRE(updateRun.ok);
	REQUIRE(updateRun.payloadJson.has_value());
	REQUIRE(updateRun.payloadJson.value().find("\"status\":\"running\"") != std::string::npos);

	const auto ttsConvertInvalid = Route(host, "p2-tts-convert-invalid", "tts.convert", std::string("{}"));
	REQUIRE_FALSE(ttsConvertInvalid.ok);
	REQUIRE(ttsConvertInvalid.error.has_value());
	REQUIRE(ttsConvertInvalid.error->code == "invalid_request");

	const auto secretsResolveInvalid = Route(host, "p2-secrets-resolve-invalid", "secrets.resolve", std::string("{}"));
	REQUIRE_FALSE(secretsResolveInvalid.ok);
	REQUIRE(secretsResolveInvalid.error.has_value());
	REQUIRE(secretsResolveInvalid.error->code == "invalid_request");

	const auto doctorMemoryStatus = Route(host, "p2-doctor-memory-status", "doctor.memory.status");
	REQUIRE(doctorMemoryStatus.ok);
	REQUIRE(doctorMemoryStatus.payloadJson.has_value());
	REQUIRE(doctorMemoryStatus.payloadJson.value().find("\"status\":\"healthy\"") != std::string::npos);
	REQUIRE(doctorMemoryStatus.payloadJson.value().find("\"dreaming\":{") != std::string::npos);
	REQUIRE(doctorMemoryStatus.payloadJson.value().find("\"phases\":{") != std::string::npos);
	REQUIRE(doctorMemoryStatus.payloadJson.value().find("\"shortTermEntries\":") != std::string::npos);
	REQUIRE(doctorMemoryStatus.payloadJson.value().find("\"promotedEntries\":") != std::string::npos);

	const auto doctorMemoryDreamDiary = Route(host, "p2-doctor-memory-dreamDiary", "doctor.memory.dreamDiary");
	REQUIRE(doctorMemoryDreamDiary.ok);
	REQUIRE(doctorMemoryDreamDiary.payloadJson.has_value());
	REQUIRE(doctorMemoryDreamDiary.payloadJson.value().find("\"entries\":[]") != std::string::npos);
	REQUIRE(doctorMemoryDreamDiary.payloadJson.value().find("\"path\":\"DREAMS.md\"") != std::string::npos);
	REQUIRE(doctorMemoryDreamDiary.payloadJson.value().find("\"found\":") != std::string::npos);
	REQUIRE(doctorMemoryDreamDiary.payloadJson.value().find("\"content\":") != std::string::npos);

	const auto backfillDreamDiary = Route(host, "p2-doctor-memory-backfill", "doctor.memory.backfillDreamDiary");
	REQUIRE(backfillDreamDiary.ok);
	REQUIRE(backfillDreamDiary.payloadJson.has_value());
	REQUIRE(backfillDreamDiary.payloadJson.value().find("\"queued\":true") != std::string::npos);

	const auto doctorMemoryStatusAfterBackfill = Route(host, "p2-doctor-memory-status-after-backfill", "doctor.memory.status");
	REQUIRE(doctorMemoryStatusAfterBackfill.ok);
	REQUIRE(doctorMemoryStatusAfterBackfill.payloadJson.has_value());
	REQUIRE(doctorMemoryStatusAfterBackfill.payloadJson.value().find("\"promotedTotal\":") != std::string::npos);

	const auto resetGrounded = Route(host, "p2-doctor-memory-reset-grounded", "doctor.memory.resetGroundedShortTerm");
	REQUIRE(resetGrounded.ok);
	REQUIRE(resetGrounded.payloadJson.has_value());
	REQUIRE(resetGrounded.payloadJson.value().find("\"removedShortTermEntries\":true") != std::string::npos);

	const auto resetDreamDiary = Route(host, "p2-doctor-memory-reset-diary", "doctor.memory.resetDreamDiary");
	REQUIRE(resetDreamDiary.ok);
	REQUIRE(resetDreamDiary.payloadJson.has_value());
	REQUIRE(resetDreamDiary.payloadJson.value().find("\"removedEntries\":true") != std::string::npos);

	const auto configGetBeforePatch =
		Route(host, "p2-config-get-before-patch", "config.get");
	REQUIRE(configGetBeforePatch.ok);
	REQUIRE(configGetBeforePatch.payloadJson.has_value());
	REQUIRE(configGetBeforePatch.payloadJson.value().find("\"hash\":") != std::string::npos);
	REQUIRE(configGetBeforePatch.payloadJson.value().find("\"plugins\":") != std::string::npos);
	REQUIRE(configGetBeforePatch.payloadJson.value().find("\"memory-core\":") != std::string::npos);
	REQUIRE(configGetBeforePatch.payloadJson.value().find("\"dreaming\":") != std::string::npos);

	const auto configPatchDisable = Route(
		host,
		"p2-config-patch-disable",
		"config.patch",
		std::string("{\"baseHash\":\"dreaming-config-0\",\"raw\":\"{\\\"dreaming\\\":{\\\"enabled\\\":false}}\"}"));
	REQUIRE_FALSE(configPatchDisable.ok);
	REQUIRE(configPatchDisable.error.has_value());
	REQUIRE(configPatchDisable.error->code == "config_hash_mismatch");

	const auto configSchemaLookup = Route(
		host,
		"p2-config-schema-lookup",
		"config.schema.lookup",
		std::string("{\"path\":\"plugins.entries.memory-core.config\"}"));
	if (configSchemaLookup.ok) {
		REQUIRE(configSchemaLookup.payloadJson.has_value());
		REQUIRE(configSchemaLookup.payloadJson.value().find("\"path\"") != std::string::npos);
	}
	else {
		REQUIRE(configSchemaLookup.error.has_value());
		const bool isSchemaPathNotFound =
			configSchemaLookup.error->code == "schema_path_not_found";
		const bool isNotSupported =
			configSchemaLookup.error->code == "not_supported";
		REQUIRE((isSchemaPathNotFound || isNotSupported));
	}
}

TEST_CASE("P2 parity methods: cron contract read-surface fields are routable", "[gateway][parity][p2][cron]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto cronStatus = Route(host, "p2-cron-status", "cron.status");
	REQUIRE(cronStatus.ok);
	REQUIRE(cronStatus.payloadJson.has_value());
	REQUIRE(cronStatus.payloadJson.value().find("\"enabled\":") != std::string::npos);
	REQUIRE(cronStatus.payloadJson.value().find("\"jobs\":") != std::string::npos);
	REQUIRE(cronStatus.payloadJson.value().find("\"nextWakeAtMs\":") != std::string::npos);

	const auto cronList = Route(
		host,
		"p2-cron-list",
		"cron.list",
		std::string("{\"limit\":1,\"offset\":0,\"enabled\":\"enabled\",\"query\":\"demo\",\"sortBy\":\"name\",\"sortDir\":\"asc\"}"));
	REQUIRE(cronList.ok);
	REQUIRE(cronList.payloadJson.has_value());
	REQUIRE(cronList.payloadJson.value().find("\"jobs\":") != std::string::npos);
	REQUIRE(cronList.payloadJson.value().find("\"total\":") != std::string::npos);
	REQUIRE(cronList.payloadJson.value().find("\"limit\":1") != std::string::npos);
	REQUIRE(cronList.payloadJson.value().find("\"offset\":0") != std::string::npos);
	REQUIRE(cronList.payloadJson.value().find("\"hasMore\":") != std::string::npos);

	const auto cronRuns = Route(
		host,
		"p2-cron-runs",
		"cron.runs",
		std::string("{\"scope\":\"job\",\"id\":\"cron-demo-hourly\",\"status\":\"ok\",\"limit\":1,\"offset\":0,\"sortDir\":\"desc\"}"));
	REQUIRE(cronRuns.ok);
	REQUIRE(cronRuns.payloadJson.has_value());
	REQUIRE(cronRuns.payloadJson.value().find("\"entries\":") != std::string::npos);
	REQUIRE(cronRuns.payloadJson.value().find("\"total\":") != std::string::npos);
	REQUIRE(cronRuns.payloadJson.value().find("\"limit\":1") != std::string::npos);
	REQUIRE(cronRuns.payloadJson.value().find("\"offset\":0") != std::string::npos);
	REQUIRE(cronRuns.payloadJson.value().find("\"hasMore\":") != std::string::npos);
}

TEST_CASE("P2 parity methods: cron mutation handlers validate params and return envelopes", "[gateway][parity][p2][cron][mutation]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto cronAddInvalid = Route(
		host,
		"p2-cron-add-invalid",
		"cron.add",
		std::string("{\"enabled\":true}"));
	REQUIRE_FALSE(cronAddInvalid.ok);
	REQUIRE(cronAddInvalid.error.has_value());
	REQUIRE(cronAddInvalid.error->code == "invalid_params");

	const auto cronAdd = Route(
		host,
		"p2-cron-add",
		"cron.add",
		std::string("{\"name\":\"Nightly sync\",\"enabled\":true,\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"sync\"}}"));
	REQUIRE(cronAdd.ok);
	REQUIRE(cronAdd.payloadJson.has_value());
	const nlohmann::json cronAdded = nlohmann::json::parse(cronAdd.payloadJson.value());
	REQUIRE(cronAdded.is_object());
	REQUIRE(cronAdded.contains("id"));
	REQUIRE(cronAdded["id"].is_string());
	const std::string cronId = cronAdded["id"].get<std::string>();
	REQUIRE_FALSE(cronId.empty());

	const auto cronUpdateInvalid = Route(
		host,
		"p2-cron-update-invalid",
		"cron.update",
		std::string("{\"patch\":{\"enabled\":false}}"));
	REQUIRE_FALSE(cronUpdateInvalid.ok);
	REQUIRE(cronUpdateInvalid.error.has_value());
	REQUIRE(cronUpdateInvalid.error->code == "invalid_params");

	const auto cronUpdate = Route(
		host,
		"p2-cron-update",
		"cron.update",
		std::string("{\"id\":\"") + cronId + "\",\"patch\":{\"enabled\":false}}");
	REQUIRE(cronUpdate.ok);
	REQUIRE(cronUpdate.payloadJson.has_value());
	const nlohmann::json cronUpdated = nlohmann::json::parse(cronUpdate.payloadJson.value());
	REQUIRE(cronUpdated.is_object());
	REQUIRE(cronUpdated.value("id", std::string()) == cronId);
	REQUIRE_FALSE(cronUpdated.value("enabled", true));

	const auto cronRunInvalid = Route(
		host,
		"p2-cron-run-invalid",
		"cron.run",
		std::string("{\"mode\":\"force\"}"));
	REQUIRE_FALSE(cronRunInvalid.ok);
	REQUIRE(cronRunInvalid.error.has_value());
	REQUIRE(cronRunInvalid.error->code == "invalid_params");

	const auto cronRun = Route(
		host,
		"p2-cron-run",
		"cron.run",
		std::string("{\"id\":\"") + cronId + "\",\"mode\":\"force\"}");
	REQUIRE(cronRun.ok);
	REQUIRE(cronRun.payloadJson.has_value());
	REQUIRE(cronRun.payloadJson.value().find("\"enqueued\":true") != std::string::npos);
	REQUIRE(cronRun.payloadJson.value().find("\"reason\":\"queued\"") != std::string::npos);
	REQUIRE(cronRun.payloadJson.value().find("\"mode\":\"force\"") != std::string::npos);

	const auto cronRemoveInvalid = Route(
		host,
		"p2-cron-remove-invalid",
		"cron.remove",
		std::string("{}"));
	REQUIRE_FALSE(cronRemoveInvalid.ok);
	REQUIRE(cronRemoveInvalid.error.has_value());
	REQUIRE(cronRemoveInvalid.error->code == "invalid_params");

	const auto cronRemove = Route(
		host,
		"p2-cron-remove",
		"cron.remove",
		std::string("{\"id\":\"") + cronId + "\"}");
	REQUIRE(cronRemove.ok);
	REQUIRE(cronRemove.payloadJson.has_value());
	REQUIRE(cronRemove.payloadJson.value().find("\"removed\":true") != std::string::npos);
	REQUIRE(cronRemove.payloadJson.value().find("\"ok\":true") != std::string::npos);
}
