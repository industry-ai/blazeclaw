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
	"Skills contracts hub: shared DTO header exists with OpenClaw-aligned core types",
	"[skills][types][contracts][hub]")
{
	const auto headerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"SkillsContracts.h";
	const std::string source = ReadTextFile(headerPath);

	REQUIRE(source.find("struct SkillInstallSpec") != std::string::npos);
	REQUIRE(source.find("struct SkillsMetadataSpec") != std::string::npos);
	REQUIRE(source.find("struct SkillRequiresSpec") != std::string::npos);
	REQUIRE(source.find("struct SkillInvocationPolicySpec") != std::string::npos);
	REQUIRE(source.find("struct SkillExposureSpec") != std::string::npos);
	REQUIRE(source.find("struct SkillSnapshotSpec") != std::string::npos);
	REQUIRE(source.find("struct SkillsRemoteEligibilityContract") !=
		std::string::npos);
}

TEST_CASE(
	"Skills service headers: shared contract aliases are consumed",
	"[skills][types][contracts][aliases]")
{
	const auto installHeaderPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"SkillsInstallService.h";
	const std::string installHeader = ReadTextFile(installHeaderPath);
	REQUIRE(installHeader.find("#include \"SkillsContracts.h\"") !=
		std::string::npos);
	REQUIRE(installHeader.find("using SkillsInstallSpec = SkillInstallSpec") !=
		std::string::npos);

	const auto eligibilityHeaderPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"SkillsEligibilityService.h";
	const std::string eligibilityHeader = ReadTextFile(eligibilityHeaderPath);
	REQUIRE(eligibilityHeader.find("#include \"SkillsContracts.h\"") !=
		std::string::npos);
	REQUIRE(
		eligibilityHeader.find("using SkillsRemoteEligibilityContext = SkillsRemoteEligibilityContract") !=
		std::string::npos);

	const auto facadeHeaderPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"SkillsFacade.h";
	const std::string facadeHeader = ReadTextFile(facadeHeaderPath);
	REQUIRE(facadeHeader.find("#include \"SkillsContracts.h\"") !=
		std::string::npos);
	REQUIRE(facadeHeader.find("using SkillsRunSnapshotSkill = SkillRunView") !=
		std::string::npos);
	REQUIRE(facadeHeader.find("using SkillsRunSnapshot = SkillSnapshotSpec") !=
		std::string::npos);

	const auto catalogHeaderPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"SkillsCatalogService.h";
	const std::string catalogHeader = ReadTextFile(catalogHeaderPath);
	REQUIRE(catalogHeader.find("std::optional<SkillsMetadataSpec> metadata") !=
		std::string::npos);
	REQUIRE(catalogHeader.find("std::optional<SkillInvocationPolicySpec> invocation") !=
		std::string::npos);
	REQUIRE(catalogHeader.find("std::optional<SkillExposureSpec> exposure") !=
		std::string::npos);

	const auto catalogImplPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"SkillsCatalogService.cpp";
	const std::string catalogImpl = ReadTextFile(catalogImplPath);
	REQUIRE(catalogImpl.find("BuildNormalizedSkillsMetadata(") !=
		std::string::npos);
	REQUIRE(catalogImpl.find("BuildNormalizedInvocationPolicy(") !=
		std::string::npos);
	REQUIRE(catalogImpl.find("BuildNormalizedExposurePolicy(") !=
		std::string::npos);
}
