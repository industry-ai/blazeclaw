#include "pch.h"
#include "HookEventService.h"

namespace blazeclaw::core {

namespace {

constexpr std::size_t kMaxEventHistory = 64;

bool IsTrimmedEmpty(const std::wstring& value) {
  const auto first = value.find_first_not_of(L" \t\r\n");
  return first == std::wstring::npos;
}

} // namespace

HookEventSnapshot HookEventService::Snapshot() const {
  return m_snapshot;
}

bool HookEventService::ValidateEventSchema(
    const HookLifecycleEvent& event,
    std::wstring& outError) {
  outError.clear();

  if (event.type != L"agent") {
    outError = L"Hook event schema invalid: expected type=agent.";
    return false;
  }

  if (event.action != L"bootstrap") {
    outError = L"Hook event schema invalid: expected action=bootstrap.";
    return false;
  }

  if (IsTrimmedEmpty(event.sessionKey)) {
    outError = L"Hook event schema invalid: sessionKey is required.";
    return false;
  }

  for (const auto& file : event.bootstrapFiles) {
    if (IsTrimmedEmpty(file.path)) {
      outError =
          L"Hook event schema invalid: bootstrapFiles entries require path.";
      return false;
    }
  }

  return true;
}

bool HookEventService::EmitAgentBootstrap(
    const std::wstring& sessionKey,
    const std::vector<HookBootstrapFile>& bootstrapFiles,
    std::wstring& outError) {
  HookLifecycleEvent event;
  event.type = L"agent";
  event.action = L"bootstrap";
  event.sessionKey = sessionKey;
  event.bootstrapFiles = bootstrapFiles;

  if (!ValidateEventSchema(event, outError)) {
    ++m_snapshot.diagnostics.validationFailedCount;
    m_snapshot.diagnostics.warnings.push_back(outError);
    return false;
  }

  ++m_snapshot.sequence;
  ++m_snapshot.diagnostics.emittedCount;
  m_snapshot.events.push_back(std::move(event));

  if (m_snapshot.events.size() > kMaxEventHistory) {
    m_snapshot.events.erase(m_snapshot.events.begin());
    ++m_snapshot.diagnostics.droppedCount;
  }

  outError.clear();
  return true;
}

bool HookEventService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) {
  (void)fixturesRoot;
  outError.clear();

  HookEventService service;

  std::wstring emitError;
  const bool validEmitted = service.EmitAgentBootstrap(
      L"main",
      std::vector<HookBootstrapFile>{
          HookBootstrapFile{.path = L"BOOTSTRAP.md", .virtualFile = true}},
      emitError);
  if (!validEmitted) {
    outError =
        L"S8 hook events fixture failed: valid bootstrap event should emit.";
    return false;
  }

  const bool invalidEmitted = service.EmitAgentBootstrap(
      L"",
      std::vector<HookBootstrapFile>{
          HookBootstrapFile{.path = L"BOOTSTRAP.md", .virtualFile = true}},
      emitError);
  if (invalidEmitted) {
    outError =
        L"S8 hook events fixture failed: invalid bootstrap event should fail.";
    return false;
  }

  const auto snapshot = service.Snapshot();
  if (snapshot.diagnostics.emittedCount == 0) {
    outError =
        L"S8 hook events fixture failed: expected emitted event count > 0.";
    return false;
  }

  if (snapshot.diagnostics.validationFailedCount == 0) {
    outError =
        L"S8 hook events fixture failed: expected schema validation failure count > 0.";
    return false;
  }

  if (snapshot.events.empty() ||
      snapshot.events.back().action != L"bootstrap") {
    outError =
        L"S8 hook events fixture failed: expected latest event action=bootstrap.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
