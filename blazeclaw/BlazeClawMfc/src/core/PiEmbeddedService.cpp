#include "pch.h"
#include "PiEmbeddedService.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <regex>

namespace blazeclaw::core {

namespace {

std::uint64_t ResolveStartedAtMs(const std::size_t ordinal) {
  return 1735689700000 + static_cast<std::uint64_t>(ordinal);
}

std::string ToLowerCopy(const std::string& value) {
  std::string lowered = value;
  std::transform(
      lowered.begin(),
      lowered.end(),
      lowered.begin(),
      [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
      });
  return lowered;
}

bool ContainsAny(const std::string& text, const std::vector<std::string>& needles) {
  for (const auto& needle : needles) {
    if (!needle.empty() && text.find(needle) != std::string::npos) {
      return true;
    }
  }

  return false;
}

std::optional<std::string> TryExtractQuoted(const std::string& message) {
  static const std::regex kChineseSingleQuote(R"(‘([^’]{1,120})’)" );
  static const std::regex kAsciiSingleQuote(R"('([^']{1,120})')");

  std::smatch match;
  if (std::regex_search(message, match, kChineseSingleQuote) && match.size() >= 2) {
    return match[1].str();
  }

  if (std::regex_search(message, match, kAsciiSingleQuote) && match.size() >= 2) {
    return match[1].str();
  }

  return std::nullopt;
}

std::optional<std::string> FindToolByAlias(
    const std::vector<blazeclaw::gateway::ToolCatalogEntry>& tools,
    const std::vector<EmbeddedToolBinding>& bindings,
    const std::vector<std::string>& aliases) {
  for (const auto& alias : aliases) {
    for (const auto& tool : tools) {
      const std::string loweredTool = ToLowerCopy(tool.id);
      if (loweredTool.find(alias) != std::string::npos) {
        return tool.id;
      }
    }

    for (const auto& binding : bindings) {
      const std::string loweredCommand = ToLowerCopy(binding.commandName);
      const std::string loweredTool = ToLowerCopy(binding.toolName);
      if (loweredCommand.find(alias) != std::string::npos ||
          loweredTool.find(alias) != std::string::npos) {
        return binding.toolName;
      }
    }
  }

  return std::nullopt;
}

} // namespace

void PiEmbeddedService::Configure(const blazeclaw::config::AppConfig& appConfig) {
  m_config = appConfig;
}

EmbeddedRunResult PiEmbeddedService::QueueRun(const EmbeddedRunRequest& request) {
  if (!m_config.embedded.enabled) {
    return EmbeddedRunResult{
      .accepted = false,
      .runId = {},
      .status = "blocked",
      .reason = "embedded_runtime_disabled",
      .startedAtMs = 0,
    };
  }

  if (ActiveRuns() >= m_config.embedded.maxQueueDepth) {
    return EmbeddedRunResult{
      .accepted = false,
      .runId = {},
      .status = "blocked",
      .reason = "embedded_queue_full",
      .startedAtMs = 0,
    };
  }

  const std::uint64_t startedAtMs = ResolveStartedAtMs(m_runsById.size());
  const std::string runId =
      "embedded-" + std::to_string(startedAtMs) + "-" +
      (request.agentId.empty() ? "default" : request.agentId);

  EmbeddedRunRecord record;
  record.runId = runId;
  record.sessionId = request.sessionId.empty() ? "main" : request.sessionId;
  record.agentId = request.agentId.empty() ? "default" : request.agentId;
  record.message = request.message;
  record.status = "running";
  record.startedAtMs = startedAtMs;

  m_runsById.insert_or_assign(runId, record);

  return EmbeddedRunResult{
    .accepted = true,
    .runId = runId,
    .status = "running",
    .reason = "accepted",
    .startedAtMs = startedAtMs,
  };
}

EmbeddedRuntimeExecutionResult PiEmbeddedService::ExecuteRun(
    const EmbeddedRuntimeExecutionRequest& request) {
  EmbeddedRuntimeExecutionResult result;
  const std::string loweredMessage = ToLowerCopy(request.run.message);
  const bool hasSearchIntent = ContainsAny(
      loweredMessage,
      {"brave", "search", "搜索", "news", "动态"});
  const bool hasSummarizeIntent = ContainsAny(
      loweredMessage,
      {"summarize", "summary", "摘要", "浓缩"});
  const bool hasNotionIntent = ContainsAny(
      loweredMessage,
      {"notion", "每日早报", "页面", "写入"});

  std::vector<std::string> executionPlan;
  if (hasSearchIntent) {
    if (const auto tool = FindToolByAlias(
            request.runtimeTools,
            request.toolBindings,
            {"brave", "search"});
        tool.has_value()) {
      executionPlan.push_back(tool.value());
    }
  }

  if (hasSummarizeIntent) {
    if (const auto tool = FindToolByAlias(
            request.runtimeTools,
            request.toolBindings,
            {"summarize", "summary", "摘要"});
        tool.has_value()) {
      executionPlan.push_back(tool.value());
    }
  }

  if (hasNotionIntent) {
    if (const auto tool = FindToolByAlias(
            request.runtimeTools,
            request.toolBindings,
            {"notion", "page", "write"});
        tool.has_value()) {
      executionPlan.push_back(tool.value());
    }
  }

  executionPlan.erase(
      std::unique(executionPlan.begin(), executionPlan.end()),
      executionPlan.end());

  if (executionPlan.empty() || !request.toolExecutor) {
    result.accepted = true;
    result.handled = false;
    result.success = true;
    result.status = "completed";
    result.reason = "no_tool_orchestration_match";
    return result;
  }

  const auto queued = QueueRun(request.run);
  result.accepted = queued.accepted;
  result.runId = queued.runId;
  result.status = queued.status;
  result.reason = queued.reason;
  result.startedAtMs = queued.startedAtMs;

  if (!queued.accepted) {
    result.errorCode = "embedded_run_rejected";
    result.errorMessage = queued.reason;
    return result;
  }

  result.handled = true;
  result.decompositionSteps = executionPlan.size();

  const std::string query =
      TryExtractQuoted(request.run.message).value_or(request.run.message);
  std::string lastOutput;

  for (const auto& toolName : executionPlan) {
    result.assistantDeltas.push_back("tools.execute.start tool=" + toolName);

    nlohmann::json args = nlohmann::json::object();
    const std::string loweredTool = ToLowerCopy(toolName);
    if (loweredTool.find("search") != std::string::npos ||
        loweredTool.find("brave") != std::string::npos) {
      args["query"] = query;
      args["topK"] = 3;
    } else if (loweredTool.find("summ") != std::string::npos) {
      args["text"] = lastOutput.empty() ? request.run.message : lastOutput;
      args["maxChars"] = 100;
    } else if (loweredTool.find("notion") != std::string::npos) {
      args["page"] = "每日早报";
      args["content"] = lastOutput.empty() ? request.run.message : lastOutput;
    } else {
      args["input"] = lastOutput.empty() ? request.run.message : lastOutput;
    }

    const auto execution = request.toolExecutor(toolName, args.dump());
    result.assistantDeltas.push_back(
        "tools.execute.result tool=" +
        toolName +
        " status=" +
        execution.status);

    if (!execution.executed || execution.status == "error") {
      if (!CompleteRun(queued.runId, "failed", queued.startedAtMs + 1)) {
        result.errorCode = "embedded_completion_failed";
        result.errorMessage = "embedded completion failed";
      }
      result.success = false;
      result.status = "failed";
      result.reason = "tool_execution_failed";
      result.errorCode = "embedded_tool_execution_failed";
      result.errorMessage = execution.output.empty()
          ? "tool execution failed"
          : execution.output;
      return result;
    }

    lastOutput = execution.output;
  }

  if (!CompleteRun(queued.runId, "completed", queued.startedAtMs + 1)) {
    result.success = false;
    result.status = "failed";
    result.reason = "embedded_completion_failed";
    result.errorCode = "embedded_completion_failed";
    result.errorMessage = "embedded completion failed";
    return result;
  }
  result.success = true;
  result.status = "completed";
  result.reason = "orchestrated";
  result.assistantText = lastOutput.empty()
      ? "Embedded orchestration completed."
      : lastOutput;
  return result;
}

bool PiEmbeddedService::CompleteRun(
    const std::string& runId,
    const std::string& status,
    const std::uint64_t completedAtMs) {
  const auto it = m_runsById.find(runId);
  if (it == m_runsById.end()) {
    return false;
  }

  it->second.status = status.empty() ? "completed" : status;
  it->second.completedAtMs = completedAtMs;
  return true;
}

std::size_t PiEmbeddedService::ActiveRuns() const {
  std::size_t active = 0;
  for (const auto& [_, run] : m_runsById) {
    if (!run.completedAtMs.has_value()) {
      ++active;
    }
  }

  return active;
}

std::optional<EmbeddedRunRecord> PiEmbeddedService::GetRun(
    const std::string& runId) const {
  const auto it = m_runsById.find(runId);
  if (it == m_runsById.end()) {
    return std::nullopt;
  }

  return it->second;
}

bool PiEmbeddedService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig cfg;
  cfg.embedded.enabled = true;
  cfg.embedded.maxQueueDepth = 1;

  PiEmbeddedService service;
  service.Configure(cfg);

  const auto first = service.QueueRun(EmbeddedRunRequest{
      .sessionId = "main",
      .agentId = "alpha",
      .message = "hello",
  });

  if (!first.accepted) {
    outError = L"Fixture validation failed: expected first embedded run accepted.";
    return false;
  }

  const auto second = service.QueueRun(EmbeddedRunRequest{
      .sessionId = "main",
      .agentId = "beta",
      .message = "hello2",
  });

  if (second.accepted || second.reason != "embedded_queue_full") {
    outError = L"Fixture validation failed: expected queue-full rejection.";
    return false;
  }

  if (!service.CompleteRun(first.runId, "completed", first.startedAtMs + 1)) {
    outError = L"Fixture validation failed: expected run completion success.";
    return false;
  }

  const auto resolved = service.GetRun(first.runId);
  if (!resolved.has_value() || resolved->status != "completed") {
    outError = L"Fixture validation failed: expected completed embedded run status.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
