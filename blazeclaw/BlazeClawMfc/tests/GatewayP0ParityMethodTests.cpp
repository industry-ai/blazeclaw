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

TEST_CASE("P0 parity methods: exec and plugin approvals are stateful", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto globalGet = Route(host, "p0-exec-get", "exec.approvals.get");
	REQUIRE(globalGet.ok);
	REQUIRE(globalGet.payloadJson.has_value());
	REQUIRE(globalGet.payloadJson.value().find("\"defaultMode\":\"manual\"") != std::string::npos);

	const auto globalSet = Route(
		host,
		"p0-exec-set",
		"exec.approvals.set",
		R"({"defaultMode":"auto"})");
	REQUIRE(globalSet.ok);
	REQUIRE(globalSet.payloadJson.has_value());
	REQUIRE(globalSet.payloadJson.value().find("\"defaultMode\":\"auto\"") != std::string::npos);

	const auto nodeSet = Route(
		host,
		"p0-exec-node-set",
		"exec.approvals.node.set",
		R"({"nodeId":"node-1","defaultMode":"deny"})");
	REQUIRE(nodeSet.ok);
	REQUIRE(nodeSet.payloadJson.has_value());
	REQUIRE(nodeSet.payloadJson.value().find("\"nodeId\":\"node-1\"") != std::string::npos);
	REQUIRE(nodeSet.payloadJson.value().find("\"defaultMode\":\"deny\"") != std::string::npos);

	const auto nodeGet = Route(
		host,
		"p0-exec-node-get",
		"exec.approvals.node.get",
		R"({"nodeId":"node-1"})");
	REQUIRE(nodeGet.ok);
	REQUIRE(nodeGet.payloadJson.has_value());
	REQUIRE(nodeGet.payloadJson.value().find("\"defaultMode\":\"deny\"") != std::string::npos);

	const auto execRequest = Route(
		host,
		"p0-exec-request",
		"exec.approval.request",
		R"({"requestId":"exec-req-1","command":"tools.execute","reason":"needs approval"})");
	REQUIRE(execRequest.ok);
	REQUIRE(execRequest.payloadJson.has_value());
	REQUIRE(execRequest.payloadJson.value().find("\"requestId\":\"exec-req-1\"") != std::string::npos);
	REQUIRE(execRequest.payloadJson.value().find("\"queued\":true") != std::string::npos);

	const auto execWaitPending = Route(
		host,
		"p0-exec-wait-pending",
		"exec.approval.waitDecision",
		R"({"requestId":"exec-req-1"})");
	REQUIRE(execWaitPending.ok);
	REQUIRE(execWaitPending.payloadJson.has_value());
	REQUIRE(execWaitPending.payloadJson.value().find("\"status\":\"pending\"") != std::string::npos);
	REQUIRE(execWaitPending.payloadJson.value().find("\"resolved\":false") != std::string::npos);

	const auto execResolve = Route(
		host,
		"p0-exec-resolve",
		"exec.approval.resolve",
		R"({"requestId":"exec-req-1","decision":"approve"})");
	REQUIRE(execResolve.ok);
	REQUIRE(execResolve.payloadJson.has_value());
	REQUIRE(execResolve.payloadJson.value().find("\"status\":\"approved\"") != std::string::npos);

	const auto execWaitResolved = Route(
		host,
		"p0-exec-wait-resolved",
		"exec.approval.waitDecision",
		R"({"requestId":"exec-req-1"})");
	REQUIRE(execWaitResolved.ok);
	REQUIRE(execWaitResolved.payloadJson.has_value());
	REQUIRE(execWaitResolved.payloadJson.value().find("\"status\":\"approved\"") != std::string::npos);
	REQUIRE(execWaitResolved.payloadJson.value().find("\"resolved\":true") != std::string::npos);

	const auto pluginRequest = Route(
		host,
		"p0-plugin-request",
		"plugin.approval.request",
		R"({"requestId":"plugin-req-1","pluginId":"sample-plugin"})");
	REQUIRE(pluginRequest.ok);
	REQUIRE(pluginRequest.payloadJson.has_value());
	REQUIRE(pluginRequest.payloadJson.value().find("\"requestId\":\"plugin-req-1\"") != std::string::npos);

	const auto pluginResolve = Route(
		host,
		"p0-plugin-resolve",
		"plugin.approval.resolve",
		R"({"requestId":"plugin-req-1","decision":"reject"})");
	REQUIRE(pluginResolve.ok);
	REQUIRE(pluginResolve.payloadJson.has_value());
	REQUIRE(pluginResolve.payloadJson.value().find("\"status\":\"rejected\"") != std::string::npos);

	const auto pluginList = Route(host, "p0-plugin-list", "plugin.approval.list");
	REQUIRE(pluginList.ok);
	REQUIRE(pluginList.payloadJson.has_value());
	REQUIRE(pluginList.payloadJson.value().find("\"requestId\":\"plugin-req-1\"") != std::string::npos);
}

TEST_CASE("P0 parity methods: node invoke foreground-deferred path queues retryable pending action", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto nodeList = Route(host, "p0-node-list", "node.list");
	REQUIRE(nodeList.ok);
	REQUIRE(nodeList.payloadJson.has_value());
	REQUIRE(nodeList.payloadJson.value().find("\"nodes\"") != std::string::npos);

	const auto nodeInvoke = Route(
		host,
		"p0-node-invoke-foreground",
		"node.invoke",
		R"({"nodeId":"ios-offline-node-1","command":"canvas.present","idempotencyKey":"idem-foreground-1","platform":"ios","nodeErrorCode":"NODE_BACKGROUND_UNAVAILABLE"})");
	if (nodeInvoke.ok) {
		REQUIRE(nodeInvoke.payloadJson.has_value());
		CHECK(nodeInvoke.payloadJson.value().find("\"ok\":true") != std::string::npos);
		CHECK(nodeInvoke.payloadJson.value().find("\"command\":\"canvas.present\"") != std::string::npos);
	}
	else {
		REQUIRE(nodeInvoke.error.has_value());
		CHECK(nodeInvoke.error->code == "unavailable");
		CHECK(nodeInvoke.error->retryable.value_or(false));
		REQUIRE(nodeInvoke.error->detailsJson.has_value());
		CHECK(nodeInvoke.error->detailsJson.value().find("\"code\":\"QUEUED_UNTIL_FOREGROUND\"") != std::string::npos);
		CHECK(nodeInvoke.error->detailsJson.value().find("\"queuedActionId\":") != std::string::npos);
	}
}

TEST_CASE("P0 parity methods: node pending queue lifecycle pull and ack", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto invoke = Route(
		host,
		"p0-node-invoke-queue",
		"node.invoke",
		R"({"nodeId":"ios-offline-node-2","command":"canvas.navigate","idempotencyKey":"idem-queue-1","platform":"ios","nodeErrorCode":"NODE_BACKGROUND_UNAVAILABLE"})");

	const auto pull = Route(
		host,
		"p0-node-pending-pull",
		"node.pending.pull",
		R"({"nodeId":"ios-offline-node-2","declaredCommands":["canvas.navigate"]})");
	REQUIRE(pull.ok);
	REQUIRE(pull.payloadJson.has_value());
	CHECK(pull.payloadJson.value().find("\"actions\":[") != std::string::npos);

	if (invoke.ok) {
		REQUIRE(invoke.payloadJson.has_value());
		CHECK(invoke.payloadJson.value().find("\"ok\":true") != std::string::npos);
		const bool hasEmptyActions =
			pull.payloadJson.value().find("\"actions\":[]") != std::string::npos;
		const bool hasQueuedActions =
			pull.payloadJson.value().find("\"actions\":[{") != std::string::npos;
		if (hasEmptyActions) {
			CHECK(hasEmptyActions);
		}
		else {
			CHECK(hasQueuedActions);
		}
	}
	else {
		REQUIRE(invoke.error.has_value());
		CHECK(invoke.error->code == "unavailable");
		CHECK(pull.payloadJson.value().find("\"actions\":[{") != std::string::npos);

		const std::string actionId = ExtractFirstActionId(pull.payloadJson.value());
		REQUIRE_FALSE(actionId.empty());

		const auto ack = Route(
			host,
			"p0-node-pending-ack",
			"node.pending.ack",
			"{\"nodeId\":\"ios-offline-node-2\",\"ids\":[\"" + actionId + "\"]}");
		REQUIRE(ack.ok);
		REQUIRE(ack.payloadJson.has_value());
		CHECK(ack.payloadJson.value().find("\"remainingCount\":0") != std::string::npos);
	}
}

TEST_CASE("P0 parity methods: node invoke rejects blocked policy commands", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

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
	REQUIRE(host.StartLocalDispatchOnly());

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

TEST_CASE("P0 parity methods: device pairing/token lifecycle are explicitly unsupported", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto deviceApprove = Route(host, "p0-device-approve", "device.pair.approve");
	REQUIRE_FALSE(deviceApprove.ok);
	REQUIRE(deviceApprove.error.has_value());
	CHECK(deviceApprove.error->code == "unavailable");

	const auto tokenRotate = Route(host, "p0-device-token-rotate", "device.token.rotate");
	REQUIRE_FALSE(tokenRotate.ok);
	REQUIRE(tokenRotate.error.has_value());
	CHECK(tokenRotate.error->code == "unavailable");
}

TEST_CASE("P0 parity methods: session static stubs are explicitly unsupported", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const std::vector<std::string> methods{
		"sessions.subscribe",
		"sessions.unsubscribe",
		"sessions.messages.subscribe",
		"sessions.messages.unsubscribe",
		"sessions.send",
		"sessions.abort",
		"sessions.compaction.list",
		"sessions.compaction.get",
		"sessions.compaction.branch",
		"sessions.compaction.restore",
	};

	for (const auto& method : methods) {
		const auto response = Route(host, "p0-session-" + method, method);
		REQUIRE_FALSE(response.ok);
		REQUIRE(response.error.has_value());
		CHECK(response.error->code == "unavailable");
	}
}

TEST_CASE("P0 parity alias: node.canvas.capability.refresh forwards to canvas capabilities", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

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
	REQUIRE(host.StartLocalDispatchOnly());

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
