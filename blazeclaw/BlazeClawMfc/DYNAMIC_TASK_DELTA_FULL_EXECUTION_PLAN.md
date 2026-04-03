# Dynamic Task-Delta Full Execution Plan

## Goal

Upgrade the `dynamic_task_delta` path from decomposition-only behavior to a full orchestration runtime that:

1. decomposes user input into ordered task deltas,
2. executes each delta with policy controls,
3. persists and streams execution state,
4. returns deterministic final outputs and structured diagnostics.

## Current Gap Summary

- Task-delta decomposition exists, but execution is incomplete/inconsistent in the active runtime path.
- Task-delta records are not fully lifecycle-driven (`planned -> requested -> running -> completed/failed/skipped`) across all tool invocations.
- Some runtime outcomes still depend on fallback/static paths rather than fully orchestrated per-delta execution.

## Target Feature Set

### A. Planning and decomposition

- Intent decomposition from `skillsPrompt + userMessage`.
- Deterministic ordered `taskDeltas` with stable fields:
  - `index`, `phase`, `toolName`, `argsJson`, `status`, `errorCode`, `startedAtMs`, `completedAtMs`, `latencyMs`, `modelTurnId`, `stepLabel`.
- Validation before execution:
  - empty tool,
  - invalid arg mode,
  - duplicate-loop signature risk.

### B. Full task-delta execution engine

- Execute each planned delta sequentially by default.
- For each step:
  - emit `tool_call` delta,
  - invoke runtime tool executor,
  - emit `tool_result` delta.
- Terminal finalization always emits `final` delta.
- Ensure every run produces a complete delta chain even on failure/cancel/timeout.

### C. Runtime controls and safeguards

- Deadline enforcement based on run start + `embedded.runTimeoutMs`.
- Retry policy for transient failures (bounded attempts).
- Loop detection for repeated tool-call signatures.
- Max-step policy enforcement.
- Runtime-tool allowlist enforcement from active tool catalog.
- Arg-mode and schema gate checks before tool invocation.

### D. Cancellation support

- Run-level cancel API path (`chat.abort`/runtime cancel callback) mapped to orchestration loop stop.
- Cancellation-aware terminal delta and status normalization.
- Cleanup of in-memory run state on terminal paths.

### E. Observability and diagnostics

- Emit telemetry per transition and run summary.
- Keep `gateway.runtime.taskDeltas.get/clear` as primary debug surface.
- Include structured failure reasons and policy-failure code mapping.
- Add operator diagnostics fields for dynamic-loop execution metrics.

### F. Persistence and retention

- Persist per-run delta history in bounded in-memory cache.
- Optional filesystem persistence hook for replay/audit.
- Retention controls for completed run state and delta payload size.

### G. Compatibility and fallback behavior

- Backward compatible with current gateway contract.
- Controlled fallback to provider/runtime path only for explicitly allowed failure classes.
- Clear status markers indicating fallback usage and reason.

### H. Test completeness

- Unit tests:
  - decomposition ordering,
  - arg-mode validation,
  - retry behavior,
  - timeout/cancel/loop detection.
- Integration tests:
  - end-to-end `chat.send -> events.poll -> final`,
  - task-delta retrieval API correctness,
  - failure and fallback branches.
- Regression tests:
  - weather/report/email chain,
  - brave/summarize/notion chain,
  - approval-gated tool flow.

## Proposed Implementation Phases

## Phase 1 — Contract hardening

1. Standardize `EmbeddedTaskDelta` lifecycle states and required fields.
2. Define deterministic error-code taxonomy for orchestration failures.
3. Lock schema expectations for gateway-facing task-delta payloads.

## Phase 2 — Decomposition normalization

1. Normalize planning output into explicit execution plan objects.
2. Add validation pass before first tool call.
3. Emit initial `plan` delta with full ordered tool list.

## Phase 3 — Execution loop completion

1. Execute each planned delta via `toolExecutorV2` (fallback `toolExecutor`).
2. Emit `tool_call`/`tool_result` deltas for every step.
3. Ensure partial-failure behavior is deterministic and terminalized.

## Phase 4 — Policy and safety enforcement

1. Deadline and timeout propagation.
2. Retry and transient classification.
3. Loop detection and max-step enforcement.
4. Tool allowlist and arg validation enforcement.

## Phase 5 — Cancellation and cleanup

1. Wire cancellation checks into execution loop boundaries.
2. Emit cancellation terminal deltas consistently.
3. Guarantee state cleanup for all terminal outcomes.

## Phase 6 — Gateway integration parity

1. Ensure `chat.send` runtime path always reflects executed deltas.
2. Keep task-delta retrieval APIs aligned with new fields.
3. Normalize event emission ordering (`delta`/`final`/`error`/`aborted`).

## Phase 7 — Telemetry and operator diagnostics

1. Add transition and run-level telemetry events.
2. Add counters for success/failure/timeout/cancel/fallback.
3. Surface rollout and health metrics in operator diagnostics.

## Phase 8 — Validation and rollout

1. Add test matrix for success/failure/cancel/timeout/approval flows.
2. Gate rollout via canary provider/session controls.
3. Promote to default path after parity and reliability thresholds pass.

## Candidate File Touchpoints

- `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.h`
- `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.cpp`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.h`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolContract.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolSchemaValidator*.cpp`
- `blazeclaw/BlazeClawMfc.Tests/*` (runtime and contract tests)

## Completion Criteria

- Every `dynamic_task_delta` run executes planned deltas end-to-end.
- Every run has deterministic terminal state with complete task-delta history.
- Cancellation, timeout, retries, and loop/policy failures are enforced and observable.
- Gateway/API consumers can reliably retrieve and interpret run deltas.
- Test suite covers core and failure paths with stable, repeatable assertions.
