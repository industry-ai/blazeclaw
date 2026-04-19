# ServiceManager Class Overview

## What `ServiceManager` Is
`ServiceManager` is the **application orchestration facade** for BlazeClaw core runtime behavior. In the current modular architecture, it is intended to act as a **composition root**: it wires core services together, manages lifecycle (`Start`/`Stop`), and delegates domain logic to specialized modules.

It should not be the place where deep business logic is implemented.

Related cross-project architecture report:

- `blazeclaw/docs/README.md` — index of `blazeclaw/docs/`.
- `blazeclaw/docs/PROTOCOL_CODEGEN.md` — gateway manifest/codegen workflow, `GatewayHostRegistration` default handler coordinator, `OkResponse` / `ErrorResponse`, JSON payload helpers.
- `blazeclaw/docs/blazeclaw-openclaw-architecture-framework-gap-analysis.md`
  - BlazeClaw vs OpenClaw architecture/framework comparison, structural mapping, and prioritized optimization suggestions for the MFC port.

---

## Role in Project Architecture

`ServiceManager` sits between:

- **Config layer** (`AppConfig` and related policies)
- **Core modules** (runtime, providers, tools, skills/hooks, bootstrap, diagnostics)
- **Gateway runtime host** (`GatewayHost`) used by transport/protocol layer

### Architecture Position

- Upstream callers (UI, gateway setup, app boot) invoke `ServiceManager` lifecycle and accessor APIs.
- `ServiceManager` initializes and coordinates:
  - `CChatRuntime` (chat queue/worker/cancel orchestration)
  - `CDeepSeekClient` (provider transport path)
  - `CToolRuntimeRegistry` (runtime tool registration)
  - `CSkillsHooksCoordinator` (skills/hooks refresh/projection/governance routing)
  - `CServiceBootstrapCoordinator` (startup policy + fixture/bootstrap flow)
  - `CDiagnosticsReportBuilder` (operator diagnostics report assembly)
- It also exposes snapshots/state readers used by diagnostics and gateway handlers.

In short: **it is the core runtime orchestrator, not a domain engine**.

---

## Key Responsibilities (from current class surface)

## 1) Lifecycle Orchestration
- `Start(const AppConfig&)`
- `Stop()`
- `IsRunning()`

Coordinates startup ordering, feature gates, module initialization, callback registration, and shutdown cleanup.

Private phases **`ConfigurePolicies`** and **`InitializeModules`** are thin wrappers; the former **`Start`**-path policy wiring and module bootstrap sequencing live in **`ServiceLifecycleStartupCoordinator`** (`ApplyConfigurePolicies`, `RunInitializeModules`), which is a **`friend`** of `ServiceManager` so it can update private members without expanding the public API.

## 2) Runtime Routing and Execution Coordination
- Active provider/model selection (`SetActiveChatProvider`, `ActiveChatProvider`, `ActiveChatModel`)
- Chat runtime and abort delegation through `CChatRuntime`
- DeepSeek credential and cancellation integration paths

## 3) State/Snapshot Facade
Exposes a consistent read facade for:
- agents/workspace/subagents
- model routing/auth/sandbox
- embeddings/local model/retrieval
- skills catalog/eligibility/prompt

This makes it the central state query point for operator and gateway-level introspection.

## 4) Diagnostics Facade
- `BuildOperatorDiagnosticsReport()`

Builds **`OperatorDiagnosticsInputs`** (projector contexts + scalar fields from live `ServiceManager` state) and delegates snapshot assembly and report text to **`OperatorDiagnosticsAssembler`** → `CDiagnosticsReportBuilder`.

## 5) Gateway Integration Layer
- `InvokeGatewayMethod(...)`
- `RouteGatewayRequest(...)`
- `PumpGatewayNetworkOnce(...)`

Bridges core state/runtime decisions with `GatewayHost` request handling.

---

## Why This Class Is Still Critical
Even after modular extraction, `ServiceManager` remains critical because it owns:

- Startup sequencing guarantees
- Runtime dependency wiring
- Cross-module policy cohesion (email fallback, hooks governance toggles, runtime gating)
- Shared cancellation/runtime state boundaries

So the goal is **not** to remove `ServiceManager`, but to keep it thin, deterministic, and composition-focused.

---

## Current Design Strengths

- Strong module decomposition already introduced (Way 3 path)
- Explicit facade APIs for external callers
- Clear ownership of runtime/gateway lifecycle
- Better test seams than prior monolithic form

---

## Optimization Suggestions

## A) Structural Optimizations (high impact)

1. **Split startup wiring into private setup phases**
   - Example: `ConfigurePolicies`, `InitializeModules`, `WireGatewayCallbacks`, `FinalizeStartup`.
   - ✅ Implemented in `ServiceManager` startup path.
   - `Start(...)` now delegates to these private phase methods in order.
   - ✅ **`ConfigurePolicies`** / **`InitializeModules`** bodies moved to **`ServiceLifecycleStartupCoordinator`**; `ServiceManager` methods forward to **`ApplyConfigurePolicies`** / **`RunInitializeModules`**.
   - Improves readability and failure isolation.

2. **Move remaining env/policy resolvers out of `ServiceManager.cpp`**
   - Keep all env parsing in bootstrap/policy resolver modules.
   - `ServiceManager` should consume resolved DTOs only.
   - ✅ Implemented for hooks policy/env resolver cluster.
   - Added `StartupPolicyResolver::HooksPolicySettings` DTO.
   - Hooks policy consumption runs inside **`ServiceLifecycleStartupCoordinator::ApplyConfigurePolicies`**, which calls
     `CServiceBootstrapCoordinator::ResolveHooksPolicySettings(...)`.
   - Removed migrated `ResolveHooks*` helper cluster from `ServiceManager.cpp`.
   - ✅ Implemented for non-hooks email policy resolver cluster.
   - Added `StartupPolicyResolver::EmailPolicySettings` DTO.
   - Email policy consumption runs in **`ApplyConfigurePolicies`**, which calls
     `CServiceBootstrapCoordinator::ResolveEmailPolicySettings(...)`.
   - Removed inline email rollout/enforcement policy branch logic from `ServiceManager.cpp`.
   - ✅ Implemented for email fallback policy orchestration extraction.
   - Added `EmailPolicyOrchestrationService` DTOs:
     - `ResolvedEmailFallbackPolicy`
     - `GatewayEmailPolicyBinding`
   - `ServiceManager` now delegates email fallback policy resolution and gateway binding projection to
     `EmailPolicyOrchestrationService` in startup, gateway callback binding, and managed reload flows.
   - Removed `ServiceManager`-owned inline `ResolveEmailFallbackPolicy(...)` resolver logic.
   - ✅ Implemented for tool skill-root/env runtime resolver cluster.
   - Added `StartupPolicyResolver::ToolRuntimePolicySettings` DTO.
   - Runtime tool wiring now consumes
     `CServiceBootstrapCoordinator::ResolveToolRuntimePolicySettings(...)`.
   - Removed migrated skill-root/env helpers from `ServiceManager.cpp`
     (`ResolveImapSmtpSkillRoot`, `ResolveBraveSearchSkillRoot`,
      `ResolveOpenClawWebBrowsingSkillRoot`, `ResolveBaiduSearchSkillRoot`,
      `ResolveBraveRequireApiKey`, `HasEnvVarValue`).
   - ✅ Runtime tool registration compaction aligned to `CToolRuntimeRegistry`
     policy injection contract.
   - Added `CToolRuntimeRegistry::ToolRuntimePolicySettings` and updated
     `RegisterAll(host, toolPolicy, deps)` so policy is injected once and
     propagated through registry dependencies.

3. **Introduce a `ServiceManagerState` aggregate**
   - Group many related member fields into state structs (hooks state, runtime metrics state, email policy state).
   - Reduces header bloat and accidental coupling.
   - ✅ Implemented with grouped state container in `ServiceManager.h`:
     - `ServiceManagerState::HooksState`
     - `ServiceManagerState::EmailPolicyState`
     - `ServiceManagerState::EmbeddedRuntimeState`
     - `ServiceManagerState::ChatRuntimeState`
   - `ServiceManager.cpp` now consumes grouped paths via `m_state.*`
     for hooks/email/embedded/chat-runtime state.

4. **Use constructor-injected module interfaces where practical**
   - Enables easier testing/mocking and clearer dependency boundaries.

## B) Runtime/Behavior Optimizations

5. **Harden startup error model with structured status object**
   - Return richer startup diagnostics (phase, reason, recoverability) instead of bool-only path.

6. **Consolidate cancellation handling strategy**
   - Consider a unified cancellation registry abstraction instead of multiple maps.

14. **Extract email runtime fallback decisioning from `ServiceManager`**
   - ✅ Implemented with `EmailFallbackRuntimeCoordinator`.
   - `ServiceManager` now delegates embedded failure fallback classification and reason
     derivation to coordinator contracts.
   - Removed direct `ShouldFallbackFromEmbeddedFailure(...)` business helper from
     `ServiceManager`.

15. **Extract email preflight health access from `ServiceManager`**
   - ✅ Implemented with `EmailPreflightHealthService`.
   - `ServiceManager` diagnostics now consume email health via service abstraction
     instead of direct executor calls.

16. **Extract email diagnostics projection assembly from `ServiceManager`**
   - ✅ Implemented with `EmailRuntimeDiagnosticsProjector`.
   - Email diagnostics field mapping moved out of
     `ServiceManager::BuildOperatorDiagnosticsReport()`.
   - ServiceManager now delegates projection and remains focused on composition.

17. **Harden extracted email service contracts with focused tests**
   - ✅ Implemented with unit/scenario/seam coverage updates.
   - Added extracted service tests for:
     - `EmailFallbackRuntimeCoordinator`
     - `EmailPreflightHealthService`
     - `EmailRuntimeDiagnosticsProjector`
   - Added ServiceManager orchestration contract checks to prove delegation paths.
   - Added fallback approval/retry transition scenarios for policy action behavior parity.

18. **Extract Phase 1 non-email deep logic seams from `ServiceManager`**
   - ✅ Implemented.
   - Added `ChatRuntimeOrchestrationCoordinator` and delegated chat request
     preparation concerns from `BindChatCallbacks()`.
   - **`SkillsCommandService::BuildEmbeddedToolBindings`**: maps tool-dispatch skill commands to **`EmbeddedToolBinding`** for embedded runtime; **`ServiceManager::BuildEmbeddedToolBindings`** delegates to the service.
   - Added `SkillsGatewayMethodHandler` and delegated `skills.update` request
     parsing/persistence/response shaping from `BindSkillsCallbacks()`.
     **Invariant:** `SkillsGatewayMethodHandler::HandleSkillsUpdate` is the **only**
     implementation for `gateway.skills.update` / `skills.update` parse, validate, and
     response framing; `GatewayHost` registers methods and forwards `RequestFrame` to the
     callback (see `GatewayHost.h`, `GatewayHost.Handlers.Runtime.cpp`, `SkillsGatewayMethodHandler.h`).
   - Added `SkillsGatewayProjectionService` and delegated
     `BuildGatewaySkillEntry(...)` projection/mapping logic.
   - Added orchestration contract coverage for all Phase 1 delegation paths.

19. **Extract Phase 2 startup and managed-reload orchestration seams**
   - ✅ Implemented.
   - Added startup orchestrators/facade:
     - `SkillsStartupCoordinator`
     - `HooksStartupCoordinator`
     - `FixtureStartupValidatorFacade`
   - `InitializeModules()` now delegates startup branch orchestration to
     extracted seams while preserving behavior parity.
   - Added `ManagedRuntimeConfigDiffCoordinator` and delegated:
     - auth-session generation guard evaluation,
     - **`ManagedRuntimeApplyPlan`** via **`EvaluateApplyPlan`** (bind/port warning + next config envelope),
     - local-model reload decision envelope.
   - **`ServiceManager`** executes plans via **`ApplyManagedRuntimeApplyPlan`** and **`ApplyManagedRuntimeAuthReject`** (thin entry from **`ApplyManagedRuntimeConfigDiff`**).
   - Added contract coverage for Phase 2 delegation seams.

20. **Extract Phase 3 diagnostics projection seams**
   - ✅ Implemented.
   - Added diagnostics projector modules:
     - `GatewayLifecycleDiagnosticsProjector`
     - `EmbeddedRuntimeDiagnosticsProjector`
     - `ModelRuntimeDiagnosticsProjector`
     - `HooksDiagnosticsProjector`
     - retained `EmailRuntimeDiagnosticsProjector`
   - `BuildOperatorDiagnosticsReport()` now primarily builds projector contexts,
     delegates snapshot field mapping to projectors, and keeps report emission via
     `CDiagnosticsReportBuilder`.
   - Added contract coverage for Phase 3 diagnostics delegation seams.

21. **Extract Phase 4 provider runtime, prompt rewrite, and diagnostics assembly**
   - ✅ Implemented.
   - **`ChatProviderRuntimeService`** + **`ChatProviderRuntimeBindings`**: multi-provider chat execution (DeepSeek, local ONNX stream, retrieval/embedded branches). `ServiceManager::ExecuteProviderChatRuntimePath` forwards via `BuildChatProviderRuntimeBindings()`.
   - **DeepSeek (Phase 8):** **`CDeepSeekClient::InvokeGatewayChat`** maps `GatewayHost::ChatRuntimeRequest` + model/API key into **`ChatRequest`** and calls **`InvokeChat`** (HTTP/SSE). `ServiceManager` only wires **`m_deepSeekClient.InvokeGatewayChat`** with **`IsDeepSeekRunCancelled`** in bindings — no duplicate protocol/payload logic in `ServiceManager`.
   - **`SkillCommandInvocationService::RewriteInvocationPromptUtf8`**: UTF-8 prompt template rewrite for slash invocations; `ResolveSkillInvocationPromptRewrite` delegates.
   - **`OperatorDiagnosticsAssembler`** + **`OperatorDiagnosticsInputs`**: builds `DiagnosticsSnapshot` from projector contexts and scalars, then `CDiagnosticsReportBuilder::BuildOperatorDiagnosticsReport`.
   - Contract coverage: `ServiceManagerStartupPhaseContractTests` (Phase 4 strings, including `InvokeGatewayChat`) and `SkillCommandInvocationServiceTests` (`RewriteInvocationPromptUtf8`).

22. **Thin lifecycle policy/module facades (`ServiceLifecycleStartupCoordinator`)**
   - ✅ Implemented.
   - **`ServiceLifecycleStartupCoordinator::ApplyConfigurePolicies`** holds hooks/email policy application and related **`m_state`** / **`m_activeConfig`** updates that previously lived in **`ServiceManager::ConfigurePolicies`**.
   - **`ServiceLifecycleStartupCoordinator::RunInitializeModules`** holds agents/embeddings/local model/chat-runtime queue/skills/hooks/fixture startup sequencing previously in **`ServiceManager::InitializeModules`**.
   - `ServiceManager` declares **`friend class ServiceLifecycleStartupCoordinator`** for controlled access to private members.
   - Contract tests read **`ServiceLifecycleStartupCoordinator.cpp`** where startup orchestration strings moved (e.g. runtime orchestration policy, `SkillsStartupCoordinator::Execute`).

23. **Gateway host binding coordinator (`GatewayHostBindingCoordinator`)**
   - ✅ Implemented.
   - **`GatewayHostBindingCoordinator::RegisterSkillsRelatedCallbacks`** and **`RegisterChatRuntimeCallbacks`** register gateway callbacks (config schema, skills refresh/update, **`SetChatRuntimeCallback`** / **`SetChatAbortCallback`** + **`CChatRuntime::Execute`**).
   - Chat runtime **branching** (cancellation, inline tools, embedded PI + email fallback, provider path, abort snapshot) lives in **`ChatRuntimeOrchestrationCoordinator.cpp`** (**`ExecuteChatRuntimeRequestBody`**, **`TryInlineToolInvocation`**, **`RunEmbeddedToolOrchestrationOrProvider`**, **`ResolveSkillsPromptForRun`**, **`OnChatRuntimeAborted`**).
   - **`ServiceManager`** declares **`friend class GatewayHostBindingCoordinator`** and **`friend class ChatRuntimeOrchestrationCoordinator`**; **`BindSkillsCallbacks`** / **`BindChatCallbacks`** delegate in one call each.
   - Contract tests: **`GatewayHostBindingCoordinator.cpp`** (Phase 1 **`PrepareChatRequest`**, skills.update); **`ChatRuntimeOrchestrationCoordinator.cpp`** (**`ExecuteProviderChatRuntimePath`**, embedded **`EvaluateEmbeddedFailure`**); **`OperatorDiagnosticsAssembler.cpp`** for projector **`Apply`** (phase3/email).

24. **Skills refresh policy + gateway publication (`SkillsAgentCommandDescriptorPolicy`, `SkillsGatewayPublicationCoordinator`)**
   - ✅ Implemented.
   - **`SkillsAgentCommandDescriptorPolicy`**: per-agent skill command descriptors (defaults + per-agent config overrides) and normalized reserved chat slash-command names for aggregation.
   - **`SkillsGatewayPublicationCoordinator`**: refresh **`m_gatewaySkillsStateProjection`** from **`BuildGatewaySkillsState()`** + bundle extension diagnostics, and publish via **`GatewayHost::SetSkillsCatalogState`**.
   - **`ServiceManager`** declares **`friend class SkillsGatewayPublicationCoordinator`**; **`RefreshGatewaySkillsStateProjection`** / **`PublishGatewaySkillsStateProjection`** delegate; **`RefreshSkillsState`** uses the policy + coordinator for the non-hooks parts of the pipeline.
   - Contract tests: **`ServiceManagerStartupPhaseContractTests`** (skills seam) and **`SkillCommandsAggregationServiceContractTests`** (policy source file).

7. **Reduce duplicated state projections**
   - Build snapshot DTOs once per report/tick where possible.
   - Reuse immutable snapshots across diagnostics and gateway publication.
   - ✅ Implemented gateway skills projection deduplication.
   - Added cached projection state and helper flow:
     - `RefreshGatewaySkillsStateProjection()` (delegates to **`SkillsGatewayPublicationCoordinator::RefreshProjection`**)
     - `PublishGatewaySkillsStateProjection()` (delegates to **`SkillsGatewayPublicationCoordinator::PublishProjection`**)
   - Replaced repeated direct `BuildGatewaySkillsState()` publish calls in
     refresh/update/startup paths with cached projection refresh + publish.
   - Per-agent skill filter **policy** for command aggregation moved to **`SkillsAgentCommandDescriptorPolicy`** (Phase 7).

## C) Maintainability Optimizations

8. **Narrow `ServiceManager.h` include footprint**
   - Use forward declarations when possible in header; move heavy includes to cpp.
   - Improves compile times and coupling.
   - ✅ Implemented include footprint narrowing pass in `ServiceManager.h`.
   - Removed unused standard headers (`<atomic>`, `<condition_variable>`,
     `<deque>`, `<functional>`, `<memory>`, `<thread>`).
   - Kept required type-complete service/module includes where members are
     owned by value.

9. **Introduce internal wiring helpers with strict naming**
   - Example: `BindChatCallbacks`, `BindSkillsCallbacks`, `BindEmbeddingsCallbacks`.
   - Easier code navigation and onboarding.
   - ✅ Implemented strict-named internal wiring helper decomposition.
   - Added helper methods:
     - `BindSkillsCallbacks()`
     - `BindGatewayPolicyCallbacks()`
     - `BindToolRuntimeCallbacks()`
     - `BindChatCallbacks()`
     - `BindEmbeddingsCallbacks()`
   - `WireGatewayCallbacks()` now acts as a sequencing facade invoking
     these helpers in deterministic order.

10. **Document invariants explicitly**
   - Startup order invariants
   - Thread-safety assumptions
   - Which fields are valid pre/post `Start`

## D) Validation and Quality Optimizations

11. **Expand diagnostics regression comparison coverage**
   - Keep selected-field comparator lightweight for smoke.
   - Add optional “strict profile” set for CI parity hardening.

12. **Add orchestration-focused unit tests for `Start` wiring**
   - Verify callback registration and module invocation contracts.
   - ✅ Added startup phase contract tests:
     - `BlazeClawMfc/tests/ServiceManagerStartupPhaseContractTests.cpp`
   - Covers phase ordering and failure-path assertion contracts.
   - Catch regressions in composition logic early.

13. **Add scenario tests for lifecycle race edges**
   - Start/Stop overlap
   - abort during queue timeout
   - provider switch while runtime active

---

## Recommended Near-Term Priorities

1. Finish extracting residual resolver/helper clusters from `ServiceManager.cpp`.
   - ✅ Hooks policy/env resolver cluster extracted to bootstrap policy module.
   - ✅ Non-hooks email policy resolver cluster extracted to bootstrap policy module.
   - ✅ Tool skill-root/env runtime resolver cluster extracted to bootstrap policy module.
   - ✅ Tool runtime registration policy injection compacted into
     `CToolRuntimeRegistry` contract path.
2. Reduce `ServiceManager.h` state surface via grouped aggregates.
   - ✅ Implemented via `ServiceManagerState` nested aggregates.
3. Add startup/wiring contract tests (phase-level orchestration coverage).
   - ✅ Implemented for startup phase ordering + failure-path assertions.
4. Keep diagnostics regression gate configurable (non-blocking local, strict CI).

---

## Final Assessment
`ServiceManager` is now close to its intended architecture role: a **composition and lifecycle façade**. Phase 4 moved **provider chat execution**, **invocation prompt rewrite**, and **operator diagnostics assembly** behind dedicated types. Phase 5 moved **`ConfigurePolicies`** / **`InitializeModules`** implementation into **`ServiceLifecycleStartupCoordinator`**. Phase 6 moved **skills/chat gateway callback registration** into **`GatewayHostBindingCoordinator`**, with **chat runtime strategies** in **`ChatRuntimeOrchestrationCoordinator`**, leaving thin **`Bind*`** entry points on **`ServiceManager`**. Phase 7 split **skills refresh policy** (**`SkillsAgentCommandDescriptorPolicy`**) from **gateway skills catalog publication** (**`SkillsGatewayPublicationCoordinator`**). Further work is optional polish (e.g. smaller strategy objects if **`ChatRuntimeOrchestrationCoordinator.cpp`** grows again).