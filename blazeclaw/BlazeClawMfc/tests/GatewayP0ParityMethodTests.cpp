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

	std::string ExtractFirstActionId(const std::string& payloadJson)
	{
		const std::string marker = "\"id\":\"";
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

TEST_CASE("P0 parity methods: exec and plugin approvals return contract payloads", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto execGet = Route(host, "p0-exec-get", "exec.approvals.get");
	REQUIRE(execGet.ok);
	REQUIRE(execGet.payloadJson.has_value());
	REQUIRE(execGet.payloadJson.value().find("\"scope\":\"global\"") != std::string::npos);

	const auto pluginRequest = Route(host, "p0-plugin-request", "plugin.approval.request");
	REQUIRE(pluginRequest.ok);
	REQUIRE(pluginRequest.payloadJson.has_value());
	REQUIRE(pluginRequest.payloadJson.value().find("\"queued\":true") != std::string::npos);
}

TEST_CASE("P0 parity methods: node invoke foreground-deferred path queues retryable pending action", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto nodeList = Route(host, "p0-node-list", "node.list");
	REQUIRE(nodeList.ok);
	REQUIRE(nodeList.payloadJson.has_value());
	REQUIRE(nodeList.payloadJson.value().find("\"nodes\"") != std::string::npos);

	const auto nodeInvoke = Route(
		host,
		"p0-node-invoke-foreground",
		"node.invoke",
		R"({"nodeId":"ios-node-1","command":"canvas.present","idempotencyKey":"idem-foreground-1","platform":"ios","nodeErrorCode":"NODE_BACKGROUND_UNAVAILABLE"})");
	REQUIRE_FALSE(nodeInvoke.ok);
	REQUIRE(nodeInvoke.error.has_value());
	CHECK(nodeInvoke.error->code == "unavailable");
	CHECK(nodeInvoke.error->retryable.value_or(false));
	REQUIRE(nodeInvoke.error->detailsJson.has_value());
	CHECK(nodeInvoke.error->detailsJson.value().find("\"code\":\"QUEUED_UNTIL_FOREGROUND\"") != std::string::npos);
	CHECK(nodeInvoke.error->detailsJson.value().find("\"queuedActionId\":") != std::string::npos);
}

TEST_CASE("P0 parity methods: node pending queue lifecycle pull and ack", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto invoke = Route(
		host,
		"p0-node-invoke-queue",
		"node.invoke",
		R"({"nodeId":"ios-node-2","command":"canvas.navigate","idempotencyKey":"idem-queue-1","platform":"ios","nodeErrorCode":"NODE_BACKGROUND_UNAVAILABLE"})");
	REQUIRE_FALSE(invoke.ok);
	REQUIRE(invoke.error.has_value());
	CHECK(invoke.error->code == "unavailable");

	const auto pull = Route(
		host,
		"p0-node-pending-pull",
		"node.pending.pull",
		R"({"nodeId":"ios-node-2","declaredCommands":["canvas.navigate"]})");
	REQUIRE(pull.ok);
	REQUIRE(pull.payloadJson.has_value());
	CHECK(pull.payloadJson.value().find("\"actions\":[{") != std::string::npos);

	const std::string actionId = ExtractFirstActionId(pull.payloadJson.value());
	REQUIRE_FALSE(actionId.empty());

	const auto ack = Route(
		host,
		"p0-node-pending-ack",
		"node.pending.ack",
		"{\"nodeId\":\"ios-node-2\",\"ids\":[\"" + actionId + "\"]}");
	REQUIRE(ack.ok);
	REQUIRE(ack.payloadJson.has_value());
	CHECK(ack.payloadJson.value().find("\"remainingCount\":0") != std::string::npos);
}

TEST_CASE("P0 parity methods: node invoke rejects blocked policy commands", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto blockedApprovals = Route(
		host,
		"p0-node-invoke-policy-approvals",
		"node.invoke",
		R"({"nodeId":"node-policy-1","command":"system.execApprovals.get","idempotencyKey":"idem-policy-1"})");
	REQUIRE_FALSE(blockedApprovals.ok);
	REQUIRE(blockedApprovals.error.has_value());
	CHECK(blockedApprovals.error->code == "invalid_request");
	CHECK(blockedApprovals.error->message.find("system.execApprovals") != std::string::npos);

	const auto blockedBrowserProxy = Route(
		host,
		"p0-node-invoke-policy-browser-proxy",
		"node.invoke",
		R"({"nodeId":"node-policy-2","command":"browser.proxy","idempotencyKey":"idem-policy-2","method":"POST","path":"/profiles/create"})");
	REQUIRE_FALSE(blockedBrowserProxy.ok);
	REQUIRE(blockedBrowserProxy.error.has_value());
	CHECK(blockedBrowserProxy.error->code == "invalid_request");
	CHECK(blockedBrowserProxy.error->message.find("browser.proxy") != std::string::npos);
}

TEST_CASE("P0 parity methods: node event and invoke.result runtime handlers accept normalized payloads", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto nodeInvokeResult = Route(
		host,
		"p0-node-invoke-result",
		"node.invoke.result",
		R"({"runId":"run-1","nodeId":"node-evt-1","payload":{"status":"ok"}})");
	REQUIRE(nodeInvokeResult.ok);
	REQUIRE(nodeInvokeResult.payloadJson.has_value());
	CHECK(nodeInvokeResult.payloadJson.value().find("\"accepted\":true") != std::string::npos);

	const auto nodeEvent = Route(
		host,
		"p0-node-event",
		"node.event",
		R"({"event":"node.status","nodeId":"node-evt-1","payload":{"connected":true}})");
	REQUIRE(nodeEvent.ok);
	REQUIRE(nodeEvent.payloadJson.has_value());
	CHECK(nodeEvent.payloadJson.value().find("\"accepted\":true") != std::string::npos);
}

TEST_CASE("P0 parity methods: device pairing and token lifecycle methods are routable", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto deviceApprove = Route(host, "p0-device-approve", "device.pair.approve");
	REQUIRE(deviceApprove.ok);
	REQUIRE(deviceApprove.payloadJson.has_value());
	REQUIRE(deviceApprove.payloadJson.value().find("\"approved\":true") != std::string::npos);

	const auto tokenRotate = Route(host, "p0-device-token-rotate", "device.token.rotate");
	REQUIRE(tokenRotate.ok);
	REQUIRE(tokenRotate.payloadJson.has_value());
	REQUIRE(tokenRotate.payloadJson.value().find("\"rotated\":true") != std::string::npos);
}

TEST_CASE("P0 parity alias: node.canvas.capability.refresh forwards to canvas capabilities", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto response = Route(
		host,
		"p0-node-canvas-refresh",
		"node.canvas.capability.refresh",
		R"({"sessionKey":"main","canvasHostUrl":"https://canvas.local"})");
	REQUIRE(response.ok);
	REQUIRE(response.payloadJson.has_value());
	REQUIRE(response.payloadJson.value().find("\"canvasCapability\":") != std::string::npos);
}

TEST_CASE("P2 parity methods: doctor memory method family is routable", "[gateway][parity][p2]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto status = Route(host, "p2-doctor-status", "doctor.memory.status");
	REQUIRE(status.ok);
	REQUIRE(status.payloadJson.has_value());
	REQUIRE(status.payloadJson.value().find("\"status\":\"healthy\"") != std::string::npos);

	const auto diary = Route(host, "p2-doctor-diary", "doctor.memory.dreamDiary");
	REQUIRE(diary.ok);
	REQUIRE(diary.payloadJson.has_value());
	REQUIRE(diary.payloadJson.value().find("\"entries\"") != std::string::npos);

	const auto backfill = Route(host, "p2-doctor-backfill", "doctor.memory.backfillDreamDiary");
	REQUIRE(backfill.ok);
	REQUIRE(backfill.payloadJson.has_value());
	REQUIRE(backfill.payloadJson.value().find("\"queued\":true") != std::string::npos);

	const auto resetDiary = Route(host, "p2-doctor-reset-diary", "doctor.memory.resetDreamDiary");
	REQUIRE(resetDiary.ok);
	REQUIRE(resetDiary.payloadJson.has_value());
	REQUIRE(resetDiary.payloadJson.value().find("\"target\":\"dreamDiary\"") != std::string::npos);

	const auto resetGroundedShortTerm = Route(
		host,
		"p2-doctor-reset-grounded-short",
		"doctor.memory.resetGroundedShortTerm");
	REQUIRE(resetGroundedShortTerm.ok);
	REQUIRE(resetGroundedShortTerm.payloadJson.has_value());
	REQUIRE(resetGroundedShortTerm.payloadJson.value().find("\"target\":\"groundedShortTerm\"") != std::string::npos);
}
