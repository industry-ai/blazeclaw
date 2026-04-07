# BlazeClawMfc Project Review

## 1) Layering and Architecture

Observed layer stack is clear and mostly consistent:

- **UI layer (MFC)**  
  `src/app/*` (`BlazeClawMfcApp`, `MainFrame`, `ChatView`)  
  Handles windowing, chat controls, timers, and status output.

- **Configuration layer**  
  `src/config/ConfigModels.h`, `ConfigLoader.cpp`, `blazeclaw.conf`  
  Strong feature-flag style config model (gateway, embedded, hooks, local model, embeddings, skills).

- **Service orchestration layer**  
  `src/core/ServiceManager.*`  
  Central composition root. Wires agent services, embeddings, local model runtime, retrieval memory, embedded orchestration, hooks, and gateway callbacks.

- **Gateway/runtime API layer**  
  `src/gateway/*`  
  Protocol + dispatcher + schema + transport. Handler split (`GatewayHost.Handlers.*`) is good for domain partitioning. Tool registry + extension lifecycle are integrated here.

- **Model/runtime layer**  
  `src/core/runtime/LocalModel/*`, `OnnxEmbeddingsService.*`  
  ONNX runtimes for generation and embeddings.

- **Extension/tooling layer**  
  Extension catalog loading and runtime tool binding in `GatewayHost`.

- **Agents/policy/governance layer**  
  `src/core/Agents*`, `src/gateway/ApprovalTokenStore.*`, `GatewayHost.Handlers.SecurityOps.cpp`  
  Adds auth-profile, sandbox, workspace, shell, transcript-safety, and approval-token controls.

Overall, this is a modular monolith with a strong `ServiceManager + GatewayHost` core and good separation by concern.

## 2) Threading Model

Effective runtime model is now mixed (UI-thread driven + dedicated worker threads):

- Gateway network pump now runs on a dedicated app-owned worker thread (`StartGatewayPumpWorker`, `GatewayPumpWorkerLoop`, `StopGatewayPumpWorker`).
- Chat polling is timer-driven on UI thread (`ChatView.cpp:468-477`, 500 ms interval).
- Chat runtime async queue/worker is present in `ServiceManager` (`StartChatRuntimeWorker`, `ChatRuntimeWorkerLoop`, `StopChatRuntimeWorker`).
- Transport/runtime internals also use background threading (e.g., WebSocket transport internals).

Current behavior:

- UI thread drives message loop.
- Gateway transport pumping is no longer driven by UI `OnIdle`.
- UI thread now submits `chat.send` via background request execution and receives completion on UI message dispatch.
- Heavy runtime execution (embedded/deepseek/local-model path) is executed via chat runtime worker when async queue is enabled.

Consequences:

- Runtime work no longer executes directly on the UI thread, which reduces direct UI contention.
- UI no longer blocks waiting for `chat.send` request/response completion at submit call sites.
- Gateway network pumping no longer competes with UI idle processing.

## 3) Memory Characteristics

### Positive controls

- Transport has explicit limits (`kMaxReadBufferBytes`, outbound queue caps).
- Chat history retention is now capped per session (`kMaxChatHistoryEntriesPerSession = 500`).
- Chat event queue retention is now capped per session (`kMaxChatEventsPerSession = 200`).
- Retrieval memory store is capped at 512 records (`RetrievalMemoryService.cpp:56-58`).
- Task delta store has bounded retention intent (`m_taskDeltasByRunId.size() > 64`).

### Risk points

- Chat history and event queues are now bounded per session, but retained payloads can still be large when message bodies are large.
- Task-delta retention is bounded (`m_taskDeltasByRunId.size() > 64` eviction), but eviction is map-order based rather than strict recency.
- Local-model cancel flags are now cleaned up via scoped terminal cleanup across success, error, and cancel paths.

## 4) Performance Issues and Hotspots

### A) UI-thread blocking

Status update (implemented): UI submit path is decoupled from synchronous `chat.send` waiting by moving request execution off the UI thread and marshalling completion back to UI state handlers.

Residual risk: event polling cadence and downstream rendering can still affect responsiveness under heavy gateway load.

### B) Chat view redraw strategy

Status update (implemented): `ChatView` uses incremental append/update synchronization for history, stream, and error rows, with selective rebuild fallback only when source history shrinks or full history reload occurs.

### C) Streaming pipeline behavior

Status update (implemented): provider-to-UI incremental streaming now emits DeepSeek SSE-derived assistant snapshots into gateway chat events during provider read, instead of waiting for full-response completion before delta publication.

Residual risk: non-DeepSeek paths and synthetic fallback paths can still use staged/simulated deltas.

### D) Embeddings throughput serialization

Status update (latest change): `OnnxEmbeddingsService::EmbedBatch` now supports optional parallel batch execution when `embeddings.executionMode=parallel`, while preserving safe initialization semantics.

Sequential fallback remains in place when execution mode is not parallel or batch size is 1.

### E) Dispatcher mutation in request hot path

Status update (latest change): `gateway.runtime.taskDeltas.get/clear` is now registered once during runtime handler initialization, not inside `chat.send`.

This removes per-request dispatcher mutation and avoids unnecessary hot-path registration overhead.

### F) Startup load is heavy

`ServiceManager::Start` performs broad initialization and multiple fixture validations, increasing startup time.

### G) Embedded orchestration timeout semantics

Status update (latest change): `PiEmbeddedService` now uses `CurrentEpochMs()` for run start and completion timestamps, and deadline checks are evaluated against this real epoch timeline.

This removes synthetic baseline time drift and prevents immediate/incorrect deadline triggering caused by mixed timestamp sources.

## 5) Architecture Summary

### Strengths

- Clear module boundaries.
- Centralized config and rollout gating.
- Solid protocol/tool abstraction.
- Explicit path for local model and remote provider execution.

### Weaknesses

- Startup path is broad and validation-heavy.
- Some retention policies are bounded but not fully policy-optimized (e.g., map-order eviction).
- Parity regression stability now depends on local-only gateway startup and contract-level assertions in parity harness, which should be kept aligned with runtime contracts.

## 6) Highest-Impact Improvements (Priority Order)

1. [Completed] Move chat runtime execution (remote/local model) off UI thread using async work queue + completion events.
   - Execution plan: `CHAT_RUNTIME_ASYNC_WORK_QUEUE_PLAN.md`
   - Status: Phase 1 (contract/state preparation) completed in code.
   - Status: Phase 2 (worker lifecycle + queued execution path) completed in code.
   - Status: Phase 3 (completion/event integration and terminal dedup sequencing) completed in code.
   - Status: Phase 4 (cancellation + timeout hardening with terminal cleanup) completed in code.
   - Status: Phase 5 (validation/rollout gating + parity coverage extensions) completed in code.
   - Latest audit: `msbuild` Debug|x64 passed; local parity chat filter run (`[parity][chat]`) currently has failures and needs stabilization follow-up.
2. [Completed] Switch chat UI updates to incremental append/update instead of full list rebuild.
   - Execution plan: `CHAT_UI_INCREMENTAL_RENDER_PLAN.md`
   - Status: Phases 1-4 completed (state tracking, incremental sync, item-level helpers, history reload consistency).
3. [Completed] Register dispatcher handlers once at startup, not inside `chat.send`.
4. [Completed] Add retention limits for `m_chatHistoryBySession` and `m_chatEventsBySession`.
5. [Completed] Fix `PiEmbeddedService` started-at/deadline logic to use real current epoch consistently.
6. [Completed] Ensure local-model cancel flags are erased across all terminal/error/cancel paths.
7. [Completed] Optionally parallelize embeddings safely (separate sessions or lock partitioning).
8. [Completed] Decouple UI submit path from synchronous `chat.send` waiting by using non-blocking submit + UI completion message handling.
9. [Completed] Move gateway network pump off `OnIdle` into an app-owned dedicated worker loop.
10. [Completed] Implement provider-to-UI incremental streaming for DeepSeek runtime path using live SSE delta emission callbacks.
11. [Completed] Stabilize parity chat regression startup and related approval/persistence assertions.

### Next recommended priorities

1. Extend true incremental provider streaming parity across non-DeepSeek runtime paths.
2. Reduce startup critical-path fixture validation cost (lazy/background validation or staged diagnostics pass).
3. Tighten persisted task-delta parity assertions from contract-level checks to deterministic phase-level checks after persistence contract hardening.

## 7) Validation Snapshot (2026-04-06)

- Build:
  - ✅ `msbuild blazeclaw/BlazeClaw.sln /t:Build /p:Configuration=Debug /p:Platform=x64`
- Tests:
  - ✅ `blazeclaw/bin/Debug/BlazeClawMfc.Tests.exe "[parity][chat]"` passed (14 test cases, 158 assertions).

## 8) Related Execution Planning Docs

- `CHAT_RUNTIME_ASYNC_WORK_QUEUE_PLAN.md` — first-pass migration plan for moving chat runtime work off the UI thread with queue-based async execution and completion events.
- `CHAT_UI_INCREMENTAL_RENDER_PLAN.md` — incremental chat UI append/update migration and completion audit.
- `DYNAMIC_TASK_DELTA_FULL_EXECUTION_PLAN.md` — dynamic task-delta orchestration hardening and rollout plan/status.
- `ORCHESTRATION_PATH_ANALYSIS.md` — analysis of `dynamic_task_delta` vs `runtime_orchestration` behavior and recommended default.
- `LLAMACPP_MODEL_LOADING_PLAN.md` — staged integration plan for GGUF/`llama.cpp` local runtime path.

---

Review date: **2026-04-07**  
Commit hash: **0b08192**
