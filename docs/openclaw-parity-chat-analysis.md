# OpenClaw vs BlazeClaw chat parity analysis (weather + scheduled email prompt)

## Index

1. [Comparing](#comparing)
    - [1.1 Scope](#scope)
    - [1.2 What was compared](#what-was-compared)
        - [1.2.1 BlazeClaw (current MFC port)](#blazeclaw-current-mfc-port)
        - [1.2.2 OpenClaw (source project)](#openclaw-source-project)
    - [1.3 Missing parity part causing response divergence](#missing-parity-part-causing-response-divergence)
    - [1.4 Why this prompt diverges](#why-this-prompt-diverges)
2. [Suggestions](#suggestions)
    - [2.1 Port extension/workflow runtime parity first](#port-extensionworkflow-runtime-parity-first)
        - [2.1.1 Implementation plan for Suggestion 1 (Port extension/workflow runtime parity first)](#implementation-plan-for-suggestion-1-port-extensionworkflow-runtime-parity-first)
        - [2.1.2 Remaining parity implementation plan (extension lifecycle manager + workflow-engine runtime)](#remaining-parity-implementation-plan-extension-lifecycle-manager-workflow-engine-runtime)
    - [2.2 Port/enable workflow tool parity (Lobster-class flow)](#portenable-workflow-tool-parity-lobster-class-flow)
    - [2.3 Port concrete capability tools needed by this prompt](#port-concrete-capability-tools-needed-by-this-prompt)
    - [2.4 Replace seeded ToolRegistry behavior with dynamic real registry](#replace-seeded-toolregistry-behavior-with-dynamic-real-registry)
    - [2.5 Close UI parity for tool lifecycle visibility](#close-ui-parity-for-tool-lifecycle-visibility)
3. [Recommended validation after parity work](#recommended-validation-after-parity-work)


# 1. Comparing

## 1.1 Scope
Prompt under comparison:

`Check tomorrow's weather in Wuhan, write a short report, and email it to jicheng@whu.edu.cn at 1 PM.`

## 1.2 What was compared

### 1.2.1 BlazeClaw (current MFC port)
- Chat request is accepted and queued (`chat.send accepted`), then UI depends on `chat.events.poll` to render output.
- Chat runtime path is local-model/deepseek callback based, now with prompt-intent orchestration support for weather+email scheduling through runtime tools (`weather.lookup` + `email.schedule`).
- Gateway tool registry is currently minimal and seeded:
  - `chat.send`
  - `memory.search`
  - (no real weather lookup tool, no real email send/schedule tool)

Evidence:
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayToolRegistry.cpp` (tool list + seeded execution)
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.cpp` (`chat.send`, `chat.events.poll` flow)
- `blazeclaw/extensions/extensions.catalog.json` (only `deepseek` extension is enabled)

### 1.2.2 OpenClaw (source project)
- Chat UI model handles both chat events and agent/tool stream events.
- OpenClaw includes extension-based workflow/tooling for multi-step operations with approvals.
- `lobster` extension explicitly targets multi-step workflows, including email triage/approval patterns.

Evidence:
- `openclaw/apps/shared/OpenClawKit/Sources/OpenClawChatUI/ChatTransport.swift` (transport supports `.agent(...)` events)
- `openclaw/apps/shared/OpenClawKit/Sources/OpenClawChatUI/ChatViewModel.swift` (handles tool phases: `start` / `result`)
- `openclaw/extensions/lobster/SKILL.md` (workflow + approval scenarios, email workflows)
- `openclaw/extensions/lobster/src/lobster-tool.ts` (real workflow tool implementation)

[back to top](#index)

## 1.3 Missing parity part causing response divergence

The key missing parity is **tool/workflow capability porting**, not just model inference.

Specifically in BlazeClaw:
1. **No equivalent of OpenClaw workflow extension stack** (e.g., Lobster-class workflow tooling).
2. **No real external action tools** for weather retrieval and scheduled email delivery in active registry.
3. **Tool execution layer is still seeded/minimal** (placeholder-style behavior), so prompts requiring side effects cannot produce OpenClaw-like behavior.
4. **Agent/tool stream UX parity is incomplete** relative to OpenClaw’s richer tool-call lifecycle display.

## 1.4 Why this prompt diverges

This prompt requires all of the following capabilities:
- external data fetch (weather)
- composition (short report)
- delayed side effect (schedule email at 1 PM)

OpenClaw can route this class of request through its extension/workflow/tool ecosystem.
BlazeClaw currently cannot execute equivalent side-effect tool chains, so behavior is necessarily different (often no actionable final result for that intent).

# 2. Suggestions (priority order)

## 2.1 Port extension/workflow runtime parity first

   - Bring over OpenClaw-like plugin/extension execution path used by workflow tools.
   - Ensure non-seeded tool execution is available to the gateway runtime.

### 2.1.1 Implementation plan for Suggestion 1 (Port extension/workflow runtime parity first)

#### Goal
Enable BlazeClaw to execute real extension/workflow tools (instead of seeded placeholders) through the gateway runtime, with OpenClaw-like plugin loading and invocation flow.

#### Phase 1: Baseline and contract definition
1. Inventory OpenClaw plugin/runtime contracts used by workflow tools.
   - Identify plugin manifest fields, tool registration API shape, runtime lifecycle hooks, and error envelope formats.
2. Define BlazeClaw parity contract document.
   - Freeze minimum compatible contract for: discovery, enable/disable, tool registration, tool invocation, and result/error serialization.
3. Add parity acceptance criteria.
   - A workflow-style extension can be discovered, loaded, invoked, and return non-seeded result payloads through gateway APIs.

#### Phase 2: Extension runtime foundation in BlazeClaw
4. Implement extension catalog loader parity.
   - Load extension metadata from BlazeClaw extension catalog with enabled-state support and startup diagnostics.
5. Implement plugin lifecycle manager.
   - Initialize runtime context, activate plugins, and handle plugin load failures without crashing gateway startup.
6. Implement dynamic tool registration bridge.
   - Replace static-only seeded registry behavior with runtime-populated tool catalog entries.

#### Phase 3: Gateway execution path integration
7. Integrate dynamic tool invocation into gateway handlers.
   - Route `gateway.tools.call.execute` to real extension-provided executors when available.
8. Preserve fallback and compatibility behavior.
   - Keep deterministic fallback behavior only when tool is unavailable; emit explicit reason codes.
9. Add structured telemetry and diagnostics.
   - Emit clear status lines/log events for extension load, tool resolution, invocation start, completion, and failure.

#### Phase 4: Workflow-tool readiness and non-seeded validation
10. Port one OpenClaw-style workflow execution path as reference implementation.
    - Use a workflow-capable extension path to prove multi-step execution plumbing end-to-end.
11. Validate non-seeded execution behavior.
    - Confirm returned outputs come from real executor path (not seeded placeholders) via logs and response payload checks.
12. Harden with failure-path tests.
    - Validate timeout, plugin-unavailable, invalid-args, and executor-error cases with stable response envelopes.

#### Deliverables
- Extension runtime parity design note (contract + mapping decisions).
- BlazeClaw dynamic extension/tool loader and lifecycle manager.
- Gateway tool execution path switched to real runtime-backed execution.
- Diagnostic events for extension/tool lifecycle.
- Test matrix proving non-seeded execution path is active.

#### Validation checklist
- `gateway.tools.catalog` includes runtime-loaded extension tools.
- `gateway.tools.call.preview` and `gateway.tools.call.execute` return runtime-backed responses.
- Logs show extension activation + real tool invocation events.
- No regression for existing `chat.send` and `memory.search` calls.

#### Implementation status (current)
- ✅ Added a runtime executor contract in `GatewayToolRegistry` so tools can run through non-seeded runtime handlers.
- ✅ Added extension tool catalog loading in `GatewayToolRegistry` from `blazeclaw/extensions/extensions.catalog.json` and extension manifests.
- ✅ Wired `GatewayHost::Start` to load extension-declared tools at startup.
- ✅ Switched `chat.send` tool execution path to runtime-backed gateway routing (real `chat.send` handler invocation) instead of seeded-only output.
- ✅ Added `LobsterExecutor` implementation and bound it during `ExtensionLifecycleManager::ActivateAll` when the lobster manifest provides an `execPath`.
- ✅ Hardened execPath validation (canonicalization + allowed-roots) in `ExtensionLifecycleManager::ActivateAll` and added unit tests for ActivateAll behavior.
- ✅ Implemented explicit extension lifecycle state machine (`discovered`/`loaded`/`active`/`failed`/`deactivated`) with deterministic activation order, reverse deactivation, duplicate tool-id conflict rejection, and lifecycle state/result query APIs.
- ✅ Added plugin host load/unload + deterministic executor resolution contract in `PluginHostAdapter` and wired it through lifecycle activation/deactivation.
- ✅ Removed hardcoded lobster fallback resolution path from plugin adapter runtime resolution.
- ✅ Removed host-inline extension runtime executor registrations for `lobster`, `weather.lookup`, and `email.schedule` in `GatewayHost::Start` (extension tools now lifecycle-managed only).
- ✅ Hardened `LobsterExecutor` runtime guardrails with explicit settings (timeout/output cap/argument cap/cwd policy), deterministic process outcome mapping, and normalized envelope handling.
- ✅ Added durable approval session persistence primitives in `ApprovalTokenStore` (typed session metadata + TTL validation + prune + restart-safe load) and integrated deterministic invalid/expired/orphaned approval token handling in governance remediation runtime flow.
- ✅ Added `chat.send` orchestration path for weather+scheduled-email prompt class, executing weather lookup and email prepare tool chain with approval-aware assistant output and `chat.events.poll` event flow compatibility.
- ✅ Added in-chat tool lifecycle rendering path in BlazeClaw chat WebView via
  `blazeclaw.gateway.tools.lifecycle` events (start/result/error/approval phases).
- ✅ Added structured telemetry + diagnostics parity baseline with deterministic
  telemetry envelopes for lifecycle, tool, and approval token/session events.
- ✅ Extended parity validation coverage with smoke + fixture + test updates:
  - WebView smoke lifecycle/catalog checks
  - prompt-level orchestration sequence checks (weather + email)
  - protocol fixture additions for orchestration responses
  - parity-oriented gateway test case (`ParityCoverageTests.cpp`) for catalog/execute/lifecycle contract
- ⚠️ Remaining for full parity: workflow-engine plugin runtime completeness and concrete non-seeded weather/email runtime backends (tracked in sections 2.1.2 and 2.3).

- ✅ Workstream A staged decomposition baseline implemented:
  - Added `ChatRunStageContext`, `IChatRunStage`, concrete stage classes,
    and `ChatRunPipelineOrchestrator`.
  - `chat.send` now executes staged pipeline scaffolding and emits
    `gateway.chat.pipeline.stages` telemetry.
- ✅ Workstream A decomposition progression beyond scaffolding:
  - `chat.send` transport/control/decomposition/runtime pre-processing now uses
    stage-owned context fields (session/message/idempotency/forceError/runtimeMessage/runId/timestamp)
    before legacy continuation logic.
- ✅ Workstream A control-stage branch extraction:
  - attachment validation and idempotency dedupe are now emitted as control-stage
    execution results (including short-circuit outcomes), and consumed by `chat.send`
    without duplicating these branches inline.
- ✅ Workstream A control-stage output enrichment:
  - attachment MIME extraction and invalid-message error envelope assembly are now
    stage-owned outputs (`attachmentMimeTypes`, `responseError`) consumed by `chat.send`.
- ✅ Workstream B task-delta contract hardening baseline implemented:
  - introduced schema validator, legacy adapter, and run-scoped repository
  - added `schemaVersion` to task-delta entries and enforced schema validation
    on persisted/replayed/runtime task-delta paths
  - strengthened `gateway.runtime.taskDeltas.get` response contract validation and
    added dedicated task-delta contract tests
- ✅ Workstream B repository hardening completion:
  - removed remaining direct map mutation in persisted-load and retention paths,
    so task-delta persistence/replay lifecycle is now repository-driven end-to-end
    for Workstream B owned flows
- ✅ Workstream C runtime-driven sequencing progression:
  - `dynamic_task_delta` now defaults to runtime callback sequencing path,
    while deterministic prompt orchestration is constrained to explicit
    `runtime_orchestration` compatibility mode
  - branch-selection telemetry (`gateway.chat.orchestration.pathSelection`) and
    parity coverage were added to validate default-vs-compat behavior
- ✅ Workstream C class-map parity extraction completed:
  - added standalone `RuntimeSequencingPolicy`, `RuntimeToolCallNormalizer`,
    and `RuntimeTranscriptGuard` components
  - migrated runtime preflight policy and transcript/tool normalization logic from
    inline gateway helper paths into extracted Workstream C classes
- ✅ Workstream D recovery/fallback baseline integration:
  - added `RecoveryPolicyEngine` + failure taxonomy/budget/compaction/
    truncation/profile/context selectors as explicit components
  - `chat.send` runtime failure path now executes ordered recovery decisions
    and emits `gateway.chat.recovery.decision` telemetry with branch outcomes
- ✅ Workstream E observability baseline integration:
  - added `GatewayLifecycleEventEmitter`, `RunSummaryBuilder`, and
    `BranchDecisionDiagnostics`
  - runtime lifecycle transitions now emit standardized `gateway.chat.lifecycle`
    telemetry and branch diagnostics emit `gateway.chat.branchDecision`
  - terminal run envelopes now emit structured `gateway.chat.runSummary` telemetry
- ✅ Cross-workstream policy parity baseline integration:
  - implemented `SendPolicyResolver`, `ToolPolicyPipeline`, and
    `TranscriptPolicyResolver`
  - `chat.send` now applies policy-guided send admission, tool policy shaping,
    and transcript sanitization before runtime callback dispatch
  - policy reason-code telemetry now emits via `gateway.chat.policy.decision`

- ✅ Phase 1-2 execution-plan status:
  - Phase 1 objectives are complete (staged pipeline and task-delta schema baseline)
  - Phase 2 objectives are complete (runtime-default sequencing, compatibility gating,
    bounded recovery controls, and send/tool/transcript policy integration)
- ✅ Phase 3 progress update:
  - UI lifecycle visibility expanded in native chat surfaces for
    queued/started/delta/final/error/aborted state diagnostics
  - fallback branch visibility in task-delta UI diagnostics now includes explicit
    fallback entries (status/code/step label)
  - lifecycle event parity coverage extended with queued/started/final assertions
- ✅ Item 11 foundational progress update:
  - added host runtime routing scaffolding (`IGatewayHostRuntime`,
    `GatewayHostRouter`, `GatewayHostEx`) and route-decision telemetry
    (`gateway.host.route.decision`)
  - added router-specific tests and route-flow parity assertions as staged
    groundwork for full dual-host parity validation
- ✅ Item 13 progress update:
  - added stable `chat.send` contract-envelope parity assertions to strengthen
    fixture/contract freeze guardrails during phased cleanup

- ✅ Item 1-7 verification update:
  - implementation coverage is now explicitly verified across staged pipeline,
    task-delta schema contract, runtime-default sequencing, compatibility gating,
    bounded recovery controls, and policy-layer integration
  - parity coverage now includes end-to-end `chat.send` send-policy denial envelope
    assertion to validate deterministic policy admission behavior

- ✅ Phase A contract/router scaffolding update:
  - `IGatewayHostRuntime` contract is in place and used by routed host surfaces
  - `GatewayHostRouter` now uses explicit route request metadata including
    feature/compat/cohort inputs with deterministic reason-code decisions
  - router tests now validate feature-gated legacy routing and cohort metadata

- ✅ Phase B legacy host alignment update:
  - `GatewayHost` interface-path behavior now explicitly preserves legacy
    non-chat dispatch semantics while keeping routed `chat.send` behavior
    contract-stable
  - parity coverage adds interface-invocation checks for both `chat.send`
    envelope invariants and legacy non-chat dispatch compatibility

- ✅ Phase C new-host scaffolding update:
  - `GatewayHostEx` now uses explicit composition dependencies and shared
    runtime health signaling (`IGatewayHostRuntime::IsHealthy`)
  - routed stage-host selection is health-gated with deterministic legacy
    fallback semantics
  - parity coverage now validates GatewayHostEx unavailable-dependency
    contract behavior (`stage_host_unavailable`)

- ✅ Phase D composition/diagnostics parity update:
  - host-level route feature/cohort wiring is now explicit and deterministic
    for router request composition
  - route decision telemetry payload construction is centralized with stable
    schema fields (`method`, `target`, `reason`, `cohort`, `fallback`)
  - coverage now includes centralized route payload helper assertions and
    `legacy_only` routing compatibility verification

- ✅ Phase E tests/parity validation update:
  - parity coverage now includes routed `chat.send` runId continuity checks
    across ACK, task-deltas, and lifecycle polling surfaces
  - router tests now assert explicit non-chat legacy route reason-code behavior
  - telemetry tests now validate route-decision payload normalization behavior

- ✅ Phase F build/execution validation update:
  - full solution build validation completed successfully for Debug x64
  - phase-f parity subset execution passed (`[parity][phase-f]`)
  - parity suite now includes route-mode switch success checks,
    `gateway.agents.run`/`gateway.agents.wait` runId lineage continuity,
    and `chat.abort` run-correlation checks

- ✅ Item 11 completion update:
  - dual-host reversible route parity is now explicitly validated with idempotent
    run-lineage continuity across `stage_pipeline_canary` and `legacy_only`
  - router reversibility checks now assert deterministic target/reason restoration
  - stage host runtime unavailability now yields deterministic legacy fallback
    diagnostics (`fallback_stage_host_runtime_unavailable`)

[back to top](#index)

### 2.1.2 Remaining parity implementation plan (extension lifecycle manager + workflow-engine runtime)

#### Goal
Implement the missing runtime layer so extension tools execute through a real lifecycle-managed plugin path,
including a workflow-engine runtime equivalent to OpenClaw's Lobster stack.

#### Plan
1. Define lifecycle/runtime parity contract baseline.
   - Lock activation/deactivation rules, runtime error envelopes, and process lifecycle semantics.
2. Complete extension lifecycle manager activation model.
   - Add startup activation ordering, per-extension state tracking,
     and deterministic deactivation on shutdown/reload.
3. Add plugin host abstraction for runtime tool backends.
   - Introduce host-owned plugin adapter interface so executors can be runtime-loaded
     without hardcoded host wiring.
4. Implement workflow-engine process runner for Lobster-class tools.
   - Add subprocess launch/monitor/timeout, output framing,
     and bounded resource controls compatible with current envelopes.
5. Wire runtime executor registration through lifecycle events.
   - Register/unregister runtime executors as plugin states change,
     and keep `gateway.tools.catalog` synchronized with active runtime state.
6. Add approval session persistence and resume recovery.
   - Persist workflow approval tokens/state across process restarts,
     and expire invalid sessions deterministically.
7. Add structured telemetry and diagnostics for lifecycle + workflow runtime.
   - Emit activation, load failure, tool invoke start/result,
     and workflow suspension/resume events.
8. Validate parity with fixture + smoke + prompt-level comparisons.
   - Extend fixture coverage and smoke scripts,
     then compare OpenClaw vs BlazeClaw tool sequence/output for target prompts.

#### Deliverables
- Lifecycle manager with explicit state machine and shutdown cleanup.
- Plugin runtime host abstraction and runtime executor bridge.
- Workflow-engine subprocess runtime for Lobster-class execution.
- Persistent approval session store with deterministic resume behavior.
- Updated fixture/smoke coverage for lifecycle and workflow runtime paths.

#### Validation checklist
- `gateway.tools.catalog` reflects runtime activation/deactivation transitions.
- `gateway.tools.call.execute` for `lobster` runs via plugin runtime, not host inline fallback.
- Workflow `run`/`resume` behavior survives gateway restart when token is still valid.
- Structured diagnostics include extension state transitions and workflow runtime outcomes.
- Same parity prompt produces matching tool-call sequence and approval semantics vs OpenClaw.

#### Documentation sync
- `docs/extension-lifecycle-plan.md` — detailed execution phases and ownership checkpoints.
- `docs/extension-lifecycle-plan-summary.md` — concise status + milestone summary.
- `docs/runtime-plugin-contract.md` — runtime contract updates for lifecycle/plugin/workflow semantics.

[back to top](#index)

## 2.2 Port/enable workflow tool parity (Lobster-class flow)

2. **Port/enable workflow tool parity (Lobster-class flow)**
   - Add workflow execution + approval/resume envelope handling.
   - Preserve deterministic workflow behavior for multi-step automation.

### Implementation plan for Suggestion 2 (Port/enable workflow tool parity)

#### Goal
Provide a deterministic Lobster-class workflow tool in BlazeClaw with approval/resume behavior and stable envelopes compatible with gateway tool execution.

#### Plan
1. Register Lobster extension metadata in BlazeClaw extension catalog.
2. Add Lobster extension manifest with workflow tool declaration.
3. Implement deterministic workflow runtime for `run` action.
4. Implement approval token storage for `needs_approval` state.
5. Implement deterministic `resume` action with approve/deny outcomes.
6. Wire runtime tool registration at gateway startup.
7. Add lifecycle cleanup for pending approval tokens on shutdown.
8. Validate with build and gateway tool execution checks.

#### Validation checklist
- `gateway.tools.catalog` lists `lobster` when extension is enabled.
- `gateway.tools.call.execute` with `{"tool":"lobster","args":{"action":"run","pipeline":"..."}}` returns deterministic envelope.
- Pipelines containing approval gate return `needs_approval` with `resumeToken`.
- `resume` with valid token and `approve=true|false` returns deterministic `ok|cancelled` envelope.
- Invalid token and invalid args return stable `invalid_args` errors.

#### Smoke command examples (manual verification)

1. Run the existing smoke script flow for Lobster execute:

```powershell
powershell -ExecutionPolicy Bypass -File blazeclaw/BlazeClawMfc/tools/chat/Invoke-WebViewChatSmoke.ps1 -OnlyFlow lobsterExecute
```

2. Expected pass signal in output:

```text
[PASS] lobster execute run/resume flow
```

3. Manual WebSocket execute example frames (for direct gateway testing):

```json
{
  "type": "req",
  "id": "lobster-run-1",
  "method": "gateway.tools.call.execute",
  "params": {
    "tool": "lobster",
    "args": {
      "action": "run",
      "pipeline": "gog.gmail.search --query 'newer_than:1d' | email.triage | approve --prompt 'Process these?'"
    }
  }
}
```

Then send resume using `resumeToken` from run output:

```json
{
  "type": "req",
  "id": "lobster-resume-1",
  "method": "gateway.tools.call.execute",
  "params": {
    "tool": "lobster",
    "args": {
      "action": "resume",
      "token": "<resumeToken>",
      "approve": true
    }
  }
}
```

#### Implementation status (current)
- ✅ Added Lobster extension assets:
  - `blazeclaw/extensions/lobster/blazeclaw.extension.json`
  - `blazeclaw/extensions/lobster/README.md`
- ✅ Registered Lobster extension in `blazeclaw/extensions/extensions.catalog.json`.
- ✅ Added gateway-native deterministic workflow runtime for tool `lobster` with:
  - `action=run`
  - approval-gated `needs_approval` envelope + token issuance
  - `action=resume` with deterministic approve/deny handling
- ✅ Added shutdown cleanup for in-memory approval token state.
- ✅ Added smoke flow `lobsterExecute` in `Invoke-WebViewChatSmoke.ps1` for execute run/resume verification.
- ✅ Added fixture update for `response_tools_call_execute.json` representing lobster run approval envelope.
- ✅ Added deterministic subprocess guardrails in `LobsterExecutor` (timeout, output cap, exit outcome mapping, cwd policy checks).
- ⚠️ Remaining for full OpenClaw parity: deeper workflow-engine backend semantics and full lobster run/resume persistence alignment on top of host-side approval persistence primitives.

[back to top](#index)

## 2.3 Port concrete capability tools needed by this prompt

   - weather lookup tool
   - email send/schedule tool
   - approval gate before outbound send

### Implementation plan for Suggestion 3 (Concrete weather/email capability tools)

#### Goal
Provide concrete gateway-executable tools for weather lookup and email scheduling, with explicit approval required before outbound email scheduling is committed.

#### Plan
1. Add an extension manifest that declares concrete tools for weather and email actions.
2. Implement deterministic `weather.lookup` tool execution path.
3. Implement `email.schedule` tool with `prepare` and `approve` actions.
4. Enforce approval token gate before scheduling is committed.
5. Add smoke verification flow for weather + email execute sequence.
6. Validate by build and run targeted smoke flow.

#### Validation checklist
- `gateway.tools.catalog` includes `weather.lookup` and `email.schedule`.
- `weather.lookup` returns deterministic forecast payload for the requested city/date.
- `email.schedule` `prepare` returns `needs_approval` with `approvalToken`.
- `email.schedule` `approve` with valid token + `approve=true` returns deterministic `ok` scheduled envelope.
- Invalid token returns stable `invalid_args` result.

#### Smoke command examples (manual verification)

Run targeted flow:

```powershell
powershell -ExecutionPolicy Bypass -File blazeclaw/BlazeClawMfc/tools/chat/Invoke-WebViewChatSmoke.ps1 -OnlyFlow weatherEmailExecute
```

Expected pass signal:

```text
[PASS] weather + email execute flow
```

Manual WebSocket execute examples:

```json
{
  "type": "req",
  "id": "weather-lookup-1",
  "method": "gateway.tools.call.execute",
  "params": {
    "tool": "weather.lookup",
    "args": {
      "city": "Wuhan",
      "date": "tomorrow"
    }
  }
}
```

```json
{
  "type": "req",
  "id": "email-prepare-1",
  "method": "gateway.tools.call.execute",
  "params": {
    "tool": "email.schedule",
    "args": {
      "action": "prepare",
      "to": "jicheng@whu.edu.cn",
      "subject": "Wuhan weather report",
      "body": "Tomorrow in Wuhan is expected to be cloudy around 20C.",
      "sendAt": "13:00"
    }
  }
}
```

Then approve with returned `approvalToken`:

```json
{
  "type": "req",
  "id": "email-approve-1",
  "method": "gateway.tools.call.execute",
  "params": {
    "tool": "email.schedule",
    "args": {
      "action": "approve",
      "approvalToken": "<approvalToken>",
      "approve": true
    }
  }
}
```

#### Implementation status (current)
- ✅ Added extension assets:
  - `blazeclaw/extensions/ops-tools/blazeclaw.extension.json`
  - `blazeclaw/extensions/ops-tools/README.md`
- ✅ Registered `ops-tools` extension in `blazeclaw/extensions/extensions.catalog.json`.
- ✅ Implemented concrete runtime adapters `weather.lookup` and `email.schedule`
  through plugin host tool-adapter resolution (no host-inline registration).
- ✅ Implemented approval-gated runtime executor `email.schedule` with persisted approval sessions:
  - `action=prepare` (issues `approvalToken` and returns `needs_approval` envelope)
  - `action=approve` (commits/cancels scheduling with invalid/expired/orphaned token handling)
- ✅ Added smoke flow `weatherEmailExecute` in `Invoke-WebViewChatSmoke.ps1`.
- ⚠️ Remaining for full parity: external provider integration depth (live weather API + real outbound scheduler service semantics).

[back to top](#index)

## 2.4 Replace seeded ToolRegistry behavior with dynamic real registry

   - Expand beyond `chat.send` / `memory.search` to extension-provided tools.

### Implementation plan for Suggestion 4 (Dynamic real registry)

#### Goal
Eliminate seeded fallback execution from `GatewayToolRegistry` so tool behavior is driven by runtime-registered executors and extension-declared tools.

#### Plan
1. Remove seeded default tool bootstrap from `GatewayToolRegistry` constructor.
2. Remove seeded execute branches (`sent:*`, `results:seeded`, `seeded_execution_v1`).
3. Return explicit runtime-missing status when no executor is registered.
4. Register built-in runtime executors (`chat.send`, `memory.search`) from `GatewayHost::Start`.
5. Keep extension tool registration as the primary expansion path.
6. Update preview policy marker to dynamic runtime semantics.
7. Validate with build and execute smoke flows.

#### Validation checklist
- `GatewayToolRegistry` does not hardcode seeded default tools.
- `gateway.tools.call.execute` fails with `unavailable_runtime` if executor is missing.
- `chat.send` and `memory.search` execute through registered runtime executors.
- Extension tools (`lobster`, `weather.lookup`, `email.schedule`) remain discoverable/executable.
- `gateway.tools.call.preview` policy reflects dynamic runtime registry.

#### Manual verification commands

Run key execute smoke flows:

```powershell
powershell -ExecutionPolicy Bypass -File blazeclaw/BlazeClawMfc/tools/chat/Invoke-WebViewChatSmoke.ps1 -OnlyFlow lobsterExecute
powershell -ExecutionPolicy Bypass -File blazeclaw/BlazeClawMfc/tools/chat/Invoke-WebViewChatSmoke.ps1 -OnlyFlow weatherEmailExecute
```

Manual memory search execute frame:

```json
{
  "type": "req",
  "id": "memory-search-1",
  "method": "gateway.tools.call.execute",
  "params": {
    "tool": "memory.search",
    "args": {
      "query": "weather",
      "sessionKey": "main",
      "limit": 3
    }
  }
}
```

#### Implementation status (current)
- ✅ Removed seeded default tool registration from `GatewayToolRegistry` constructor.
- ✅ Removed seeded execute fallback branches in `GatewayToolRegistry::Execute`.
- ✅ Added explicit `unavailable_runtime` outcome when runtime executor is missing.
- ✅ Registered `memory.search` as a dynamic runtime executor in `GatewayHost::Start` (state-backed search over session chat history).
- ✅ Updated preview policy marker to `dynamic_runtime_preview_v1`.
- ✅ Extension runtime executors are now lifecycle-managed through plugin adapter load/resolve/unload flow.
- ⚠️ Remaining for full parity: fully externalized runtime executor discovery/backends for concrete extension tools (weather/email/workflow runtime completeness).

[back to top](#index)

## 2.5 Close UI parity for tool lifecycle visibility

   - Surface tool start/result/error states in WebView2 chat output.
   - Keep polling/bridge diagnostics on by config/env in debug builds.

### Implementation plan for Suggestion 5 (UI parity for tool lifecycle visibility)

#### Goal
Expose tool lifecycle visibility in WebView2 bridge output so users can see tool execute start/result/error states during chat orchestration.

#### Plan
1. Add helper functions in `BlazeClawMFCView` to detect tool execute RPC methods.
2. Parse key fields (`tool`, optional `action`) from request params for start-line visibility.
3. Parse execute result fields (`tool`, `status`, `executed`) from response payload for result visibility.
4. Emit status lines for `gateway.tools.call.execute` in `openclaw.ws.req` path.
5. Emit status lines for `gateway.tools.call.execute` in `blazeclaw.gateway.rpc` path.
6. Emit explicit error status line when app context is unavailable.
7. Validate build and verify lines appear in Output window (`COutputWnd::m_wndOutputDebug`).

#### Validation checklist
- For tool execute requests, Output window shows `tools.execute.start` with tool/action details.
- Successful execute responses show `tools.execute.result` with tool/status/executed fields.
- Failed execute responses show `tools.execute.error` with concise error details.
- Visibility works for both bridge channels:
  - `openclaw.ws.req`
  - `blazeclaw.gateway.rpc`

#### Manual verification commands

Run targeted smoke flows that invoke tool execute:

```powershell
powershell -ExecutionPolicy Bypass -File blazeclaw/BlazeClawMfc/tools/chat/Invoke-WebViewChatSmoke.ps1 -OnlyFlow lobsterExecute
powershell -ExecutionPolicy Bypass -File blazeclaw/BlazeClawMfc/tools/chat/Invoke-WebViewChatSmoke.ps1 -OnlyFlow weatherEmailExecute
```

Then inspect `COutputWnd::m_wndOutputDebug` for lines like:

```text
[Chat] tools.execute.start - tool=lobster action=run
[Chat] tools.execute.result - tool=lobster status=needs_approval executed=true
[Chat] tools.execute.start - tool=email.schedule action=prepare
[Chat] tools.execute.result - tool=email.schedule status=needs_approval executed=true
```

#### Implementation status (current)
- ✅ Added tool lifecycle helper parsers in `BlazeClawMFCView.cpp`.
- ✅ Added `tools.execute.start` + `tools.execute.result/error` output lines for `openclaw.ws.req` path.
- ✅ Added `tools.execute.start` + `tools.execute.result/error` output lines for `blazeclaw.gateway.rpc` path.
- ✅ Added explicit `app_unavailable` error lifecycle line for tool execute requests.
- ⚠️ Remaining for full parity: richer in-chat UI rendering of tool phases (timeline/cards), not only debug output lines.

[back to top](#index)

# 3. Recommended validation after parity work

Run the same prompt in both projects and compare:
1. tool selection
2. tool call sequence
3. approval behavior
4. final user-visible response
5. side-effect execution result (email scheduling)

If these 5 match, parity for this scenario is effectively reached.

[back to top](#index)