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

TEST_CASE("P2 parity methods: classic agent aliases are routable", "[gateway][parity][p2]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

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
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto heartbeat = Route(host, "p2-heartbeat", "last-heartbeat");
	REQUIRE(heartbeat.ok);
	REQUIRE(heartbeat.payloadJson.has_value());
	REQUIRE(heartbeat.payloadJson.value().find("\"lastHeartbeatMs\"") != std::string::npos);

	const auto presence = Route(host, "p2-presence", "system-presence");
	REQUIRE(presence.ok);
	REQUIRE(presence.payloadJson.has_value());
	REQUIRE(presence.payloadJson.value().find("\"present\":true") != std::string::npos);

	const auto identity = Route(host, "p2-gateway-identity", "gateway.identity.get");
	REQUIRE(identity.ok);
	REQUIRE(identity.payloadJson.has_value());
	REQUIRE(identity.payloadJson.value().find("\"blazeclaw.gateway\"") != std::string::npos);
}

TEST_CASE("P2 parity methods: commands and tool-effective aliases are routable", "[gateway][parity][p2]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto commandsList = Route(host, "p2-commands-list", "commands.list");
	REQUIRE(commandsList.ok);
	REQUIRE(commandsList.payloadJson.has_value());
	REQUIRE(commandsList.payloadJson.value().find("\"tools\"") != std::string::npos);

	const auto toolsEffective = Route(host, "p2-tools-effective", "tools.effective");
	REQUIRE(toolsEffective.ok);
	REQUIRE(toolsEffective.payloadJson.has_value());
	REQUIRE(toolsEffective.payloadJson.value().find("\"tools\"") != std::string::npos);
}

TEST_CASE("P2 parity methods: skills, web login, update, and doctor memory methods are routable", "[gateway][parity][p2]")
{
	GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());

	const auto skillsSearch = Route(host, "p2-skills-search", "skills.search");
	REQUIRE(skillsSearch.ok);
	REQUIRE(skillsSearch.payloadJson.has_value());
	REQUIRE(skillsSearch.payloadJson.value().find("\"skills\"") != std::string::npos);

	const auto webLoginStart = Route(host, "p2-web-login-start", "web.login.start");
	REQUIRE(webLoginStart.ok);
	REQUIRE(webLoginStart.payloadJson.has_value());
	REQUIRE(webLoginStart.payloadJson.value().find("\"status\":\"not_supported\"") != std::string::npos);

	const auto updateRun = Route(host, "p2-update-run", "update.run");
	REQUIRE(updateRun.ok);
	REQUIRE(updateRun.payloadJson.has_value());
	REQUIRE(updateRun.payloadJson.value().find("\"status\":\"not_supported\"") != std::string::npos);

	const auto doctorMemoryStatus = Route(host, "p2-doctor-memory-status", "doctor.memory.status");
	REQUIRE(doctorMemoryStatus.ok);
	REQUIRE(doctorMemoryStatus.payloadJson.has_value());
	REQUIRE(doctorMemoryStatus.payloadJson.value().find("\"status\":\"healthy\"") != std::string::npos);
}
