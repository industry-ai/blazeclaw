#include "core/SkillsFrontmatterCompat.h"

#include <catch2/catch_all.hpp>

using blazeclaw::core::ParseSkillFrontmatterCompat;
using blazeclaw::core::ResolveOpenClawMetadataCompat;
using blazeclaw::core::ResolveSkillExposurePolicyCompat;
using blazeclaw::core::ResolveSkillInvocationPolicyCompat;

TEST_CASE(
	"Skills frontmatter compat: parser extracts required fields",
	"[skills][frontmatter][compat][parser]") {
	const std::wstring content =
		L"---\n"
		L"name: demo\n"
		L"description: demo description\n"
		L"user-invocable: false\n"
		L"---\n"
		L"# body\n";

	std::vector<std::wstring> errors;
	const auto parsed = ParseSkillFrontmatterCompat(content, errors);
	REQUIRE(parsed.has_value());
	REQUIRE(errors.empty());
	REQUIRE(parsed->name == L"demo");
	REQUIRE(parsed->description == L"demo description");
}

TEST_CASE(
	"Skills frontmatter compat: metadata and invocation projection",
	"[skills][frontmatter][compat][projection]") {
	const std::wstring content =
		L"---\n"
		L"name: demo\n"
		L"description: desc\n"
		L"openclaw.skillkey: demo-key\n"
		L"primary-env: DEMO_ENV\n"
		L"requires-bins: git, node\n"
		L"install-kind: node\n"
		L"install-package: @scope/demo\n"
		L"user-invocable: false\n"
		L"disable-model-invocation: true\n"
		L"---\n";

	std::vector<std::wstring> errors;
	const auto parsed = ParseSkillFrontmatterCompat(content, errors);
	REQUIRE(parsed.has_value());

	const auto metadata = ResolveOpenClawMetadataCompat(parsed.value());
	REQUIRE(metadata.skillKey == L"demo-key");
	REQUIRE(metadata.primaryEnv == L"DEMO_ENV");
	REQUIRE(metadata.requirements.bins.size() == 2);
	REQUIRE(metadata.install.size() == 1);
	REQUIRE(metadata.install.front().kind == L"node");
	REQUIRE(metadata.install.front().package == L"@scope/demo");

	const auto invocation = ResolveSkillInvocationPolicyCompat(parsed.value());
	REQUIRE(invocation.userInvocable == false);
	REQUIRE(invocation.disableModelInvocation == true);

	const auto exposure = ResolveSkillExposurePolicyCompat(parsed.value(), invocation);
	REQUIRE(exposure.userInvocable == false);
}
