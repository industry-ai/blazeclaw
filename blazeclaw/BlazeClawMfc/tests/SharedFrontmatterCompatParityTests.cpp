#include "core/SharedFrontmatterCompat.h"

#include <catch2/catch_all.hpp>

using blazeclaw::core::ApplyOpenClawManifestInstallCommonFieldsCompat;
using blazeclaw::core::GetFrontmatterStringCompat;
using blazeclaw::core::NormalizeStringListCompat;
using blazeclaw::core::ParseFrontmatterBoolCompat;
using blazeclaw::core::ParseOpenClawManifestInstallBaseCompat;
using blazeclaw::core::ResolveOpenClawManifestBlockCompat;
using blazeclaw::core::ResolveOpenClawManifestInstallCompat;
using blazeclaw::core::ResolveOpenClawManifestOsCompat;
using blazeclaw::core::ResolveOpenClawManifestRequiresCompat;

TEST_CASE(
	"Shared frontmatter compat: normalize list/string/bool contracts",
	"[shared][frontmatter][compat][core]") {
	const auto stringList = NormalizeStringListCompat(nlohmann::json("a, b,,c"));
	REQUIRE(stringList == std::vector<std::wstring>{L"a", L"b", L"c"});

	const auto arrayList = NormalizeStringListCompat(
		nlohmann::json::array({ L" a ", L"", 42 }));
	REQUIRE(arrayList == std::vector<std::wstring>{L"a", L"42"});

	REQUIRE(ParseFrontmatterBoolCompat(L"true", false));
	REQUIRE_FALSE(ParseFrontmatterBoolCompat(L"false", true));
	REQUIRE(ParseFrontmatterBoolCompat(L"maybe", true));
}

TEST_CASE(
	"Shared frontmatter compat: resolve manifest block and derived helpers",
	"[shared][frontmatter][compat][manifest]") {
	const std::map<std::wstring, std::wstring> frontmatter = {
		{ L"metadata", L"{ openclaw: { requires: { bins: 'git,node' }, os: ['darwin','linux'], install: [{ kind: 'brew', id: 'brew.git', label: 'Git', bins: ['git'] }] } }" },
	};

	const auto manifest = ResolveOpenClawManifestBlockCompat(frontmatter);
	REQUIRE(manifest.has_value());

	const auto requiresResult =
		ResolveOpenClawManifestRequiresCompat(manifest.value());
	REQUIRE(requiresResult.has_value());
	REQUIRE(requiresResult->bins == std::vector<std::wstring>{L"git", L"node"});

	const auto os = ResolveOpenClawManifestOsCompat(manifest.value());
	REQUIRE(os == std::vector<std::wstring>{L"darwin", L"linux"});

	const auto install = ResolveOpenClawManifestInstallCompat(manifest.value());
	REQUIRE(install.size() == 1);
}

TEST_CASE(
	"Shared frontmatter compat: parse install base and apply common fields",
	"[shared][frontmatter][compat][install]") {
	const auto parsed = ParseOpenClawManifestInstallBaseCompat(
		nlohmann::json::parse(R"({"type":" Brew ","id":"brew.git","label":"Git","bins":[" git ","git"]})"),
		std::vector<std::wstring>{ L"brew", L"npm" });
	REQUIRE(parsed.has_value());
	REQUIRE(parsed->kind == L"brew");
	REQUIRE(parsed->id.has_value());
	REQUIRE(parsed->id.value() == L"brew.git");

	std::wstring id = L"";
	std::wstring label = L"";
	std::vector<std::wstring> bins;
	ApplyOpenClawManifestInstallCommonFieldsCompat(id, label, bins, parsed.value());
	REQUIRE(id == L"brew.git");
	REQUIRE(label == L"Git");
	REQUIRE(bins == std::vector<std::wstring>{L"git", L"git"});

	const std::map<std::wstring, std::wstring> fields = {
		{ L"enabled", L" true " },
	};
	REQUIRE(GetFrontmatterStringCompat(fields, L"enabled") == L"true");
}

TEST_CASE(
	"Shared frontmatter compat: resolves legacy blazeclaw manifest key",
	"[shared][frontmatter][compat][manifest][legacy-key]") {
	const std::map<std::wstring, std::wstring> frontmatter = {
		{ L"metadata", L"{ blazeclaw: { skillKey: 'legacy-key', requires: { bins: ['git'] } } }" },
	};

	const auto manifest = ResolveOpenClawManifestBlockCompat(frontmatter);
	REQUIRE(manifest.has_value());
	REQUIRE(manifest->contains("skillKey"));
	REQUIRE(manifest->at("skillKey").get<std::string>() == "legacy-key");

	const auto requiresResult = ResolveOpenClawManifestRequiresCompat(manifest.value());
	REQUIRE(requiresResult.has_value());
	REQUIRE(requiresResult->bins == std::vector<std::wstring>{L"git"});
}
