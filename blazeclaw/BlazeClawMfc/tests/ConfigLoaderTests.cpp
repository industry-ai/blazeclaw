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

TEST_CASE("ConfigLoader parses skills entries env and config maps", "[config][skills][entries]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_skills_entries_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "skills-entries.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"skills.entries.demo.enabled=true\n";
		out << L"skills.entries.demo.apiKey=demo-secret\n";
		out << L"skills.entries.demo.env.API_BASE=https://api.example.test\n";
		out << L"skills.entries.demo.config.timeoutMs=3000\n";
		out << L"skills.entries.demo.config.retries=2\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	const auto it = config.skills.entries.find(L"demo");
	REQUIRE(it != config.skills.entries.end());
	REQUIRE(it->second.enabled.has_value());
	REQUIRE(it->second.enabled.value());
	REQUIRE(it->second.apiKey == L"demo-secret");
	REQUIRE(it->second.env.contains(L"API_BASE"));
	REQUIRE(it->second.env.at(L"API_BASE") == L"https://api.example.test");
	REQUIRE(it->second.config.contains(L"timeoutMs"));
	REQUIRE(it->second.config.at(L"timeoutMs") == L"3000");
	REQUIRE(it->second.config.contains(L"retries"));
	REQUIRE(it->second.config.at(L"retries") == L"2");

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
