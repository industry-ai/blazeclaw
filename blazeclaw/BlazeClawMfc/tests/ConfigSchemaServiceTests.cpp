#include "core/ConfigSchemaService.h"

#include <catch2/catch_all.hpp>

namespace {

	blazeclaw::gateway::SkillsCatalogGatewayEntry MakeSkill(
		const std::string& skillKey,
		std::vector<std::string> requiresConfig,
		std::vector<std::string> configPathHints = {}) {
		blazeclaw::gateway::SkillsCatalogGatewayEntry entry;
		entry.name = skillKey;
		entry.skillKey = skillKey;
		entry.requiresConfig = std::move(requiresConfig);
		entry.configPathHints = std::move(configPathHints);
		return entry;
	}

	blazeclaw::config::AppConfig MakeConfig(std::initializer_list<const wchar_t*> channels) {
		blazeclaw::config::AppConfig config;
		for (const auto* channel : channels) {
			if (channel != nullptr) {
				config.enabledChannels.emplace_back(channel);
			}
		}

		return config;
	}

} // namespace

TEST_CASE("ConfigSchemaService derives sensitive and sensitive-url hint tags", "[schema][hints]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;
	skills.entries.push_back(MakeSkill(
		"discord",
		{ "channels.discord.token" },
		{ "channels.discord.webhookAuthUrl" }));

	const auto state = service.BuildGatewayState(MakeConfig({ L"discord" }), skills);
	REQUIRE_FALSE(state.uiHintsJson.empty());

	const auto tokenLookup = service.Lookup(state, "channels.discord.token");
	REQUIRE(tokenLookup.has_value());
	REQUIRE(tokenLookup->hint.has_value());
	REQUIRE(tokenLookup->hint->sensitive);
	REQUIRE(tokenLookup->hint->tags.size() >= 3);
	REQUIRE(
		std::find(
			tokenLookup->hint->tags.begin(),
			tokenLookup->hint->tags.end(),
			"sensitive") != tokenLookup->hint->tags.end());

	const auto urlLookup = service.Lookup(state, "channels.discord.webhookAuthUrl");
	REQUIRE(urlLookup.has_value());
	REQUIRE(urlLookup->hint.has_value());
	REQUIRE(urlLookup->hint->sensitive);
	REQUIRE(
		std::find(
			urlLookup->hint->tags.begin(),
			urlLookup->hint->tags.end(),
			"sensitive-url") != urlLookup->hint->tags.end());
}

TEST_CASE("ConfigSchemaService lookup blocks forbidden path segments", "[schema][lookup][guard]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;
	skills.entries.push_back(MakeSkill("discord", { "channels.discord.token" }));

	const auto state = service.BuildGatewayState(MakeConfig({ L"discord" }), skills);
	REQUIRE_FALSE(service.Lookup(state, "channels.__proto__.x").has_value());
	REQUIRE_FALSE(service.Lookup(state, "channels.prototype.x").has_value());
	REQUIRE_FALSE(service.Lookup(state, "channels.constructor.x").has_value());
}

TEST_CASE("ConfigSchemaService lookup returns wildcard child for additionalProperties", "[schema][lookup][children]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;
	skills.entries.push_back(MakeSkill("discord", { "channels.discord.token" }));

	const auto state = service.BuildGatewayState(MakeConfig({ L"discord" }), skills);
	const auto lookup = service.Lookup(state, "channels");
	REQUIRE(lookup.has_value());
	REQUIRE_FALSE(lookup->children.empty());

	const auto wildcardIt = std::find_if(
		lookup->children.begin(),
		lookup->children.end(),
		[](const blazeclaw::gateway::ConfigSchemaGatewayChild& child) {
			return child.key == "*";
		});
	REQUIRE(wildcardIt != lookup->children.end());
}

TEST_CASE("ConfigSchemaService cache returns stable generatedAt for identical key", "[schema][cache]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;
	skills.snapshotVersion = 7;
	skills.entries.push_back(MakeSkill("discord", { "channels.discord.token" }));

	const auto config = MakeConfig({ L"discord" });
	const auto state1 = service.BuildGatewayState(config, skills);
	const auto state2 = service.BuildGatewayState(config, skills);

	REQUIRE(state1.generatedAt == state2.generatedAt);
	REQUIRE(state1.schemaJson == state2.schemaJson);
	REQUIRE(state1.uiHintsJson == state2.uiHintsJson);
}

TEST_CASE("ConfigSchemaService cache invalidates on demand", "[schema][cache][invalidate]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;
	skills.snapshotVersion = 9;
	skills.entries.push_back(MakeSkill("discord", { "channels.discord.token" }));

	const auto config = MakeConfig({ L"discord" });
	const auto state1 = service.BuildGatewayState(config, skills);
	service.Invalidate();
	const auto state2 = service.BuildGatewayState(config, skills);

	REQUIRE(state1.schemaJson == state2.schemaJson);
	REQUIRE(state1.uiHintsJson == state2.uiHintsJson);
}
