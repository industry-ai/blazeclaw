#include "config/ConfigLoader.h"

#include <catch2/catch_all.hpp>
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
