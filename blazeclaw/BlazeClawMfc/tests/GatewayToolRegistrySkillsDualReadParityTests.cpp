#include "pch.h"
#include "gateway/GatewayToolRegistry.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>

namespace {
	std::filesystem::path MakeTempDir(const std::string& suffix) {
		auto dir = std::filesystem::temp_directory_path() /
			("blazeclaw_tool_registry_dual_read_" + suffix + "_" + std::to_string(std::rand()));
		std::filesystem::create_directories(dir);
		return dir;
	}

	void WriteUtf8File(const std::filesystem::path& path, const std::string& text) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream out(path, std::ios::binary);
		REQUIRE(out.is_open());
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
	}

	std::string FindToolSource(
		const std::vector<blazeclaw::gateway::ToolCatalogEntry>& tools,
		const std::string& toolId) {
		const auto it = std::find_if(
			tools.begin(),
			tools.end(),
			[&](const blazeclaw::gateway::ToolCatalogEntry& tool) {
				return tool.id == toolId;
			});
		if (it == tools.end()) {
			return {};
		}
		return it->source;
	}
}

TEST_CASE("Catalog-derived skill tools register without manifests", "[gateway][tools][skills][dual-read]") {
	blazeclaw::gateway::GatewayToolRegistry registry;

	const std::vector<blazeclaw::gateway::ToolCatalogEntry> catalogTools{
		blazeclaw::gateway::ToolCatalogEntry{
			.id = "humanizer.rewrite",
			.label = "Humanizer Rewrite",
			.category = "skill",
			.skillKey = "humanizer",
			.installKind = "skill",
			.source = "ignored.incoming",
			.enabled = true,
		}
	};

	const auto registered = registry.RegisterSkillToolsFromCatalogEntries(catalogTools, true);
	REQUIRE(registered == 1);

	const auto tools = registry.List();
	REQUIRE_FALSE(tools.empty());
	REQUIRE(FindToolSource(tools, "humanizer.rewrite") == "skills.catalog");

	const auto diagnostics = registry.GetSkillToolSourceDiagnostics();
	REQUIRE(diagnostics.catalogRegistered >= 1);
	REQUIRE(diagnostics.manifestRegistered == 0);
}

TEST_CASE("Manifest source wins over catalog fallback for duplicate tool id", "[gateway][tools][skills][dual-read]") {
	blazeclaw::gateway::GatewayToolRegistry registry;

	const std::vector<blazeclaw::gateway::ToolCatalogEntry> catalogTools{
		blazeclaw::gateway::ToolCatalogEntry{
			.id = "web_browsing.search.web",
			.label = "Catalog Search",
			.category = "skill",
			.skillKey = "web-browsing",
			.installKind = "skill",
			.source = "skills.catalog",
			.enabled = true,
		}
	};

	const auto skillsRoot = MakeTempDir("mixed_mode_manifest_first");
	const auto skillDir = skillsRoot / "web-browsing";
	WriteUtf8File(
		skillDir / "tool-manifest.json",
		R"({"tools":[{"id":"web_browsing.search.web","label":"Manifest Search","category":"skill","enabled":true}]})");

	const auto synced = registry.SyncSkillToolsManifestFirst(
		std::vector<std::string>{ skillsRoot.string() },
		catalogTools,
		true);
	REQUIRE(synced >= 1);

	const auto tools = registry.List();
	const auto source = FindToolSource(tools, "web_browsing.search.web");
	REQUIRE(source == "skills.tool-manifest");

	const auto diagnostics = registry.GetSkillToolSourceDiagnostics();
	REQUIRE(diagnostics.manifestRegistered >= 1);

	std::error_code ec;
	std::filesystem::remove_all(skillsRoot, ec);
}

TEST_CASE("Missing manifest is deterministically generated then loaded", "[gateway][tools][skills][dual-read]") {
	blazeclaw::gateway::GatewayToolRegistry registry;

	const auto skillsRoot = MakeTempDir("generated_manifest");
	const auto skillDir = skillsRoot / "imap-smtp-email";
	std::filesystem::create_directories(skillDir);

	const std::vector<blazeclaw::gateway::ToolCatalogEntry> catalogTools{
		blazeclaw::gateway::ToolCatalogEntry{
			.id = "imap_smtp_email.smtp.send",
			.label = "SMTP Send",
			.category = "skill",
			.skillKey = "imap-smtp-email",
			.installKind = "skill",
			.source = "skills.catalog",
			.enabled = true,
		}
	};

	const auto synced = registry.SyncSkillToolsManifestFirst(
		std::vector<std::string>{ skillsRoot.string() },
		catalogTools,
		true);
	REQUIRE(synced >= 1);

	const auto generatedManifest = skillDir / "tool-manifest.json";
	REQUIRE(std::filesystem::exists(generatedManifest));

	const auto generatedTextFirst = [&generatedManifest]() {
		std::ifstream input(generatedManifest, std::ios::binary);
		std::string text;
		input.seekg(0, std::ios::end);
		const auto size = input.tellg();
		if (size > 0) {
			text.resize(static_cast<std::size_t>(size));
			input.seekg(0, std::ios::beg);
			input.read(text.data(), static_cast<std::streamsize>(text.size()));
		}
		return text;
	}();

	const auto tools = registry.List();
	REQUIRE(FindToolSource(tools, "imap_smtp_email.smtp.send") == "skills.tool-manifest");

	const auto secondSync = registry.SyncSkillToolsManifestFirst(
		std::vector<std::string>{ skillsRoot.string() },
		catalogTools,
		true);
	REQUIRE(secondSync >= 1);

	const auto generatedTextSecond = [&generatedManifest]() {
		std::ifstream input(generatedManifest, std::ios::binary);
		std::string text;
		input.seekg(0, std::ios::end);
		const auto size = input.tellg();
		if (size > 0) {
			text.resize(static_cast<std::size_t>(size));
			input.seekg(0, std::ios::beg);
			input.read(text.data(), static_cast<std::streamsize>(text.size()));
		}
		return text;
	}();

	REQUIRE(generatedTextFirst == generatedTextSecond);

	const auto diagnostics = registry.GetSkillToolSourceDiagnostics();
	REQUIRE(diagnostics.manifestsGenerated >= 1);
	REQUIRE(diagnostics.manifestGenerationFailed == 0);

	std::error_code ec;
	std::filesystem::remove_all(skillsRoot, ec);
}

TEST_CASE("Invalid catalog descriptors are rejected deterministically", "[gateway][tools][skills][dual-read]") {
	blazeclaw::gateway::GatewayToolRegistry registry;

	const std::vector<blazeclaw::gateway::ToolCatalogEntry> catalogTools{
		blazeclaw::gateway::ToolCatalogEntry{
			.id = "",
			.label = "Missing ID",
			.category = "skill",
			.skillKey = "invalid",
			.installKind = "skill",
			.source = "skills.catalog",
			.enabled = true,
		},
		blazeclaw::gateway::ToolCatalogEntry{
			.id = "summarize.extract",
			.label = "",
			.category = "",
			.skillKey = "",
			.installKind = "",
			.source = "skills.catalog",
			.enabled = true,
		}
	};

	const auto registered = registry.RegisterSkillToolsFromCatalogEntries(catalogTools, true);
	REQUIRE(registered == 1);

	const auto tools = registry.List();
	const auto it = std::find_if(
		tools.begin(),
		tools.end(),
		[](const blazeclaw::gateway::ToolCatalogEntry& entry) {
			return entry.id == "summarize.extract";
		});
	REQUIRE(it != tools.end());
	REQUIRE(it->label == "summarize.extract");
	REQUIRE(it->category == "skill");
	REQUIRE(it->skillKey == "summarize");
	REQUIRE(it->installKind == "skill");
	REQUIRE(it->source == "skills.catalog");

	const auto diagnostics = registry.GetSkillToolSourceDiagnostics();
	REQUIRE(diagnostics.catalogRejected >= 1);
}
