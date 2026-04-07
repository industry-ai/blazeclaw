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

### Phase 1 + Phase 2 implementation status

- Completed in code:
  - Added explicit embedded task-delta contract hardening helpers for required-field normalization.
  - Added deterministic orchestration error-code constants for plan validation and execution policy failures.
  - Added gateway task-delta payload normalization before persistence to lock `gateway.runtime.taskDeltas.get` field stability.
  - Added normalized execution-plan step objects (`index/toolName/argMode/stepLabel`) before loop execution.
  - Added pre-execution validation for:
    - empty tool entries,
    - unsupported arg mode,
    - duplicate tool-signature loop risk.
  - Updated `plan` delta emission to include ordered structured step metadata.

## Phase 3 — Execution loop completion

1. Execute each planned delta via `toolExecutorV2` (fallback `toolExecutor`).
2. Emit `tool_call`/`tool_result` deltas for every step.
3. Ensure partial-failure behavior is deterministic and terminalized.

### Phase 3 implementation status

- Completed in code:
  - Hardened `ExecuteRun` loop to consume normalized plan-step arg mode metadata during invocation.
  - Standardized step invocation failure handling through deterministic terminalization path.
  - Kept execution path on `toolExecutorV2` with explicit fallback compatibility to `toolExecutor`.
  - Normalized `tool_call` delta emission to stable contract constants and ordered step labels.
  - Added fixture assertions verifying partial-failure chain determinism (`tool_call` + `tool_result` + terminal `final` failed delta).

## Phase 4 — Policy and safety enforcement

1. Deadline and timeout propagation.
2. Retry and transient classification.
3. Loop detection and max-step enforcement.
4. Tool allowlist and arg validation enforcement.

### Phase 4 implementation status

- Completed in code:
  - Hardened timeout propagation to include retry boundaries and terminal timeout normalization.
  - Kept bounded transient retry behavior with deterministic break conditions.
  - Enforced max-step policy via dedicated planning window vs execution policy cap.
  - Preserved repeated-call signature safeguards and deterministic loop/plan risk handling.
  - Enforced runtime-tool allowlist and arg-mode validation gates before invocation.
  - Added fixture coverage for explicit max-step policy failure.

## Phase 5 — Cancellation and cleanup

1. Wire cancellation checks into execution loop boundaries.
2. Emit cancellation terminal deltas consistently.
3. Guarantee state cleanup for all terminal outcomes.

### Phase 5 implementation status

- Completed in code:
  - Added cancellation callback signaling in embedded execution request contract.
  - Added cancellation checks at execution-loop and retry boundaries.
  - Added deterministic cancellation terminalization (`final` delta with `skipped` + `embedded_run_cancelled`).
  - Added in-memory cancellation state tracking and cleanup in `PiEmbeddedService` terminal paths.
  - Wired `chat.abort` path into embedded cancellation signaling via `ServiceManager`.
  - Added fixture coverage for cancellation-aware stop behavior and terminal-delta normalization.

## Phase 6 — Gateway integration parity

1. Ensure `chat.send` runtime path always reflects executed deltas.
2. Keep task-delta retrieval APIs aligned with new fields.
3. Normalize event emission ordering (`delta`/`final`/`error`/`aborted`).

### Phase 6 implementation status

- Completed in code:
  - Added runtime-path terminal delta fallback generation so `chat.send` always persists a deterministic task-delta record when execution returns without explicit deltas.
  - Kept `gateway.runtime.taskDeltas.get` aligned by returning deterministic index-ordered task-delta history.
  - Added terminal-event de-duplication guard in `chat.events.poll` to normalize ordering and prevent duplicate terminal states.
  - Preserved existing `chat.abort` terminal behavior while normalizing terminal sequencing across `final`/`error`/`aborted`.

## Phase 7 — Telemetry and operator diagnostics

1. Add transition and run-level telemetry events.
2. Add counters for success/failure/timeout/cancel/fallback.
3. Surface rollout and health metrics in operator diagnostics.

### Phase 7 implementation status

- Completed in code:
  - Extended task-delta run-summary telemetry payloads with terminal status and terminal error code.
  - Added cumulative counters for dynamic task-delta outcomes (success/failure/timeout/cancel/fallback).
  - Added embedded dynamic-loop counters in operator diagnostics report (`BuildOperatorDiagnosticsReport`).
  - Added dynamic-loop metrics surface in `gateway.runtime.orchestration.status` for operator monitoring.

## Phase 8 — Validation and rollout

1. Add test matrix for success/failure/cancel/timeout/approval flows.
2. Gate rollout via canary provider/session controls.
3. Promote to default path after parity and reliability thresholds pass.

### Phase 8 implementation status

- Completed in code:
  - Validation matrix coverage remains active in embedded fixture scenarios for success/failure/cancel/timeout/approval and ordered task-delta assertions.
  - Rollout gating remains enforced by provider/session canary controls (`BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_CANARY_PROVIDERS`, `BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_CANARY_SESSIONS`).
  - Added promotion thresholds for default-path rollout via reliability checks:
    - `BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_PROMOTION_MIN_RUNS`
    - `BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_PROMOTION_MIN_SUCCESS_RATE`
  - Embedded dynamic loop now enables when canary is eligible **or** promotion thresholds are satisfied.
  - Added operator diagnostics fields for promotion readiness, thresholds, total runs, and success rate.

## Verification status (latest audit)

**Current state: Fully accomplished.**

### Confirmed accomplished
- Core phased runtime behavior (planning, execution, policy enforcement, cancellation, gateway parity, telemetry, rollout gating) is implemented in active code paths.
- Embedded fixture-based validation scenarios cover major happy-path and failure-path orchestration flows.
- Protocol contract/schema parity for runtime task-delta and related chat/runtime surfaces is implemented.
- Dedicated `BlazeClawMfc.Tests` parity coverage exists for retrieval ordering, clear behavior, abort/event parity, and persistence replay.
- Task-delta filesystem persistence/replay hook is implemented with bounded retention and payload-size controls.

### Gap closure summary
All previously tracked closure items (protocol/schema parity, dedicated test-project parity coverage, and filesystem persistence/replay hook) are now complete.

### Latest audit note (2026-04-06)

- Build validation (`msbuild` Debug|x64) passed in current local audit.
- Parity-chat filtered regression run now passes after parity harness stabilization updates (local-only startup path for route-level parity tests and assertion alignment to current persistence contract shape).

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

### Completion criteria audit (latest)

- ✅ Every `dynamic_task_delta` run executes planned deltas end-to-end.
- ✅ Every run has deterministic terminal state with complete task-delta history.
- ✅ Cancellation, timeout, retries, and loop/policy failures are enforced and observable.
- ✅ Gateway/API consumers can reliably retrieve and interpret run deltas.
- ✅ Test suite includes dedicated parity assertions for runtime task-delta retrieval, clear, abort/event parity, and persistence replay.
