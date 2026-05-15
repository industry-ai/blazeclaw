#include "config/ConfigLoader.h"
#include "config/ConfigModels.h"

#include <catch2/catch_all.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include "app/ChatUiStartupResolver.h"

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

TEST_CASE("ConfigLoader parses and normalizes speech hotwords policy", "[config][speech][hotwords]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_speech_hotwords_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "speech-hotwords.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"speech.hotwords_enabled=true\n";
		out << L"speech.hotwords=[\"火龙虾\", \"火龙虾\", \" 云深科技 \"]\n";
		out << L"speech.hotwords_max_count=2\n";
		out << L"speech.hotwords_apply_stage=decoder_init_and_step\n";
		out << L"speech.hotwords_debug_dump_prompt=true\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.speechRecognition.hotwordsEnabled);
	REQUIRE(config.speechRecognition.hotwordsMaxCount == 2);
	REQUIRE(config.speechRecognition.hotwordsApplyStage == L"decoder_init_and_step");
	REQUIRE(config.speechRecognition.hotwordsDebugDumpPrompt);
	REQUIRE(config.speechRecognition.hotwords.size() == 2);
	REQUIRE(config.speechRecognition.hotwords[0] == L"火龙虾");
	REQUIRE(config.speechRecognition.hotwords[1] == L"云深科技");

	std::filesystem::remove_all(root);
}

TEST_CASE("ConfigLoader normalizes and validates skills entry config keys", "[config][skills][entries][normalize]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_skills_entries_normalize_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "skills-entries-normalize.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"skills.entries.demo.config.Timeout Ms=3000\n";
		out << L"skills.entries.demo.config.$$$=bad\n";
		out << L"skills.entries.demo.config.retry-count=3\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	const auto it = config.skills.entries.find(L"demo");
	REQUIRE(it != config.skills.entries.end());
	REQUIRE(it->second.config.contains(L"timeoutms"));
	REQUIRE(it->second.config.at(L"timeoutms") == L"3000");
	REQUIRE(it->second.config.contains(L"retry-count"));
	REQUIRE(it->second.config.at(L"retry-count") == L"3");
	REQUIRE_FALSE(it->second.config.contains(L"$$$"));

	REQUIRE(config.skills.entryConfigRawCount == 3);
	REQUIRE(config.skills.entryConfigNormalizedCount >= 1);
	REQUIRE(config.skills.entryConfigMalformedCount == 1);

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

TEST_CASE("ConfigLoader parses openclaw-original skills policy", "[config][skills][openclaw-original]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_openclaw_original_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "openclaw-original.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"skills.openclawOriginal.enabled=false\n";
		out << L"skills.openclawOriginal.autoImportTools=false\n";
		out << L"skills.openclawOriginal.sourceDir=skills-openclaw-original\n";
		out << L"skills.openclawOriginal.promoteToManaged=false\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE_FALSE(config.skills.openclawOriginal.enabled);
	REQUIRE_FALSE(config.skills.openclawOriginal.autoImportTools);
	REQUIRE(config.skills.openclawOriginal.sourceDir == L"skills-openclaw-original");
	REQUIRE_FALSE(config.skills.openclawOriginal.promoteToManaged);

	std::filesystem::remove_all(root);
}

TEST_CASE("ConfigLoader defaults openclaw-original policy", "[config][skills][openclaw-original][defaults]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_openclaw_original_defaults_" +
			std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "openclaw-original-defaults.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"skills.allowBundled=demo\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.skills.openclawOriginal.enabled);
	REQUIRE(config.skills.openclawOriginal.autoImportTools);
	REQUIRE(config.skills.openclawOriginal.sourceDir ==
		L"blazeclaw/skills-openclaw-original");
	REQUIRE(config.skills.openclawOriginal.promoteToManaged);

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

TEST_CASE(
	"ConfigLoader S1: LoadFromFile and BuildGatewayStartupConfigFileSnapshot share stable digest",
	"[config][loader][s1]")
{
	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_s1_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto path = (root / "snap.conf").wstring();
	{
		std::wofstream out(path);
		REQUIRE(out.is_open());
		out << L"gateway.port=12345\n";
	}

	blazeclaw::config::AppConfig cfg;
	blazeclaw::config::ConfigLoader loader;
	blazeclaw::config::GatewayStartupConfigFileSnapshot fromLoad;
	REQUIRE(loader.LoadFromFile(path, cfg, &fromLoad));
	REQUIRE(fromLoad.fileExisted);
	REQUIRE_FALSE(fromLoad.contentDigest.empty());
	REQUIRE(fromLoad.contentDigest.rfind("u64:", 0) == 0);

	blazeclaw::config::GatewayStartupConfigFileSnapshot direct;
	blazeclaw::config::BuildGatewayStartupConfigFileSnapshot(
		path,
		{ 99, 100 },
		direct);
	REQUIRE(direct.contentDigest == fromLoad.contentDigest);
	REQUIRE(direct.internalWriteHashesAtRecord.size() == 2u);
	REQUIRE(direct.internalWriteHashesAtRecord[0] == 99u);

	std::filesystem::remove_all(root);
}

namespace {

std::filesystem::path MakeChatUiResolverTempRoot(const std::string& suffix)
{
	return std::filesystem::temp_directory_path() /
		("blazeclaw_chat_ui_resolver_" + suffix + "_" + std::to_string(std::rand()));
}

void EnsureChatUiResolverFile(const std::filesystem::path& path)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path.string(), std::ios::binary);
	REQUIRE(out.is_open());
	out << "<!doctype html><title>test</title>";
}

} // namespace

TEST_CASE("Chat UI resolver finds deterministic source path from repo root in dev preference", "[chat-ui][resolver][repo-root]")
{
	namespace resolver = blazeclaw::app::chatui;

	const auto root = MakeChatUiResolverTempRoot("repo_root");
	const auto sourceIndex = root / "blazeclaw" / "BlazeClawMfc" / "web" / "chat" / "index.html";
	const auto distIndex = root / "blazeclaw" / "BlazeClawMfc" / "web" / "chat" / "dist" / "index.html";
	EnsureChatUiResolverFile(sourceIndex);
	EnsureChatUiResolverFile(distIndex);

	const auto found = resolver::FindChatUiIndex(root, resolver::StartupPreference::PreferSource);
	REQUIRE(found.has_value());
	REQUIRE(found->selectedPath == sourceIndex);
	REQUIRE(found->selectedDist == false);

	std::filesystem::remove_all(root);
}

TEST_CASE("Chat UI resolver finds deterministic source path from project root in dev preference", "[chat-ui][resolver][project-root]")
{
	namespace resolver = blazeclaw::app::chatui;

	const auto workspaceRoot = MakeChatUiResolverTempRoot("project_root");
	const auto projectRoot = workspaceRoot / "BlazeClawMfc";
	const auto sourceIndex = projectRoot / "web" / "chat" / "index.html";
	const auto distIndex = projectRoot / "web" / "chat" / "dist" / "index.html";
	EnsureChatUiResolverFile(sourceIndex);
	EnsureChatUiResolverFile(distIndex);

	const auto found = resolver::FindChatUiIndex(projectRoot, resolver::StartupPreference::PreferSource);
	REQUIRE(found.has_value());
	REQUIRE(found->selectedPath == sourceIndex);
	REQUIRE(found->selectedDist == false);

	std::filesystem::remove_all(workspaceRoot);
}

TEST_CASE("Chat UI resolver from module/bin parent finds same intended target in dev preference", "[chat-ui][resolver][module-bin]")
{
	namespace resolver = blazeclaw::app::chatui;

	const auto root = MakeChatUiResolverTempRoot("module_bin");
	const auto moduleDir = root / "bin" / "Debug";
	const auto sourceIndex = root / "BlazeClawMfc" / "web" / "chat" / "index.html";
	const auto distIndex = root / "BlazeClawMfc" / "web" / "chat" / "dist" / "index.html";
	EnsureChatUiResolverFile(sourceIndex);
	EnsureChatUiResolverFile(distIndex);
	std::filesystem::create_directories(moduleDir);

	const auto roots = resolver::BuildOrderedRoots(moduleDir, root);
	REQUIRE_FALSE(roots.empty());

	bool foundSource = false;
	for (const auto& candidateRoot : roots)
	{
		const auto found = resolver::FindChatUiIndex(candidateRoot, resolver::StartupPreference::PreferSource);
		if (found.has_value())
		{
			REQUIRE(found->selectedPath == sourceIndex);
			REQUIRE(found->selectedDist == false);
			foundSource = true;
			break;
		}
	}

	REQUIRE(foundSource);
	std::filesystem::remove_all(root);
}

TEST_CASE("Chat UI resolver prefers dist in dist preference mode", "[chat-ui][resolver][dist-mode]")
{
	namespace resolver = blazeclaw::app::chatui;

	const auto root = MakeChatUiResolverTempRoot("dist_mode");
	const auto sourceIndex = root / "BlazeClawMfc" / "web" / "chat" / "index.html";
	const auto distIndex = root / "BlazeClawMfc" / "web" / "chat" / "dist" / "index.html";
	EnsureChatUiResolverFile(sourceIndex);
	EnsureChatUiResolverFile(distIndex);

	const auto found = resolver::FindChatUiIndex(root, resolver::StartupPreference::PreferDist);
	REQUIRE(found.has_value());
	REQUIRE(found->selectedPath == distIndex);
	REQUIRE(found->selectedDist == true);

	std::filesystem::remove_all(root);
}
