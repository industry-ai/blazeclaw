#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace {

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream in(path.string(), std::ios::in | std::ios::binary);
		REQUIRE(in.is_open());
		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

} // namespace

TEST_CASE(
	"S3 parity: server-methods-list event emitters exist for parity-critical families",
	"[gateway][parity][s3][server-methods-list]")
{
	const std::string transportSource = ReadTextFile(
		std::filesystem::path("BlazeClawMfc") / "src" / "gateway" / "GatewayWebSocketTransport.cpp");
	REQUIRE(transportSource.find("connect.challenge") != std::string::npos);

	const std::string transportHandlersSource = ReadTextFile(
		std::filesystem::path("BlazeClawMfc") / "src" / "gateway" / "GatewayHost.Handlers.Transport.cpp");
	REQUIRE(transportHandlersSource.find("\"features\":") != std::string::npos);
	REQUIRE(transportHandlersSource.find("RegisteredMethods()") != std::string::npos);
	REQUIRE(transportHandlersSource.find("GatewayEventCatalogNames()") != std::string::npos);

	const std::string securityOpsSource = ReadTextFile(
		std::filesystem::path("BlazeClawMfc") / "src" / "gateway" / "GatewayHost.Handlers.SecurityOps.cpp");
	REQUIRE(securityOpsSource.find("node.pair.requested") != std::string::npos);
	REQUIRE(securityOpsSource.find("node.pair.resolved") != std::string::npos);
	REQUIRE(securityOpsSource.find("device.pair.resolved") != std::string::npos);
	REQUIRE(securityOpsSource.find("exec.approval.requested") != std::string::npos);
	REQUIRE(securityOpsSource.find("exec.approval.resolved") != std::string::npos);
	REQUIRE(securityOpsSource.find("plugin.approval.requested") != std::string::npos);
	REQUIRE(securityOpsSource.find("plugin.approval.resolved") != std::string::npos);
	REQUIRE(securityOpsSource.find("node.invoke.request") != std::string::npos);
}

TEST_CASE(
	"S3 parity: event diff artifact covers canonical event families with explicit deferred set",
	"[gateway][parity][s3][server-methods-list]")
{
	const auto artifactPath =
		std::filesystem::path("..") /
		"artifacts" /
		"gateway-parity" /
		"openclaw-vs-blazeclaw-event-diff.json";
	const nlohmann::json eventDiff = nlohmann::json::parse(ReadTextFile(artifactPath));

	REQUIRE(eventDiff.contains("summary"));
	REQUIRE(eventDiff.contains("rows"));
	REQUIRE(eventDiff["summary"].value("openclawEventCount", 0) == 24);

	std::unordered_map<std::string, std::string> statusByEvent;
	for (const auto& row : eventDiff["rows"]) {
		const std::string eventName = row.value("event", std::string{});
		if (eventName.empty()) {
			continue;
		}
		statusByEvent[eventName] = row.value("status", std::string{});
	}

	REQUIRE(statusByEvent["connect.challenge"] == "implemented");
	REQUIRE(statusByEvent["node.pair.requested"] == "implemented");
	REQUIRE(statusByEvent["node.pair.resolved"] == "implemented");
	REQUIRE(statusByEvent["device.pair.resolved"] == "implemented");
	REQUIRE(statusByEvent["exec.approval.requested"] == "implemented");
	REQUIRE(statusByEvent["exec.approval.resolved"] == "implemented");
	REQUIRE(statusByEvent["plugin.approval.requested"] == "implemented");
	REQUIRE(statusByEvent["plugin.approval.resolved"] == "implemented");
	REQUIRE(statusByEvent["node.invoke.request"] == "implemented");

	REQUIRE(statusByEvent["session.message"] == "implemented");
	REQUIRE(statusByEvent["session.tool"] == "implemented");
	REQUIRE(statusByEvent["sessions.changed"] == "implemented");
	REQUIRE(statusByEvent["voicewake.changed"] == "implemented");
	REQUIRE(statusByEvent["update.available"] == "implemented");
	REQUIRE(statusByEvent["device.pair.requested"] == "implemented");
}
