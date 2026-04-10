# Embedded Orchestration Path Analysis

## Scope

This compares the two configured embedded orchestration paths:

- `dynamic_task_delta`
- `runtime_orchestration`

Based on current code behavior in:

- `src/config/ConfigLoader.cpp`
- `src/gateway/GatewayHost.cpp`
- `src/gateway/GatewayHost.Handlers.Runtime.cpp`
- `src/core/ServiceManager.cpp`
- `src/core/PiEmbeddedService.cpp`

## 1) Path selection and normalization

### ConfigLoader behavior

`NormalizeEmbeddedOrchestrationPath(...)` accepts:

- `runtime_orchestration`
- `dynamic_task_delta`

Any other value is normalized to `dynamic_task_delta`.

### GatewayHost behavior

`GatewayHost::SetEmbeddedOrchestrationPath(...)` uses the same model:

- explicit `runtime_orchestration` is kept
- all other values become `dynamic_task_delta`

So, **`dynamic_task_delta` is the effective default/fallback path**.

## 2) `runtime_orchestration` path behavior

In `chat.send`, this path is only used when all of the following are true:

- path is `runtime_orchestration`
- no forced error
- no attachments
- normalized message is not empty

Then `TryOrchestrateWeatherEmailPrompt(...)` is invoked.

### Characteristics

- Focused, deterministic, hardcoded orchestration pattern.
- Current logic and synthetic task-delta shaping is tightly tied to known intent family (weather/report/email style flow).
- Useful when you want strict and narrow workflow handling.

### Limitations

- Not general-purpose.
- Intent matching is narrower than dynamic planner execution.
- Less reusable for arbitrary tool chains.

## 3) `dynamic_task_delta` path behavior

When `runtime_orchestration` does not handle a request (or when path is not runtime), `chat.send` continues to `m_chatRuntimeCallback`.

In `ServiceManager`, callback execution calls `PiEmbeddedService::ExecuteRun(...)` with:

- tool bindings from skills commands
- runtime tool catalog
- dynamic loop gating (`embedded.dynamicToolLoopEnabled` + canary)

Inside `PiEmbeddedService`:

- Builds dynamic execution plan from prompt window (`skillsPrompt + message`).
- Executes tool loop with policy controls:
  - max policy steps
  - loop detection
  - runtime tool allow checks
  - arg-mode validation
  - transient retry policy
  - deadline enforcement
- Emits structured task deltas for plan/calls/results/final.

### Characteristics

- Generalized orchestration engine.
- Better observability via ordered task-delta records.
- Better extensibility for new tools/intents.
- Aligned with current feature direction (dynamic tool-loop architecture).

### Tradeoffs

- Higher complexity than narrow hardcoded orchestration.
- More policy and runtime moving parts.

## 4) Which path is better?

## Recommendation: **`dynamic_task_delta` is better as the primary path**

Reason:

1. It is the normalized default/fallback path already.
2. It is broader and more future-proof for multi-tool orchestration.
3. It has stronger governance and runtime controls.
4. It provides better structured telemetry/task-delta diagnostics.

## When `runtime_orchestration` can still be useful

Use `runtime_orchestration` only for tightly-scoped, deterministic, low-variance prompt families where a fixed workflow is explicitly desired.

## Practical guidance

- Keep production default as `dynamic_task_delta`.
- Reserve `runtime_orchestration` for niche, explicitly gated scenarios.
- If both are retained, treat `runtime_orchestration` as compatibility mode, not the growth path.

## Latest audit note (2026-04-06)

- Path recommendation remains unchanged: `dynamic_task_delta` should stay primary.
- Current local parity-chat filtered run passes after parity harness stabilization updates; no startup-failure recurrence observed in latest audit run.

## Implementation update (2026-04-10)

- `chat.send` deterministic weather/report/email orchestration is now compatibility-gated to
  `runtime_orchestration` only.
- `dynamic_task_delta` remains runtime-callback-first by default and no longer executes the
  deterministic prompt orchestration branch.
- Branch-selection telemetry now emits `gateway.chat.orchestration.pathSelection` to make
  path/compat decisions observable.

## Extraction update (2026-04-10)

- Workstream C class extraction pass introduced explicit runtime components:
  - `RuntimeSequencingPolicy`
  - `RuntimeToolCallNormalizer`
  - `RuntimeTranscriptGuard`
- Ordered preflight policy resolution and runtime transcript/tool normalization logic now
  execute through these extracted classes rather than relying solely on inline helper blocks.
