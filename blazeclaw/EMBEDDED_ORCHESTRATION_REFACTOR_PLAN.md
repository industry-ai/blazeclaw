# Embedded Orchestration Adapter Refactor & Hardening Plan

## Goal
Refactor BlazeClaw embedded orchestration so it is task-decomposition and tool-execution agnostic, supports LLM-driven dynamic tool calls, and emits ordered task-delta metadata (`tool`, `args`, `result`, status, timing) for verification and replay.

## Success Criteria
- No hardcoded flow patterns in adapter core (for example specific brave/summarize/notion matching logic).
- Any skill/tool combination can execute when declared via skills prompt + command snapshot + tool registry.
- Runtime supports iterative LLM-guided tool calling until terminal assistant response.
- Task-delta stream captures ordered executions with deterministic metadata for test assertions.
- Existing weather/email behavior remains supported via compatibility layer or strategy plugin.

## Scope
- In scope: orchestration adapter architecture, runtime execution loop, schemas, telemetry, tests, rollout controls.
- Out of scope: provider SDK rewrites unrelated to tool orchestration, UI redesign.

## Workstreams

### 1) Architecture Refactor: Decomposition Engine vs Execution Engine
**Objective:** Split concerns so decomposition logic is model-driven and execution transport is generic.

- Introduce adapter layers:
  - `Plan/Decomposition Layer`: asks LLM to produce next actionable step/tool call.
  - `Execution Layer`: validates and executes requested tool via runtime API.
  - `State Layer`: stores run context, intermediate artifacts, task deltas.
  - `Termination Layer`: decides continue/stop/retry/fail.
- Remove hardcoded tool ordering and intent-specific branches from core adapter.
- Keep specialized legacy flows as optional strategy plugins behind feature flag.

**Deliverables**
- New adapter interfaces and implementation split.
- Legacy path wrapper for backward compatibility.

### 2) Dynamic Tool Call Protocol
**Objective:** Support arbitrary LLM-selected tool calls with strict validation.

- Define model-to-runtime tool call contract:
  - requested tool name
  - arguments payload
  - rationale or step label
  - optional dependencies on previous outputs
- Add guardrails:
  - tool existence check
  - argument schema validation
  - allow/deny policy enforcement
  - max iterations / loop detection
  - timeout and cancellation propagation
- Add retry policy for transient tool/runtime failures.

**Deliverables**
- Dynamic tool call loop with configurable limits and safety checks.
- Structured error categories for deterministic handling.

### 3) Task Delta Model and Observability
**Objective:** Standardize execution trace metadata for deterministic order verification.

- Define `TaskDelta` structure for each step:
  - `index`, `runId`, `sessionId`, `phase` (`plan|tool_call|tool_result|final`)
  - `toolName`, `argsJson`, `resultJson`, `status`, `errorCode`
  - timing (`startedAtMs`, `completedAtMs`, `latencyMs`)
  - optional `modelTurnId` and `stepLabel`
- Persist ordered deltas in-memory and expose through runtime/gateway query API.
- Emit telemetry events for each delta transition.

**Deliverables**
- Unified task-delta type in runtime core.
- Query endpoint(s) for ordered delta retrieval.

### 4) Skills Prompt / Command Snapshot Schema Hardening
**Objective:** Make skills metadata expressive enough for generic orchestration.

- Extend command snapshot fields (proposal):
  - `toolName` (required for dispatch kind=tool)
  - `argSchema` or schema reference
  - `resultSchema` (optional)
  - `idempotencyHint` / side-effect category
  - `retryPolicyHint`
  - `requiresApproval` metadata
- Extend skills prompt representation for planner context:
  - concise tool capability descriptors
  - preconditions and side-effect notes
- Ensure backward compatibility for existing snapshot consumers.

**Deliverables**
- Updated schema docs and parser updates.
- Compatibility fallback for legacy skill entries.

### 5) Runtime Tool Execution API Evolution
**Objective:** Provide a stable generic execution API for orchestration core.

- Promote runtime API from simple execute call to richer contract:
  - request: `tool`, `args`, correlation metadata, deadline
  - response: `executed`, `status`, `result`, `error`, `timing`
- Add standardized serialization/parsing helpers for args and results.
- Add capability discovery endpoint for adapter startup binding.

**Deliverables**
- Versioned runtime tool execution contract.
- Adapter integration with new API.

### 6) Verification Strategy
**Objective:** Prove deterministic behavior and genericity.

- Add parity tests for target prompt:
  - ordered tool sequence contains brave-search -> summarize -> notion
  - decomposition steps >= 3
  - final response includes Notion write outcome
- Add generic matrix tests:
  - arbitrary 2/3/4-step tool combinations from snapshots
  - tool failures, retries, cancellations, and partial completion
  - loop protection and max-step enforcement
- Add schema compatibility tests for legacy snapshots.

**Deliverables**
- Automated tests and fixtures for delta-order assertions.
- Regression suite for adapter behavior.

### 7) Rollout and Risk Control
**Objective:** Safely deploy refactor without breaking current runtime.

- Feature flag/config key: `embedded.dynamicToolLoopEnabled`.
- Canary rollout by provider/session scope.
- Fallback path: provider one-shot or legacy orchestrator when critical failures occur.
- Add runtime diagnostics summary for production triage.

**Deliverables**
- Rollout checklist.
- Observability dashboard fields and failure playbook.

## Step-by-Step Implementation Process

## Process Optimization (Smoke-Test-Driven)

To reduce integration risk and speed up feedback, execute refactor work in two
verification lanes that run throughout Steps 0-12:

- **Lane A (Operational Smoke):**
  `weather.lookup -> report.compose -> email.schedule`
- **Lane B (Parity Smoke):**
  `brave-search -> summarize -> notion`

Each implementation step must produce task-delta evidence for at least one lane,
and Steps 6-10 must produce evidence for both lanes.

### Process Gates

1. **Design Gate (before Step 3):**
   Task-delta schema finalized (`plan|tool_call|tool_result|final`) with ordered
   correlation fields.
2. **Execution Gate (before Step 8):**
   Dynamic loop supports arbitrary tool selection from command snapshots with no
   hardcoded flow-specific branching in adapter core.
3. **Parity Gate (before Step 11):**
   Lane A and Lane B pass deterministic order assertions with expected terminal
   outputs.
4. **Rollout Gate (before Step 12):**
   Feature-flagged canary passes and fallback strategy validated.

### Step 0 — Baseline and Guardrails
**Implement**
- Freeze current adapter behavior with baseline tests for existing paths (including weather/email).
- Add feature flag scaffold: `embedded.dynamicToolLoopEnabled` (default `false` at baseline stage).
- Register two smoke fixtures as CI references (Lane A and Lane B).

**Deliverables**
- Baseline regression tests.
- Feature flag config wiring.

**Acceptance**
- Existing runtime behavior unchanged when flag is off.
- Smoke fixtures can run in baseline mode and produce comparable traces.

### Step 1 — Introduce Task Delta Core Model
**Implement**
- Add unified `TaskDelta` and `TaskDeltaStatus` types in embedded runtime core.
- Add in-memory ordered delta store per run/session.
- Add helper APIs to append deltas with timestamps and correlation IDs.

**Deliverables**
- Core data structures and append/query helpers.

**Acceptance**
- Adapter can emit `plan`, `tool_call`, `tool_result`, and `final` deltas in strict order.
- Delta model can represent both Lane A and Lane B without schema changes.

### Step 2 — Extract Adapter Layers (No Behavior Change Yet)
**Implement**
- Split current embedded adapter into modules:
  - decomposition/planning
  - tool execution
  - state/context
  - termination policy
- Keep existing sequence logic temporarily through a legacy strategy module.

**Deliverables**
- New interfaces + module boundaries.

**Acceptance**
- Build passes and baseline tests remain green with identical behavior.

### Step 3 — Runtime Tool Execution API v2
**Implement**
- Introduce versioned execution contract:
  - request: tool, args, correlation metadata, deadline/timeout
  - response: executed, status, result, error, timing
- Add compatibility adapter from old execute API to v2.

**Deliverables**
- API v2 types and adapter bridge.

**Acceptance**
- Existing tools run through compatibility bridge without functional change.

### Step 4 — Skills Command Schema Hardening
**Implement**
- Extend command snapshot with optional metadata:
  - `argSchema`, `resultSchema`, `idempotencyHint`, `retryPolicyHint`, `requiresApproval`
- Preserve compatibility for legacy skills missing new fields.

**Deliverables**
- Updated snapshot builders/parsers.
- Schema documentation updates.

**Acceptance**
- Legacy snapshots continue to load and execute.

### Step 5 — Skills Prompt Planner Context Upgrade
**Implement**
- Extend prompt generation with concise capability descriptors, preconditions, and side-effect hints.
- Ensure prompt stays bounded by existing limits/truncation rules.

**Deliverables**
- Planner-oriented skills prompt format.

**Acceptance**
- Prompt includes enough metadata for model-driven decomposition without exceeding limits.

### Step 6 — Add LLM-Driven Decomposition Loop
**Implement**
- Add iterative planning-execution loop:
  1) model proposes next step/tool call
  2) validate
  3) execute tool
  4) feed result back into next turn
  5) stop on terminal response or policy limit
- Persist each iteration as task deltas.

**Deliverables**
- Dynamic loop implementation with model turn state.

**Acceptance**
- No hardcoded tool sequence in adapter core.
- For both lanes, tool selection is produced by model turns + snapshot/tool metadata.

### Step 7 — Safety and Termination Policies
**Implement**
- Add guardrails:
  - max step count
  - repeated-call/loop detector
  - timeout/deadline handling
  - allow/deny policy checks
  - argument validation hooks
- Add retry policy for transient execution errors.

**Deliverables**
- Configurable policy module + error taxonomy.

**Acceptance**
- Runaway loops and invalid tool calls are deterministically blocked.

### Step 8 — Gateway/Runtime Observability Surface
**Implement**
- Expose ordered task deltas via runtime/gateway query endpoint(s).
- Emit telemetry per delta transition and run summary.

**Deliverables**
- Delta query API and telemetry wiring.

**Acceptance**
- External callers can verify tool execution order and results by run ID.
- Smoke tests can query and assert task deltas without parsing freeform logs.

### Step 9 — Parity Test Case for Brave→Summarize→Notion
**Implement**
- Add fixture for the target Chinese prompt.
- Assert:
  - decomposition steps >= 3
  - ordered deltas include brave-search -> summarize -> notion
  - final output includes Notion write result

**Deliverables**
- Deterministic parity test.

**Acceptance**
- Test passes under dynamic loop mode.

### Step 9A — Operational Smoke Test for Weather→Report→Email
**Implement**
- Add fixture for:
  `Check today's weather in Wuhan, write a short report, and email it to jichengwhu@163.com now.`
- Assert:
  - decomposition steps >= 3
  - ordered deltas include weather.lookup -> report.compose -> email.schedule
  - final output includes email scheduling/sending result

**Deliverables**
- Deterministic operational smoke test with ordered task-delta assertions.

**Acceptance**
- Test passes under dynamic loop mode with immediate execution semantics (`now`).

### Step 10 — Generic Matrix and Failure-Mode Tests
**Implement**
- Add matrix tests for arbitrary 2/3/4-step tool combinations.
- Add failure/retry/cancel/approval-gated scenarios.
- Add schema compatibility tests (legacy vs enriched snapshots).

**Deliverables**
- Expanded regression suite.

**Acceptance**
- Core behaviors are stable across generalized and failure scenarios.
- Lane A and Lane B continue to pass as regression anchors.

### Step 11 — Staged Rollout
**Implement**
- Enable dynamic loop behind feature flag for canary provider/session scopes.
- Add fallback to legacy strategy for critical failures.

**Deliverables**
- Rollout controls and fallback policy.

**Acceptance**
- Canary runs stable; rollback path validated.

### Step 12 — Default-On and Cleanup
**Implement**
- Flip default to dynamic loop after stability window.
- Remove obsolete hardcoded branching from adapter core.
- Keep legacy strategy only as optional fallback module.

**Deliverables**
- Cleaned adapter core and final docs.

**Acceptance**
- Dynamic task-delta decomposition is default behavior with maintained compatibility.

## Execution Checklist (Tracking)
- [x] Step 0 complete
- [x] Step 1 complete
- [x] Step 2 complete
- [x] Step 3 complete
- [x] Step 4 complete
- [x] Step 5 complete
- [x] Step 6 complete
- [x] Step 7 complete
- [x] Step 8 complete
- [x] Step 9 complete
- [x] Step 9A complete
- [x] Step 10 complete
- [x] Step 11 complete
- [x] Step 12 complete

## Implementation Status Update

### Completed now: Step 0 (Baseline and Guardrails)
- Added feature flag scaffold: `embedded.dynamicToolLoopEnabled` (default `false`) in runtime config model and loader.
- Added corresponding entries to config templates (`blazeclaw.conf` variants) for controlled rollout.
- Wired runtime to respect the flag before embedded dynamic orchestration handling.

### Completed now: Step 1 (Task Delta Core Model)
- Added unified in-memory task-delta structure in `PiEmbeddedService`.
- Added ordered append/query helpers (`AppendTaskDelta`, `GetTaskDeltas`, `ClearTaskDeltas`).
- Added delta emission phases: `plan`, `tool_call`, `tool_result`, `final`.
- Added fixture validations for:
  - dynamic loop disabled baseline path,
  - ordered task-delta index and phase coverage when dynamic loop is enabled.

### Completed now: Step 2 (Adapter Layer Extraction)
- Extracted planning concern into dedicated helper strategy (`ResolveLegacyAliasExecutionPlan`).
- Extracted tool argument construction into execution helper (`BuildLegacyToolArgs`).
- Extracted termination state shaping into dedicated helper (`FinalizeExecutionResult`).
- Preserved runtime behavior and task-delta semantics while splitting concerns for next dynamic-loop migration.

### Completed now: Step 3 (Runtime Tool Execution API v2)
- Added versioned tool execution request/response contract (`ToolExecuteRequestV2`, `ToolExecuteResultV2`) with correlation and timing metadata.
- Implemented `GatewayToolRegistry::ExecuteV2(...)` with compatibility bridge to legacy `Execute(...)`.
- Added host surface `GatewayHost::ExecuteRuntimeToolV2(...)` and wired ServiceManager embedded runtime callback to use v2.
- Updated embedded orchestration execution path to consume v2 responses while preserving legacy fallback behavior.

### Completed now: Step 4 (Skills Command Schema Hardening)
- Extended skills command dispatch schema with `argSchema`, `resultSchema`, `idempotencyHint`, `retryPolicyHint`, and `requiresApproval`.
- Parsed new metadata from skill frontmatter with backward-compatible defaults for legacy entries.
- Extended fixture validation and fixture data to assert hardened metadata parsing and legacy fallback behavior.
- Surfaced command metadata through gateway state and `gateway.skills.commands` response payload.

### Completed now: Step 5 (Skills Prompt Planner Context Upgrade)
- Extended prompt snapshot with planner context metadata per included skill (capability, preconditions, side effects, command tool hint).
- Parsed planner metadata from frontmatter with bounded fallback values and truncation safeguards.
- Added planner-oriented prompt block generation (`## Planner Context`) while preserving existing prompt limits and compatibility behavior.
- Extended S2 prompt fixture inputs and assertions to validate planner context parsing and rendering.

### Completed now: Step 6 (LLM-Driven Decomposition Loop)
- Replaced fixed legacy plan selection entrypoint with dynamic decomposition planning that derives ordered tool steps from `skillsPrompt + user message` and declared command/runtime metadata.
- Added iterative step execution with per-step model-turn correlation (`model-turn-{index}`) and dynamic arg construction honoring binding `argMode`.
- Preserved fallback behavior by retaining legacy alias-based argument defaults when richer metadata is unavailable.
- Extended embedded fixture validation to assert deterministic dynamic tool-call order (`brave-search -> summarize -> notion.write`).

### Completed now: Step 7 (Safety and Termination Policies)
- Added runtime guardrails for max-step enforcement, repeated-call loop detection, runtime tool allow checks, and arg-mode validation.
- Added timeout/deadline enforcement from `embedded.runTimeoutMs` and propagated deadlines to v2 tool execution requests.
- Added bounded transient retry policy for tool execution failures using status/error heuristics.
- Preserved deterministic failure handling by emitting terminal task deltas with stable error codes (`embedded_deadline_exceeded`, `embedded_tool_blocked`, etc.).
- Extended fixture validations to cover timeout and blocked-tool policy failures.

### Completed now: Step 8 (Gateway/Runtime Observability Surface)
- Extended runtime chat result contract to carry ordered task-delta entries from embedded execution to gateway runtime handlers.
- Persisted run-scoped task deltas in gateway memory with bounded retention for diagnostics and verification queries.
- Added telemetry emission for task-delta transitions and per-run task-delta summary metrics.
- Added runtime query endpoints:
  - `gateway.runtime.taskDeltas.get` (ordered retrieval by runId)
  - `gateway.runtime.taskDeltas.clear` (per-run or global cache clear)
- Registered task-delta query endpoints once during runtime handler startup to avoid per-`chat.send` dispatcher mutation.

### Completed now: Step 9 (Parity Test Case for Brave→Summarize→Notion)
- Added deterministic parity fixture scenario in embedded runtime validation using the target Chinese prompt.
- Added assertions for `decompositionSteps >= 3` under dynamic loop mode.
- Added ordered `tool_call` sequence assertions for `brave-search -> summarize -> notion.write`.
- Added terminal output assertion ensuring final assistant text includes Notion write result evidence for page `每日早报`.

### Completed now: Step 9A (Operational Smoke Test for Weather→Report→Email)
- Added deterministic operational smoke fixture scenario using prompt: weather in Wuhan -> short report -> email now.
- Added assertions for `decompositionSteps >= 3` under dynamic loop mode.
- Added ordered `tool_call` sequence assertions for `weather.lookup -> report.compose -> email.schedule`.
- Added terminal output assertion ensuring final assistant text includes email scheduling/sending result for `jichengwhu@163.com`.

### Completed now: Step 10 (Generic Matrix and Failure-Mode Tests)
- Added generalized matrix fixture coverage for deterministic 2-step, 3-step, and 4-step tool combinations with ordered `tool_call` task-delta assertions.
- Added transient failure retry fixture scenario verifying bounded retry recovery behavior.
- Added hard failure fixture scenario asserting deterministic terminal error code handling.
- Added approval-gated scenario validation for `needs_approval` tool-result deltas and successful terminal completion.
- Added legacy snapshot compatibility scenario validating execution when binding metadata (e.g., `argMode`) is missing.

### Completed now: Step 11 (Staged Rollout)
- Added canary rollout scope controls for embedded dynamic loop using provider/session targeting from environment lists.
- Gated dynamic-loop activation on both feature flag and canary eligibility decisions.
- Added fallback policy so critical embedded orchestration failures can continue through existing provider/local runtime path.
- Added embedded rollout diagnostics fields (`dynamicLoopEnabled`, `canaryEligible`, `fallbackUsed`, `fallbackReason`) to operator report output.

### Completed now: Step 12 (Default-On and Cleanup)
- Flipped embedded dynamic loop defaults to enabled in runtime config model, config loader fallback parsing, and shipped config templates.
- Removed obsolete legacy alias planning fallback from embedded decomposition core to keep planning metadata-driven.
- Preserved optional compatibility behavior through execution bridge fallbacks and staged rollout fallback controls.
- Finalized refactor checklist with dynamic task-delta orchestration as default behavior.

## Primary File Targets (Expected)
- `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.h`
- `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.cpp`
- `blazeclaw/BlazeClawMfc/src/core/SkillsCommandService.h`
- `blazeclaw/BlazeClawMfc/src/core/SkillsCommandService.cpp`
- `blazeclaw/BlazeClawMfc/src/core/SkillsPromptService.h`
- `blazeclaw/BlazeClawMfc/src/core/SkillsPromptService.cpp`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.h`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`
- `blazeclaw/PARITY_PORTING_ANALYSIS.md`

## Open Questions
- Which provider models are authoritative for decomposition turns in mixed-provider mode?
- Should task deltas be persisted beyond process lifetime for audit/replay?
- Do we require strict JSON schema validation for all tool args before execution?
- How should approval-gated tools surface paused/resume states in task delta stream?

## Risks
- Overly strict schema validation may block existing skill tools.
- Dynamic loop without robust stop conditions may create runaway calls.
- Backward-compatibility gaps in snapshot fields may degrade existing flows.

## Mitigations
- Compatibility adapter for missing snapshot fields.
- Hard max step count + repeated-call detector.
- Fallback to legacy path under feature flag with telemetry annotation.

## Milestones
- M1: Architecture split + task-delta model ready.
- M2: Dynamic tool loop + runtime API v2 integrated.
- M3: Schema hardening + compatibility tests green.
- M4: Brave-search/summarize/notion parity assertions green.
- M5: Staged rollout enabled with diagnostics.

## Post-Implementation Procedure Review

### Review scope
- Reviewed end-to-end execution across Steps 0-12.
- Reviewed process gates (Design, Execution, Parity, Rollout).
- Reviewed runtime behavior alignment with smoke lanes A/B.
- Reviewed documentation consistency and residual action items.

### Outcome summary by objective
- **Architecture objective:** Met.
  - Adapter concerns are split and dynamic decomposition is now the default planning path.
- **Dynamic tool protocol objective:** Met.
  - Runtime uses versioned execution contracts, policy guardrails, retry logic, and deterministic failure codes.
- **Task-delta observability objective:** Met.
  - Ordered deltas are emitted, surfaced via gateway query endpoints, and instrumented with telemetry.
- **Schema hardening objective:** Met.
  - Command/prompt metadata enriched with backward-compatible defaults.
- **Parity objective (lane B):** Met.
  - Brave -> summarize -> notion ordered assertions and final Notion evidence are covered.
- **Operational smoke objective (lane A):** Met.
  - Weather -> report -> email ordered assertions and final email evidence are covered.

### Process-gate assessment
- **Design Gate (before Step 3):** Passed.
  - Task-delta schema and phases finalized and enforced in validation.
- **Execution Gate (before Step 8):** Passed.
  - Core dynamic loop no longer depends on flow-specific hardcoded planning branches.
- **Parity Gate (before Step 11):** Passed.
  - Lane A and Lane B deterministic order and terminal evidence checks are present.
- **Rollout Gate (before Step 12):** Passed.
  - Canary controls and fallback diagnostics implemented and exposed in runtime report.

### Key decisions captured
- Dynamic loop was promoted to default-on only after guardrails, observability, and parity coverage were in place.
- Compatibility was preserved through execution-bridge fallback paths rather than keeping legacy planning in core.
- Runtime/gateway observability was standardized around run-scoped ordered task-delta retrieval.

### Residual follow-up items (non-blocking)
- Define authoritative decomposition-provider policy for mixed-provider sessions.
- Decide persistence strategy for task deltas beyond process lifetime (optional audit/replay durability).
- Decide strictness profile for universal tool-argument schema validation in production.
- Define paused/resume semantics for approval-gated tools in task-delta lifecycle model.

### Final readiness statement
The refactor procedure is complete and coherent against the original plan scope.
Dynamic task-delta decomposition is now default behavior with staged-rollout controls,
deterministic verification lanes, and compatibility-preserving fallback behavior.

## Operator Runbook Checklist

### 1) Canary environment variables
- Set canary providers:
  - `BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_CANARY_PROVIDERS=deepseek,local`
- Set canary sessions:
  - `BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_CANARY_SESSIONS=main,canary-session-1`
- Runtime config key for dynamic loop:
  - `embedded.dynamicToolLoopEnabled=true`

### 2) Rollback trigger conditions
- Trigger rollback/canary reduction when any of the following is sustained:
  - rising `fallbackUsed=true` with repeated `fallbackReason` in diagnostics,
  - repeated critical embedded failures (`embedded_deadline_exceeded`, `embedded_tool_execution_failed`, `embedded_loop_detected`),
  - smoke-lane order regressions (Lane A or Lane B task-delta sequence mismatch),
  - gateway task-delta query shows missing `tool_call/tool_result` transitions.

### 3) Quick health queries
- Runtime diagnostics snapshot (embedded rollout state):
  - read `embedded.dynamicLoopEnabled`, `embedded.canaryEligible`, `embedded.fallbackUsed`, `embedded.fallbackReason` from operator diagnostics report.
- Task-delta retrieval by run:
  - `gateway.runtime.taskDeltas.get` with `runId`.
- Task-delta cache maintenance:
  - `gateway.runtime.taskDeltas.clear` with `runId` (or omit to clear all).
- Skills/runtime sanity checks:
  - `gateway.skills.status`
  - `gateway.skills.commands`


# Smoke Test Prompts for Process Verification

## Lane A: Operational smoke (weather/report/email)

User prompt:
```
Check today's weather in Wuhan, write a short report, and email it to jichengwhu@163.com now.
```

Use LLM-driven decomposition and verify ordered runtime task deltas:

Expected ordered tool execution:
```
weather.lookup -> report.compose -> email.schedule
```

Expected terminal output:
```
Email scheduling/sending result for jichengwhu@163.com with report content.
```

## Lane B: Parity smoke (brave/summarize/notion)

User prompt:
```
使用 brave-search multi-search-engine搜索‘今日大模型行业最新动态’，提取前 3 条核心新闻。接着用 Summarize 把它们浓缩成一段 100 字以内的摘要，最后调用 Notion 技能，将摘要写入我的 Notion ‘每日早报’ 页面中。
```

Use LLM-driven decomposition and verify ordered runtime task deltas:

Expected ordered tool execution:
```
brave-search -> summarize -> notion
```

Expected terminal output:
```
Notion write result confirming update to page '每日早报'.
```

## Decomposition Prompt Template (for both lanes)

```
Please split the following sentence into executable tasks with explicit tool calls,
ordered dependencies, and completion criteria:
`<USER_PROMPT>`
```

## Task-Delta Verification Requirements

- Every tool execution must emit `tool_call` and `tool_result` deltas.
- Deltas must include `toolName`, `argsJson`, `status`, and `resultJson`.
- Order assertions must use task-delta indices, not freeform text logs.
- Final assistant output must include lane-specific terminal success evidence.


Task: Understand the full scope and sequence of activities.
Output: A clear breakdown of three main action items.


2. Check today’s weather in Wuhan


Sub-task 2.1: Determine the current date and time (to confirm "today").
Sub-task 2.2: Query a weather API or source for Wuhan’s weather data (e.g., temperature, conditions, forecast).
Sub-task 2.3: Format the retrieved data into a human-readable summary.


3. Write a short report


Sub-task 3.1: Define report structure (e.g., date, location, weather summary, notable details).
Sub-task 3.2: Insert the formatted weather data into the report.
Sub-task 3.3: Review/edit for clarity and completeness.


4. Email it to jichengwhu@163.com


Sub-task 4.1: Validate recipient email format (jichengwhu@163.com).
Sub-task 4.2: Prepare email details (subject, body, attachment if needed).
Sub-task 4.3: Send the email using an SMTP service or email client automation.


5. “Now” constraint


Task: Ensure the entire workflow is triggered immediately, not scheduled for later.
Note: This may require error handling and timeout rules if any step fails.


Final grouped executable task list:

1. Get Wuhan weather data for today  
   • Fetch date  

   • Call weather API for Wuhan  

   • Parse and format result

3. Generate weather report  
   • Create report template  

   • Populate with weather data  

   • Finalize text

5. Send email with report  
   • Set email subject/body  

   • Attach or embed report  

   • Send to jichengwhu@163.com

7. Log completion / handle errors  
   • Confirm sent status  

   • Notify if any step failed

Would you like me to detail any of these tasks further, for example, with specific API examples or email automation code snippets?
```

Find out tools and skills needed for each step, and verify that the runtime executes them in the correct order with expected results. 

Use the registered runtime tools and skills to accomplish task-delta and verify the results for each step, ensuring the final output includes the Notion write result.
