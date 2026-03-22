#pragma once

#include "../app/pch.h"

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

struct AgentToolsConfig {
  std::wstring profile = L"full";
  std::vector<std::wstring> allow;
  std::vector<std::wstring> deny;
  std::vector<std::wstring> ownerOnly;
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
  AgentToolsConfig tools;
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

struct AcpRuntimeConfig {
  bool enabled = false;
  std::wstring defaultAgent;
  bool allowThreadSpawn = true;
};

struct EmbeddedRuntimeConfig {
  bool enabled = true;
  std::uint32_t runTimeoutMs = 120000;
  std::uint32_t maxQueueDepth = 64;
};

struct ModelsRoutingConfig {
  std::wstring primary = L"default";
  std::wstring fallback = L"reasoner";
  std::vector<std::wstring> allow;
  std::map<std::wstring, std::wstring> aliases;
  std::uint32_t maxFailoverAttempts = 3;
};

struct EmbeddingsConfig {
  bool enabled = false;
  std::wstring provider = L"onnx";
  std::wstring modelPath;
  std::wstring tokenizerPath;
  std::uint32_t dimension = 384;
  std::uint32_t maxSequenceLength = 256;
  bool normalize = true;
  std::uint32_t intraThreads = 0;
  std::uint32_t interThreads = 0;
  std::wstring executionMode = L"sequential";
};

struct AuthProfileEntryConfig {
  std::wstring id;
  std::wstring provider;
  std::wstring credentialRef;
  std::uint32_t cooldownSeconds = 0;
  bool enabled = true;
};

struct AuthProfilesConfig {
  std::vector<std::wstring> order;
  std::map<std::wstring, AuthProfileEntryConfig> entries;
};

struct SandboxConfig {
  bool enabled = false;
  std::wstring runtime = L"host";
  std::wstring workspaceMirrorRoot;
  bool allowHostNetwork = false;
  bool browserEnabled = false;
};

struct TranscriptSafetyConfig {
  bool repairEnabled = true;
  bool writeLockEnabled = true;
  bool redactSecrets = true;
  std::uint32_t maxPayloadChars = 120000;
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

struct ChatUiConfig {
  std::wstring mode = L"webview2";
};

struct LocalModelConfig {
  bool enabled = false;
  std::wstring provider = L"onnx";
  std::wstring rolloutStage = L"dev";
  std::wstring storageRoot = L"models/chat";
  std::wstring version = L"";
  std::wstring modelPath;
  std::wstring modelSha256;
  std::wstring tokenizerPath;
  std::wstring tokenizerSha256;
  std::uint32_t maxTokens = 256;
  double temperature = 0.0;
  bool verboseMetrics = false;
};

struct AppConfig {
  GatewayConfig gateway;
  AgentConfig agent;
  ChatUiConfig chat;
  LocalModelConfig localModel;
  AgentsConfig agents;
  AcpRuntimeConfig acp;
  EmbeddedRuntimeConfig embedded;
  ModelsRoutingConfig models;
  EmbeddingsConfig embeddings;
  AuthProfilesConfig authProfiles;
  SandboxConfig sandbox;
  TranscriptSafetyConfig transcript;
  SkillsConfig skills;
  std::vector<std::wstring> enabledChannels;
};

} // namespace blazeclaw::config
