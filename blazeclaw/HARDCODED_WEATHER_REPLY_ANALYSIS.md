# Hardcoded Weather Reply Issue Analysis and Suggestions

## Problem Summary
When the user enters:

`Check today's weather in Wuhan, write a short report, and email it to jichengwhu@163.com now.`

the runtime can return a deterministic orchestration-style response instead of a true data-driven result path. The UI expectation is:
1. show decomposed tasks in WebView2,
2. execute each task one by one,
3. show each step result in order.

Current behavior can bypass the dynamic task-delta path and produce hardcoded-like output text/deltas.

## Evidence from Current Implementation

### 1) Prompt-level hardcoded orchestration interception in gateway runtime handler
- File: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.cpp`
- `TryOrchestrateWeatherEmailPrompt(...)` builds a fixed weather-email flow for matching prompts (`weather.lookup` -> report composition -> `email.schedule`).
- Relevant section: around lines 546-704.
- It also assembles fixed assistant delta strings manually:
  - `tools.execute.start tool=weather.lookup`
  - `task.execute.start task=report.compose`
  - `tools.execute.start tool=email.schedule action=prepare`
- Relevant section: around lines 682-689.

This means matching prompts can be handled by this specialized branch instead of generic dynamic tool decomposition.

### 2) Task-delta persistence is tied to runtime callback path, not orchestration shortcut path
- File: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.cpp`
- The weather orchestration shortcut is executed before `m_chatRuntimeCallback`.
- Task-delta persistence (`persistTaskDeltas(runtimeResult.taskDeltas, runtimeResult.ok)`) is executed in the runtime callback block.
- Relevant section: around lines 1749-1805.

Impact: if shortcut orchestration path is used, WebView2 may only receive provider-style text deltas instead of structured `taskDeltas` (`plan|tool_call|tool_result|final`) for detailed step visualization.

### 3) Embedded decomposition is still heuristic, not true LLM iterative planning
- File: `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.cpp`
- `ResolveDynamicExecutionPlan(...)` currently ranks tools by keyword position in `skillsPrompt + message` and visited-set ordering.
- Relevant section: around lines 182-237.

This is deterministic and generic-ish but not a true model-driven decomposition turn loop. It can still look hardcoded from user perspective when outputs are templated.

## Root Cause
The main cause of the observed "hardcoded reply" is the **specialized weather-email orchestration shortcut in gateway runtime handler** being applied before the embedded runtime callback path that emits and persists structured task-deltas.

## Three Main Action Items

### Action Item 1 — Remove or gate the weather-email shortcut from default runtime path
- Add feature-flag guard for `TryOrchestrateWeatherEmailPrompt(...)` (default off in production path), or remove this branch from core flow.
- Ensure weather/report/email requests go through `m_chatRuntimeCallback` -> embedded runtime dynamic loop.

### Action Item 2 — Make WebView2 consume structured task-deltas as first-class timeline data
- Ensure every orchestration path emits structured task deltas (`plan`, `tool_call`, `tool_result`, `final`).
- In UI, render per-delta cards (tool name, args, status, result, timing, error code) by index order.
- Prefer querying `gateway.runtime.taskDeltas.get` by runId rather than parsing freeform assistant delta strings.

### Action Item 3 — Replace heuristic planner with real iterative model-driven decomposition
- Upgrade `ResolveDynamicExecutionPlan(...)` from keyword scanning to iterative model turn planning:
  1) model proposes next tool call,
  2) runtime validates and executes,
  3) result fed back to next model turn,
  4) stop on terminal response/policy limits.
- Keep guardrails (max steps, loop detection, timeout, allow/deny, schema checks).

## Verification Checklist (Post-fix)
1. For Lane A prompt (Wuhan weather/report/email):
   - Ordered `tool_call` deltas: `weather.lookup -> report.compose -> email.schedule`.
   - Ordered `tool_result` deltas present for each call.
   - WebView2 shows decomposition + each execution result.
2. For Lane B prompt (brave/summarize/notion):
   - Ordered `tool_call` deltas: `brave-search -> summarize -> notion.write`.
   - Final output includes Notion write result evidence.
3. No flow-specific hardcoded matching in adapter core path.

## Suggested Priority
- **P0:** Action Item 1 + Action Item 2 (immediately restores user-visible behavior in WebView2).
- **P1:** Action Item 3 (completes architecture objective and removes hardcoded perception entirely).
