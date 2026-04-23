#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

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
	"skills.ts parity: OpenClaw aggregate export families are mapped in BlazeClaw docs",
	"[skills][parity][aggregate][mapping]")
{
	const std::string source = ReadTextFile(
		std::filesystem::path("..") / "openclaw" / "src" / "agents" / "skills.ts");
	REQUIRE(source.find("export {") != std::string::npos);
	REQUIRE(source.find("resolveSkillConfig") != std::string::npos);
	REQUIRE(source.find("applySkillEnvOverrides") != std::string::npos);
	REQUIRE(source.find("buildWorkspaceSkillSnapshot") != std::string::npos);
	REQUIRE(source.find("buildWorkspaceSkillCommandSpecs") != std::string::npos);
	REQUIRE(source.find("resolveSkillsInstallPreferences") != std::string::npos);

	const std::string mappingDoc = ReadTextFile(
		std::filesystem::path("..") /
		"docs" /
		"compare" /
		"skills.ts" /
		"OPENCLAW_AGENTS_SKILLS_TS_AGGREGATE_EXPORT_MAPPING_S1.md");
	REQUIRE(mappingDoc.find("`skills/config.ts` export family") != std::string::npos);
	REQUIRE(mappingDoc.find("`skills/env-overrides.ts` export family") != std::string::npos);
	REQUIRE(mappingDoc.find("`skills/workspace.ts` export family") != std::string::npos);
	REQUIRE(mappingDoc.find("`skills/command-specs.ts` export family") != std::string::npos);
	REQUIRE(mappingDoc.find("`resolveSkillsInstallPreferences`") != std::string::npos);
}

TEST_CASE(
	"skills.ts parity: startup refresh path remains facade-first orchestration",
	"[skills][parity][aggregate][facade]")
{
	const std::string startupCoordinator = ReadTextFile(
		std::filesystem::path("BlazeClawMfc") / "src" / "core" / "ServiceLifecycleStartupCoordinator.cpp");
	REQUIRE(startupCoordinator.find("manager.RefreshSkillsState(manager.m_activeConfig, true, L\"startup-minimal\")") != std::string::npos);
	REQUIRE(startupCoordinator.find("manager.m_skillsFacade.RefreshSkillsState(") == std::string::npos);

	const std::string hooksCoordinator = ReadTextFile(
		std::filesystem::path("BlazeClawMfc") / "src" / "core" / "skills" / "CSkillsHooksCoordinator.cpp");
	REQUIRE(hooksCoordinator.find("context.skillsFacade.RefreshSkillsState(") != std::string::npos);
}

TEST_CASE(
	"skills.ts parity: aggregate guard checks cover install, prompt fallback, and run snapshot shape",
	"[skills][parity][aggregate][guards]")
{
	const std::string facadeSource = ReadTextFile(
		std::filesystem::path("BlazeClawMfc") / "src" / "core" / "SkillsFacade.cpp");
	REQUIRE(facadeSource.find("manager == L\"pnpm\"") != std::string::npos);
	REQUIRE(facadeSource.find("manager == L\"yarn\"") != std::string::npos);
	REQUIRE(facadeSource.find("manager == L\"bun\"") != std::string::npos);
	REQUIRE(facadeSource.find("manager == L\"npm\"") != std::string::npos);
	REQUIRE(facadeSource.find("resolved.nodeManager = L\"npm\"") != std::string::npos);
	REQUIRE(facadeSource.find("if (runSnapshot != nullptr)") != std::string::npos);
	REQUIRE(facadeSource.find("if (promptSnapshot != nullptr)") != std::string::npos);
	REQUIRE(facadeSource.find("promptService.BuildSnapshot(") != std::string::npos);
	REQUIRE(facadeSource.find("snapshot.version = watch.version") != std::string::npos);
	REQUIRE(facadeSource.find("snapshot.resolvedSkills.push_back(entry.skillName)") != std::string::npos);
	REQUIRE(facadeSource.find("runSkill.requiredEnv") != std::string::npos);

	const std::string hooksCoordinator = ReadTextFile(
		std::filesystem::path("BlazeClawMfc") / "src" / "core" / "skills" / "CSkillsHooksCoordinator.cpp");
	REQUIRE(hooksCoordinator.find("commandsBySkill") != std::string::npos);
	REQUIRE(hooksCoordinator.find("context.commands") != std::string::npos);
	REQUIRE(hooksCoordinator.find("commandProjectedCount") != std::string::npos);
}

TEST_CASE(
	"skills.ts parity: OpenClaw change-trigger protocol is documented and anchored",
	"[skills][parity][aggregate][trigger-protocol]")
{
	const std::string planDoc = ReadTextFile(
		std::filesystem::path("..") /
		"docs" /
		"compare" /
		"skills.ts" /
		"OPENCLAW_AGENTS_SKILLS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md");
	REQUIRE(planDoc.find("Add OpenClaw-change trigger protocol") != std::string::npos);
	REQUIRE(planDoc.find("Step 4 complete") != std::string::npos);

	const std::string protocolDoc = ReadTextFile(
		std::filesystem::path("..") /
		"docs" /
		"compare" /
		"skills.ts" /
		"OPENCLAW_AGENTS_SKILLS_TS_CHANGE_TRIGGER_PROTOCOL_S4.md");
	REQUIRE(protocolDoc.find("Trigger conditions") != std::string::npos);
	REQUIRE(protocolDoc.find("Required update bundle") != std::string::npos);
	REQUIRE(protocolDoc.find("`openclaw/src/agents/skills.ts`") != std::string::npos);
	REQUIRE(protocolDoc.find("`SkillsTsAggregateParityTests.cpp`") != std::string::npos);
}

TEST_CASE(
	"skills.ts parity: gateway diagnostics contract uses projection counters for install parity",
	"[skills][parity][aggregate][diagnostics]")
{
	const std::string hooksCoordinator = ReadTextFile(
		std::filesystem::path("BlazeClawMfc") / "src" / "core" / "skills" / "CSkillsHooksCoordinator.cpp");
	REQUIRE(hooksCoordinator.find("installContractProjectedCount") != std::string::npos);
	REQUIRE(hooksCoordinator.find("installContractFallbackCount") != std::string::npos);

	const std::string gatewayHandler = ReadTextFile(
		std::filesystem::path("BlazeClawMfc") / "src" / "gateway" / "GatewayHost.Handlers.Runtime.ChatPipeline.cpp");
	REQUIRE(gatewayHandler.find("\"installContractProjected\":") != std::string::npos);
	REQUIRE(gatewayHandler.find("\"installContractFallback\":") != std::string::npos);
	REQUIRE(gatewayHandler.find("const auto& state = host.m_skillsCatalogState;") != std::string::npos);
}

TEST_CASE(
	"skills.ts parity: compare docs stay synchronized across plan and umbrella analysis",
	"[skills][parity][aggregate][docs-sync]")
{
	const std::string planDoc = ReadTextFile(
		std::filesystem::path("..") /
		"docs" /
		"compare" /
		"skills.ts" /
		"OPENCLAW_AGENTS_SKILLS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md");
	const std::string umbrellaDoc = ReadTextFile(
		std::filesystem::path("..") /
		"docs" /
		"compare" /
		"skills.ts" /
		"OPENCLAW_SKILLS_TS_CAPABILITY_PARITY_ANALYSIS.md");

	REQUIRE(planDoc.find("Step 6 complete") != std::string::npos);
	REQUIRE(umbrellaDoc.find("Step 1-6 synchronized status") != std::string::npos);
	REQUIRE(umbrellaDoc.find("OPENCLAW_AGENTS_SKILLS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md") != std::string::npos);
	REQUIRE(umbrellaDoc.find("OPENCLAW_AGENTS_SKILLS_TS_AGGREGATE_EXPORT_MAPPING_S1.md") != std::string::npos);
	REQUIRE(umbrellaDoc.find("OPENCLAW_AGENTS_SKILLS_TS_CHANGE_TRIGGER_PROTOCOL_S4.md") != std::string::npos);
}
