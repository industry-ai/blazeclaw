#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

	std::string ReadSource(const std::filesystem::path& path)
	{
		std::ifstream in(path.string());
		REQUIRE(in.is_open());
		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadViewSource()
	{
		return ReadSource(
			std::filesystem::path("BlazeClawMfc") /
			"src" /
			"app" /
			"BlazeClawMFCView.cpp");
	}

	std::string ReadSkillViewSource()
	{
		return ReadSource(
			std::filesystem::path("BlazeClawMfc") /
			"src" /
			"app" /
			"SkillView.cpp");
	}

} // namespace

TEST_CASE(
	"Notion config resolver contract: bundled precedence is defined before local roots",
	"[skills][notion][config][contract]")
{
	const std::string source = ReadViewSource();
	const auto bundledPos = source.find("skills-bundled");
	const auto localPos = source.find("skills");
	const auto openclawPos = source.find("skills-openclaw-original");

	REQUIRE(bundledPos != std::string::npos);
	REQUIRE(localPos != std::string::npos);
	REQUIRE(openclawPos != std::string::npos);
	REQUIRE(bundledPos < localPos);
	REQUIRE(localPos < openclawPos);
}

TEST_CASE(
	"Generated config host contract: missing-field warning and status channels are handled",
	"[skills][config][generated][contract]")
{
	const std::string source = ReadViewSource();
	REQUIRE(
		source.find("No configuration fields were exposed for this skill") !=
		std::string::npos);
	REQUIRE(source.find("generated-warning") != std::string::npos);
	REQUIRE(source.find("generated-status") != std::string::npos);
	REQUIRE(source.find("blazeclaw.skill.config.saved") != std::string::npos);
	REQUIRE(source.find("blazeclaw.skill.config.validation") != std::string::npos);
	REQUIRE(source.find("blazeclaw.skill.config.error") != std::string::npos);
}

TEST_CASE(
	"SkillView payload contract: canonical config fields are normalized for all categories",
	"[skills][skillview][payload][contract]")
{
	const std::string source = ReadSkillViewSource();
	REQUIRE(source.find("BuildCanonicalSkillPayload(") != std::string::npos);
	REQUIRE(source.find("\"primaryEnv\"") != std::string::npos);
	REQUIRE(source.find("\"requiresEnv\"") != std::string::npos);
	REQUIRE(source.find("\"requiresConfig\"") != std::string::npos);
	REQUIRE(source.find("\"configPathHints\"") != std::string::npos);
	REQUIRE(source.find("\"browserGroup\"") != std::string::npos);
	REQUIRE(source.find("\"browserDisplayName\"") != std::string::npos);
	REQUIRE(source.find("\"browserSourceLabel\"") != std::string::npos);
	REQUIRE(source.find("\"browserVariantLabel\"") != std::string::npos);
}

TEST_CASE(
	"SkillView openclaw-original browser contract uses imported grouping without manifest warning suffix",
	"[skills][skillview][openclaw-original][contract]")
{
	const std::string source = ReadSkillViewSource();
	REQUIRE(source.find("return \"imported\";") != std::string::npos);
	REQUIRE(source.find("[missing tool-manifest.json]") == std::string::npos);
	REQUIRE(source.find("browserDisplayName") != std::string::npos);
}

TEST_CASE(
	"Skill config bridge contract: load and save channels remain wired",
	"[skills][config][bridge][contract]")
{
	const std::string source = ReadViewSource();
	REQUIRE(source.find("blazeclaw.skill.config.ready") != std::string::npos);
	REQUIRE(source.find("blazeclaw.skill.config.save") != std::string::npos);
	REQUIRE(source.find("LoadSkillConfigToBridge(") != std::string::npos);
	REQUIRE(source.find("PersistSkillConfigFromPayload(") != std::string::npos);
	REQUIRE(source.find("SaveSkillConfigEnv(") != std::string::npos);
	REQUIRE(source.find("LoadSkillConfigEnv(") != std::string::npos);
}

