#include "pch.h"
#include "HookExecutionService.h"

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <stdexcept>
#include <tuple>

namespace blazeclaw::core {

namespace {

constexpr std::uint64_t kPerHookTimeoutMs = 25;
constexpr std::size_t kMaxBootstrapFiles = 128;

std::wstring Trim(const std::wstring& value) {
  const auto first = std::find_if_not(
      value.begin(),
      value.end(),
      [](const wchar_t ch) { return std::iswspace(ch) != 0; });
  const auto last = std::find_if_not(
      value.rbegin(),
      value.rend(),
      [](const wchar_t ch) { return std::iswspace(ch) != 0; })
                        .base();

  if (first >= last) {
    return {};
  }

  return std::wstring(first, last);
}

std::wstring ToLower(const std::wstring& value) {
  std::wstring lowered = value;
  std::transform(
      lowered.begin(),
      lowered.end(),
      lowered.begin(),
      [](const wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
      });
  return lowered;
}

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  const int required = MultiByteToWideChar(
      CP_UTF8,
      0,
      value.c_str(),
      static_cast<int>(value.size()),
      nullptr,
      0);
  if (required <= 0) {
    return std::wstring(value.begin(), value.end());
  }

  std::wstring output(static_cast<std::size_t>(required), L'\0');
  const int converted = MultiByteToWideChar(
      CP_UTF8,
      0,
      value.c_str(),
      static_cast<int>(value.size()),
      output.data(),
      required);
  if (converted <= 0) {
    return std::wstring(value.begin(), value.end());
  }

  return output;
}

std::optional<std::wstring> ReadFileUtf8(const std::filesystem::path& filePath) {
  std::ifstream input(filePath, std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  std::string content(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  return Utf8ToWide(content);
}

std::vector<std::wstring> ExtractBootstrapPathsFromHandler(
    const std::wstring& handlerContent,
    std::wstring& outError) {
  outError.clear();
  std::vector<std::wstring> paths;

  constexpr auto kPushPattern = L"bootstrapFiles.push({";
  std::size_t cursor = 0;
  while (cursor < handlerContent.size()) {
    const auto pushPos = handlerContent.find(kPushPattern, cursor);
    if (pushPos == std::wstring::npos) {
      break;
    }

    const auto blockEnd = handlerContent.find(L"});", pushPos);
    if (blockEnd == std::wstring::npos) {
      outError = L"Malformed handler contract: unterminated bootstrapFiles.push block.";
      return {};
    }

    const auto block = handlerContent.substr(pushPos, blockEnd - pushPos + 3);
    const auto pathPos = block.find(L"path");
    if (pathPos == std::wstring::npos) {
      outError = L"Malformed handler contract: missing path in bootstrapFiles.push block.";
      return {};
    }

    const auto colonPos = block.find(L':', pathPos + 4);
    if (colonPos == std::wstring::npos) {
      outError = L"Malformed handler contract: invalid path field in bootstrapFiles.push block.";
      return {};
    }

    const auto quotePos = block.find_first_of(L"'\"", colonPos + 1);
    if (quotePos == std::wstring::npos) {
      outError = L"Malformed handler contract: path must be quoted.";
      return {};
    }

    const wchar_t quote = block[quotePos];
    const auto endQuotePos = block.find(quote, quotePos + 1);
    if (endQuotePos == std::wstring::npos) {
      outError = L"Malformed handler contract: unterminated quoted path.";
      return {};
    }

    const auto path = Trim(block.substr(quotePos + 1, endQuotePos - quotePos - 1));
    if (path.empty()) {
      outError = L"Malformed handler contract: empty path value.";
      return {};
    }

    paths.push_back(path);
    cursor = blockEnd + 3;
  }

  return paths;
}

std::vector<const HookCatalogEntry*> BuildDispatchOrder(
    const HookCatalogSnapshot& hooks,
    const HookLifecycleEvent& event) {
  const auto matchesEvent = [&](const std::wstring& hookEvent) {
    const auto left = ToLower(Trim(hookEvent));
    const auto right =
        ToLower(Trim(event.type + std::wstring(L".") + event.action));
    return !left.empty() && left == right;
  };

  std::vector<const HookCatalogEntry*> ordered;
  for (const auto& hook : hooks.entries) {
    if (!hook.validMetadata || !hook.safeHandlerPath || !hook.handlerExists) {
      continue;
    }

    if (!matchesEvent(hook.frontmatter.eventName)) {
      continue;
    }

    ordered.push_back(&hook);
  }

  std::sort(
      ordered.begin(),
      ordered.end(),
      [](const HookCatalogEntry* left, const HookCatalogEntry* right) {
        const auto leftSkill = ToLower(left->skillName);
        const auto rightSkill = ToLower(right->skillName);
        if (leftSkill == rightSkill) {
          return ToLower(left->frontmatter.name) <
                 ToLower(right->frontmatter.name);
        }

        return leftSkill < rightSkill;
      });

  return ordered;
}

} // namespace

HookExecutionSnapshot HookExecutionService::Snapshot() const {
  return m_snapshot;
}

bool HookExecutionService::MatchesEvent(
    const std::wstring& hookEvent,
    const HookLifecycleEvent& event) {
  const auto left = ToLower(Trim(hookEvent));
  const auto right =
      ToLower(Trim(event.type + std::wstring(L".") + event.action));
  return !left.empty() && left == right;
}

bool HookExecutionService::IsSafeBootstrapPath(const std::wstring& value) {
  const std::wstring trimmed = Trim(value);
  if (trimmed.empty()) {
    return false;
  }

  if (trimmed.find(L"..") != std::wstring::npos) {
    return false;
  }

  if (trimmed.find(L'\\') != std::wstring::npos) {
    return false;
  }

  if (trimmed.find(L':') != std::wstring::npos) {
    return false;
  }

  if (!trimmed.empty() && trimmed.front() == L'/') {
    return false;
  }

  return true;
}

bool HookExecutionService::Dispatch(
    const HookLifecycleEvent& event,
    const HookCatalogSnapshot& hooks,
    const HookExecutionPolicy& policy,
    std::wstring& outError) {
  outError.clear();

  m_snapshot.diagnostics.lastReminderState.clear();
  m_snapshot.diagnostics.lastReminderReason.clear();

  if (!MatchesEvent(L"agent.bootstrap", event)) {
    ++m_snapshot.diagnostics.skippedCount;
    ++m_snapshot.diagnostics.reminderSkippedCount;
    m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
    m_snapshot.diagnostics.lastReminderReason = L"unsupported_event";
    outError = L"Hook dispatch skipped: unsupported event.";
    return false;
  }

  if (event.sessionKey.find(L":subagent:") != std::wstring::npos) {
    ++m_snapshot.diagnostics.skippedCount;
    ++m_snapshot.diagnostics.reminderSkippedCount;
    m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
    m_snapshot.diagnostics.lastReminderReason = L"subagent_session";
    outError = L"Hook dispatch skipped: subagent session.";
    return false;
  }

  if (!policy.reminderEnabled) {
    ++m_snapshot.diagnostics.skippedCount;
    ++m_snapshot.diagnostics.reminderSkippedCount;
    m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
    m_snapshot.diagnostics.lastReminderReason = L"reminder_disabled";
    outError = L"Hook dispatch skipped: reminder policy disabled.";
    return false;
  }

  m_snapshot.bootstrapFiles = event.bootstrapFiles;
  ++m_snapshot.diagnostics.dispatchCount;
  ++m_snapshot.diagnostics.reminderTriggeredCount;
  m_snapshot.diagnostics.lastReminderState = L"reminder_triggered";

  const auto ordered = BuildDispatchOrder(hooks, event);
  bool injected = false;
  for (const auto* hook : ordered) {
    const auto started = std::chrono::steady_clock::now();
    try {
      std::vector<HookBootstrapFile> proposed;
      const auto handlerContent = ReadFileUtf8(hook->handlerFile);
      if (!handlerContent.has_value()) {
        ++m_snapshot.diagnostics.failureCount;
        m_snapshot.diagnostics.warnings.push_back(
            L"Hook execution failed: cannot read handler file.");
        continue;
      }

      std::wstring parseError;
      const auto extractedPaths = ExtractBootstrapPathsFromHandler(
          handlerContent.value(),
          parseError);
      if (!parseError.empty()) {
        ++m_snapshot.diagnostics.failureCount;
        ++m_snapshot.diagnostics.reminderSkippedCount;
        m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
        m_snapshot.diagnostics.lastReminderReason = L"handler_contract_invalid";
        m_snapshot.diagnostics.warnings.push_back(parseError);
        continue;
      }

      for (const auto& extractedPath : extractedPaths) {
        proposed.push_back(
            HookBootstrapFile{.path = extractedPath, .virtualFile = true});
      }

      if (ToLower(policy.reminderVerbosity) == L"minimal") {
        proposed.erase(
            std::remove_if(
                proposed.begin(),
                proposed.end(),
                [](const HookBootstrapFile& item) {
                  return ToLower(item.path) != L"self_evolving_reminder.md";
                }),
            proposed.end());
      }

      for (const auto& file : proposed) {
        if (!IsSafeBootstrapPath(file.path)) {
          ++m_snapshot.diagnostics.guardRejectedCount;
          ++m_snapshot.diagnostics.reminderSkippedCount;
          m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
          m_snapshot.diagnostics.lastReminderReason = L"unsafe_path";
          m_snapshot.diagnostics.warnings.push_back(
              L"Hook mutation rejected due to unsafe bootstrap file path.");
          continue;
        }

        if (m_snapshot.bootstrapFiles.size() >= kMaxBootstrapFiles) {
          ++m_snapshot.diagnostics.guardRejectedCount;
          ++m_snapshot.diagnostics.reminderSkippedCount;
          m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
          m_snapshot.diagnostics.lastReminderReason = L"bootstrap_limit";
          m_snapshot.diagnostics.warnings.push_back(
              L"Hook mutation rejected due to bootstrap file limit.");
          continue;
        }

        m_snapshot.bootstrapFiles.push_back(file);
        if (ToLower(file.path) == L"self_evolving_reminder.md") {
          injected = true;
        }
      }

      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started);
      if (elapsed.count() > static_cast<long long>(kPerHookTimeoutMs)) {
        ++m_snapshot.diagnostics.timeoutCount;
        m_snapshot.diagnostics.warnings.push_back(
            L"Hook execution timeout threshold exceeded.");
        continue;
      }

      ++m_snapshot.diagnostics.successCount;
    } catch (const std::exception&) {
      ++m_snapshot.diagnostics.failureCount;
      ++m_snapshot.diagnostics.reminderSkippedCount;
      m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
      m_snapshot.diagnostics.lastReminderReason = L"exception";
      m_snapshot.diagnostics.warnings.push_back(
          L"Hook execution failed with exception.");
    } catch (...) {
      ++m_snapshot.diagnostics.failureCount;
      ++m_snapshot.diagnostics.reminderSkippedCount;
      m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
      m_snapshot.diagnostics.lastReminderReason = L"unknown_exception";
      m_snapshot.diagnostics.warnings.push_back(
          L"Hook execution failed with unknown exception.");
    }
  }

  if (injected) {
    ++m_snapshot.diagnostics.reminderInjectedCount;
    m_snapshot.diagnostics.lastReminderState = L"reminder_injected";
    m_snapshot.diagnostics.lastReminderReason = L"hook_execution";
  } else if (m_snapshot.diagnostics.lastReminderState.empty()) {
    ++m_snapshot.diagnostics.reminderSkippedCount;
    m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
    m_snapshot.diagnostics.lastReminderReason = L"no_injection";
  }

  return true;
}

bool HookExecutionService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) {
  outError.clear();

  const auto root = fixturesRoot / L"s9-hooks-exec" / L"workspace";
  blazeclaw::config::AppConfig appConfig;
  appConfig.skills.limits.maxCandidatesPerRoot = 32;
  appConfig.skills.limits.maxSkillsLoadedPerSource = 32;
  appConfig.skills.limits.maxSkillFileBytes = 32 * 1024;

  const SkillsCatalogService catalogService;
  const auto catalog = catalogService.LoadCatalog(root, appConfig);

  const HookCatalogService hookCatalogService;
  const auto hooks = hookCatalogService.BuildSnapshot(catalog);

  HookExecutionService service;
  std::wstring dispatchError;
  const bool ok = service.Dispatch(
      HookLifecycleEvent{
          .type = L"agent",
          .action = L"bootstrap",
          .sessionKey = L"main",
          .bootstrapFiles =
              std::vector<HookBootstrapFile>{
                  HookBootstrapFile{.path = L"BOOTSTRAP.md", .virtualFile = true}}},
      hooks,
      HookExecutionPolicy{},
      dispatchError);

  if (!ok) {
    outError =
        L"S9 hooks exec fixture failed: expected successful dispatch for bootstrap event.";
    return false;
  }

  const auto snapshot = service.Snapshot();
  const bool hasReminder = std::any_of(
      snapshot.bootstrapFiles.begin(),
      snapshot.bootstrapFiles.end(),
      [](const HookBootstrapFile& file) {
        return ToLower(file.path) == L"self_evolving_reminder.md";
      });
  if (!hasReminder) {
    outError =
        L"S9 hooks exec fixture failed: expected self-evolving reminder bootstrap file injection.";
    return false;
  }

  const bool hasGeneralizedContext = std::any_of(
      snapshot.bootstrapFiles.begin(),
      snapshot.bootstrapFiles.end(),
      [](const HookBootstrapFile& file) {
        return ToLower(file.path) == L"hook_generalized_context.md";
      });
  if (!hasGeneralizedContext) {
    outError =
        L"S9 hooks exec fixture failed: expected generalized hook bootstrap file injection.";
    return false;
  }

  if (snapshot.diagnostics.guardRejectedCount == 0) {
    outError =
        L"S9 hooks exec fixture failed: expected unsafe mutation guard rejection count > 0.";
    return false;
  }

  if (snapshot.diagnostics.dispatchCount == 0 ||
      snapshot.diagnostics.successCount == 0) {
    outError =
        L"S9 hooks exec fixture failed: expected dispatch and success counters > 0.";
    return false;
  }

  if (snapshot.diagnostics.reminderTriggeredCount == 0 ||
      snapshot.diagnostics.reminderInjectedCount == 0) {
    outError =
        L"S9 hooks exec fixture failed: expected reminder triggered/injected telemetry counters > 0.";
    return false;
  }

  if (snapshot.diagnostics.lastReminderState.empty() ||
      snapshot.diagnostics.lastReminderReason.empty()) {
    outError =
        L"S9 hooks exec fixture failed: expected reminder state and reason telemetry values.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
