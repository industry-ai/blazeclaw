#include <catch2/catch_all.hpp>

#include "gateway/GatewayHost.h"
#include "gateway/GatewayPersistencePaths.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

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

	std::string ExtractJsonStringField(const std::string& payloadJson, const std::string& fieldName)
	{
		const std::string marker = "\"" + fieldName + "\":\"";
		const std::size_t markerPos = payloadJson.find(marker);
		if (markerPos == std::string::npos) {
			return {};
		}
		const std::size_t valueStart = markerPos + marker.size();
		const std::size_t valueEnd = payloadJson.find('"', valueStart);
		if (valueEnd == std::string::npos || valueEnd <= valueStart) {
			return {};
		}
		return payloadJson.substr(valueStart, valueEnd - valueStart);
	}

} // namespace

TEST_CASE("P3 parity methods: device pairing lifecycle methods are stateful", "[gateway][parity][p3]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto requestPairing = Route(
		host,
		"p3-device-request",
		"node.pair.request",
		R"({"nodeId":"device-node-1","displayName":"Device Node","platform":"ios","commands":["camera.capture"]})");
	REQUIRE(requestPairing.ok);

	const auto listPending = Route(host, "p3-device-list-pending", "device.pair.list");
	REQUIRE(listPending.ok);
	REQUIRE(listPending.payloadJson.has_value());
	CHECK(listPending.payloadJson.value().find("\"pendingCount\":") != std::string::npos);
	const std::string requestId = ExtractJsonStringField(listPending.payloadJson.value(), "requestId");
	REQUIRE_FALSE(requestId.empty());

	const auto approve = Route(
		host,
		"p3-device-approve",
		"device.pair.approve",
		"{\"requestId\":\"" + requestId + "\",\"callerScopes\":[\"node.pair.approve\",\"node.pair.approve.media\"]}");
	REQUIRE(approve.ok);
	REQUIRE(approve.payloadJson.has_value());
	CHECK(approve.payloadJson.value().find("\"approved\":true") != std::string::npos);
	CHECK(approve.payloadJson.value().find("\"nodeId\":\"device-node-1\"") != std::string::npos);

	const auto remove = Route(
		host,
		"p3-device-remove",
		"device.pair.remove",
		R"({"nodeId":"device-node-1"})");
	REQUIRE(remove.ok);
	REQUIRE(remove.payloadJson.has_value());
	CHECK(remove.payloadJson.value().find("\"removed\":true") != std::string::npos);
}

TEST_CASE("P3 parity methods: config apply/patch include deep metadata", "[gateway][parity][p3]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto configGet = Route(host, "p3-config-get", "config.get");
	REQUIRE(configGet.ok);
	REQUIRE(configGet.payloadJson.has_value());
	const std::string hash = ExtractJsonStringField(configGet.payloadJson.value(), "hash");
	REQUIRE_FALSE(hash.empty());

	const auto applyInvalid = Route(
		host,
		"p3-config-apply-invalid",
		"config.apply",
		"{\"baseHash\":\"" + hash + "\",\"raw\":\"[]\"}");
	REQUIRE_FALSE(applyInvalid.ok);
	REQUIRE(applyInvalid.error.has_value());
	CHECK(applyInvalid.error->code == "invalid_request");

	const auto applyOk = Route(
		host,
		"p3-config-apply-ok",
		"config.apply",
		"{\"baseHash\":\"" + hash + "\",\"raw\":\"{\\\"dreaming\\\":{\\\"enabled\\\":true}}\"}");
	REQUIRE(applyOk.ok);
	REQUIRE(applyOk.payloadJson.has_value());
	CHECK(applyOk.payloadJson.value().find("\"changedPaths\":") != std::string::npos);
	CHECK(applyOk.payloadJson.value().find("\"restart\":{") != std::string::npos);

	const auto hash2 = ExtractJsonStringField(applyOk.payloadJson.value(), "hash");
	REQUIRE_FALSE(hash2.empty());
	const auto patchOk = Route(
		host,
		"p3-config-patch-ok",
		"config.patch",
		"{\"baseHash\":\"" + hash2 + "\",\"raw\":\"{\\\"dreaming\\\":{\\\"enabled\\\":false}}\"}");
	REQUIRE(patchOk.ok);
	REQUIRE(patchOk.payloadJson.has_value());
	CHECK(patchOk.payloadJson.value().find("\"changedPaths\":") != std::string::npos);
	CHECK(patchOk.payloadJson.value().find("\"restart\":{") != std::string::npos);
}

TEST_CASE("P3 parity methods: tools effective enforces trusted context", "[gateway][parity][p3]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto missingSession = Route(host, "p3-tools-effective-missing-session", "tools.effective", R"({"agentId":"default"})");
	REQUIRE_FALSE(missingSession.ok);
	REQUIRE(missingSession.error.has_value());
	CHECK(missingSession.error->code == "invalid_request");

	const auto missingAgent = Route(host, "p3-tools-effective-missing-agent", "tools.effective", R"({"sessionId":"main"})");
	REQUIRE_FALSE(missingAgent.ok);
	REQUIRE(missingAgent.error.has_value());
	CHECK(missingAgent.error->code == "invalid_request");

	const auto effective = Route(
		host,
		"p3-tools-effective-ok",
		"tools.effective",
		R"({"sessionId":"main","agentId":"default","category":"tool"})");
	REQUIRE(effective.ok);
	REQUIRE(effective.payloadJson.has_value());
	CHECK(effective.payloadJson.value().find("\"context\":{") != std::string::npos);
	CHECK(effective.payloadJson.value().find("\"count\":") != std::string::npos);
}

TEST_CASE("P3 parity methods: skills/update/tts/secrets and transport envelopes hardened", "[gateway][parity][p3]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto skillsInvalidSource = Route(
		host,
		"p3-skills-invalid-source",
		"skills.search",
		R"({"query":"email","source":"invalid"})");
	REQUIRE_FALSE(skillsInvalidSource.ok);
	REQUIRE(skillsInvalidSource.error.has_value());
	CHECK(skillsInvalidSource.error->code == "invalid_request");

	const auto skillsInstall = Route(
		host,
		"p3-skills-install",
		"skills.install",
		R"({"name":"email.schedule","source":"clawhub","version":"1.2.3"})");
	REQUIRE(skillsInstall.ok);
	REQUIRE(skillsInstall.payloadJson.has_value());
	CHECK(skillsInstall.payloadJson.value().find("\"source\":\"clawhub\"") != std::string::npos);
	CHECK(skillsInstall.payloadJson.value().find("\"version\":\"1.2.3\"") != std::string::npos);

	const auto updateRun = Route(
		host,
		"p3-update-run",
		"update.run",
		R"({"channel":"beta"})");
	REQUIRE(updateRun.ok);
	REQUIRE(updateRun.payloadJson.has_value());
	CHECK(updateRun.payloadJson.value().find("\"channel\":\"beta\"") != std::string::npos);
	CHECK(updateRun.payloadJson.value().find("\"steps\":[") != std::string::npos);

	const auto ttsInvalidProvider = Route(
		host,
		"p3-tts-invalid-provider",
		"tts.convert",
		R"({"text":"hello","provider":"bad"})");
	REQUIRE_FALSE(ttsInvalidProvider.ok);
	REQUIRE(ttsInvalidProvider.error.has_value());
	CHECK(ttsInvalidProvider.error->code == "invalid_request");

	const auto secretsUnknownTarget = Route(
		host,
		"p3-secrets-unknown-target",
		"secrets.resolve",
		R"({"commandName":"email.schedule","targetIds":["unknown"]})");
	REQUIRE_FALSE(secretsUnknownTarget.ok);
	REQUIRE(secretsUnknownTarget.error.has_value());
	CHECK(secretsUnknownTarget.error->code == "invalid_request");

	const auto heartbeat = Route(host, "p3-heartbeat", "last-heartbeat");
	REQUIRE(heartbeat.ok);
	REQUIRE(heartbeat.payloadJson.has_value());
	CHECK(heartbeat.payloadJson.value().find("\"ok\":true") != std::string::npos);
	CHECK(heartbeat.payloadJson.value().find("\"status\":") != std::string::npos);

	const auto systemEvent = Route(host, "p3-system-event", "system-event");
	REQUIRE(systemEvent.ok);
	REQUIRE(systemEvent.payloadJson.has_value());
	CHECK(systemEvent.payloadJson.value().find("\"source\":\"gateway.transport\"") != std::string::npos);

	// S3: transport + security ops handlers use `GatewayRuntimeContext` (same behavior as pre-S3).
	const auto& s3 = host.RuntimeContext();
	REQUIRE(s3.IsBound());
	REQUIRE(s3.dispatcher != nullptr);
	REQUIRE(s3.transport != nullptr);
	const auto transportStatus = Route(host, "p3-s3-transport-status", "gateway.transport.status");
	REQUIRE(transportStatus.ok);
	REQUIRE(transportStatus.payloadJson.has_value());
	CHECK(transportStatus.payloadJson.value().find("\"running\":") != std::string::npos);
	CHECK(transportStatus.payloadJson.value().find("\"endpoint\":") != std::string::npos);
}

TEST_CASE("P3 parity methods: sessions preview and usage use transcript fallback envelope", "[gateway][parity][p3][sessions]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto createSession = Route(
		host,
		"p3-session-create",
		"gateway.sessions.create",
		R"({"sessionId":"p3-session-utils","scope":"thread","active":true})");
	REQUIRE(createSession.ok);

	const auto preview = Route(
		host,
		"p3-session-preview",
		"gateway.sessions.preview",
		R"({"sessionId":"p3-session-utils"})");
	REQUIRE(preview.ok);
	REQUIRE(preview.payloadJson.has_value());
	CHECK(preview.payloadJson.value().find("\"title\":") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"usage\":{") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"modelProvider\":") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"fallbackSource\":") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"totalTokensFresh\":") != std::string::npos);

	const auto usage = Route(
		host,
		"p3-session-usage",
		"gateway.sessions.usage",
		R"({"sessionId":"p3-session-utils"})");
	REQUIRE(usage.ok);
	REQUIRE(usage.payloadJson.has_value());
	CHECK(usage.payloadJson.value().find("\"tokens\":{") != std::string::npos);
	CHECK(usage.payloadJson.value().find("\"estimatedCostUsd\":") != std::string::npos);
	CHECK(usage.payloadJson.value().find("\"model\":") != std::string::npos);
	CHECK(usage.payloadJson.value().find("\"fallbackSource\":") != std::string::npos);
	CHECK(usage.payloadJson.value().find("\"totalTokensFresh\":") != std::string::npos);

	const auto usageEnvelope = Route(
		host,
		"p3-session-usage-envelope",
		"sessions.usage",
		R"({"sessionId":"p3-session-utils","startDate":"2026-04-01","endDate":"2026-04-30"})");
	REQUIRE(usageEnvelope.ok);
	REQUIRE(usageEnvelope.payloadJson.has_value());
	CHECK(usageEnvelope.payloadJson.value().find("\"sessions\":[{") != std::string::npos);
	CHECK(usageEnvelope.payloadJson.value().find("\"totals\":{") != std::string::npos);
	CHECK(usageEnvelope.payloadJson.value().find("\"modelProvider\":") != std::string::npos);

	const auto listWithDerived = Route(
		host,
		"p3-session-list-derived",
		"gateway.session.list",
		R"({"includeDerivedTitles":true,"includeLastMessage":true,"search":"p3-session-utils"})");
	REQUIRE(listWithDerived.ok);
	REQUIRE(listWithDerived.payloadJson.has_value());
	CHECK(listWithDerived.payloadJson.value().find("\"derivedTitle\":") != std::string::npos);
	CHECK(listWithDerived.payloadJson.value().find("\"modelProvider\":") != std::string::npos);
	CHECK(listWithDerived.payloadJson.value().find("\"contextTokens\":") != std::string::npos);
	CHECK(listWithDerived.payloadJson.value().find("\"estimatedCostUsd\":") != std::string::npos);
	CHECK(listWithDerived.payloadJson.value().find("\"totalTokensFresh\":") != std::string::npos);
}

TEST_CASE("P3 parity methods: session list resolves freshest entry across multi-store paths", "[gateway][parity][p3][sessions][multistore]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const std::filesystem::path stateRoot = blazeclaw::gateway::ResolveGatewayStateDirectory();
	std::error_code ec;
	std::filesystem::create_directories(stateRoot / "agents" / "ops", ec);
	REQUIRE_FALSE(ec);
	{
		std::ofstream defaultStore(stateRoot / "sessions.state", std::ios::out | std::ios::trunc);
		REQUIRE(defaultStore.is_open());
		defaultStore << "agent:ops:main|thread|1\n";
	}
	{
		std::ofstream agentStore(stateRoot / "agents" / "ops" / "sessions.state", std::ios::out | std::ios::trunc);
		REQUIRE(agentStore.is_open());
		agentStore << "agent:ops:main|default|0\n";
	}

	const auto list = Route(
		host,
		"p3-session-list-multistore",
		"gateway.session.list",
		R"({"search":"agent:ops:main","includeGlobal":true,"includeUnknown":true})");
	REQUIRE(list.ok);
	REQUIRE(list.payloadJson.has_value());
	CHECK(list.payloadJson.value().find("\"id\":\"agent:ops:main\"") != std::string::npos);
	CHECK(list.payloadJson.value().find("\"scope\":\"thread\"") != std::string::npos);
	CHECK(list.payloadJson.value().find("\"active\":true") != std::string::npos);
}

TEST_CASE("P3 parity methods: session resolve uses freshest multi-store match", "[gateway][parity][p3][sessions][resolve][multistore]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const std::filesystem::path stateRoot = blazeclaw::gateway::ResolveGatewayStateDirectory();
	std::error_code ec;
	std::filesystem::create_directories(stateRoot / "chat-transcripts", ec);
	REQUIRE_FALSE(ec);
	std::filesystem::create_directories(stateRoot / "agents" / "ops", ec);
	REQUIRE_FALSE(ec);

	{
		std::ofstream transcript(stateRoot / "chat-transcripts" / "agent_ops_main.jsonl", std::ios::out | std::ios::trunc);
		REQUIRE(transcript.is_open());
		transcript
			<< "{\"messageId\":\"m1\",\"sessionKey\":\"agent:ops:main\",\"role\":\"assistant\",\"text\":\"latest\","
			<< "\"timestamp\":1735689600200}"
			<< "\n";
	}
	{
		std::ofstream defaultStore(stateRoot / "sessions.state", std::ios::out | std::ios::trunc);
		REQUIRE(defaultStore.is_open());
		defaultStore << "agent:ops:main|default|0\n";
	}
	{
		std::ofstream agentStore(stateRoot / "agents" / "ops" / "sessions.state", std::ios::out | std::ios::trunc);
		REQUIRE(agentStore.is_open());
		agentStore << "agent:ops:main|thread|1\n";
	}

	const auto resolved = Route(
		host,
		"p3-session-resolve-multistore",
		"gateway.sessions.resolve",
		R"({"sessionId":"agent:ops:main"})");
	REQUIRE(resolved.ok);
	REQUIRE(resolved.payloadJson.has_value());
	CHECK(resolved.payloadJson.value().find("\"id\":\"agent:ops:main\"") != std::string::npos);
	CHECK(resolved.payloadJson.value().find("\"scope\":\"thread\"") != std::string::npos);
	CHECK(resolved.payloadJson.value().find("\"active\":true") != std::string::npos);

	const auto aliasResolved = Route(
		host,
		"p3-session-get-multistore",
		"sessions.get",
		R"({"sessionId":"agent:ops:main"})");
	REQUIRE(aliasResolved.ok);
	REQUIRE(aliasResolved.payloadJson.has_value());
	CHECK(aliasResolved.payloadJson.value().find("\"scope\":\"thread\"") != std::string::npos);
	CHECK(aliasResolved.payloadJson.value().find("\"active\":true") != std::string::npos);
}

TEST_CASE("P3 parity methods: transcript model identity overrides runtime defaults", "[gateway][parity][p3][sessions][model-precedence]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const std::filesystem::path stateRoot = blazeclaw::gateway::ResolveGatewayStateDirectory();
	std::error_code ec;
	std::filesystem::create_directories(stateRoot / "chat-transcripts", ec);
	REQUIRE_FALSE(ec);
	{
		std::ofstream transcript(stateRoot / "chat-transcripts" / "agent_ops_main.jsonl", std::ios::out | std::ios::trunc);
		REQUIRE(transcript.is_open());
		transcript
			<< "{\"messageId\":\"m1\",\"sessionKey\":\"agent:ops:main\",\"role\":\"assistant\","
			<< "\"message\":{\"id\":\"m1\",\"role\":\"assistant\",\"text\":\"result\","
			<< "\"timestamp\":1735689600200,\"model\":\"deepseek/deepseek-reasoner\",\"modelProvider\":\"deepseek\"}}"
			<< "\n";
	}
	{
		std::ofstream catalog(stateRoot / "model-catalog.state", std::ios::out | std::ios::trunc);
		REQUIRE(catalog.is_open());
		catalog
			<< "{\"provider\":\"deepseek\",\"model\":\"deepseek/deepseek-reasoner\","
			<< "\"inputCostPer1k\":0.001,\"outputCostPer1k\":0.003,\"contextTokens\":77777,\"default\":false}"
			<< "\n";
	}
	{
		std::ofstream store(stateRoot / "sessions.state", std::ios::out | std::ios::trunc);
		REQUIRE(store.is_open());
		store << "agent:ops:main|thread|1\n";
	}

	const auto preview = Route(
		host,
		"p3-session-model-preview",
		"gateway.sessions.preview",
		R"({"sessionId":"agent:ops:main"})");
	REQUIRE(preview.ok);
	REQUIRE(preview.payloadJson.has_value());
	CHECK(preview.payloadJson.value().find("\"modelProvider\":\"deepseek\"") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"model\":\"deepseek/deepseek-reasoner\"") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"contextTokens\":77777") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"modelSource\":\"transcript-runtime\"") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"catalogSource\":\"runtime-state\"") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"estimatedCostUsd\":") != std::string::npos);

	const auto list = Route(
		host,
		"p3-session-model-list",
		"gateway.session.list",
		R"({"search":"agent:ops:main","includeDerivedTitles":true})");
	REQUIRE(list.ok);
	REQUIRE(list.payloadJson.has_value());
	CHECK(list.payloadJson.value().find("\"modelProvider\":\"deepseek\"") != std::string::npos);
	CHECK(list.payloadJson.value().find("\"model\":\"deepseek/deepseek-reasoner\"") != std::string::npos);
	CHECK(list.payloadJson.value().find("\"contextTokens\":77777") != std::string::npos);
}

TEST_CASE("P3 parity methods: external catalog cache and transcript metadata overrides are applied", "[gateway][parity][p3][sessions][catalog-external][overrides]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const std::filesystem::path stateRoot = blazeclaw::gateway::ResolveGatewayStateDirectory();
	std::error_code ec;
	std::filesystem::create_directories(stateRoot / "chat-transcripts", ec);
	REQUIRE_FALSE(ec);

	const std::filesystem::path externalCache = stateRoot / "catalog-external-test.state";
	const std::filesystem::path overridePath = stateRoot / "catalog-overrides-test.state";
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_EXTERNAL_CACHE_PATH", externalCache.string().c_str()) == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_OVERRIDES_PATH", overridePath.string().c_str()) == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_URL", "http://127.0.0.1:9/unreachable") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_TOKEN", "test-token") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_TIMEOUT_MS", "250") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_RETRY_COUNT", "1") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_RETRY_DELAY_MS", "1") == 0);

	{
		std::ofstream external(externalCache, std::ios::out | std::ios::trunc);
		REQUIRE(external.is_open());
		external
			<< "{\"entries\":[{\"provider\":\"seed\",\"model\":\"seed/default\","
			<< "\"inputCostPer1k\":0.002,\"outputCostPer1k\":0.004,\"contextTokens\":22222,\"default\":false}]}"
			<< "\n"; // wrapped JSON shape (not JSONL) to verify normalization
	}
	{
		std::ofstream overrides(overridePath, std::ios::out | std::ios::trunc);
		REQUIRE(overrides.is_open());
		overrides
			<< "{\"provider\":\"seed\",\"model\":\"seed/default\","
			<< "\"inputCostPer1k\":0.003,\"outputCostPer1k\":0.005,\"contextTokens\":33333,\"default\":false}"
			<< "\n";
	}
	{
		std::ofstream transcript(stateRoot / "chat-transcripts" / "agent_ops_main.jsonl", std::ios::out | std::ios::trunc);
		REQUIRE(transcript.is_open());
		transcript
			<< "{\"messageId\":\"m1\",\"sessionKey\":\"agent:ops:main\",\"role\":\"assistant\","
			<< "\"text\":\"result\",\"timestamp\":1735689600200,"
			<< "\"modelOverride\":\"seed/default\",\"providerOverride\":\"seed\","
			<< "\"contextTokensOverride\":44444,\"inputCostPer1kOverride\":0.006,\"outputCostPer1kOverride\":0.007}"
			<< "\n";
	}
	{
		std::ofstream store(stateRoot / "sessions.state", std::ios::out | std::ios::trunc);
		REQUIRE(store.is_open());
		store << "agent:ops:main|thread|1\n";
	}

	const auto preview = Route(
		host,
		"p3-session-model-preview-override",
		"gateway.sessions.preview",
		R"({"sessionId":"agent:ops:main"})");
	REQUIRE(preview.ok);
	REQUIRE(preview.payloadJson.has_value());
	CHECK(preview.payloadJson.value().find("\"modelSource\":\"transcript-override\"") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"catalogSource\":\"transcript-override\"") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"modelOverrideApplied\":true") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"pricingOverrideApplied\":true") != std::string::npos);
	CHECK(preview.payloadJson.value().find("\"contextTokens\":44444") != std::string::npos);

	const auto usage = Route(
		host,
		"p3-session-model-usage-override",
		"gateway.sessions.usage",
		R"({"sessionId":"agent:ops:main","openClawEnvelope":false})");
	REQUIRE(usage.ok);
	REQUIRE(usage.payloadJson.has_value());
	CHECK(usage.payloadJson.value().find("\"catalogSource\":\"transcript-override\"") != std::string::npos);
	CHECK(usage.payloadJson.value().find("\"pricingOverrideApplied\":true") != std::string::npos);

	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_EXTERNAL_CACHE_PATH", "") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_OVERRIDES_PATH", "") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_URL", "") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_TOKEN", "") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_TIMEOUT_MS", "") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_RETRY_COUNT", "") == 0);
	REQUIRE(_putenv_s("BLAZECLAW_MODEL_CATALOG_SERVICE_RETRY_DELAY_MS", "") == 0);
}
