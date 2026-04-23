#include <catch2/catch_all.hpp>

#include "gateway/GatewayHost.h"

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
