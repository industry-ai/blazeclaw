#pragma once

#include "HookCatalogService.h"
#include "HookEventService.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct HookExecutionDiagnostics {
  std::uint64_t dispatchCount = 0;
  std::uint64_t successCount = 0;
  std::uint64_t failureCount = 0;
  std::uint64_t skippedCount = 0;
  std::uint64_t timeoutCount = 0;
  std::uint64_t guardRejectedCount = 0;
  std::uint64_t reminderTriggeredCount = 0;
  std::uint64_t reminderInjectedCount = 0;
  std::uint64_t reminderSkippedCount = 0;
  std::wstring lastReminderState;
  std::wstring lastReminderReason;
  std::vector<std::wstring> warnings;
};

struct HookExecutionPolicy {
  bool reminderEnabled = true;
  std::wstring reminderVerbosity = L"normal";
};

struct HookExecutionSnapshot {
  std::vector<HookBootstrapFile> bootstrapFiles;
  HookExecutionDiagnostics diagnostics;
};

class HookExecutionService {
public:
  [[nodiscard]] HookExecutionSnapshot Snapshot() const;

  [[nodiscard]] bool Dispatch(
      const HookLifecycleEvent& event,
      const HookCatalogSnapshot& hooks,
      const HookExecutionPolicy& policy,
      std::wstring& outError);

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError);

private:
  [[nodiscard]] static bool MatchesEvent(
      const std::wstring& hookEvent,
      const HookLifecycleEvent& event);
  [[nodiscard]] static bool IsSafeBootstrapPath(const std::wstring& value);

  HookExecutionSnapshot m_snapshot;
};

} // namespace blazeclaw::core
