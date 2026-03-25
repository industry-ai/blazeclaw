#include "pch.h"
#include "HookExecutionService.h"

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <stdexcept>

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

std::wstring BuildSelfEvolvingReminder() {
  return L"## Self-Evolving Reminder\n"
         L"Log corrections to .learnings/LEARNINGS.md\n"
         L"Log failures to .learnings/ERRORS.md\n"
         L"Log missing capabilities to .learnings/FEATURE_REQUESTS.md\n";
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
    std::wstring& outError) {
  outError.clear();

  if (!MatchesEvent(L"agent.bootstrap", event)) {
    ++m_snapshot.diagnostics.skippedCount;
    outError = L"Hook dispatch skipped: unsupported event.";
    return false;
  }

  if (event.sessionKey.find(L":subagent:") != std::wstring::npos) {
    ++m_snapshot.diagnostics.skippedCount;
    outError = L"Hook dispatch skipped: subagent session.";
    return false;
  }

  m_snapshot.bootstrapFiles = event.bootstrapFiles;
  ++m_snapshot.diagnostics.dispatchCount;

  const auto ordered = BuildDispatchOrder(hooks, event);
  for (const auto* hook : ordered) {
    const auto started = std::chrono::steady_clock::now();
    try {
      std::vector<HookBootstrapFile> proposed;
      const auto skill = ToLower(hook->skillName);
      if (skill == L"self-evolving") {
        proposed.push_back(HookBootstrapFile{
            .path = L"SELF_EVOLVING_REMINDER.md",
            .virtualFile = true});
      } else if (skill == L"hook-unsafe-mutation") {
        proposed.push_back(HookBootstrapFile{.path = L"../UNSAFE.md", .virtualFile = true});
      }

      for (const auto& file : proposed) {
        if (!IsSafeBootstrapPath(file.path)) {
          ++m_snapshot.diagnostics.guardRejectedCount;
          m_snapshot.diagnostics.warnings.push_back(
              L"Hook mutation rejected due to unsafe bootstrap file path.");
          continue;
        }

        if (m_snapshot.bootstrapFiles.size() >= kMaxBootstrapFiles) {
          ++m_snapshot.diagnostics.guardRejectedCount;
          m_snapshot.diagnostics.warnings.push_back(
              L"Hook mutation rejected due to bootstrap file limit.");
          continue;
        }

        m_snapshot.bootstrapFiles.push_back(file);
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
      m_snapshot.diagnostics.warnings.push_back(
          L"Hook execution failed with exception.");
    } catch (...) {
      ++m_snapshot.diagnostics.failureCount;
      m_snapshot.diagnostics.warnings.push_back(
          L"Hook execution failed with unknown exception.");
    }
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

  return true;
}

} // namespace blazeclaw::core
