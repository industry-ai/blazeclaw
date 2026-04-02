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

- Feature flag: `embedded.orchestration.dynamicToolLoop.enabled`.
- Canary rollout by provider/session scope.
- Fallback path: provider one-shot or legacy orchestrator when critical failures occur.
- Add runtime diagnostics summary for production triage.

**Deliverables**
- Rollout checklist.
- Observability dashboard fields and failure playbook.

## Step-by-Step Implementation Process

### Step 0 — Baseline and Guardrails
**Implement**
- Freeze current adapter behavior with baseline tests for existing paths (including weather/email).
- Add feature flag scaffold: `embedded.orchestration.dynamicToolLoop.enabled` (default `false`).

**Deliverables**
- Baseline regression tests.
- Feature flag config wiring.

**Acceptance**
- Existing runtime behavior unchanged when flag is off.

### Step 1 — Introduce Task Delta Core Model
**Implement**
- Add unified `TaskDelta` and `TaskDeltaStatus` types in embedded runtime core.
- Add in-memory ordered delta store per run/session.
- Add helper APIs to append deltas with timestamps and correlation IDs.

**Deliverables**
- Core data structures and append/query helpers.

**Acceptance**
- Adapter can emit `plan`, `tool_call`, `tool_result`, and `final` deltas in strict order.

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

### Step 10 — Generic Matrix and Failure-Mode Tests
**Implement**
- Add matrix tests for arbitrary 2/3/4-step tool combinations.
- Add failure/retry/cancel/approval-gated scenarios.
- Add schema compatibility tests (legacy vs enriched snapshots).

**Deliverables**
- Expanded regression suite.

**Acceptance**
- Core behaviors are stable across generalized and failure scenarios.

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
- [ ] Step 0 complete
- [ ] Step 1 complete
- [ ] Step 2 complete
- [ ] Step 3 complete
- [ ] Step 4 complete
- [ ] Step 5 complete
- [ ] Step 6 complete
- [ ] Step 7 complete
- [ ] Step 8 complete
- [ ] Step 9 complete
- [ ] Step 10 complete
- [ ] Step 11 complete
- [ ] Step 12 complete

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
