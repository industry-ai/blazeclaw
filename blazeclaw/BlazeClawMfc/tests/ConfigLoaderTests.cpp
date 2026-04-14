#include "config/ConfigLoader.h"

#include <catch2/catch_all.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>

TEST_CASE("ConfigLoader parses embedded.orchestrationPath values", "[config][embedded][orchestration]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto runtimeConfigPath = root / "runtime.conf";
	{
		std::wofstream out(runtimeConfigPath);
		REQUIRE(out.is_open());
		out << L"embedded.orchestrationPath=runtime_orchestration\n";
	}

	blazeclaw::config::AppConfig runtimeConfig;
	REQUIRE(loader.LoadFromFile(runtimeConfigPath.wstring(), runtimeConfig));
	REQUIRE(runtimeConfig.embedded.orchestrationPath == L"runtime_orchestration");

	const auto fallbackConfigPath = root / "fallback.conf";
	{
		std::wofstream out(fallbackConfigPath);
		REQUIRE(out.is_open());
		out << L"embedded.orchestrationPath=unexpected_mode\n";
	}

	blazeclaw::config::AppConfig fallbackConfig;
	REQUIRE(loader.LoadFromFile(fallbackConfigPath.wstring(), fallbackConfig));
	REQUIRE(fallbackConfig.embedded.orchestrationPath == L"dynamic_task_delta");

	std::filesystem::remove_all(root);
}

TEST_CASE("ConfigLoader parses agent/default skills allowlist semantics", "[config][agents][skills]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_agents_skills_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "agents-skills.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"agents.defaults.skills=alpha-skill,hidden-skill\n";
		out << L"agents.list.alpha.skills=\n";
		out << L"agents.list.beta.skills=beta-skill\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.agents.defaults.skills.has_value());
	REQUIRE(config.agents.defaults.skills->size() == 2);
	REQUIRE(std::find(
		config.agents.defaults.skills->begin(),
		config.agents.defaults.skills->end(),
		L"alpha-skill") != config.agents.defaults.skills->end());

	const auto alphaIt = config.agents.entries.find(L"alpha");
	REQUIRE(alphaIt != config.agents.entries.end());
	REQUIRE(alphaIt->second.skills.has_value());
	REQUIRE(alphaIt->second.skills->empty());

	const auto betaIt = config.agents.entries.find(L"beta");
	REQUIRE(betaIt != config.agents.entries.end());
	REQUIRE(betaIt->second.skills.has_value());
	REQUIRE(betaIt->second.skills->size() == 1);
	REQUIRE(betaIt->second.skills->front() == L"beta-skill");

	std::filesystem::remove_all(root);
}
