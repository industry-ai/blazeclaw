#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct HookBootstrapFile {
  std::wstring path;
  bool virtualFile = false;
};

struct HookLifecycleEvent {
  std::wstring type;
  std::wstring action;
  std::wstring sessionKey;
  std::vector<HookBootstrapFile> bootstrapFiles;
};

struct HookEventDiagnostics {
  std::uint64_t emittedCount = 0;
  std::uint64_t validationFailedCount = 0;
  std::uint64_t droppedCount = 0;
  std::vector<std::wstring> warnings;
};

struct HookEventSnapshot {
  std::vector<HookLifecycleEvent> events;
  std::uint64_t sequence = 0;
  HookEventDiagnostics diagnostics;
};

class HookEventService {
public:
  [[nodiscard]] HookEventSnapshot Snapshot() const;

  [[nodiscard]] bool EmitAgentBootstrap(
      const std::wstring& sessionKey,
      const std::vector<HookBootstrapFile>& bootstrapFiles,
      std::wstring& outError);

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError);

private:
  [[nodiscard]] static bool ValidateEventSchema(
      const HookLifecycleEvent& event,
      std::wstring& outError);

  HookEventSnapshot m_snapshot;
};

} // namespace blazeclaw::core
