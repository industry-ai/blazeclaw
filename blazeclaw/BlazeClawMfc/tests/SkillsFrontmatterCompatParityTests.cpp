#include "core/SkillsFrontmatterCompat.h"

#include <catch2/catch_all.hpp>

using blazeclaw::core::ParseSkillFrontmatterCompat;
using blazeclaw::core::ResolveOpenClawMetadataCompat;
using blazeclaw::core::ResolveSkillExposurePolicyCompat;
using blazeclaw::core::ResolveSkillInstallFormulaCompat;
using blazeclaw::core::ResolveSkillInstallKindCompat;
using blazeclaw::core::ResolveSkillInstallNodeManagerCompat;
using blazeclaw::core::ResolveSkillInstallPackageCompat;
using blazeclaw::core::ResolveSkillInstallPreferBrewCompat;
using blazeclaw::core::ResolveSkillInstallUrlCompat;
using blazeclaw::core::ResolveSkillInvocationPolicyCompat;
using blazeclaw::core::ResolveSkillKeyCompat;

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

TEST_CASE(
	"Skills frontmatter compat: install safety normalization and skill-key fallback",
	"[skills][frontmatter][compat][safety]") {
	const std::wstring content =
		L"---\n"
		L"name: safety\n"
		L"description: safety desc\n"
		L"install-kind: download\n"
		L"install-url: javascript:alert(1)\n"
		L"openclaw.skillkey:\n"
		L"---\n";

	std::vector<std::wstring> errors;
	const auto parsed = ParseSkillFrontmatterCompat(content, errors);
	REQUIRE(parsed.has_value());

	const auto metadata = ResolveOpenClawMetadataCompat(parsed.value());
	REQUIRE(metadata.install.empty());

	const std::wstring fallbackKey = ResolveSkillKeyCompat(&metadata, L"safety");
	REQUIRE(fallbackKey == L"safety");
}

TEST_CASE(
	"Skills frontmatter compat: install fallback helper routing",
	"[skills][frontmatter][compat][routing]") {
	const std::wstring content =
		L"---\n"
		L"name: install-routing\n"
		L"description: install routing desc\n"
		L"openclaw.install.kind: node\n"
		L"openclaw.install.package: @scope/tool\n"
		L"openclaw.install.url: javascript:alert(1)\n"
		L"openclaw.install.formula: wget\n"
		L"openclaw.install.preferbrew: true\n"
		L"openclaw.install.nodemanager: yarn\n"
		L"---\n";

	std::vector<std::wstring> errors;
	const auto parsed = ParseSkillFrontmatterCompat(content, errors);
	REQUIRE(parsed.has_value());

	const auto kind = ResolveSkillInstallKindCompat(parsed.value());
	REQUIRE(kind == L"node");

	const auto package = ResolveSkillInstallPackageCompat(parsed.value(), kind);
	REQUIRE(package == L"@scope/tool");

	const auto formula = ResolveSkillInstallFormulaCompat(parsed.value());
	REQUIRE(formula == L"wget");

	const auto url = ResolveSkillInstallUrlCompat(parsed.value());
	REQUIRE(url.empty());

	REQUIRE(ResolveSkillInstallPreferBrewCompat(parsed.value(), false));
	REQUIRE(ResolveSkillInstallNodeManagerCompat(parsed.value(), L"npm") == L"yarn");
}

TEST_CASE(
	"Skills frontmatter compat: metadata manifest helper routing",
	"[skills][frontmatter][compat][shared-frontmatter]") {
	const std::wstring content =
		L"---\n"
		L"name: shared-manifest\n"
		L"description: shared manifest desc\n"
		L"metadata: { openclaw: { skillKey: 'manifest-key', primaryEnv: 'MANIFEST_ENV', requires: { bins: ['git','node'] }, install: [{ kind: 'brew', formula: 'wget', bins: ['wget'] }] } }\n"
		L"---\n";

	std::vector<std::wstring> errors;
	const auto parsed = ParseSkillFrontmatterCompat(content, errors);
	REQUIRE(parsed.has_value());
	REQUIRE(errors.empty());

	const auto metadata = ResolveOpenClawMetadataCompat(parsed.value());
	REQUIRE(metadata.skillKey == L"manifest-key");
	REQUIRE(metadata.primaryEnv == L"MANIFEST_ENV");
	REQUIRE(metadata.requirements.bins == std::vector<std::wstring>{L"git", L"node"});
	REQUIRE(metadata.install.size() == 1);
	REQUIRE(metadata.install.front().kind == L"brew");
	REQUIRE(metadata.install.front().formula == L"wget");
}
