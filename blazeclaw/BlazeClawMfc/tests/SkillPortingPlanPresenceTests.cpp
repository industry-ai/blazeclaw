#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {

	std::filesystem::path SkillsRootFromBlazeClawCwd() {
		return std::filesystem::path("skills");
	}

} // namespace

TEST_CASE(
	"Phase F contract: every blazeclaw/skills/*/ directory has PORTING_PLAN.md",
	"[skills][phasef][contract]")
{
	const auto root = SkillsRootFromBlazeClawCwd();
	REQUIRE(std::filesystem::exists(root));
	REQUIRE(std::filesystem::is_directory(root));

	std::vector<std::string> missing;
	for (const auto& entry : std::filesystem::directory_iterator(root)) {
		if (!entry.is_directory()) {
			continue;
		}
		const auto plan = entry.path() / "PORTING_PLAN.md";
		if (!std::filesystem::exists(plan)) {
			missing.push_back(entry.path().filename().string());
		}
	}

	if (!missing.empty()) {
		std::string joined;
		for (std::size_t i = 0; i < missing.size(); ++i) {
			if (i > 0) {
				joined += ", ";
			}
			joined += missing[i];
		}
		FAIL("Missing PORTING_PLAN.md for: " + joined);
	}
}

TEST_CASE(
	"Phase F contract: SKILL_PORTING.md lists every skills child with PORTING_PLAN",
	"[skills][phasef][contract]")
{
	const auto docPath = std::filesystem::path("docs") / "SKILL_PORTING.md";
	REQUIRE(std::filesystem::exists(docPath));

	std::ifstream in(docPath.string());
	REQUIRE(in.is_open());
	const std::string doc(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	std::set<std::string> dirsWithPlans;
	for (const auto& entry : std::filesystem::directory_iterator(SkillsRootFromBlazeClawCwd())) {
		if (!entry.is_directory()) {
			continue;
		}
		const auto plan = entry.path() / "PORTING_PLAN.md";
		if (std::filesystem::exists(plan)) {
			dirsWithPlans.insert(entry.path().filename().string());
		}
	}

	for (const auto& name : dirsWithPlans) {
		const std::string needle = "blazeclaw/skills/" + name + "/";
		INFO("SKILL_PORTING.md should list " << needle << " in the PORTING_PLAN table");
		REQUIRE(doc.find(needle) != std::string::npos);
	}
}
