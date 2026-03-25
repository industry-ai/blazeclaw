#include "pch.h"
#include "HookExecutionService.h"

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
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

std::string ToNarrowAscii(const std::wstring& value) {
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
  }

  return output;
}

std::wstring EscapeJson(const std::wstring& value) {
  std::wstring escaped;
  escaped.reserve(value.size() + 8);
  for (const auto ch : value) {
    switch (ch) {
      case L'\\':
        escaped += L"\\\\";
        break;
      case L'"':
        escaped += L"\\\"";
        break;
      case L'\n':
        escaped += L"\\n";
        break;
      case L'\r':
        escaped += L"\\r";
        break;
      case L'\t':
        escaped += L"\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }

  return escaped;
}

std::wstring BuildEventJson(const HookLifecycleEvent& event) {
  std::wstringstream builder;
  builder << L"{\"type\":\"" << EscapeJson(event.type)
          << L"\",\"action\":\"" << EscapeJson(event.action)
          << L"\",\"sessionKey\":\"" << EscapeJson(event.sessionKey)
          << L"\",\"context\":{\"bootstrapFiles\":[";

  for (std::size_t i = 0; i < event.bootstrapFiles.size(); ++i) {
    const auto& file = event.bootstrapFiles[i];
    if (i > 0) {
      builder << L",";
    }

    builder << L"{\"path\":\"" << EscapeJson(file.path)
            << L"\",\"virtual\":"
            << (file.virtualFile ? L"true" : L"false")
            << L"}";
  }

  builder << L"]}}";
  return builder.str();
}

bool WriteUtf8File(const std::filesystem::path& filePath, const std::wstring& content) {
  std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  const std::string narrow = ToNarrowAscii(content);
  output.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
  return output.good();
}

std::wstring BuildTsRunnerScript() {
  return LR"JS(
const fsRead = async (p) => {
  try {
    const mod = await import('node:fs/promises');
    return await mod.readFile(p, 'utf8');
  } catch {
    return await Deno.readTextFile(p);
  }
};

const toFileUrl = async (p) => {
  try {
    const mod = await import('node:url');
    return mod.pathToFileURL(p).href;
  } catch {
    return new URL(`file://${p.replace(/\\/g, '/')}`).href;
  }
};

const main = async () => {
  const args = process.argv.slice(2);
  const handlerPath = args[0];
  const eventPath = args[1];
  const raw = await fsRead(eventPath);
  const event = JSON.parse(raw);
  if (!event.context || typeof event.context !== 'object') {
    event.context = { bootstrapFiles: [] };
  }
  if (!Array.isArray(event.context.bootstrapFiles)) {
    event.context.bootstrapFiles = [];
  }

  const before = event.context.bootstrapFiles.length;
  const handlerUrl = await toFileUrl(handlerPath);
  const mod = await import(handlerUrl);
  const handler = mod.default ?? mod.handler;
  if (typeof handler !== 'function') {
    console.log('ERROR\tmissing_handler_export');
    return 11;
  }

  await handler(event);

  const files = Array.isArray(event.context.bootstrapFiles)
    ? event.context.bootstrapFiles
    : [];
  for (let i = before; i < files.length; i++) {
    const item = files[i] || {};
    if (typeof item.path !== 'string') {
      continue;
    }
    const virtualFlag = item.virtual === true || item.virtualFile === true ? '1' : '0';
    console.log(`MUTATION\t${item.path}\t${virtualFlag}`);
  }

  return 0;
};

main()
  .then((code) => {
    if (typeof process !== 'undefined') {
      process.exit(code || 0);
    }
  })
  .catch((err) => {
    console.log(`ERROR\t${err?.message || 'runtime_failure'}`);
    if (typeof process !== 'undefined') {
      process.exit(12);
    }
  });
)JS";
}

bool ParseRunnerOutput(
    const std::vector<std::wstring>& lines,
    std::vector<HookBootstrapFile>& outMutations,
    std::wstring& outError) {
  outError.clear();
  for (const auto& rawLine : lines) {
    const auto line = Trim(rawLine);
    if (line.empty()) {
      continue;
    }

    if (line.rfind(L"ERROR\t", 0) == 0) {
      outError = L"TypeScript runtime execution failed: " + line.substr(6);
      return false;
    }

    if (line.rfind(L"MUTATION\t", 0) != 0) {
      continue;
    }

    const auto firstTab = line.find(L'\t', 9);
    if (firstTab == std::wstring::npos) {
      continue;
    }

    const auto path = Trim(line.substr(9, firstTab - 9));
    const auto flag = Trim(line.substr(firstTab + 1));
    if (path.empty()) {
      continue;
    }

    outMutations.push_back(
        HookBootstrapFile{.path = path, .virtualFile = (flag == L"1")});
  }

  return true;
}

bool ExecuteTypeScriptHook(
    const std::filesystem::path& handlerPath,
    const HookLifecycleEvent& event,
    std::vector<HookBootstrapFile>& outMutations,
    std::wstring& outRuntimeName,
    std::wstring& outError) {
  outMutations.clear();
  outRuntimeName.clear();
  outError.clear();

  std::error_code ec;
  const auto tempRoot = std::filesystem::temp_directory_path(ec) / L"blazeclaw-hook-runtime";
  std::filesystem::create_directories(tempRoot, ec);
  if (ec) {
    outError = L"Unable to initialize temporary hook runtime directory.";
    return false;
  }

  const auto nonce = std::to_wstring(
      static_cast<std::uint64_t>(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto runnerFile = tempRoot / (L"hook-runner-" + nonce + L".mjs");
  const auto eventFile = tempRoot / (L"hook-event-" + nonce + L".json");

  if (!WriteUtf8File(runnerFile, BuildTsRunnerScript())) {
    outError = L"Unable to write TypeScript runtime bridge script.";
    return false;
  }

  if (!WriteUtf8File(eventFile, BuildEventJson(event))) {
    outError = L"Unable to write TypeScript runtime event payload.";
    std::filesystem::remove(runnerFile, ec);
    return false;
  }

  wchar_t* runtimeOverride = nullptr;
  std::size_t runtimeLen = 0;
  std::vector<std::pair<std::wstring, std::wstring>> runtimeCandidates;
  if (_wdupenv_s(
          &runtimeOverride,
          &runtimeLen,
          L"BLAZECLAW_HOOK_TS_RUNTIME") == 0 &&
      runtimeOverride != nullptr && runtimeLen > 0) {
    runtimeCandidates.emplace_back(runtimeOverride, runtimeOverride);
  }
  if (runtimeOverride != nullptr) {
    free(runtimeOverride);
  }

  runtimeCandidates.emplace_back(L"bun", L"bun");
  runtimeCandidates.emplace_back(L"tsx", L"tsx");
  runtimeCandidates.emplace_back(L"node --loader ts-node/esm", L"node-ts-node");

  bool executed = false;
  std::wstring lastError;
  for (const auto& candidate : runtimeCandidates) {
    const auto command = candidate.first +
        L" \"" + runnerFile.wstring() +
        L"\" \"" + handlerPath.wstring() +
        L"\" \"" + eventFile.wstring() +
        L"\" 2>&1";
    FILE* pipe = _wpopen(command.c_str(), L"rt");
    if (pipe == nullptr) {
      lastError = L"runtime process launch failed";
      continue;
    }

    std::vector<std::wstring> lines;
    wchar_t buffer[512];
    while (fgetws(buffer, static_cast<int>(std::size(buffer)), pipe) != nullptr) {
      lines.emplace_back(buffer);
    }

    const int exitCode = _pclose(pipe);
    if (exitCode != 0) {
      std::wstring runtimeError;
      std::vector<HookBootstrapFile> ignored;
      if (ParseRunnerOutput(lines, ignored, runtimeError) && runtimeError.empty()) {
        runtimeError = L"non-zero process exit";
      }

      lastError = runtimeError.empty() ? L"non-zero process exit" : runtimeError;
      continue;
    }

    std::wstring parseError;
    if (!ParseRunnerOutput(lines, outMutations, parseError)) {
      lastError = parseError;
      continue;
    }

    outRuntimeName = candidate.second;
    executed = true;
    break;
  }

  std::filesystem::remove(runnerFile, ec);
  std::filesystem::remove(eventFile, ec);

  if (!executed) {
    outError =
        L"TypeScript runtime unavailable or failed. Last error: " +
        (lastError.empty() ? L"unknown" : lastError);
    return false;
  }

  return true;
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
      std::wstring runtimeName;
      std::wstring runtimeError;
      if (!ExecuteTypeScriptHook(
              hook->handlerFile,
              event,
              proposed,
              runtimeName,
              runtimeError)) {
        ++m_snapshot.diagnostics.failureCount;
        ++m_snapshot.diagnostics.reminderSkippedCount;
        m_snapshot.diagnostics.lastReminderState = L"reminder_skipped";
        m_snapshot.diagnostics.lastReminderReason = L"ts_runtime_failure";
        m_snapshot.diagnostics.warnings.push_back(
            runtimeError);
        continue;
      }

      if (!runtimeName.empty()) {
        m_snapshot.diagnostics.engineMode = runtimeName;
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
