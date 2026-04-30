# CBridge Class and Its Usage in CBlazeClawMFCView

## Index


## 0. Overview of `CBridge`

`CBridge` is a utility class in the BlazeClaw MFC application that manages orchestration and polling 
of chat events between the UI and the backend gateway. It owns lifecycle state, poll scheduling, failure 
backoff, and batch delivery of polled events to the view.

### Key Responsibilities

- **Lifecycle Management:** Tracks gateway connection/provider/model changes and emits lifecycle updates to the UI.
- **Polling:** Periodically invokes `chat.events.poll` with adaptive intervals and failure backoff.
- **Event Handling:** Parses poll payloads and forwards event batches to the UI callback.
- **Health Reporting:** Emits poll health transitions (`healthy`, `degraded`, `disconnected`).
- **Trace Counters:** Maintains request/response/event counters for diagnostics.



## Usage in `CBlazeClawMFCView`

`CBlazeClawMFCView` owns `m_bridge` and wires all bridge dependencies:
- Gateway-running probe
- Active provider/model getters
- Session ID provider
- UI target window handle
- Gateway request router
- Lifecycle/event/poll-health emitters

At runtime:
1. `OnTimer` calls `m_bridge.OnTimerTick(...)`.
2. `CBridge::StartEventsPollAsync()` sends method `chat.events.poll`.
3. Poll completion returns to the view message handler, which calls `m_bridge.HandlePollCompleted(...)`.
4. `CBridge::HandlePollResponse(...)` extracts `events` and forwards them through `handleEventsBatch`.

## All Upstream Event-Generation Paths That Finally Reach `CBridge`

The events consumed by `CBridge` are the `events` array from `chat.events.poll`. Those events originate from per-session queue writes into `host.m_chatEventsBySession[sessionKey]`.

### 1) Primary generator file
- **File:** `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`
	- ln 1820: `// assistant reveal for parity with chat.events.poll (8-char steps).`
	- ln 2109: `"chat.events.poll",`
- **Mechanism:** `PushEventWithRetentionLimit(..., GatewayHost::ChatEventState{...})`
- **Queue:** `host.m_chatEventsBySession[sessionKey]`

### 1.1 Confirmed non-generator `chat.events.poll` references

The following places reference the `chat.events.poll` method name but do **not** fire/generate chat events. They are validation/contract/fixture checks only:

- **File:** `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolContract.cpp`
  - Fixture/contract response validation path (`ValidateDecodedResponseCase(...)`) using `"chat.events.poll"` as method context.
  - No writes to `m_chatEventsBySession`.

- **File:** `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolSchemaValidator.Request.cpp`
  - `ValidateChatEventsPollParams(...)` validates request params (`sessionKey`, `limit`, and allowed fields).
  - `directValidators` registration maps `"chat.events.poll"` to the request validator.
  - No event enqueue logic.

- **File:** `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolSchemaValidator.Response.cpp`
  - Response schema validation for `chat.events.poll` payload shape (`sessionKey`, `events`, `count`, event required fields).
  - No event enqueue logic.

### 2) Concrete generation sites in the runtime chat pipeline

#### 2.1 Late-join replay delta generation
- `chat.send` control-plane late-join replay appends synthetic replay `delta` events to the session queue.
- Purpose: let late-joining recipients receive current assistant progress.

#### 2.2 Runtime callback pre-lifecycle generation
- Before runtime callback execution, queue appends:
  - `queued`
  - `started`
- Purpose: represent run lifecycle start for poll consumers.

#### 2.3 Runtime provider stream delta generation (`onAssistantDelta`)
- Inside runtime callback streaming lambda, each accepted assistant/tool delta appends `delta` events.
- Purpose: real-time incremental assistant output for polling path.

#### 2.4 Non-stream/fallback staged generation in chat send flow
- If lifecycle was not already enqueued, chat send flow appends `queued` and `started`.
- Non-streamed responses append assistant `delta` staging events.
- Purpose: keep parity when provider does not stream incrementally.

#### 2.5 `chat.inject` generation
- Handler `chat.inject` appends terminal `final` event with injected assistant message.
- Purpose: inject external assistant message into normal poll stream.

#### 2.6 `chat.abort` generation
- Handler `chat.abort` removes prior queued events for run and appends terminal `aborted` event.
- Purpose: explicit abort becomes a terminal poll event.

#### 2.7 `chat.events.poll` self-generation while draining
- During poll, active runs can append additional events before dequeue:
  - progressive `delta` events (provider deltas or synthetic 8-char progression)
  - terminal `final` or `error` when stream completes/fails
- Purpose: polling tick advances stream and can generate new events immediately before returning the batch.

### 3) Serialization path into poll response
- **Helper:** `BuildChatEventJson(...)`
- **File:** `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.RuntimeHelpers.inl`
- `chat.events.poll` dequeues `ChatEventState` entries and serializes each into response `events` payload.

### 4) Route from generator to `CBridge`

1. Runtime handlers enqueue `ChatEventState` into `m_chatEventsBySession`.
2. `chat.events.poll` drains queue and returns serialized `events` JSON.
3. `CBridge::StartEventsPollAsync()` calls `chat.events.poll`.
4. `CBridge::HandlePollResponse(...)` extracts `events` and emits to view/UI.

## Related Paths (Important but Not Poll-Origin Generators for `CBridge`)

### Push lifecycle fanout path (parallel transport)
- `EmitPushLifecycleEvent(...)` in `GatewayHost.Handlers.RuntimeHelpers.inl`
- `GatewayEventFanoutService::BuildChatLifecycleEventFrame(...)` in `GatewayEventFanoutService.cpp`
- This is push/WebSocket-compatible fanout. It is related lifecycle transport, but it is not the queued poll-origin event generation that `CBridge` consumes via `chat.events.poll`.

### UI delivery layer
- `EventTransport.cpp` and `CBlazeClawMFCView` are delivery/adaptation layers after `CBridge` receives poll events.
- They are not original gateway event generators.

## `chat.events.poll` Performance Analysis and Optimization Directions

### Why slowdown can happen

1. **Polling too frequently when idle**
   - Timer-driven polling can still wake and request often even when `events` is empty.
   - Cost: repeated request/parse overhead and unnecessary CPU wakeups.

2. **JSON serialization and parsing overhead**
   - Poll handler serializes queue entries (`BuildChatEventJson(...)`) into response payload.
   - `CBridge::HandlePollResponse(...)` scans/extracts `events` from payload text.
   - Cost: repeated string allocation/copy in a hot path.

3. **High allocation pressure for streaming deltas**
   - Delta-heavy runs create many short-lived event/message strings.
   - Cost: allocator churn and worse cache locality under burst output.

4. **Burst queue drain latency spikes**
   - When session queues accumulate, a single poll can process a large batch.
   - Cost: frame-time spikes and delayed responsiveness.

5. **Downstream UI batch handling cost**
   - Even if poll is fast, large event batches can still block UI update/dispatch.
   - Cost: perceived "poll is slow" due to downstream bottlenecks.

### Actionable optimization recommendations

1. **Adaptive idle backoff in `CBridge`**
   - Increase poll interval progressively after consecutive empty polls.
   - Reset to active interval as soon as non-empty events are returned.

2. **Bound per-poll work and smooth batches**
   - Tune `limit` (or make it adaptive) to avoid very large single-poll batches.
   - Optionally process large batches in smaller UI sub-batches.

3. **Coalesce adjacent `delta` events**
   - In poll drain path, merge consecutive `delta` entries for same `runId/sessionKey`.
   - Preserve terminal states (`final`, `error`, `aborted`) without coalescing.

4. **Reduce string churn in serialization**
   - Reserve response buffer capacity before concatenation in poll response build.
   - Prefer append-to-buffer style over many temporary string constructions.

5. **Minimize redundant lifecycle emissions**
   - Ensure `queued`/`started` are not duplicated across fallback and stream parity paths.

6. **Queue pressure guardrails**
   - Near retention limits, compact noisy delta tails while preserving terminal events.

7. **UI throttling/coalescing**
   - Throttle visual updates by time/event count while preserving event semantics.

### Suggested implementation order (best ROI first)

1. Adaptive idle backoff (`CBridge` poll scheduling).
2. Delta coalescing in `chat.events.poll` dequeue path.
3. Poll serialization allocation optimization.
4. UI batch throttling/coalescing.

## Migration Plan: `server push` Hybrid Design (Push-First + Poll Fallback)

### Migration goals
- Reduce latency and idle overhead by delivering chat lifecycle/events in near real-time via push transport.
- Preserve reliability by keeping `chat.events.poll` as compatibility fallback.
- Maintain OpenClaw parity event semantics (`runId`, `sessionKey`, `state`, `timestamp`, optional `message`/`error`).

### Scope and non-goals
- **In scope:** `CBridge`, gateway chat event emission path, transport fanout integration, reconnect recovery, observability.
- **Out of scope (phase 1):** removing `chat.events.poll` entirely.

### Target architecture
1. **Primary channel:** server push event stream (WebSocket/fanout) for chat lifecycle and content events.
2. **Fallback channel:** existing `chat.events.poll` path when push is unavailable/degraded.
3. **Recovery channel:** sequence/cursor-aware catch-up (single poll burst) after reconnect.

### Event contract strategy
- Keep one canonical chat event shape shared by push and poll.
- Ensure event ordering semantics are consistent per `sessionKey` and `runId`.
- Keep terminal event guarantees (`final`, `error`, `aborted`) across both channels.

### Phased implementation plan

#### Phase 0: Baseline and guardrails
1. Add baseline telemetry for poll-only mode:
   - Poll frequency, empty-poll ratio, average batch size, p95 poll handler latency.
2. Add push health telemetry fields:
   - connected/disconnected, reconnect count, push lag, dropped-frame count.
3. Add feature flags:
   - `bridge.push.enabled`
   - `bridge.push.fallbackPoll.enabled`
   - `bridge.push.recoveryPoll.enabled`

#### Phase 1: Push event ingestion in `CBridge`
1. Add push subscription lifecycle to bridge initialization.
2. Route received push events into the same downstream batch/event handling path used by poll.
3. Keep timer-driven poll active behind fallback guard (not removed yet).
4. Add de-dup guard (event sequence/run-state aware) to avoid double-delivery during overlap.

#### Phase 2: Gateway push parity hardening
1. Ensure all poll-origin states have push equivalents:
   - `queued`, `started`, `delta`, `final`, `error`, `aborted`.
2. Align serialization fields and optional payload behavior between push and poll.
3. Verify lifecycle fanout payloads are compatible with bridge consumer parser.

#### Phase 3: Hybrid failover and recovery
1. Detect push degradation/disconnect and switch bridge to fallback polling.
2. On push reconnect, perform bounded catch-up poll using sequence/cursor watermark.
3. Resume push-primary mode after successful catch-up and health confirmation.

#### Phase 4: Performance controls
1. Coalesce adjacent push `delta` events under burst conditions.
2. Throttle UI update cadence while preserving semantic event order.
3. Keep bounded per-cycle event processing to avoid frame-time spikes.

#### Phase 5: Validation and rollout
1. Add tests for:
   - push-only delivery
   - push->poll failover
   - reconnect catch-up without duplicates
   - terminal-event delivery guarantees.
2. Ship behind feature flag and run staged rollout:
   - internal users -> limited % -> default-on.
3. Keep emergency rollback path to poll-only mode.

### Risks and mitigations
- **Risk:** duplicate events during channel overlap.
  - **Mitigation:** event sequence/run-state de-dup in bridge.
- **Risk:** out-of-order delivery after reconnect.
  - **Mitigation:** sequence watermark + catch-up poll before resuming push-primary.
- **Risk:** UI churn under delta bursts.
  - **Mitigation:** delta coalescing and UI throttling.

### Exit criteria
- Push-primary mode enabled by default.
- Poll traffic reduced significantly in healthy sessions.
- No regression in terminal event delivery and user-visible ordering.
- Fallback polling remains functional for degraded transport scenarios.

## Implementation Status Update (2026-04-25)

Implemented in code:

1. **Phase 0 - Baseline and guardrails**
   - Added bridge push feature flags in `CBridge::Config`:
     - `pushEnabled`
     - `pushFallbackPollEnabled`
     - `pushRecoveryPollEnabled`
   - Added baseline poll telemetry counters in `CBridge`:
     - total polls, empty polls, total events, p95 estimate proxy.
   - Extended poll-health emission payload path (view binding includes these fields).

2. **Phase 1 - Push event ingestion in `CBridge`**
   - Added push lifecycle/event APIs:
     - `HandlePushConnected(...)`
     - `HandlePushDisconnected(...)`
     - `HandlePushChatEventFrame(...)`
   - Added push event channel handling in `CBlazeClawMFCView`:
     - `blazeclaw.gateway.chat.push.state`
     - `blazeclaw.gateway.chat.push.event`
   - Push events now flow into the same batch/event handling path (`handleEventsBatch`) used by poll.
   - Added sequence/fingerprint de-dup and bounded dedupe cache.

3. **Phase 2 - Gateway push parity hardening**
   - Added `GatewayEventFanoutService::BuildChatEventFrame(...)` to produce canonical `chat` event frames from event payload objects, aligned with poll event shape strategy.

4. **Phase 3 - Hybrid failover and recovery**
   - Bridge now supports push-primary behavior when enabled.
   - On push disconnect, fallback polling can resume when `pushFallbackPollEnabled=true`.
   - On push reconnect, bounded recovery poll is triggered when `pushRecoveryPollEnabled=true`.

5. **Phase 4 - Performance controls**
   - Added push batch clamp (`pushIngestionMaxBatchEvents`).
   - Added UI cadence throttle (`pushUiThrottleMs`).
   - Added dropped-frame accounting and push-health degraded reasons (`duplicate-seq`, `duplicate-event`, `push-batch-clamped`, `push-ui-throttled`).

6. **Phase 5 - Validation and rollout**
   - Added contract tests in `ToolRuntimeRegistryIntegrationTests.cpp` covering:
     - bridge push APIs and recovery hooks,
     - push feature flag wiring in view,
     - push channel handling hooks.
   - Rollout remains feature-flagged (default push-off unless env-enabled).

Suggested rollout env toggles:

- `BLAZECLAW_BRIDGE_PUSH_ENABLED=true`
- `BLAZECLAW_BRIDGE_PUSH_FALLBACK_POLL_ENABLED=true`
- `BLAZECLAW_BRIDGE_PUSH_RECOVERY_POLL_ENABLED=true`

## Incident Analysis: "stuck with no response" for ordered weather/email prompts

### Reproduction signals from user
- Prompt A: `Check my inbox and tell me if any email needs a reply within 2 hours.`
- Prompt B: `Check tomorrow's weather in Wuhan, write a short report, and email it to jicheng@whu.edu.cn now.`
- Observed log:
  - `[SkillPath] runId=... tried paths:`
  - no following per-tool lines

### Root cause summary
The runtime enters **strict ordered preflight** for weather+email intent, but one or more required runtime tools are unavailable/mismatched at preflight time. The run is then marked failed and terminalized as `error`, but the UI-side success extraction currently only reads assistant text from `state=final` events, so the user perceives "no response".

### Detailed causal chain
1. **Intent policy upgrades prompt to strict ordered sequence**
   - `ChatOrchestrationPolicy` sets strict ordered targets (`weather.lookup`, `email.schedule`) when weather+email flow is detected.
2. **Preflight resolves/validates ordered targets**
   - `RuntimeSequencingPolicy::BuildOrderedSequencePreflight(...)` resolves ordered targets against runtime tools.
3. **Strict preflight blocks when required targets are missing/unavailable**
   - In `GatewayHost.Handlers.Runtime.ChatPipeline.cpp`, strict mode with missing targets triggers:
     - `failed = true`
     - `orchestrationHandled = true`
     - error code `ordered_sequence_target_unavailable`
     - remediation text generated into `assistantText`
4. **Task deltas are persisted, but no actual tool_result entries are produced**
   - This explains `[SkillPath] ... tried paths:` with no item lines:
     - Find-output helper prints details only for `phase == "tool_result"`.
     - Strict preflight failure path mostly emits `plan/preflight/final` entries.
5. **Poll path emits terminal `error`, not `final` message text**
   - `chat.events.poll` reconciliation emits `state="error"` when `run.failed == true`.
6. **View extraction logic only looks for `state="final"` text**
   - `TryExtractFinalAssistantText(...)` scans only `"state":"final"`, so remediation text on failed runs is not surfaced in the normal assistant-response path.

### Why it feels like a hang
- Run lifecycle progresses internally (queued/started/error), but user-facing assistant text path depends on `final.text` extraction.
- Failed ordered preflight produces an error terminal path that does not satisfy that extraction contract.

## Step-by-step action plan to fix

1. **Add explicit diagnostics for ordered preflight misses in UI path** ✅
   - Implemented in `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`:
     - `EmitSkillPathLinesFromEvents(...)` now emits terminal error diagnostics lines containing runId/errorCode/errorMessage for `state=error` events.

2. **Make failed runs emit user-visible assistant content** ✅
   - Implemented in `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`:
     - poll terminalization path now includes `message` payload for failed runs when `run.assistantText` exists, instead of always returning null message on error.

3. **Harden view-side extraction for terminal errors** ✅
   - Implemented in `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`:
     - `TryExtractFinalAssistantText(...)` now falls back to parsing `state=error` event text and then `errorCode` + `errorMessage` when no `final` text exists.

4. **Improve SkillPath reporting for preflight-only failures** ✅
   - Implemented in `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`:
     - `ReportRunSkillPathsToFindOutput(...)` now emits fallback summaries from `phase=preflight` and `phase=final` task deltas when no `tool_result` rows are present.

5. **Validate runtime tool availability assumptions** ✅
   - Implemented in `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`:
     - added telemetry event `gateway.chat.runtime.required_tools.readiness` with readiness booleans for `weather.lookup` and `email.schedule` plus runtime tool count.

6. **Guard strict policy with capability readiness (optional but recommended)** ✅
   - Implemented in `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`:
     - when strict ordering is policy-derived (`orderedSequencePolicyOverridePtr != nullptr`) and missing targets are detected, strict allowlist is downgraded to advisory and telemetry `gateway.chat.ordered.preflight.strict_downgraded` is emitted.
     - explicit-call strict flows remain unaffected.

7. **Add integration tests for this exact failure mode** ✅
   - Implemented in `BlazeClawMfc/tests/GatewayWeatherEmailRegressionTests.cpp`:
     - added test: `Strict ordered preflight missing target emits visible error and preflight task-delta diagnostics`.
     - validates terminal `error` visibility (with assistant-facing message text), expected error code `ordered_sequence_target_unavailable`, and presence of preflight/final task-delta diagnostics without `tool_result`.

8. **Regression verify with both provided prompts** 🔄
   - Code paths are now instrumented to avoid silent failure and expose terminal guidance.
   - Final validation requires running build + regression tests and manual prompt replay in the target environment.
