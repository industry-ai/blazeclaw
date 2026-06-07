#include "config/ConfigLoader.h"
#include "config/ConfigModels.h"
#include "config/ConfigLoaderSkillEntryNormalizationHelpers.h"
#include "config/ConfigLoaderSpeechNormalizationHelpers.h"

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
		out << L"speech.hotwords=[\"火龙虾\", \"火龙虾\", \" 炎图科技 \"]\n";
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
	REQUIRE(config.speechRecognition.hotwords[1] == L"炎图科技");

	std::filesystem::remove_all(root);
}

TEST_CASE("ConfigLoader parses speech CUDA DLL loading settings", "[config][speech][cuda]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_speech_cuda_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "speech-cuda.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"speech.cuda.dll_preload_enabled=true\n";
		out << L"speech.cuda.dll_directories=D:\\nVidia\\bin;D:\\nVidia\\cudnn9.20\\bin\\12.9\\x64\n";
		out << L"speech.cuda.dll_preload_names=cublas64_12.dll,cublasLt64_12.dll,cudnn64_9.dll\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.speechRecognition.cudaDllPreloadEnabled);
	REQUIRE(config.speechRecognition.cudaDllDirectories.size() == 2);
	REQUIRE(config.speechRecognition.cudaDllDirectories[0] == L"D:\\nVidia\\bin");
	REQUIRE(config.speechRecognition.cudaDllDirectories[1] == L"D:\\nVidia\\cudnn9.20\\bin\\12.9\\x64");
	REQUIRE(config.speechRecognition.cudaDllPreloadNames.size() == 3);
	REQUIRE(config.speechRecognition.cudaDllPreloadNames[0] == L"cublas64_12.dll");
	REQUIRE(config.speechRecognition.cudaDllPreloadNames[1] == L"cublasLt64_12.dll");
	REQUIRE(config.speechRecognition.cudaDllPreloadNames[2] == L"cudnn64_9.dll");

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
	REQUIRE(it->second.config.contains(L"timeoutms"));
	REQUIRE(it->second.config.at(L"timeoutms") == L"3000");
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
	"ConfigLoader normalizes speech latency aliases with deterministic precedence",
	"[config][speech][streaming][alias]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_streaming_alias_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto aliasWinsPath = root / "streaming-alias-wins.conf";
	{
		std::wofstream out(aliasWinsPath);
		REQUIRE(out.is_open());
		out << L"speech.streaming.latency_profile=balanced\n";
		out << L"speech.streaming.latency_profile=low-latency\n";
	}

	blazeclaw::config::AppConfig aliasWinsConfig;
	REQUIRE(loader.LoadFromFile(aliasWinsPath.wstring(), aliasWinsConfig));
	REQUIRE(aliasWinsConfig.speechRecognition.streamingLatencyProfile == L"low_latency");

	const auto canonicalWinsPath = root / "streaming-canonical-wins.conf";
	{
		std::wofstream out(canonicalWinsPath);
		REQUIRE(out.is_open());
		out << L"speech.streaming.latency_profile=low-latency\n";
		out << L"speech.streaming.latency_profile=balanced\n";
	}

	blazeclaw::config::AppConfig canonicalWinsConfig;
	REQUIRE(loader.LoadFromFile(canonicalWinsPath.wstring(), canonicalWinsConfig));
	REQUIRE(canonicalWinsConfig.speechRecognition.streamingLatencyProfile == L"balanced");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader normalizes skills config key aliases with last-write precedence",
	"[config][skills][entries][normalize][precedence]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_skills_key_alias_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto aliasWinsPath = root / "skills-key-alias-wins.conf";
	{
		std::wofstream out(aliasWinsPath);
		REQUIRE(out.is_open());
		out << L"skills.entries.demo.config.timeoutms=1000\n";
		out << L"skills.entries.demo.config.Timeout Ms=3000\n";
	}

	blazeclaw::config::AppConfig aliasWinsConfig;
	REQUIRE(loader.LoadFromFile(aliasWinsPath.wstring(), aliasWinsConfig));
	const auto aliasWinsIt = aliasWinsConfig.skills.entries.find(L"demo");
	REQUIRE(aliasWinsIt != aliasWinsConfig.skills.entries.end());
	REQUIRE(aliasWinsIt->second.config.size() == 1);
	REQUIRE(aliasWinsIt->second.config.contains(L"timeoutms"));
	REQUIRE(aliasWinsIt->second.config.at(L"timeoutms") == L"3000");

	const auto canonicalWinsPath = root / "skills-key-canonical-wins.conf";
	{
		std::wofstream out(canonicalWinsPath);
		REQUIRE(out.is_open());
		out << L"skills.entries.demo.config.Timeout Ms=3000\n";
		out << L"skills.entries.demo.config.timeoutms=1000\n";
	}

	blazeclaw::config::AppConfig canonicalWinsConfig;
	REQUIRE(loader.LoadFromFile(canonicalWinsPath.wstring(), canonicalWinsConfig));
	const auto canonicalWinsIt = canonicalWinsConfig.skills.entries.find(L"demo");
	REQUIRE(canonicalWinsIt != canonicalWinsConfig.skills.entries.end());
	REQUIRE(canonicalWinsIt->second.config.size() == 1);
	REQUIRE(canonicalWinsIt->second.config.contains(L"timeoutms"));
	REQUIRE(canonicalWinsIt->second.config.at(L"timeoutms") == L"1000");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader Priority 1 contract: speech hotwords_enabled value extraction offset",
	"[config][priority1][speech][hotwords]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_priority1_hotwords_enabled_" +
			std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "priority1-hotwords-enabled.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"speech.hotwords_enabled=false\n";
		out << L"speech.hotwords=[\"demo\"]\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));
	REQUIRE_FALSE(config.speechRecognition.hotwordsEnabled);
	REQUIRE(config.speechRecognition.hotwords.size() == 1);
	REQUIRE(config.speechRecognition.hotwords[0] == L"demo");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader Priority 1 contract: skills entry config keys normalize to lowercase",
	"[config][priority1][skills][entries][normalize]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_priority1_skills_keys_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "priority1-skills-keys.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"skills.entries.demo.config.timeoutMs=3000\n";
		out << L"skills.entries.demo.config.Retry-Count=2\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	const auto it = config.skills.entries.find(L"demo");
	REQUIRE(it != config.skills.entries.end());
	REQUIRE(it->second.config.contains(L"timeoutms"));
	REQUIRE(it->second.config.at(L"timeoutms") == L"3000");
	REQUIRE(it->second.config.contains(L"retry-count"));
	REQUIRE(it->second.config.at(L"retry-count") == L"2");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader Priority 1 helper API: skill entry key normalization symbol contract",
	"[config][priority1][helper][skill-entry]") {
	REQUIRE(
		blazeclaw::config::skill_entry_normalization::NormalizeSkillEntryConfigKey(
			L"Timeout Ms") == L"timeoutms");
	REQUIRE(
		blazeclaw::config::skill_entry_normalization::NormalizeSkillEntryConfigKey(
			L"$$$") == L"");
	REQUIRE(
		blazeclaw::config::skill_entry_normalization::NormalizeSkillEntryConfigKey(
			L"retry-count") == L"retry-count");
}

TEST_CASE(
	"ConfigLoader Priority 1 helper API: speech hotword parser symbol contract",
	"[config][priority1][helper][speech][hotwords]") {
	const auto parsed = blazeclaw::config::speech_normalization::ParseSpeechHotwordsValue(
		L"[\" 火龙虾 \", \" 云深科技 \"]");
	REQUIRE(parsed.size() == 2);
	REQUIRE(parsed[0] == L"火龙虾");
	REQUIRE(parsed[1] == L"云深科技");
}

TEST_CASE(
	"ConfigLoader applies deterministic precedence for repeated models.alias mappings",
	"[config][models][alias][mapping]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_model_alias_mapping_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "models-alias-mapping.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"models.alias.fast=gpt-4o\n";
		out << L"models.alias.fast=gpt-4.1\n";
		out << L"models.alias.fast-mini=gpt-4o-mini\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));
	REQUIRE(config.models.aliases.size() == 2);
	REQUIRE(config.models.aliases.contains(L"fast"));
	REQUIRE(config.models.aliases.at(L"fast") == L"gpt-4.1");
	REQUIRE(config.models.aliases.contains(L"fast-mini"));
	REQUIRE(config.models.aliases.at(L"fast-mini") == L"gpt-4o-mini");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader applies stable fallbacks for invalid bool numeric and string forms",
	"[config][normalize][edge][fallback]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_edge_fallback_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "edge-fallback.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"agent.streaming=not-a-bool\n";
		out << L"acp.enabled=not-a-bool\n";
		out << L"embedded.enabled=not-a-bool\n";
		out << L"chat.localModel.provider=unsupported-provider\n";
		out << L"embedded.orchestrationPath=unsupported-mode\n";
		out << L"speech.streaming.latency_profile=unexpected-profile\n";
		out << L"speech.model_variant=unexpected-variant\n";
		out << L"speech.runtime_hot_mode=unexpected-mode\n";
		out << L"speech.threads=not-a-number\n";
		out << L"speech.sample_rate=not-a-number\n";
		out << L"embeddings.dimension=not-a-number\n";
		out << L"chat.localModel.maxTokens=not-a-number\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.agent.enableStreaming);
	REQUIRE_FALSE(config.acp.enabled);
	REQUIRE(config.embedded.enabled);
	REQUIRE(config.localModel.provider == L"onnx");
	REQUIRE(config.embedded.orchestrationPath == L"dynamic_task_delta");
	REQUIRE(config.speechRecognition.streamingLatencyProfile == L"balanced");
	REQUIRE(config.speechRecognition.modelVariant == L"auto");
	REQUIRE(config.speechRecognition.runtimeHotMode == L"always_online");
	REQUIRE(config.speechRecognition.threads == 4);
	REQUIRE(config.speechRecognition.sampleRate == 16000);
	REQUIRE(config.embeddings.dimension == 384);

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader normalizes malformed CSV and list inputs to stable shapes",
	"[config][normalize][edge][csv]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_edge_csv_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "edge-csv.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"speech.allowed_languages=en,, ; zh-CN ; ; en\n";
		out << L"speech.cuda.dll_preload_names=cublas64_12.dll, , ; cudnn64_9.dll;;\n";
		out << L"email.policy.default.backends=Himalaya, , ; imap-smtp-email ; ; himalaya\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.speechRecognition.allowedLanguages.size() == 2);
	REQUIRE(config.speechRecognition.allowedLanguages[0] == L"en");
	REQUIRE(config.speechRecognition.allowedLanguages[1] == L"zh-cn");
	REQUIRE(config.speechRecognition.cudaDllPreloadNames.size() == 2);
	REQUIRE(config.speechRecognition.cudaDllPreloadNames[0] == L"cublas64_12.dll");
	REQUIRE(config.speechRecognition.cudaDllPreloadNames[1] == L"cudnn64_9.dll");
	REQUIRE(config.email.policy.defaults.backends.size() == 2);
	REQUIRE(config.email.policy.defaults.backends[0] == L"himalaya");
	REQUIRE(config.email.policy.defaults.backends[1] == L"imap-smtp-email");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader handles quoted speech hotwords input with stable non-empty output",
	"[config][speech][hotwords][edge][quotes]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_edge_hotwords_quotes_" + std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "edge-hotwords-quotes.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"speech.hotwords_max_count=8\n";
		out << L"speech.hotwords=[\" 火龙虾 \", \" 云深科技 \"]\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.speechRecognition.hotwords.size() == 2);
	REQUIRE(config.speechRecognition.hotwords[0] == L"火龙虾");
	REQUIRE(config.speechRecognition.hotwords[1] == L"云深科技");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader regression snapshot: speech streaming normalization remains stable",
	"[config][snapshot][speech][streaming]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_snapshot_speech_streaming_" +
			std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "snapshot-speech-streaming.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"# snapshot\n";
		out << L"speech.streaming.chunk_ms=90\n";
		out << L"speech.streaming.lookback_ms=9999\n";
		out << L"speech.streaming.latency_profile=low-latency\n";
		out << L"speech.streaming.preview_chunk_ms=2000\n";
		out << L"speech.streaming.preview_lookback_ms=5000\n";
		out << L"speech.chunk_ms=100\n";
		out << L"speech.overlap_ms=9999\n";
		out << L"speech.execution_mode=PARALLEL\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.speechRecognition.streamingLatencyProfile == L"low_latency");
	REQUIRE(config.speechRecognition.streamingPreviewChunkMs == 1500);
	REQUIRE(config.speechRecognition.streamingPreviewLookbackMs == 1499);
	REQUIRE(config.speechRecognition.chunkMs == 320);
	REQUIRE(config.speechRecognition.overlapMs == 319);
	REQUIRE(config.speechRecognition.executionMode == L"parallel");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader regression snapshot: email fallback policy profile mapping remains stable",
	"[config][snapshot][email][policy]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_snapshot_email_policy_" +
			std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "snapshot-email-policy.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"# snapshot\n";
		out << L"email.policy.default.backends=IMAP-SMTP-EMAIL,Himalaya,himalaya\n";
		out << L"email.policy.default.actions.unavailable=retry_then_continue\n";
		out << L"email.policy.default.actions.authError=bad-value\n";
		out << L"email.policy.default.actions.execError=continue\n";
		out << L"email.policy.default.retry.maxAttempts=9\n";
		out << L"email.policy.default.retry.retryDelayMs=500000\n";
		out << L"email.policy.default.approval.requiresApproval=no\n";
		out << L"email.policy.default.approval.tokenTtlMinutes=0\n";
		out << L"email.policy.capability.SMTP.actions.authError=continue\n";
		out << L"email.policy.tool.Send_Email.retry.retryDelayMs=42\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.email.policy.defaults.id == L"default");
	REQUIRE(config.email.policy.defaults.backends.size() == 2);
	REQUIRE(config.email.policy.defaults.backends[0] == L"himalaya");
	REQUIRE(config.email.policy.defaults.backends[1] == L"imap-smtp-email");
	REQUIRE(config.email.policy.defaults.actions.unavailable == L"retry_then_continue");
	REQUIRE(config.email.policy.defaults.actions.authError == L"stop");
	REQUIRE(config.email.policy.defaults.actions.execError == L"continue");
	REQUIRE(config.email.policy.defaults.retry.maxAttempts == 8);
	REQUIRE(config.email.policy.defaults.retry.retryDelayMs == 300000);
	REQUIRE_FALSE(config.email.policy.defaults.approval.requiresApproval);
	REQUIRE(config.email.policy.defaults.approval.tokenTtlMinutes == 60);

	REQUIRE(config.email.policy.capability.contains(L"smtp"));
	const auto& smtpProfile = config.email.policy.capability.at(L"smtp");
	REQUIRE(smtpProfile.id == L"smtp");
	REQUIRE(smtpProfile.backends == config.email.policy.defaults.backends);
	REQUIRE(smtpProfile.actions.authError == L"continue");
	REQUIRE(smtpProfile.retry.maxAttempts == 1);

	REQUIRE(config.email.policy.tool.contains(L"send_email"));
	const auto& sendEmailProfile = config.email.policy.tool.at(L"send_email");
	REQUIRE(sendEmailProfile.id == L"send_email");
	REQUIRE(sendEmailProfile.retry.retryDelayMs == 42);
	REQUIRE(sendEmailProfile.backends == config.email.policy.defaults.backends);

	auto resolved = blazeclaw::config::ResolveEmailFallbackPolicy(
		config.email.policy,
		L"send_email",
		L"smtp");
	REQUIRE(resolved.profileId == L"send_email");
	REQUIRE(resolved.retryDelayMs == 42);
	REQUIRE(resolved.onAuthError == L"stop");

	resolved = blazeclaw::config::ResolveEmailFallbackPolicy(
		config.email.policy,
		L"unknown_tool",
		L"smtp");
	REQUIRE(resolved.profileId == L"smtp");
	REQUIRE(resolved.onAuthError == L"continue");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"ConfigLoader regression snapshot: skill filter and backend normalization remain stable",
	"[config][snapshot][skills][filter][backend]") {
	blazeclaw::config::ConfigLoader loader;

	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_config_loader_snapshot_skills_filters_" +
			std::to_string(std::rand()));
	std::filesystem::create_directories(root);

	const auto configPath = root / "snapshot-skills-filter-backend.conf";
	{
		std::wofstream out(configPath);
		REQUIRE(out.is_open());
		out << L"# snapshot\n";
		out << L"agents.defaults.skills=beta-skill,alpha-skill,beta-skill\n";
		out << L"agents.list.worker.skills=delta-skill,alpha-skill,delta-skill\n";
		out << L"skills.install.nodeManager=YARN\n";
		out << L"skills.remoteEligibility.platform=Windows\n";
		out << L"skills.remoteEligibility.platform=win32\n";
		out << L"skills.remoteEligibility.platform=linux\n";
		out << L"email.policy.default.backends=IMAP-SMTP-EMAIL,Himalaya,IMAP-SMTP-EMAIL\n";
	}

	blazeclaw::config::AppConfig config;
	REQUIRE(loader.LoadFromFile(configPath.wstring(), config));

	REQUIRE(config.agents.defaults.skills.has_value());
	REQUIRE(config.agents.defaults.skills->size() == 2);
	REQUIRE(config.agents.defaults.skills->at(0) == L"alpha-skill");
	REQUIRE(config.agents.defaults.skills->at(1) == L"beta-skill");

	REQUIRE(config.agents.entries.contains(L"worker"));
	const auto& workerSkills = config.agents.entries.at(L"worker").skills;
	REQUIRE(workerSkills.has_value());
	REQUIRE(workerSkills->size() == 2);
	REQUIRE(workerSkills->at(0) == L"alpha-skill");
	REQUIRE(workerSkills->at(1) == L"delta-skill");

	REQUIRE(config.skills.install.nodeManager == L"yarn");
	REQUIRE_FALSE(config.skills.remoteEligibility.platforms.empty());

	REQUIRE(config.email.policy.defaults.backends.size() == 2);
	REQUIRE(config.email.policy.defaults.backends[0] == L"himalaya");
	REQUIRE(config.email.policy.defaults.backends[1] == L"imap-smtp-email");

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
