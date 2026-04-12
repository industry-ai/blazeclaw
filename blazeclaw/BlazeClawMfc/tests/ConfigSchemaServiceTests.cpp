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

TEST_CASE("ConfigSchemaService cache key includes config path hint content", "[schema][cache][key]") {
	blazeclaw::core::ConfigSchemaService service;
	auto config = MakeConfig({ L"discord" });

	blazeclaw::gateway::SkillsCatalogGatewayState skillsWithHintA;
	skillsWithHintA.snapshotVersion = 5;
	skillsWithHintA.entries.push_back(MakeSkill(
		"discord",
		{ "channels.discord.token" },
		{ "channels.discord.webhookAuthUrl" }));

	blazeclaw::gateway::SkillsCatalogGatewayState skillsWithHintB;
	skillsWithHintB.snapshotVersion = 5;
	skillsWithHintB.entries.push_back(MakeSkill(
		"discord",
		{ "channels.discord.token" },
		{ "channels.discord.endpoint" }));

	const auto stateA = service.BuildGatewayState(config, skillsWithHintA);
	const auto stateB = service.BuildGatewayState(config, skillsWithHintB);

	const auto lookupA = service.Lookup(stateA, "channels.discord.webhookAuthUrl");
	const auto lookupB = service.Lookup(stateB, "channels.discord.webhookAuthUrl");

	REQUIRE(lookupA.has_value());
	REQUIRE(lookupA->hint.has_value());
	const bool lookupBHasHint =
		lookupB.has_value() && lookupB->hint.has_value();
	REQUIRE_FALSE(lookupBHasHint);
}

TEST_CASE("ConfigSchemaService cache key ignores skill order", "[schema][cache][order]") {
	blazeclaw::core::ConfigSchemaService service;
	auto config = MakeConfig({ L"discord", L"slack" });

	blazeclaw::gateway::SkillsCatalogGatewayState skillsOrderedA;
	skillsOrderedA.snapshotVersion = 11;
	skillsOrderedA.entries.push_back(MakeSkill("discord", { "channels.discord.token" }));
	skillsOrderedA.entries.push_back(MakeSkill("slack", { "channels.slack.botToken" }));

	blazeclaw::gateway::SkillsCatalogGatewayState skillsOrderedB;
	skillsOrderedB.snapshotVersion = 11;
	skillsOrderedB.entries.push_back(MakeSkill("slack", { "channels.slack.botToken" }));
	skillsOrderedB.entries.push_back(MakeSkill("discord", { "channels.discord.token" }));

	const auto stateA = service.BuildGatewayState(config, skillsOrderedA);
	const auto stateB = service.BuildGatewayState(config, skillsOrderedB);

	REQUIRE(stateA.generatedAt == stateB.generatedAt);
	REQUIRE(stateA.schemaJson == stateB.schemaJson);
	REQUIRE(stateA.uiHintsJson == stateB.uiHintsJson);
}

TEST_CASE("ConfigSchemaService projects requiresConfig paths into schema lookup", "[schema][projection]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;
	skills.entries.push_back(MakeSkill(
		"custom",
		{ "plugins.entries.custom.config.apiKey" },
		{}));

	const auto state = service.BuildGatewayState(MakeConfig({ L"discord" }), skills);
	const auto lookup = service.Lookup(state, "plugins.entries.custom.config.apiKey");
	REQUIRE(lookup.has_value());
	REQUIRE(lookup->schemaJson.find("\"type\":\"string\"") != std::string::npos);
	REQUIRE(lookup->schemaJson.find("\"minLength\":1") != std::string::npos);
}

TEST_CASE("ConfigSchemaService adds heartbeat target hint enrichment", "[schema][hints][heartbeat]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;

	const auto state = service.BuildGatewayState(MakeConfig({ L"discord", L"slack" }), skills);
	const auto lookup = service.Lookup(state, "agents.defaults.heartbeat.target");
	REQUIRE(lookup.has_value());
	REQUIRE(lookup->hint.has_value());
	REQUIRE(lookup->hint->placeholder == "last");
	REQUIRE(
		lookup->hint->help.find("Known channels") != std::string::npos);
}

TEST_CASE("ConfigSchemaService lookup strip keeps expanded schema scalar keys", "[schema][lookup][strip]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;

	blazeclaw::gateway::ConfigSchemaGatewayState state;
	state.schemaJson =
		"{"
		"\"type\":\"object\","
		"\"properties\":{"
		"\"sample\":{"
		"\"type\":\"string\","
		"\"$id\":\"sample-id\","
		"\"$schema\":\"https://json-schema.org/draft/2020-12/schema\","
		"\"contentEncoding\":\"base64\","
		"\"contentMediaType\":\"application/octet-stream\","
		"\"exclusiveMinimum\":1,"
		"\"exclusiveMaximum\":9,"
		"\"multipleOf\":2"
		"}"
		"}"
		"}";
	state.uiHintsJson = "{}";
	state.version = "schema-v1";
	state.generatedAt = "2026-04-12T00:00:00Z";

	const auto lookup = service.Lookup(state, "sample");
	REQUIRE(lookup.has_value());
	REQUIRE(lookup->schemaJson.find("\"$id\":\"sample-id\"") != std::string::npos);
	REQUIRE(lookup->schemaJson.find("\"$schema\"") != std::string::npos);
	REQUIRE(lookup->schemaJson.find("\"contentEncoding\":\"base64\"") != std::string::npos);
	REQUIRE(lookup->schemaJson.find("\"exclusiveMinimum\":1") != std::string::npos);
	REQUIRE(lookup->schemaJson.find("\"multipleOf\":2") != std::string::npos);
}

TEST_CASE("ConfigSchemaService merges plugin extension schema metadata", "[schema][merge][plugin]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;
	auto entry = MakeSkill("voice-call", { "plugins.entries.voice-call.config.apiKey" });
	entry.pluginConfigSchemaJson =
		"{"
		"\"type\":\"object\","
		"\"properties\":{"
		"\"apiKey\":{\"type\":\"string\",\"minLength\":8},"
		"\"endpoint\":{\"type\":\"string\"}"
		"}"
		"}";
	skills.entries.push_back(entry);

	const auto state = service.BuildGatewayState(MakeConfig({ L"discord" }), skills);
	const auto lookup = service.Lookup(state, "plugins.entries.voice-call.config");
	REQUIRE(lookup.has_value());

	const auto hasApiKeyChild = std::find_if(
		lookup->children.begin(),
		lookup->children.end(),
		[](const blazeclaw::gateway::ConfigSchemaGatewayChild& child) {
			return child.key == "apiKey";
		}) != lookup->children.end();
	REQUIRE(hasApiKeyChild);
}

TEST_CASE("ConfigSchemaService merges plugin extension ui-hints metadata", "[schema][merge][hints][plugin]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;
	auto entry = MakeSkill("voice-call", { "plugins.entries.voice-call.config.apiKey" });
	entry.pluginConfigUiHintsJson =
		"{"
		"\"apiKey\":{\"label\":\"Voice API Key\",\"sensitive\":true},"
		"\"endpoint\":{\"label\":\"Voice Endpoint\"}"
		"}";
	skills.entries.push_back(entry);

	const auto state = service.BuildGatewayState(MakeConfig({ L"discord" }), skills);
	const auto lookup = service.Lookup(state, "plugins.entries.voice-call.config.apiKey");
	REQUIRE(lookup.has_value());
	REQUIRE(lookup->hint.has_value());
	REQUIRE(lookup->hint->label == "Voice API Key");
	REQUIRE(lookup->hint->sensitive);
}

TEST_CASE("ConfigSchemaService merges channel extension schema and hints metadata", "[schema][merge][channel]") {
	blazeclaw::core::ConfigSchemaService service;
	blazeclaw::gateway::SkillsCatalogGatewayState skills;
	auto entry = MakeSkill("bridge", { "channels.discord.token" });
	entry.channelConfigSchemasJson =
		"{"
		"\"discord\":{"
		"\"type\":\"object\","
		"\"properties\":{"
		"\"botToken\":{\"type\":\"string\"}"
		"}"
		"}"
		"}";
	entry.channelConfigUiHintsJson =
		"{"
		"\"discord\":{"
		"\"botToken\":{\"label\":\"Discord Bot Token\",\"sensitive\":true}"
		"}"
		"}";
	skills.entries.push_back(entry);

	const auto state = service.BuildGatewayState(MakeConfig({ L"discord" }), skills);
	const auto schemaLookup = service.Lookup(state, "channels.discord");
	REQUIRE(schemaLookup.has_value());
	const auto hasBotTokenChild = std::find_if(
		schemaLookup->children.begin(),
		schemaLookup->children.end(),
		[](const blazeclaw::gateway::ConfigSchemaGatewayChild& child) {
			return child.key == "botToken";
		}) != schemaLookup->children.end();
	REQUIRE(hasBotTokenChild);

	const auto hintLookup = service.Lookup(state, "channels.discord.botToken");
	REQUIRE(hintLookup.has_value());
	REQUIRE(hintLookup->hint.has_value());
	REQUIRE(hintLookup->hint->label == "Discord Bot Token");
	REQUIRE(hintLookup->hint->sensitive);
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
