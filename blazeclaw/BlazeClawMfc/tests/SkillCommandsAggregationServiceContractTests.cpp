#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream in(path.string());
		REQUIRE(in.is_open());
		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

} // namespace

TEST_CASE(
	"SkillCommandsAggregationService contract: descriptor seam is defined",
	"[skills][command-aggregation][contract]")
{
	const auto headerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"SkillCommandsAggregationService.h";
	const std::string source = ReadTextFile(headerPath);

	REQUIRE(source.find("struct AgentSkillCommandDescriptor") != std::string::npos);
	REQUIRE(source.find("std::optional<std::vector<std::wstring>> skillFilter") !=
		std::string::npos);
	REQUIRE(source.find("class SkillCommandsAggregationService") != std::string::npos);
	REQUIRE(source.find("AgentSkillCommandAggregationContext") != std::string::npos);
}

TEST_CASE(
	"SkillCommandsAggregationService contract: canonical workspace grouping is applied",
	"[skills][command-aggregation][canonical]")
{
	const auto implPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"SkillCommandsAggregationService.cpp";
	const std::string source = ReadTextFile(implPath);

	REQUIRE(source.find("std::filesystem::weakly_canonical") != std::string::npos);
	REQUIRE(source.find("std::map<std::wstring, WorkspaceGroupEntry> groupedWorkspaces") !=
		std::string::npos);
	REQUIRE(source.find("MergeSkillFilters(") != std::string::npos);
	REQUIRE(source.find("if (!existing.has_value() || !incoming.has_value())") !=
		std::string::npos);
	REQUIRE(source.find("if (normalizedExisting.empty())") != std::string::npos);
	REQUIRE(source.find("if (normalizedIncoming.empty())") != std::string::npos);
}

TEST_CASE(
	"ServiceManager contract: aggregation seam is wired in refresh path",
	"[skills][command-aggregation][servicemanager]")
{
	const auto serviceManagerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"ServiceManager.cpp";
	const std::string source = ReadTextFile(serviceManagerPath);

	REQUIRE(source.find("std::vector<AgentSkillCommandDescriptor> commandDescriptors") !=
		std::string::npos);
	REQUIRE(source.find("m_skillCommandsAggregationService.BuildSnapshot") !=
		std::string::npos);
	REQUIRE(source.find("config.agents.defaults.skills") != std::string::npos);
	REQUIRE(source.find("configEntryIt->second.skills.has_value()") != std::string::npos);
	REQUIRE(
		source.find("GatewayHost::ListReservedChatSlashCommandNames") !=
		std::string::npos);
}

TEST_CASE(
	"GatewayHost contract: reserved chat slash command registry exists",
	"[skills][command-aggregation][reserved-registry]")
{
	const auto gatewayHeaderPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.h";
	const std::string gatewayHeader = ReadTextFile(gatewayHeaderPath);
	REQUIRE(
		gatewayHeader.find("ListReservedChatSlashCommandNames") !=
		std::string::npos);

	const auto gatewayImplPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.cpp";
	const std::string gatewayImpl = ReadTextFile(gatewayImplPath);
	REQUIRE(
		gatewayImpl.find("GatewayHost::ListReservedChatSlashCommandNames") !=
		std::string::npos);
	REQUIRE(gatewayImpl.find("\"skill\"") != std::string::npos);
}
