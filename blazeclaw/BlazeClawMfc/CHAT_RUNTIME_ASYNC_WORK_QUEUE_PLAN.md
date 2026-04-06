# Chat Runtime Async Work Queue Plan (Phase 1)

## Objective
Move chat runtime execution (remote HTTP and local model inference) off the UI thread using an async work queue and completion events, while keeping existing gateway/chat contracts stable.

## Scope
- BlazeClawMfc chat runtime execution path
- UI-to-runtime handoff and completion signaling
- Gateway/chat event publication ordering
- Cancellation, timeout, and error propagation
- Focus on first implementation pass for safe rollout

## Out of Scope (Phase 1)
- Full streaming redesign for true token-by-token remote parsing
- Chat list rendering optimization (tracked separately)
- Multi-queue priority scheduler

## Current Problem Summary
- UI thread currently drives chat requests and can block on network/model work.
- Slow HTTP or local inference can stall typing and repaint responsiveness.
- Completion is tightly coupled to synchronous call sites.

## Target End State
1. UI thread enqueues chat work and returns immediately.
2. Background worker executes remote/local runtime requests.
3. Completion events marshal final and intermediate state back to UI-safe handlers.
4. Cancel/timeout/error outcomes are normalized and deterministic.
5. Existing gateway polling/event APIs remain compatible.

## Proposed Architecture

### A. Chat work queue
- Introduce a bounded in-process queue for chat runtime jobs.
- Each job carries immutable request payload:
  - sessionId
  - requestId/runId
  - provider/model snapshot
  - prompt/history snapshot
  - cancellation token/flag reference
  - timing metadata
- Enqueue returns quickly on UI thread.

### B. Worker execution model
- Start a dedicated worker thread (or small fixed worker pool of size 1 in Phase 1).
- Worker loop:
  1. dequeue request,
  2. execute runtime path (DeepSeek/local model),
  3. emit lifecycle events.
- Preserve sequential ordering per session in Phase 1.

### C. Completion events
- Define lifecycle notifications:
  - queued
  - started
  - delta (optional in current pipeline)
  - completed
  - failed
  - cancelled
  - timed_out
- Marshal completion back to UI/main-thread-safe integration point.
- Ensure terminal event is emitted exactly once.

### D. State and retention
- Keep bounded retention for per-session request tracking.
- Store terminal metadata for diagnostics and event polling.
- Remove transient execution state after terminalization.

### E. Safety and policy
- Timeout checks must run against real current epoch timeline.
- Cancellation must be checked at queue wait boundary and runtime call boundaries.
- Queue backpressure policy:
  - reject with explicit error when capacity exceeded, or
  - evict oldest non-running request (deferred decision; default reject in Phase 1).

## Implementation Phases

### Phase 1 — Contract and state preparation
1. Define chat runtime job model and lifecycle status enum.
2. Add queue and run-state containers in service orchestration layer.
3. Add deterministic error codes for queue-full, cancelled, timed-out, worker-unavailable.

### Phase 1 implementation status

- Completed in code:
  - Added `ChatRuntimeJobLifecycleStatus` enum in `ServiceManager`.
  - Added `ChatRuntimeJob` and `ChatRuntimeRunState` contract structures.
  - Added bounded queue and run-state containers in `ServiceManager` (`m_chatRuntimeQueue`, `m_chatRuntimeRunsById`).
  - Added deterministic error codes:
    - `chat_runtime_queue_full`
    - `chat_runtime_cancelled`
    - `chat_runtime_timed_out`
    - `chat_runtime_worker_unavailable`
  - Added queue state reset on startup/shutdown and cancellation state normalization for abort requests.

### Phase 2 — Worker and enqueue path
1. Add worker lifecycle start/stop tied to `ServiceManager` startup/shutdown.
2. Move synchronous runtime execution from UI-driven path to queued execution.
3. Keep gateway response contract behavior stable for callers.

### Phase 2 implementation status

- Completed in code:
  - Added dedicated chat runtime worker lifecycle in `ServiceManager` (`StartChatRuntimeWorker`, `StopChatRuntimeWorker`, `ChatRuntimeWorkerLoop`).
  - Wired worker startup at service startup and worker drain/shutdown at service stop.
  - Moved chat runtime provider execution to worker-queued jobs via executable job payloads.
  - Preserved callback response contract by awaiting worker completion and returning the same `ChatRuntimeResult` shape.
  - Added worker-unavailable terminal behavior for queued jobs during shutdown.

### Phase 3 — Completion and event integration
1. Emit completion events through existing chat event surfaces.
2. Normalize terminal sequencing (`final`/`error`/`aborted`) and deduplicate terminal emission.
3. Ensure UI updates consume completion state without blocking.

### Phase 3 implementation status

- Completed in code:
  - Added lifecycle event emission on existing chat event surfaces for run `queued` and `started` states.
  - Preserved completion publication via existing `chat.events.poll` terminal states (`final`/`error`/`aborted`).
  - Added per-run terminal enqueue tracking to prevent duplicate terminal scheduling.
  - Added terminal publication dedup safeguards during poll dequeue to avoid duplicate terminal delivery.
  - Kept UI update consumption timer/poll compatible by continuing to use bounded event queues and non-blocking poll responses.

### Phase 4 — Cancellation and timeout hardening
1. Wire `chat.abort` to queued and running request cancellation.
2. Add timeout enforcement for queued wait + execution duration.
3. Guarantee cleanup of cancel flags and run-state records across all terminal paths.

### Phase 4 implementation status

- Completed in code:
  - Wired `chat.abort` to cancel queued jobs before execution and notify waiting callers with deterministic `chat_runtime_cancelled` terminal results.
  - Preserved running-request cancellation behavior for embedded, DeepSeek, and local-model paths.
  - Added queue-wait timeout enforcement in worker dequeue path (`chat_runtime_timed_out` when wait exceeds threshold).
  - Added execution-duration timeout enforcement for runtime execution path (`chat_runtime_timed_out` when runtime work exceeds threshold).
  - Added terminal-path cleanup for cancellation flags (`DeepSeek`/`embedded`) and runtime run/job state containers.

### Phase 5 — Validation and rollout
1. Add/extend tests for queueing, completion ordering, timeout, cancel, and queue-full behavior.
2. Build and run targeted regression checks for existing DeepSeek/local flows.
3. Gate rollout behind feature flag and enable progressively.

### Phase 5 implementation status

- Completed in code:
  - Added/extended parity coverage tests for:
    - queue lifecycle ordering (`queued` -> `started` -> `delta` -> `final`),
    - timeout terminal behavior (`chat_runtime_timed_out`),
    - queue-full backend error propagation (`chat_runtime_queue_full`),
    - existing abort/terminal parity remains covered.
  - Added rollout gating feature flag for async queue path:
    - `BLAZECLAW_CHAT_RUNTIME_ASYNC_QUEUE_ENABLED`.
  - Added timeout override env controls for progressive validation and rollout tuning:
    - `BLAZECLAW_CHAT_RUNTIME_QUEUE_WAIT_TIMEOUT_MS`
    - `BLAZECLAW_CHAT_RUNTIME_EXECUTION_TIMEOUT_MS`
  - Added synchronous fallback runtime path when async queue rollout flag is disabled.
  - Validated end-to-end regression by building `BlazeClaw.sln` with `msbuild` (Debug|x64).

## Candidate File Touchpoints
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.h`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`
- `blazeclaw/BlazeClawMfc/src/app/ChatView.cpp`
- `blazeclaw/BlazeClawMfc/src/app/BlazeClawMfcApp.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Events.cpp`
- `blazeclaw/BlazeClawMfc/tests/*` and `blazeclaw/BlazeClawMfc.Tests/*`

## Risks and Mitigations
1. **Race conditions in shared state**
   - Mitigation: strict lock ownership boundaries and immutable request payloads.
2. **Out-of-order event delivery**
   - Mitigation: monotonic sequence IDs and terminal dedup guard.
3. **Shutdown deadlocks**
   - Mitigation: cooperative stop token + bounded join timeout.
4. **Queue growth under burst load**
   - Mitigation: bounded capacity with explicit rejection policy and diagnostics.

## Acceptance Criteria
- UI thread no longer directly executes blocking runtime chat work.
- Chat requests are processed through async queue + completion events.
- Cancel/timeout/error terminal states are deterministic and observable.
- Existing chat/gateway consumer contracts remain compatible.
- Solution builds successfully via `msbuild blazeclaw/BlazeClaw.sln`.

## Verification Checklist
- Enqueue path returns quickly under slow remote response simulation.
- Completion events are ordered and single-terminal for each request.
- `chat.abort` cancels queued and active runs consistently.
- Queue-full path returns stable error payload.
- DeepSeek and local model regression scenarios still pass.

## Completion Audit (Latest)

Current state: **fully accomplished**.

- Verified in code:
  - Phase 1 contract/state model and deterministic error-code baseline.
  - Phase 2 worker lifecycle and queued execution migration.
  - Phase 3 completion-event publication and terminal dedup sequencing.
  - Phase 4 cancellation/timeout hardening and terminal-path cleanup.
  - Phase 5 rollout gating + validation coverage.
- Verified by validation run:
  - Build: `msbuild blazeclaw/BlazeClaw.sln /t:Build /p:Configuration=Debug /p:Platform=x64` (pass).
  - Regression tests: `blazeclaw/bin/Debug/BlazeClawMfc.Tests.exe "[parity][chat]"` currently reports local failures in latest audit run; previous passing evidence remains documented in earlier review snapshots.

### Latest audit note (2026-04-06)

- Implementation completion status for this migration remains unchanged.
- Local validation currently shows parity-chat instability that should be triaged separately from migration completeness.

### Follow-on completion note (2026-04-06)

- UI submit call path has now been decoupled from synchronous `chat.send` waiting in `CChatView` by executing request submission on a background thread and handling completion via UI message dispatch.
- This closes the primary residual UI-blocking gap called out in the project review after initial worker-queue migration.

### Additional threading follow-on (2026-04-06)

- Gateway network pumping has been moved out of `CBlazeClawMFCApp::OnIdle` into a dedicated app-owned pump worker loop.
- This removes idle-loop transport pumping from the UI thread and further reduces UI contention risk.

The migration item "Move chat runtime execution off UI thread using async work queue + completion events" is complete for this planned scope.
