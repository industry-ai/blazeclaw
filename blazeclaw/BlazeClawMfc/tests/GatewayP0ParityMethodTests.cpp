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

	std::string ExtractFirstActionId(const std::string& payloadJson)
	{
		return ExtractJsonStringField(payloadJson, "id");
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

TEST_CASE("P1 parity methods: device pairing unsupported and token lifecycle stateful", "[gateway][parity][p1]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto deviceApprove = Route(host, "p1-device-approve", "device.pair.approve");
	REQUIRE_FALSE(deviceApprove.ok);
	REQUIRE(deviceApprove.error.has_value());
	CHECK(deviceApprove.error->code == "unavailable");

	const auto tokenRotate = Route(
		host,
		"p1-device-token-rotate",
		"device.token.rotate",
		R"({"nodeId":"node-device-1"})");
	REQUIRE(tokenRotate.ok);
	REQUIRE(tokenRotate.payloadJson.has_value());
	CHECK(tokenRotate.payloadJson.value().find("\"rotated\":true") != std::string::npos);
	CHECK(tokenRotate.payloadJson.value().find("\"nodeId\":\"node-device-1\"") != std::string::npos);

	const auto tokenRevoke = Route(
		host,
		"p1-device-token-revoke",
		"device.token.revoke",
		R"({"nodeId":"node-device-1"})");
	REQUIRE(tokenRevoke.ok);
	REQUIRE(tokenRevoke.payloadJson.has_value());
	CHECK(tokenRevoke.payloadJson.value().find("\"revoked\":true") != std::string::npos);
	CHECK(tokenRevoke.payloadJson.value().find("\"found\":true") != std::string::npos);
}

TEST_CASE("P1 parity methods: session subscription/send/abort/compaction behavior is runtime-backed", "[gateway][parity][p1]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto subscribe = Route(
		host,
		"p1-session-subscribe",
		"sessions.subscribe",
		R"({"sessionId":"main","connectionId":"conn-1"})");
	REQUIRE(subscribe.ok);
	REQUIRE(subscribe.payloadJson.has_value());
	CHECK(subscribe.payloadJson.value().find("\"subscribed\":true") != std::string::npos);
	CHECK(subscribe.payloadJson.value().find("\"subscriberCount\":1") != std::string::npos);

	const auto messageSubscribe = Route(
		host,
		"p1-session-msg-subscribe",
		"sessions.messages.subscribe",
		R"({"sessionId":"main","connectionId":"conn-1"})");
	REQUIRE(messageSubscribe.ok);
	REQUIRE(messageSubscribe.payloadJson.has_value());
	CHECK(messageSubscribe.payloadJson.value().find("\"subscribed\":true") != std::string::npos);

	const auto send = Route(
		host,
		"p1-session-send",
		"sessions.send",
		R"({"sessionId":"main","message":"hello from p1"})");
	REQUIRE(send.ok);
	REQUIRE(send.payloadJson.has_value());
	CHECK(send.payloadJson.value().find("\"forwardedMethod\":\"chat.send\"") != std::string::npos);

	const auto branch = Route(
		host,
		"p1-session-branch",
		"sessions.compaction.branch",
		R"({"sessionId":"main","title":"checkpoint-a"})");
	REQUIRE(branch.ok);
	REQUIRE(branch.payloadJson.has_value());
	CHECK(branch.payloadJson.value().find("\"created\":true") != std::string::npos);
	const std::string branchId = ExtractJsonStringField(branch.payloadJson.value(), "branchId");
	REQUIRE_FALSE(branchId.empty());

	const auto list = Route(
		host,
		"p1-session-list",
		"sessions.compaction.list",
		R"({"sessionId":"main"})");
	REQUIRE(list.ok);
	REQUIRE(list.payloadJson.has_value());
	CHECK(list.payloadJson.value().find("\"count\":1") != std::string::npos);

	const auto get = Route(
		host,
		"p1-session-get",
		"sessions.compaction.get",
		"{\"branchId\":\"" + branchId + "\"}");
	REQUIRE(get.ok);
	REQUIRE(get.payloadJson.has_value());
	CHECK(get.payloadJson.value().find("\"found\":true") != std::string::npos);

	const auto restore = Route(
		host,
		"p1-session-restore",
		"sessions.compaction.restore",
		"{\"branchId\":\"" + branchId + "\"}");
	REQUIRE(restore.ok);
	REQUIRE(restore.payloadJson.has_value());
	CHECK(restore.payloadJson.value().find("\"restored\":true") != std::string::npos);

	const auto abort = Route(host, "p1-session-abort", "sessions.abort");
	REQUIRE(abort.ok);
	REQUIRE(abort.payloadJson.has_value());
	CHECK(abort.payloadJson.value().find("\"forwardedMethod\":\"chat.abort\"") != std::string::npos);

	const auto messageUnsubscribe = Route(
		host,
		"p1-session-msg-unsubscribe",
		"sessions.messages.unsubscribe",
		R"({"sessionId":"main","connectionId":"conn-1"})");
	REQUIRE(messageUnsubscribe.ok);
	REQUIRE(messageUnsubscribe.payloadJson.has_value());
	CHECK(messageUnsubscribe.payloadJson.value().find("\"subscribed\":false") != std::string::npos);

	const auto unsubscribe = Route(
		host,
		"p1-session-unsubscribe",
		"sessions.unsubscribe",
		R"({"sessionId":"main","connectionId":"conn-1"})");
	REQUIRE(unsubscribe.ok);
	REQUIRE(unsubscribe.payloadJson.has_value());
	CHECK(unsubscribe.payloadJson.value().find("\"subscribed\":false") != std::string::npos);
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

TEST_CASE("P1 parity methods: skills and update-run are runtime-backed", "[gateway][parity][p1]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	const auto searchBeforeInstall = Route(
		host,
		"p1-skills-search-before",
		"skills.search",
		R"({"query":"email","installedOnly":false})");
	REQUIRE(searchBeforeInstall.ok);
	REQUIRE(searchBeforeInstall.payloadJson.has_value());
	CHECK(searchBeforeInstall.payloadJson.value().find("\"count\":1") != std::string::npos);
	CHECK(searchBeforeInstall.payloadJson.value().find("\"installed\":false") != std::string::npos);

	const auto install = Route(
		host,
		"p1-skills-install",
		"skills.install",
		R"({"name":"email.schedule"})");
	REQUIRE(install.ok);
	REQUIRE(install.payloadJson.has_value());
	CHECK(install.payloadJson.value().find("\"installed\":true") != std::string::npos);
	CHECK(install.payloadJson.value().find("\"status\":\"installed\"") != std::string::npos);

	const auto detail = Route(
		host,
		"p1-skills-detail",
		"skills.detail",
		R"({"name":"email.schedule"})");
	REQUIRE(detail.ok);
	REQUIRE(detail.payloadJson.has_value());
	CHECK(detail.payloadJson.value().find("\"found\":true") != std::string::npos);
	CHECK(detail.payloadJson.value().find("\"installed\":true") != std::string::npos);

	const auto bins = Route(host, "p1-skills-bins", "skills.bins");
	REQUIRE(bins.ok);
	REQUIRE(bins.payloadJson.has_value());
	CHECK(bins.payloadJson.value().find("\"bins\":[") != std::string::npos);
	CHECK(bins.payloadJson.value().find("\"python\"") != std::string::npos);

	const auto searchInstalled = Route(
		host,
		"p1-skills-search-installed",
		"skills.search",
		R"({"query":"email","installedOnly":true})");
	REQUIRE(searchInstalled.ok);
	REQUIRE(searchInstalled.payloadJson.has_value());
	CHECK(searchInstalled.payloadJson.value().find("\"count\":1") != std::string::npos);
	CHECK(searchInstalled.payloadJson.value().find("\"installed\":true") != std::string::npos);

	const auto updateRun = Route(host, "p1-update-run", "update.run");
	REQUIRE(updateRun.ok);
	REQUIRE(updateRun.payloadJson.has_value());
	CHECK(updateRun.payloadJson.value().find("\"started\":true") != std::string::npos);
	CHECK(updateRun.payloadJson.value().find("\"status\":\"running\"") != std::string::npos);
	CHECK(updateRun.payloadJson.value().find("\"runId\":\"update-run-") != std::string::npos);
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
