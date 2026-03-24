#include "pch.h"
#include "ServiceManager.h"

#include "../gateway/GatewayProtocolModels.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <unordered_map>

namespace blazeclaw::core {

namespace {

std::string ToNarrow(const std::wstring& value) {
  std::string output;
  output.reserve(value.size());

  for (const wchar_t ch : value) {
    output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
  }

  return output;
}

std::wstring ToWide(const std::string& value) {
  std::wstring output;
  output.reserve(value.size());
  for (const char ch : value) {
    output.push_back(
        static_cast<wchar_t>(static_cast<unsigned char>(ch)));
  }

  return output;
}

std::uint64_t CurrentEpochMs() {
  const auto now = std::chrono::system_clock::now();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count());
}

std::string BuildAttachmentSummary(
    const std::vector<std::string>& attachmentMimeTypes) {
  std::string summary = "[attachments]";
  if (attachmentMimeTypes.empty()) {
    summary += "\n- image (mimeType=unknown)";
    return summary;
  }

  for (const auto& mimeType : attachmentMimeTypes) {
    summary += "\n- image (mimeType=";
    summary += mimeType.empty() ? "unknown" : mimeType;
    summary += ")";
  }

  return summary;
}

std::string BuildQwen3ChatPrompt(
    const std::string& userMessage,
    const bool hasAttachments,
    const std::vector<std::string>& attachmentMimeTypes,
    const bool strictNoEcho) {
  std::string normalizedUserMessage = userMessage;
  if (normalizedUserMessage.empty()) {
    normalizedUserMessage = "User sent image attachments.";
  }

  if (hasAttachments) {
    normalizedUserMessage += "\n\n";
    normalizedUserMessage += BuildAttachmentSummary(attachmentMimeTypes);
    normalizedUserMessage +=
        "\nInstruction: respond as a text assistant. "
        "Do not repeat the user message.";
  }

  std::string prompt;
  prompt.reserve(normalizedUserMessage.size() + 256);
  prompt += "<|im_start|>system\n";
  prompt += "You are a helpful assistant. Answer the user directly. ";
  prompt += "Do not echo the user prompt verbatim. ";
  if (strictNoEcho) {
    prompt += "Do not quote or repeat the user's wording. ";
    prompt += "Give only the helpful answer content. ";
  }
  prompt += "\n";
  prompt += "<|im_end|>\n";
  prompt += "<|im_start|>user\n";
  prompt += normalizedUserMessage;
  prompt += "\n<|im_end|>\n";
  prompt += "<|im_start|>assistant\n";
  return prompt;
}

std::string BuildLocalModelPrompt(
    const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
  return BuildQwen3ChatPrompt(
      request.message,
      request.hasAttachments,
      request.attachmentMimeTypes,
      false);
}

std::string BuildLocalModelRetryPrompt(
    const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
  return BuildQwen3ChatPrompt(
      request.message,
      request.hasAttachments,
      request.attachmentMimeTypes,
      true);
}

std::string TrimAsciiWhitespace(const std::string& value) {
  const std::size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }

  const std::size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string NormalizeForEchoCheck(const std::string& value) {
  const std::string trimmed = TrimAsciiWhitespace(value);
  std::string normalized;
  normalized.reserve(trimmed.size());

  bool previousWasSpace = false;
  for (const char ch : trimmed) {
    const unsigned char code = static_cast<unsigned char>(ch);
    if (std::isspace(code) != 0) {
      if (!previousWasSpace) {
        normalized.push_back(' ');
        previousWasSpace = true;
      }
      continue;
    }

    if (std::ispunct(code) != 0) {
      continue;
    }

    normalized.push_back(
        static_cast<char>(std::tolower(code)));
    previousWasSpace = false;
  }

  return TrimAsciiWhitespace(normalized);
}

bool IsLikelyEchoResponse(
    const std::string& userMessage,
    const std::string& assistantText) {
  if (userMessage.empty() || assistantText.empty()) {
    return false;
  }

  const std::string normalizedUser =
      NormalizeForEchoCheck(userMessage);
  const std::string normalizedAssistant =
      NormalizeForEchoCheck(assistantText);
  if (normalizedUser.empty() || normalizedAssistant.empty()) {
    return false;
  }

  if (normalizedAssistant == normalizedUser) {
    return true;
  }

  if (normalizedAssistant.size() > normalizedUser.size() &&
      normalizedAssistant.rfind(normalizedUser, 0) == 0) {
    const std::string trailing = TrimAsciiWhitespace(
        normalizedAssistant.substr(normalizedUser.size()));
    return trailing.empty();
  }

  return false;
}

bool IsOneOfChannels(
    const std::vector<std::wstring>& enabledChannels,
    const std::wstring& candidate) {
  for (const auto& channel : enabledChannels) {
    if (_wcsicmp(channel.c_str(), candidate.c_str()) == 0) {
      return true;
    }
  }

  return false;
}

} // namespace

ServiceManager::ServiceManager() = default;

blazeclaw::gateway::SkillsCatalogGatewayState ServiceManager::BuildGatewaySkillsState() const {
  blazeclaw::gateway::SkillsCatalogGatewayState gatewaySkillsState;
  gatewaySkillsState.entries.reserve(m_skillsCatalog.entries.size());

  std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
  for (const auto& eligibility : m_skillsEligibility.entries) {
    eligibilityByName.emplace(eligibility.skillName, eligibility);
  }

  std::unordered_map<std::wstring, SkillsCommandSpec> commandsBySkill;
  for (const auto& command : m_skillsCommands.commands) {
    commandsBySkill.emplace(command.skillName, command);
  }

  std::unordered_map<std::wstring, SkillsInstallPlanEntry> installBySkill;
  for (const auto& plan : m_skillsInstall.entries) {
    installBySkill.emplace(plan.skillName, plan);
  }

  for (const auto& entry : m_skillsCatalog.entries) {
    const auto eligibilityIt = eligibilityByName.find(entry.skillName);
    const bool hasEligibility = eligibilityIt != eligibilityByName.end();

    std::string commandName;
    const auto commandIt = commandsBySkill.find(entry.skillName);
    if (commandIt != commandsBySkill.end()) {
      commandName = ToNarrow(commandIt->second.name);
    }

    std::string installKind;
    std::string installCommand;
    std::string installReason;
    bool installExecutable = false;
    const auto installIt = installBySkill.find(entry.skillName);
    if (installIt != installBySkill.end()) {
      installKind = ToNarrow(installIt->second.kind);
      installCommand = ToNarrow(installIt->second.command);
      installReason = ToNarrow(installIt->second.reason);
      installExecutable = installIt->second.executable;
    }

    gatewaySkillsState.entries.push_back(
        blazeclaw::gateway::SkillsCatalogGatewayEntry{
            .name = ToNarrow(entry.skillName),
            .skillKey = hasEligibility ? ToNarrow(eligibilityIt->second.skillKey)
                                       : ToNarrow(entry.skillName),
            .commandName = commandName,
            .installKind = installKind,
            .installCommand = installCommand,
            .installExecutable = installExecutable,
            .installReason = installReason,
            .description = ToNarrow(entry.description),
            .source = ToNarrow(SkillsCatalogService::SourceKindLabel(entry.sourceKind)),
            .precedence = entry.precedence,
            .eligible = hasEligibility ? eligibilityIt->second.eligible : false,
            .disabled = hasEligibility ? eligibilityIt->second.disabled : false,
            .blockedByAllowlist =
                hasEligibility ? eligibilityIt->second.blockedByAllowlist : false,
            .disableModelInvocation =
                hasEligibility ? eligibilityIt->second.disableModelInvocation : false,
            .validFrontmatter = entry.validFrontmatter,
            .validationErrorCount = entry.validationErrors.size(),
        });
  }

  gatewaySkillsState.rootsScanned = m_skillsCatalog.diagnostics.rootsScanned;
  gatewaySkillsState.rootsSkipped = m_skillsCatalog.diagnostics.rootsSkipped;
  gatewaySkillsState.oversizedSkillFiles =
      m_skillsCatalog.diagnostics.oversizedSkillFiles;
  gatewaySkillsState.invalidFrontmatterFiles =
      m_skillsCatalog.diagnostics.invalidFrontmatterFiles;
  gatewaySkillsState.warningCount = m_skillsCatalog.diagnostics.warnings.size();
  gatewaySkillsState.eligibleCount = m_skillsEligibility.eligibleCount;
  gatewaySkillsState.disabledCount = m_skillsEligibility.disabledCount;
  gatewaySkillsState.blockedByAllowlistCount =
      m_skillsEligibility.blockedByAllowlistCount;
  gatewaySkillsState.missingRequirementsCount =
      m_skillsEligibility.missingRequirementsCount;
  gatewaySkillsState.promptIncludedCount = m_skillsPrompt.includedCount;
  gatewaySkillsState.promptChars = m_skillsPrompt.promptChars;
  gatewaySkillsState.promptTruncated = m_skillsPrompt.truncated;
  gatewaySkillsState.snapshotVersion = m_skillsWatch.version;
  gatewaySkillsState.watchEnabled = m_skillsWatch.watchEnabled;
  gatewaySkillsState.watchDebounceMs = m_skillsWatch.debounceMs;
  gatewaySkillsState.watchReason = ToNarrow(m_skillsWatch.reason);
  gatewaySkillsState.prompt = ToNarrow(m_skillsPrompt.prompt);
  gatewaySkillsState.sandboxSyncOk = m_skillsSync.success;
  gatewaySkillsState.sandboxSynced = m_skillsSync.copiedSkills;
  gatewaySkillsState.sandboxSkipped = m_skillsSync.skippedSkills;
  gatewaySkillsState.envAllowed = m_skillsEnvOverrides.allowedCount;
  gatewaySkillsState.envBlocked = m_skillsEnvOverrides.blockedCount;
  gatewaySkillsState.installExecutableCount = m_skillsInstall.executableCount;
  gatewaySkillsState.installBlockedCount = m_skillsInstall.blockedCount;
  gatewaySkillsState.scanInfoCount = m_skillSecurityScan.infoCount;
  gatewaySkillsState.scanWarnCount = m_skillSecurityScan.warnCount;
  gatewaySkillsState.scanCriticalCount = m_skillSecurityScan.criticalCount;
  gatewaySkillsState.scanScannedFiles = m_skillSecurityScan.scannedFileCount;
  return gatewaySkillsState;
}

void ServiceManager::RefreshSkillsState(
    const blazeclaw::config::AppConfig& config,
    const bool forceRefresh,
    const std::wstring& reason) {
  m_skillsCatalog = m_skillsCatalogService.LoadCatalog(
      std::filesystem::current_path(),
      config);
  m_skillsEligibility = m_skillsEligibilityService.Evaluate(
      m_skillsCatalog,
      config);
  m_skillsPrompt = m_skillsPromptService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillsCommands = m_skillsCommandService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility);
  m_skillsSync = m_skillsSyncService.SyncToSandbox(
      std::filesystem::current_path(),
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillsEnvOverrides = m_skillsEnvOverrideService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillsInstall = m_skillsInstallService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillSecurityScan = m_skillSecurityScanService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillsEnvOverrideService.Apply(m_skillsEnvOverrides);
  m_skillsWatch = m_skillsWatchService.Observe(
      m_skillsCatalog,
      config,
      forceRefresh,
      reason);
}

bool ServiceManager::Start(const blazeclaw::config::AppConfig& config) {
  if (m_running) {
    return true;
  }

  m_activeConfig = config;
  m_agentsScope = m_agentsCatalogService.BuildSnapshot(
      std::filesystem::current_path(),
      m_activeConfig);
  m_agentsWorkspace = m_agentsWorkspaceService.BuildSnapshot(m_agentsScope);
  m_agentsToolPolicy = m_agentsToolPolicyService.BuildSnapshot(
      m_agentsScope,
      m_activeConfig);
  m_agentsShellRuntimeService.Configure(m_activeConfig);
  m_agentsModelRoutingService.Configure(m_activeConfig);
  m_modelRouting = m_agentsModelRoutingService.Snapshot();
  m_agentsAuthProfileService.Configure(m_activeConfig);
  m_agentsAuthProfileService.Initialize(std::filesystem::current_path());
  m_authProfiles = m_agentsAuthProfileService.Snapshot(1735690000000);
  m_sandbox = m_agentsSandboxService.BuildSnapshot(m_agentsScope, m_activeConfig);
  m_agentsTranscriptSafetyService.Configure(m_activeConfig);
  m_subagentRegistryService.Configure(m_activeConfig);
  m_subagentRegistryService.Initialize(std::filesystem::current_path());
  m_subagentRegistry = m_subagentRegistryService.Snapshot();
  m_lastAcpDecision = m_acpSpawnService.Evaluate(
      m_activeConfig,
      AcpSpawnRequest{
          .requesterSessionId = "main",
          .requesterAgentId = "default",
          .targetAgentId = "",
          .threadRequested = false,
          .requesterSandboxed = false,
      });
  m_embeddingsService.Configure(m_activeConfig);
  m_embeddings = m_embeddingsService.Snapshot();

  m_localModelRolloutEligible = IsLocalModelRolloutEligible();
  m_localModelActivationEnabled = false;
  m_localModelActivationReason.clear();

  if (!m_activeConfig.localModel.enabled) {
    m_localModelActivationReason = "config_disabled";
  } else if (!m_localModelRolloutEligible) {
    m_localModelActivationReason = "rollout_stage_not_eligible";
  }

  m_localModelRuntime.Configure(m_activeConfig);
  const bool localModelLoaded = m_localModelRuntime.LoadModel();
  m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();
  if (!localModelLoaded && m_localModelRuntimeSnapshot.status.empty()) {
    m_localModelRuntimeSnapshot.status = "load_failed";
  }

  if (m_activeConfig.localModel.enabled &&
      m_localModelRolloutEligible &&
      localModelLoaded &&
      m_localModelRuntimeSnapshot.ready) {
    m_localModelActivationEnabled = true;
    m_localModelActivationReason = "active";
  } else if (m_activeConfig.localModel.enabled &&
             m_localModelRolloutEligible &&
             !m_localModelRuntimeSnapshot.ready) {
    m_localModelActivationReason = "initialization_failed";
  }

  TRACE(
      "[LocalModel] startup.gating enabled=%s rolloutEligible=%s activation=%s reason=%s stage=%S status=%s\n",
      m_activeConfig.localModel.enabled ? "true" : "false",
      m_localModelRolloutEligible ? "true" : "false",
      m_localModelActivationEnabled ? "true" : "false",
      m_localModelActivationReason.c_str(),
      m_activeConfig.localModel.rolloutStage.c_str(),
      m_localModelRuntimeSnapshot.status.c_str());

  if (m_localModelActivationEnabled && m_localModelRuntimeSnapshot.ready) {
    std::string localContractFailure;
    if (!m_localModelRuntime.VerifyDeterministicContract(localContractFailure)) {
      m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();
      const bool enforceContract =
          _wcsicmp(m_activeConfig.localModel.rolloutStage.c_str(), L"dev") != 0;
      m_localModelRuntimeSnapshot.status = enforceContract
          ? "contract_verification_failed"
          : "contract_verification_warning";
      m_localModelActivationEnabled = !enforceContract;
      m_localModelActivationReason = enforceContract
          ? "contract_verification_failed"
          : "active_contract_warning";
      if (!localContractFailure.empty()) {
        m_localModelRuntimeSnapshot.error = localmodel::TextGenerationError{
            .code = localmodel::TextGenerationErrorCode::InferenceFailed,
            .message = localContractFailure,
        };
      }

      m_skillsCatalog.diagnostics.warnings.push_back(
          L"local-model deterministic contract verification failed");
    }
  }

  m_retrievalMemoryService.Configure(m_activeConfig);
  m_retrievalMemory = m_retrievalMemoryService.Snapshot();
  m_piEmbeddedService.Configure(m_activeConfig);
  RefreshSkillsState(m_activeConfig, true, L"startup");

  std::wstring fixtureError;
  const std::vector<std::filesystem::path> fixtureCandidates = {
      std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"agents",
      std::filesystem::current_path() / L"fixtures" / L"agents",
      std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"skills-catalog",
      std::filesystem::current_path() / L"fixtures" / L"skills-catalog",
  };

  for (const auto& candidate : fixtureCandidates) {
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec) || ec) {
      continue;
    }

    const std::wstring candidateName = candidate.filename().wstring();
    if (candidateName == L"agents") {
      if (!m_agentsCatalogService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-scope fixture validation failed: " + fixtureError);
      }

      if (!m_agentsWorkspaceService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-workspace fixture validation failed: " + fixtureError);
      }

      if (!m_agentsToolPolicyService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-tool-policy fixture validation failed: " + fixtureError);
      }

      if (!m_agentsShellRuntimeService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-shell-runtime fixture validation failed: " + fixtureError);
      }

      if (!m_agentsModelRoutingService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-model-routing fixture validation failed: " + fixtureError);
      }

      if (!m_agentsAuthProfileService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-auth-profile fixture validation failed: " + fixtureError);
      }

      if (!m_agentsSandboxService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-sandbox fixture validation failed: " + fixtureError);
      }

      if (!m_agentsTranscriptSafetyService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-transcript fixture validation failed: " + fixtureError);
      }

      if (!m_subagentRegistryService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-subagent fixture validation failed: " + fixtureError);
      }

      if (!m_acpSpawnService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-acp fixture validation failed: " + fixtureError);
      }

      if (!m_embeddingsService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-embeddings fixture validation failed: " + fixtureError);
      }

      if (!m_retrievalMemoryService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-retrieval fixture validation failed: " + fixtureError);
      }

      if (!m_piEmbeddedService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-embedded fixture validation failed: " + fixtureError);
      }

      continue;
    }

    if (!m_skillsCatalogService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-catalog fixture validation failed: " + fixtureError);
    }

    if (!m_skillsEligibilityService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-eligibility fixture validation failed: " + fixtureError);
    }

    if (!m_skillsPromptService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-prompt fixture validation failed: " + fixtureError);
    }

    if (!m_skillsCommandService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-command fixture validation failed: " + fixtureError);
    }

    if (!m_skillsWatchService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-watch fixture validation failed: " + fixtureError);
    }

    if (!m_skillsSyncService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-sync fixture validation failed: " + fixtureError);
    }

    if (!m_skillsEnvOverrideService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-env fixture validation failed: " + fixtureError);
    }

    if (!m_skillsInstallService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-install fixture validation failed: " + fixtureError);
    }

    if (!m_skillSecurityScanService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-scan fixture validation failed: " + fixtureError);
    }

    break;
  }

  m_gatewayHost.SetSkillsCatalogState(BuildGatewaySkillsState());
  m_gatewayHost.SetSkillsRefreshCallback([this]() {
    RefreshSkillsState(m_activeConfig, true, L"manual-refresh");
    return BuildGatewaySkillsState();
  });

  m_gatewayHost.SetChatRuntimeCallback([this](
      const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
    const std::string sessionId =
        request.sessionKey.empty() ? "main" : request.sessionKey;

    if (m_localModelActivationEnabled) {
      const std::string prompt = BuildLocalModelPrompt(request);
      TRACE(
          "[LocalModel] request.enqueue runId=%s session=%s promptChars=%zu attachments=%s\n",
          request.runId.c_str(),
          sessionId.c_str(),
          prompt.size(),
          request.hasAttachments ? "true" : "false");
      TRACE(
          "[LocalModel] request.start runId=%s\n",
          request.runId.c_str());

      const auto localResult = m_localModelRuntime.GenerateStream(
          localmodel::TextGenerationRequest{
              .runId = request.runId,
              .prompt = prompt,
              .maxTokens = std::nullopt,
              .temperature = std::nullopt,
          },
          nullptr);
      m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();

      if (!localResult.ok) {
        const std::string errorCode =
            localResult.error.has_value()
            ? localmodel::TextGenerationErrorCodeToString(
                localResult.error->code)
            : "chat_runtime_error";
        const std::string errorMessage =
            localResult.error.has_value() &&
                !localResult.error->message.empty()
            ? localResult.error->message
            : "local model generation failed";
        TRACE(
            "[LocalModel] request.terminal runId=%s state=%s latencyMs=%u tokens=%u reason=%s\n",
            request.runId.c_str(),
            localResult.cancelled ? "aborted" : "error",
            localResult.latencyMs,
            localResult.generatedTokens,
            errorMessage.c_str());
        return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
            .ok = false,
            .assistantText = {},
            .modelId = localResult.modelId,
            .errorCode = errorCode,
            .errorMessage = errorMessage,
        };
      }

      std::string assistantText = localResult.text;
      std::string modelId = localResult.modelId;
      std::uint32_t latencyMs = localResult.latencyMs;
      std::uint32_t generatedTokens = localResult.generatedTokens;
      if (IsLikelyEchoResponse(request.message, assistantText)) {
        TRACE(
            "[LocalModel] request.retry runId=%s reason=echo_detected\n",
            request.runId.c_str());
        const std::string retryPrompt = BuildLocalModelRetryPrompt(request);
        const auto retryResult = m_localModelRuntime.GenerateStream(
            localmodel::TextGenerationRequest{
                .runId = request.runId + "-retry",
                .prompt = retryPrompt,
                .maxTokens = std::nullopt,
                .temperature = std::nullopt,
            },
            nullptr);
        m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();

        if (retryResult.ok &&
            !IsLikelyEchoResponse(request.message, retryResult.text)) {
          assistantText = retryResult.text;
          modelId = retryResult.modelId;
          latencyMs = retryResult.latencyMs;
          generatedTokens = retryResult.generatedTokens;
        }
      }

      if (IsLikelyEchoResponse(request.message, assistantText)) {
        TRACE(
            "[LocalModel] request.terminal runId=%s state=error latencyMs=%u tokens=%u reason=echo_output_detected\n",
            request.runId.c_str(),
            latencyMs,
            generatedTokens);
        return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
            .ok = false,
            .assistantText = {},
            .modelId = modelId,
            .errorCode = "local_model_echo_output",
            .errorMessage = "local model echoed user input",
        };
      }

      TRACE(
          "[LocalModel] request.terminal runId=%s state=final latencyMs=%u tokens=%u\n",
          request.runId.c_str(),
          latencyMs,
          generatedTokens);

      return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
          .ok = true,
          .assistantText = assistantText,
          .modelId = modelId,
          .errorCode = {},
          .errorMessage = {},
      };
    }

    if (m_activeConfig.localModel.enabled &&
        !m_localModelActivationEnabled) {
      TRACE(
          "[LocalModel] request.fallback runId=%s reason=%s rolloutEligible=%s status=%s\n",
          request.runId.c_str(),
          m_localModelActivationReason.c_str(),
          m_localModelRolloutEligible ? "true" : "false",
          m_localModelRuntimeSnapshot.status.c_str());
    }

    const auto modelSelection = m_agentsModelRoutingService.SelectModel(
        m_activeConfig.agent.model.empty()
            ? std::string()
            : ToNarrow(m_activeConfig.agent.model),
        "chat.send");

    std::string retrievalContext;
    if (m_activeConfig.embeddings.enabled && !request.message.empty()) {
      const auto userEmbedding = m_embeddingsService.EmbedText(
          EmbeddingRequest{
              .text = ToWide(request.message),
              .normalize = true,
              .traceId = "chat-retrieval-query",
          });
      if (userEmbedding.ok) {
        const auto matches = m_retrievalMemoryService.Query(
            sessionId,
            userEmbedding.vector,
            2);
        if (!matches.empty()) {
          retrievalContext = " [ctx:";
          for (std::size_t i = 0; i < matches.size(); ++i) {
            if (i > 0) {
              retrievalContext += " | ";
            }

            retrievalContext += matches[i].text;
          }

          retrievalContext += "]";
        }

        m_retrievalMemoryService.Upsert(
            sessionId,
            "user",
            request.message,
            userEmbedding.vector,
            CurrentEpochMs());
        m_retrievalMemory = m_retrievalMemoryService.Snapshot();
      }
    }

    const auto embeddedRun = m_piEmbeddedService.QueueRun(
        EmbeddedRunRequest{
            .sessionId = sessionId,
            .agentId = "default",
            .message = request.message,
        });

    if (!embeddedRun.accepted) {
      m_agentsModelRoutingService.RecordFailover(
          modelSelection.selectedModel,
          embeddedRun.reason,
          embeddedRun.startedAtMs == 0 ? 1735689800000 : embeddedRun.startedAtMs);
      return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
          .ok = false,
          .assistantText = {},
          .modelId = modelSelection.selectedModel,
          .errorCode = "embedded_run_rejected",
          .errorMessage = embeddedRun.reason,
      };
    }

    const std::string assistantText = request.message.empty()
        ? "Received image attachment."
        : ("Model(" + modelSelection.selectedModel + "): " +
            request.message + retrievalContext);

    if (m_activeConfig.embeddings.enabled && !assistantText.empty()) {
      const auto assistantEmbedding = m_embeddingsService.EmbedText(
          EmbeddingRequest{
              .text = ToWide(assistantText),
              .normalize = true,
              .traceId = "chat-retrieval-index",
          });
      if (assistantEmbedding.ok) {
        m_retrievalMemoryService.Upsert(
            sessionId,
            "assistant",
            assistantText,
            assistantEmbedding.vector,
            CurrentEpochMs());
        m_retrievalMemory = m_retrievalMemoryService.Snapshot();
      }
    }

    const bool completed = m_piEmbeddedService.CompleteRun(
        embeddedRun.runId,
        "completed",
        embeddedRun.startedAtMs + 1);
    if (!completed) {
      m_agentsModelRoutingService.RecordFailover(
          modelSelection.selectedModel,
          "embedded_completion_failed",
          embeddedRun.startedAtMs + 1);
      return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
          .ok = false,
          .assistantText = {},
          .modelId = modelSelection.selectedModel,
          .errorCode = "embedded_completion_failed",
          .errorMessage = "embedded completion failed",
      };
    }

    return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
        .ok = true,
        .assistantText = assistantText,
        .modelId = modelSelection.selectedModel,
        .errorCode = {},
        .errorMessage = {},
    };
  });

  m_gatewayHost.SetChatAbortCallback([this](
      const blazeclaw::gateway::GatewayHost::ChatAbortRequest& request) {
    if (!m_localModelActivationEnabled) {
      return false;
    }

    const bool cancelled = m_localModelRuntime.Cancel(request.runId);
    m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();
    return cancelled;
  });

  m_gatewayHost.SetEmbeddingsGenerateCallback([this](
      const blazeclaw::gateway::GatewayHost::EmbeddingsGenerateRequest& request) {
    const auto result = m_embeddingsService.EmbedText(
        EmbeddingRequest{
            .text = ToWide(request.text),
            .normalize = request.normalize,
            .traceId = request.traceId,
        });

    blazeclaw::gateway::GatewayHost::EmbeddingsGenerateResult gatewayResult;
    gatewayResult.ok = result.ok;
    gatewayResult.vector = result.vector;
    gatewayResult.dimension = result.dimension;
    gatewayResult.provider = result.provider;
    gatewayResult.modelId = result.modelId;
    gatewayResult.latencyMs = result.latencyMs;
    gatewayResult.status = m_embeddingsService.Snapshot().status;

    if (result.error.has_value()) {
      gatewayResult.errorCode =
          EmbeddingErrorCodeToString(result.error->code);
      gatewayResult.errorMessage = result.error->message;
    }

    return gatewayResult;
  });

  m_gatewayHost.SetEmbeddingsBatchCallback([this](
      const blazeclaw::gateway::GatewayHost::EmbeddingsBatchRequest& request) {
    std::vector<std::wstring> texts;
    texts.reserve(request.texts.size());
    for (const auto& text : request.texts) {
      texts.push_back(ToWide(text));
    }

    const auto result = m_embeddingsService.EmbedBatch(
        EmbeddingBatchRequest{
            .texts = std::move(texts),
            .normalize = request.normalize,
            .traceId = request.traceId,
        });

    blazeclaw::gateway::GatewayHost::EmbeddingsBatchResult gatewayResult;
    gatewayResult.ok = result.ok;
    gatewayResult.vectors = result.vectors;
    gatewayResult.dimension = result.dimension;
    gatewayResult.provider = result.provider;
    gatewayResult.modelId = result.modelId;
    gatewayResult.latencyMs = result.latencyMs;
    gatewayResult.status = m_embeddingsService.Snapshot().status;

    if (result.error.has_value()) {
      gatewayResult.errorCode =
          EmbeddingErrorCodeToString(result.error->code);
      gatewayResult.errorMessage = result.error->message;
    }

    return gatewayResult;
  });

  const bool gatewayStarted = m_gatewayHost.Start(config.gateway);
  m_running = gatewayStarted;
  return m_running;
}

void ServiceManager::Stop() {
  m_skillsEnvOverrideService.RevertAll();
  m_gatewayHost.Stop();
  m_running = false;
}

bool ServiceManager::IsRunning() const noexcept {
  return m_running;
}

const FeatureRegistry& ServiceManager::Registry() const noexcept {
  return m_registry;
}

const AgentScopeSnapshot& ServiceManager::AgentsScope() const noexcept {
  return m_agentsScope;
}

const AgentsWorkspaceSnapshot& ServiceManager::AgentsWorkspace() const noexcept {
  return m_agentsWorkspace;
}

const SubagentRegistrySnapshot& ServiceManager::SubagentRegistry() const noexcept {
  return m_subagentRegistry;
}

const AcpSpawnDecision& ServiceManager::LastAcpDecision() const noexcept {
  return m_lastAcpDecision;
}

std::size_t ServiceManager::ActiveEmbeddedRuns() const noexcept {
  return m_piEmbeddedService.ActiveRuns();
}

const AgentsToolPolicySnapshot& ServiceManager::ToolPolicy() const noexcept {
  return m_agentsToolPolicy;
}

std::size_t ServiceManager::ShellProcessCount() const noexcept {
  return m_agentsShellRuntimeService.ListProcesses().size();
}

const ModelRoutingSnapshot& ServiceManager::ModelRouting() const noexcept {
  return m_modelRouting;
}

const AuthProfileSnapshot& ServiceManager::AuthProfiles() const noexcept {
  return m_authProfiles;
}

const SandboxSnapshot& ServiceManager::Sandbox() const noexcept {
  return m_sandbox;
}

const EmbeddingsServiceSnapshot& ServiceManager::Embeddings() const noexcept {
  return m_embeddings;
}

const localmodel::LocalModelRuntimeSnapshot& ServiceManager::LocalModelRuntime() const noexcept {
  return m_localModelRuntimeSnapshot;
}

bool ServiceManager::LocalModelRolloutEligible() const noexcept {
  return m_localModelRolloutEligible;
}

bool ServiceManager::LocalModelActivationEnabled() const noexcept {
  return m_localModelActivationEnabled;
}

const std::string& ServiceManager::LocalModelActivationReason() const noexcept {
  return m_localModelActivationReason;
}

const RetrievalMemorySnapshot& ServiceManager::RetrievalMemory() const noexcept {
  return m_retrievalMemory;
}

std::string ServiceManager::BuildOperatorDiagnosticsReport() const {
  const auto featureStateLabel = [](const FeatureState state) {
    switch (state) {
      case FeatureState::Implemented:
        return "implemented";
      case FeatureState::InProgress:
        return "in_progress";
      case FeatureState::Planned:
      default:
        return "planned";
    }
  };

  std::size_t implementedCount = 0;
  std::size_t inProgressCount = 0;
  std::size_t plannedCount = 0;
  for (const auto& feature : m_registry.Features()) {
    if (feature.state == FeatureState::Implemented) {
      ++implementedCount;
      continue;
    }

    if (feature.state == FeatureState::InProgress) {
      ++inProgressCount;
      continue;
    }

    ++plannedCount;
  }

  const auto routing = ModelRouting();
  const auto auth = AuthProfiles();
  const auto sandbox = Sandbox();
  const auto embeddings = Embeddings();
  const auto localModel = LocalModelRuntime();
  const auto retrieval = RetrievalMemory();
  const bool configFeatureImplemented =
      m_registry.IsImplemented(L"embeddings-config-foundation");

  std::string report =
      "{\"runtime\":{\"running\":" + std::string(m_running ? "true" : "false") +
      ",\"gatewayWarning\":\"" + m_gatewayHost.LastWarning() + "\"},"
      "\"agents\":{\"count\":" + std::to_string(m_agentsScope.entries.size()) +
      ",\"defaultAgent\":\"" + ToNarrow(m_agentsScope.defaultAgentId) + "\"},"
      "\"subagents\":{\"active\":" + std::to_string(m_subagentRegistry.activeRuns) +
      ",\"pendingAnnounce\":" + std::to_string(m_subagentRegistry.pendingAnnounce) + "},"
      "\"acp\":{\"lastAllowed\":" +
      std::string(m_lastAcpDecision.allowed ? "true" : "false") +
      ",\"reason\":\"" + m_lastAcpDecision.reason + "\"},"
      "\"embedded\":{\"activeRuns\":" + std::to_string(ActiveEmbeddedRuns()) + "},"
      "\"tools\":{\"policyEntries\":" + std::to_string(m_agentsToolPolicy.entries.size()) +
      ",\"shellProcesses\":" + std::to_string(ShellProcessCount()) + "},"
      "\"modelAuth\":{\"primary\":\"" + routing.primaryModel +
      "\",\"fallback\":\"" + routing.fallbackModel +
      "\",\"failovers\":" + std::to_string(routing.failoverHistory.size()) +
      ",\"authProfiles\":" + std::to_string(auth.entries.size()) + "},"
      "\"sandbox\":{\"enabledCount\":" + std::to_string(sandbox.enabledCount) +
      ",\"browserEnabledCount\":" + std::to_string(sandbox.browserEnabledCount) + "},"
      "\"embeddings\":{\"enabled\":" +
      std::string(embeddings.enabled ? "true" : "false") +
      ",\"ready\":" +
      std::string(embeddings.ready ? "true" : "false") +
      ",\"provider\":\"" + embeddings.provider +
      "\",\"status\":\"" + embeddings.status +
      "\",\"dimension\":" + std::to_string(embeddings.dimension) +
      ",\"maxSequenceLength\":" +
      std::to_string(embeddings.maxSequenceLength) +
      ",\"modelPathConfigured\":" +
      std::string(!embeddings.modelPath.empty() ? "true" : "false") +
      ",\"tokenizerPathConfigured\":" +
      std::string(!embeddings.tokenizerPath.empty() ? "true" : "false") +
      ",\"configFeatureImplemented\":" +
      std::string(configFeatureImplemented ? "true" : "false") + "},"
      "\"localModel\":{\"enabled\":" +
      std::string(localModel.enabled ? "true" : "false") +
      ",\"ready\":" +
      std::string(localModel.ready ? "true" : "false") +
      ",\"rolloutEligible\":" +
      std::string(m_localModelRolloutEligible ? "true" : "false") +
      ",\"activationEnabled\":" +
      std::string(m_localModelActivationEnabled ? "true" : "false") +
      ",\"activationReason\":\"" + m_localModelActivationReason +
      ",\"provider\":\"" + localModel.provider +
      "\",\"rolloutStage\":\"" + localModel.rolloutStage +
      "\",\"storageRoot\":\"" + localModel.storageRoot +
      "\",\"version\":\"" + localModel.version +
      "\",\"status\":\"" + localModel.status +
      "\",\"verboseMetrics\":" +
      std::string(localModel.verboseMetrics ? "true" : "false") +
      ",\"runtimeDllPresent\":" +
      std::string(localModel.runtimeDllPresent ? "true" : "false") +
      ",\"maxTokens\":" +
      std::to_string(localModel.maxTokens) +
      ",\"temperature\":" +
      std::to_string(localModel.temperature) +
      ",\"modelLoadAttempts\":" +
      std::to_string(localModel.modelLoadAttempts) +
      ",\"modelLoadFailures\":" +
      std::to_string(localModel.modelLoadFailures) +
      ",\"requestsStarted\":" +
      std::to_string(localModel.requestsStarted) +
      ",\"requestsCompleted\":" +
      std::to_string(localModel.requestsCompleted) +
      ",\"requestsFailed\":" +
      std::to_string(localModel.requestsFailed) +
      ",\"requestsCancelled\":" +
      std::to_string(localModel.requestsCancelled) +
      ",\"cumulativeTokens\":" +
      std::to_string(localModel.cumulativeTokens) +
      ",\"cumulativeLatencyMs\":" +
      std::to_string(localModel.cumulativeLatencyMs) +
      ",\"lastLatencyMs\":" +
      std::to_string(localModel.lastLatencyMs) +
      ",\"lastGeneratedTokens\":" +
      std::to_string(localModel.lastGeneratedTokens) +
      ",\"lastTokensPerSecond\":" +
      std::to_string(localModel.lastTokensPerSecond) +
      ",\"modelPathConfigured\":" +
      std::string(!localModel.modelPath.empty() ? "true" : "false") +
      ",\"modelHashConfigured\":" +
      std::string(!localModel.modelExpectedSha256.empty() ? "true" : "false") +
      ",\"modelHashVerified\":" +
      std::string(localModel.modelHashVerified ? "true" : "false") +
      ",\"tokenizerPathConfigured\":" +
      std::string(!localModel.tokenizerPath.empty() ? "true" : "false") +
      ",\"tokenizerHashConfigured\":" +
      std::string(!localModel.tokenizerExpectedSha256.empty() ? "true" : "false") +
      ",\"tokenizerHashVerified\":" +
      std::string(localModel.tokenizerHashVerified ? "true" : "false") +
      "},"
      "\"retrieval\":{\"enabled\":" +
      std::string(retrieval.enabled ? "true" : "false") +
      ",\"recordCount\":" + std::to_string(retrieval.recordCount) +
      ",\"lastQueryCount\":" + std::to_string(retrieval.lastQueryCount) +
      ",\"status\":\"" + retrieval.status + "\"},"
      "\"skills\":{\"catalogEntries\":" + std::to_string(m_skillsCatalog.entries.size()) +
      ",\"promptIncluded\":" + std::to_string(m_skillsPrompt.includedCount) + "},"
      "\"features\":{\"implemented\":" + std::to_string(implementedCount) +
      ",\"inProgress\":" + std::to_string(inProgressCount) +
      ",\"planned\":" + std::to_string(plannedCount) +
      ",\"registryState\":\"" + featureStateLabel(
          m_registry.Features().empty() ? FeatureState::Planned : m_registry.Features().front().state) +
      "\"}}";

  return report;
}

const SkillsCatalogSnapshot& ServiceManager::SkillsCatalog() const noexcept {
  return m_skillsCatalog;
}

const SkillsEligibilitySnapshot& ServiceManager::SkillsEligibility() const noexcept {
  return m_skillsEligibility;
}

const SkillsPromptSnapshot& ServiceManager::SkillsPrompt() const noexcept {
  return m_skillsPrompt;
}

std::string ServiceManager::InvokeGatewayMethod(
    const std::string& method,
    const std::optional<std::string>& paramsJson) const {
  const blazeclaw::gateway::protocol::RequestFrame request{
      .id = "ui-probe",
      .method = method,
      .paramsJson = paramsJson,
  };

  const auto response = RouteGatewayRequest(request);
  if (response.ok) {
    return response.payloadJson.has_value() ? response.payloadJson.value()
                                            : "ok";
  }

  if (!response.error.has_value()) {
    return "error_unknown";
  }

  const auto& error = response.error.value();
  return error.code + ":" + error.message;
}

blazeclaw::gateway::protocol::ResponseFrame ServiceManager::RouteGatewayRequest(
    const blazeclaw::gateway::protocol::RequestFrame& request) const {
  if (!m_running) {
    return blazeclaw::gateway::protocol::ResponseFrame{
        .id = request.id,
        .ok = false,
        .payloadJson = std::nullopt,
        .error = blazeclaw::gateway::protocol::ErrorShape{
            .code = "service_not_running",
            .message = "Service manager is not running.",
            .detailsJson = std::nullopt,
            .retryable = false,
            .retryAfterMs = std::nullopt,
        },
    };
  }

  if (request.method.empty()) {
    return blazeclaw::gateway::protocol::ResponseFrame{
        .id = request.id,
        .ok = false,
        .payloadJson = std::nullopt,
        .error = blazeclaw::gateway::protocol::ErrorShape{
            .code = "invalid_method",
            .message = "Gateway method must not be empty.",
            .detailsJson = std::nullopt,
            .retryable = false,
            .retryAfterMs = std::nullopt,
        },
    };
  }

  return m_gatewayHost.RouteRequest(request);
}

bool ServiceManager::PumpGatewayNetworkOnce(std::string& error) {
  if (!m_running) {
    error = "service manager is not running";
    return false;
  }

  return m_gatewayHost.PumpNetworkOnce(error);
}

bool ServiceManager::IsLocalModelRolloutEligible() const {
  const std::wstring stage = m_activeConfig.localModel.rolloutStage;
  if (_wcsicmp(stage.c_str(), L"stable") == 0) {
    return true;
  }

  if (_wcsicmp(stage.c_str(), L"nightly") == 0) {
    return IsOneOfChannels(m_activeConfig.enabledChannels, L"nightly");
  }

  return true;
}

} // namespace blazeclaw::core
