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

TEST_CASE("P0 parity methods: node lifecycle methods are routable", "[gateway][parity][p0]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto nodeList = Route(host, "p0-node-list", "node.list");
	REQUIRE(nodeList.ok);
	REQUIRE(nodeList.payloadJson.has_value());
	REQUIRE(nodeList.payloadJson.value().find("\"nodes\"") != std::string::npos);

	const auto nodeInvoke = Route(host, "p0-node-invoke", "node.invoke");
	REQUIRE(nodeInvoke.ok);
	REQUIRE(nodeInvoke.payloadJson.has_value());
	REQUIRE(nodeInvoke.payloadJson.value().find("\"queued\":true") != std::string::npos);
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

	const auto response = Route(host, "p0-node-canvas-refresh", "node.canvas.capability.refresh");
	REQUIRE(response.ok);
	REQUIRE(response.payloadJson.has_value());
	REQUIRE(response.payloadJson.value().find("\"host\":\"a2ui\"") != std::string::npos);
}
