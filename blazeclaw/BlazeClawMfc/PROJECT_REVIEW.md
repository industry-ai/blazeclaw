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

Overall, this is a modular monolith with a strong `ServiceManager + GatewayHost` core and good separation by concern.

## 2) Threading Model

Effective runtime model is mostly single-threaded at app level:

- Gateway network pump is called from `CBlazeClawMFCApp::OnIdle` (`BlazeClawMfcApp.cpp:506-519`).
- Chat polling is timer-driven on UI thread (`ChatView.cpp:468-477`, 500 ms interval).
- No explicit `std::thread` / `CreateThread` usage was found in `BlazeClawMfc/src`.

Current behavior:

- UI thread drives message loop.
- UI thread also pumps gateway transport.
- UI thread also issues chat requests.

Consequences:

- Heavy gateway callback work can stall UI responsiveness.
- `chat.send` can synchronously invoke DeepSeek HTTP flow (`WinHttpSendRequest/ReceiveResponse/ReadData` in `ServiceManager.cpp`), which is blocking.
- ONNX calls can block call sites when reached on UI-driven flows.

## 3) Memory Characteristics

### Positive controls

- Transport has explicit limits (`kMaxReadBufferBytes`, outbound queue caps).
- Chat history retention is now capped per session (`kMaxChatHistoryEntriesPerSession = 500`).
- Chat event queue retention is now capped per session (`kMaxChatEventsPerSession = 200`).
- Retrieval memory store is capped at 512 records (`RetrievalMemoryService.cpp:56-58`).
- Task delta store has bounded retention intent (`m_taskDeltasByRunId.size() > 64`).

### Risk points

- Chat history and event queues are now bounded per session, but retained payloads can still be large when message bodies are large.
- Local-model cancel flags are now cleaned up via scoped terminal cleanup across success, error, and cancel paths.

## 4) Performance Issues and Hotspots

### A) UI-thread blocking

Main issue: network pump + request routing + potential remote HTTP in UI-driven execution.

Expected symptoms: typing lag, delayed repaint, temporary freeze under slow network/model conditions.

### B) Chat view redraw strategy

`SyncItemsFromState()` clears and rebuilds full message list on update (`ChatView.cpp:882+`). With frequent polling, this causes repeated O(n) rebuild/redraw churn.

### C) Streaming pipeline behavior

DeepSeek response is fully read before delta extraction in `ServiceManager` (`responseBody` accumulation), then deltas are emitted from stored buffers. This increases time-to-first-token compared with true incremental streaming.

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

- Runtime execution is largely UI-thread-centric.
- Chat/event/render loop has avoidable hot-path inefficiencies.
- Some state containers are not retention-bounded.
- Embedded orchestration has correctness/perf smells.

## 6) Highest-Impact Improvements (Priority Order)

1. [In Progress] Move chat runtime execution (remote/local model) off UI thread using async work queue + completion events.
   - Execution plan: `CHAT_RUNTIME_ASYNC_WORK_QUEUE_PLAN.md`
   - Status: Phase 1 (contract/state preparation) completed in code.
   - Status: Phase 2 (worker lifecycle + queued execution path) completed in code.
   - Status: Phase 3 (completion/event integration and terminal dedup sequencing) completed in code.
2. Switch chat UI updates to incremental append/update instead of full list rebuild.
3. [Completed] Register dispatcher handlers once at startup, not inside `chat.send`.
4. [Completed] Add retention limits for `m_chatHistoryBySession` and `m_chatEventsBySession`.
5. [Completed] Fix `PiEmbeddedService` started-at/deadline logic to use real current epoch consistently.
6. [Completed] Ensure local-model cancel flags are erased across all terminal/error/cancel paths.
7. [Completed] Optionally parallelize embeddings safely (separate sessions or lock partitioning).

## 7) Related Execution Planning Docs

- `CHAT_RUNTIME_ASYNC_WORK_QUEUE_PLAN.md` — first-pass migration plan for moving chat runtime work off the UI thread with queue-based async execution and completion events.
- `DYNAMIC_TASK_DELTA_FULL_EXECUTION_PLAN.md` — dynamic task-delta orchestration hardening and rollout plan/status.
