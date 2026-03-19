#pragma once

#include "pch.h"

#include <map>
#include <optional>

namespace blazeclaw::config {

struct GatewayConfig {
  std::wstring bindAddress = L"127.0.0.1";
  std::uint16_t port = 56789;
};

struct AgentConfig {
  std::wstring model = L"default";
  bool enableStreaming = true;
};

struct AgentIdentityConfig {
  std::wstring name;
  std::wstring emoji;
  std::wstring theme;
  std::wstring avatar;
};

struct AgentSubagentsConfig {
  std::uint32_t maxDepth = 3;
  std::vector<std::wstring> allowAgents;
};

struct AgentEntryConfig {
  std::wstring id;
  std::wstring name;
  std::wstring workspace;
  std::wstring agentDir;
  std::wstring model;
  bool isDefault = false;
  AgentIdentityConfig identity;
  AgentSubagentsConfig subagents;
};

struct AgentsDefaultsConfig {
  std::wstring agentId = L"default";
  std::wstring workspace;
  std::wstring workspaceRoot;
  std::wstring agentDirRoot;
  std::wstring model;
};

struct AgentsConfig {
  AgentsDefaultsConfig defaults;
  std::map<std::wstring, AgentEntryConfig> entries;
};

struct SkillEntryConfig {
  std::optional<bool> enabled;
  std::wstring apiKey;
  std::map<std::wstring, std::wstring> env;
};

struct SkillsLoadConfig {
  bool watch = true;
  std::uint32_t watchDebounceMs = 250;
  std::vector<std::wstring> extraDirs;
};

struct SkillsLimitsConfig {
  std::uint32_t maxCandidatesPerRoot = 300;
  std::uint32_t maxSkillsLoadedPerSource = 200;
  std::uint32_t maxSkillsInPrompt = 150;
  std::uint32_t maxSkillsPromptChars = 30000;
  std::uint32_t maxSkillFileBytes = 256000;
};

struct SkillsInstallConfig {
  bool preferBrew = true;
  std::wstring nodeManager = L"npm";
};

struct SkillsConfig {
  std::map<std::wstring, SkillEntryConfig> entries;
  std::vector<std::wstring> allowBundled;
  SkillsLoadConfig load;
  SkillsLimitsConfig limits;
  SkillsInstallConfig install;
};

struct AppConfig {
  GatewayConfig gateway;
  AgentConfig agent;
  AgentsConfig agents;
  SkillsConfig skills;
  std::vector<std::wstring> enabledChannels;
};

} // namespace blazeclaw::config
