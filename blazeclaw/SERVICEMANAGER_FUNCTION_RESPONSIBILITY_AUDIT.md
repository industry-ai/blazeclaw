# ServiceManager Function Responsibility Audit

## Scope

Audit of `blazeclaw/BlazeClawMfc/src/core/ServiceManager.h` and `ServiceManager.cpp` to:

1. List **all** member functions grouped by functionality.
2. Check whether they stay within **service lifecycle / composition / delegation** responsibility.
3. Identify **deep business or domain logic** still present in `ServiceManager` and suggest refactor seams.

**Implementation note:** Extension bundle command sources are implemented by `ExtensionBundleCommandSourceAdapter` in `ExtensionBundleCommandSourceAdapter.cpp` (owned by `ServiceManager` via `std::unique_ptr`).

---

## 1) Complete member function inventory (by functional area)

### A. Lifecycle and top-level orchestration

| Function | Role |
|----------|------|
| `ServiceManager()` | Construct sub-services and default state. |
| `~ServiceManager()` | Teardown. |
| `SetSkillsHostCallbacks(SkillsHostCallbacks)` | Inject UI persistence callbacks for skills. |
| `Start(const AppConfig&)` | Entry: start services and gateway. |
| `Stop()` | Shutdown: stop workers, gateway, cleanup. |
| `IsRunning()` | Query running flag. |

**Private lifecycle phases**

| Function | Role |
|----------|------|
| `ConfigurePolicies(const AppConfig&)` | Thin: delegates to **`ServiceLifecycleStartupCoordinator::ApplyConfigurePolicies`**. |
| `InitializeModules()` | Thin: delegates to **`ServiceLifecycleStartupCoordinator::RunInitializeModules`**. |
| `WireGatewayCallbacks()` | Bind gateway to `Bind*` methods. |
| `FinalizeStartup(const AppConfig&)` | Last startup steps after modules wired. |

**Assessment:** Appropriate for a composition root; policy application and module initialization **sequencing** live in **`ServiceLifecycleStartupCoordinator.cpp`** (friend of `ServiceManager`); `ConfigurePolicies` / `InitializeModules` remain **thin entry points** in `ServiceManager.cpp`.

---

### B. Read facade / snapshots / state accessors

| Function |
|----------|
| `Registry()` |
| `AgentsScope()` |
| `AgentsWorkspace()` |
| `SubagentRegistry()` |
| `LastAcpDecision()` |
| `ActiveEmbeddedRuns()` |
| `ToolPolicy()` |
| `ShellProcessCount()` |
| `ModelRouting()` |
| `AuthProfiles()` |
| `Sandbox()` |
| `Embeddings()` |
| `LocalModelRuntime()` |
| `LocalModelRolloutEligible()` |
| `LocalModelActivationEnabled()` |
| `LocalModelActivationReason()` |
| `RetrievalMemory()` |
| `SkillsCatalog()` |
| `SkillsEligibility()` |
| `SkillsPrompt()` |
| `RunSkillsSnapshot()` |

**Assessment:** Pure **facade getters** — aligned with service management.

---

### C. Gateway integration surface

| Function | Role |
|----------|------|
| `InvokeGatewayMethod(method, paramsJson?)` | Stringly-typed gateway method dispatch. |
| `RouteGatewayRequest(RequestFrame)` | Full protocol routing. |
| `PumpGatewayNetworkOnce(error)` | Drive transport once. |

**Private — gateway / config schema composition**

| Function | Role |
|----------|------|
| ~~`BindSkillsCallbacks()`~~ | **Removed:** skills wiring is invoked from **`GatewayHostBindingCoordinator::WireAllGatewayServiceCallbacks`** via **`RegisterSkillsRelatedCallbacks`**. |
| `BindGatewayPolicyCallbacks()` | Policy hooks on gateway. |
| `BindToolRuntimeCallbacks()` | Tool registry callbacks. |
| ~~`BindChatCallbacks()`~~ | **Removed:** chat wiring is invoked from **`WireAllGatewayServiceCallbacks`** via **`RegisterChatRuntimeCallbacks`** (registration in **`GatewayHostBindingCoordinator.cpp`**; chat branches in **`ChatRuntimeOrchestrationCoordinator.cpp`**). |
| `BindEmbeddingsCallbacks()` | Embeddings lifecycle on gateway. |
| `BuildConfigSchemaGatewayState()` | Expose config schema state for gateway. |
| `LookupConfigSchemaGatewayPath(path)` | Schema lookup helper. |
| `WriteConfigSchemaDocumentationSnapshot(path, error)` | Doc export. |

**Assessment:** **Mixed.** Routing/pump are appropriate. Skills/chat **gateway wiring** is split: **`GatewayHostBindingCoordinator`** registers callbacks; **`ChatRuntimeOrchestrationCoordinator`** owns chat runtime branches (**`ExecuteChatRuntimeRequestBody`**, **`TryInlineToolInvocation`**, **`RunEmbeddedToolOrchestrationOrProvider`**, **`OnChatRuntimeAborted`**). **`ServiceManager`** declares **`friend class ChatRuntimeOrchestrationCoordinator`**. **`gateway.skills.update`** / **`skills.update`**: **`GatewayHost`** only registers methods and forwards **`RequestFrame`**; **`SkillsGatewayMethodHandler::HandleSkillsUpdate`** is the **single** parse/validate/response implementation (documented on the handler and at **`SetSkillsUpdateCallback`** / runtime registration).

---

### D. Skills / hooks orchestration and projection

| Function | Role |
|----------|------|
| `BuildGatewaySkillsState()` | Build skills catalog state for gateway. |
| `RefreshGatewaySkillsStateProjection()` | Thin: **`SkillsGatewayPublicationCoordinator::RefreshProjection`**. |
| `PublishGatewaySkillsStateProjection()` | Thin: **`SkillsGatewayPublicationCoordinator::PublishProjection`**. |
| `BuildGatewaySkillEntry(...)` | Map catalog + eligibility + command + install → gateway entry. |
| `BuildRuntimeSkillCommandSourceAdapters()` | Adapters for runtime skill commands. |
| `RefreshSkillsState(config, force, reason)` | Orchestrates hooks refresh + aggregation + schema invalidate; **per-agent skill filters** and **reserved slash names** come from **`SkillsAgentCommandDescriptorPolicy`**. |

**Assessment:** **Improved.** Per-agent skill-filter and reserved-name **policy** lives in **`SkillsAgentCommandDescriptorPolicy`**; gateway catalog projection refresh/publish **orchestration** lives in **`SkillsGatewayPublicationCoordinator`** (friend of `ServiceManager`). `RefreshSkillsState` still sequences services and applies aggregated commands to `m_skillsCommands` (composition-root duty).

---

### E. Chat runtime / inline / embedded helpers

| Function | Role |
|----------|------|
| `ResolveSkillInvocationToolTarget(...)` | Delegates to `SkillCommandInvocationService` + filtering. |
| `ResolveSkillInvocationPromptRewrite(...)` | Thin: forwards UTF-8 prompt rewrite to `SkillCommandInvocationService::RewriteInvocationPromptUtf8`. |
| `ShouldLoadSkillCommandsForInlineActions(...)` | Slash vs builtin vs skill loading gate. |
| `BuildOrderedAllowedToolTargets(...)` | Ordering / resolution for tool allowlist. |
| `ExtractInlineToolResultText(...)` | Parse tool result payload for inline path. |
| `TryExecuteInlineToolInvocation(...)` | Execute inline tool path with gateway host. |
| `ConvertEmbeddedTaskDeltas(...)` | Map embedded deltas → gateway task deltas. |
| `ApplyEmbeddedExecutionTelemetry(...)` | Update embedded runtime counters in `m_state`. |
| `IsEmbeddedDynamicLoopCanaryEligible(...)` | Canary gating from state/config. |
| `IsEmbeddedDynamicLoopPromotionReady()` | Promotion readiness from counters. |
| `IsLocalModelRolloutEligible()` | Rollout flag (duplicate naming vs public `LocalModelRolloutEligible` accessors — see code). |

**Assessment:** **Mixed.** Several are thin; prompt rewrite rules live in **`SkillCommandInvocationService`**; inline/tool orchestration helpers may still embed product rules over time.

---

### F. Embedded tooling and provider execution (high-impact paths)

| Function | Role |
|----------|------|
| `BuildEmbeddedToolBindings()` | Thin: **`m_skillsCommandService.BuildEmbeddedToolBindings(m_skillsCommands)`**. |
| `ExecuteProviderChatRuntimePath(...)` | Thin: builds `ChatProviderRuntimeBindings` via `BuildChatProviderRuntimeBindings()` and delegates to `ChatProviderRuntimeService::ExecuteProviderPath`. |

**Assessment:** Provider branching and provider-local helpers live in **`ChatProviderRuntimeService`**; embedded tool binding rows are built in **`SkillsCommandService::BuildEmbeddedToolBindings`**; `ServiceManager` remains composition glue (callbacks + config snapshots passed through bindings).

---

### G. Managed reload / cleanup / lifecycle internals

| Function | Role |
|----------|------|
| `ApplyManagedRuntimeConfigDiff(nextConfig, warningMessage)` | Hot reload: builds **`ManagedRuntimeApplyPlan`** via **`ManagedRuntimeConfigDiffCoordinator::EvaluateApplyPlan`**, then **`ApplyManagedRuntimeAuthReject`** or **`ApplyManagedRuntimeApplyPlan`**. |
| `ApplyManagedRuntimeAuthReject(authGuard, warningMessage)` | Auth-session generation reject path (state + diagnostics + lifecycle transition). |
| `ApplyManagedRuntimeApplyPlan(plan, warningMessage)` | Applies accepted plan side effects (chat/local model/gateway/email/schema/skills projection). |
| `ResetGatewayOwnedRuntimeCleanup()` | Clear cleanup registry. |
| `RegisterGatewayOwnedRuntimeCleanup(name, action)` | Register LIFO cleanup. |
| `ExecuteGatewayOwnedRuntimeCleanup()` | Run registered cleanups. |
| `ExecuteGatewayStartupFailureCleanup(config, startupResult)` | Failure path cleanup. |
| `ExecuteNonGatewayRuntimeCleanup()` | Non-gateway teardown. |
| `RecordGatewayLifecycleTransition(transition)` | Append transition string. |
| `QueueManagedConfigInternalWriteHash(hash)` | Pending write coordination. |
| `ConsumeManagedConfigInternalWriteHash()` | Consume one pending hash. |

**Assessment:** Registry/cleanup helpers are **orchestration**. Managed reload uses **`ManagedRuntimeApplyPlan`** from **`ManagedRuntimeConfigDiffCoordinator`**; **`ApplyManagedRuntimeApplyPlan`** still sequences imperative subsystem updates (acceptable composition-root duty; further shrink would be visitors/strategies if needed).

---

### H. Provider credential / cancellation / active model

| Function | Role |
|----------|------|
| `SetActiveChatProvider(provider, model)` | Updates active provider/model; auth generation side effects on certain mutations. |
| `ActiveChatProvider()` / `ActiveChatModel()` | Getters. |
| `ResolveDeepSeekCredentialUtf8()` | Credential resolution from config/store. |
| `HasDeepSeekCredential()` | Boolean helper. |
| `BuildChatProviderRuntimeBindings()` (DeepSeek slot) | Wires **`m_deepSeekClient.InvokeGatewayChat`** + **`IsDeepSeekRunCancelled`** only; HTTP/SSE + JSON payload live in **`CDeepSeekClient::InvokeChat`**. |
| `IsDeepSeekRunCancelled` / `Mark` / `Clear` | Per-run cancel map. |
| `IsEmbeddedRunCancelled` / `Mark` / `Clear` | Embedded cancel map. |

**Assessment:** **Mixed.** Getters and cancel maps are **integration glue**. DeepSeek **transport** (HTTP, SSE, request JSON) lives in **`CDeepSeekClient`**; **`ServiceManager`** does not map gateway fields to **`ChatRequest`** — that mapping is **`CDeepSeekClient::InvokeGatewayChat`**.

---

### I. Diagnostics

| Function | Role |
|----------|------|
| `BuildOperatorDiagnosticsReport()` | Builds **`OperatorDiagnosticsInputs`** (projector contexts + scalar fields) and delegates assembly/report string to **`OperatorDiagnosticsAssembler::Build`** → `CDiagnosticsReportBuilder`. |

**Assessment:** Snapshot assembly and feature histogram logic sit in **`OperatorDiagnosticsAssembler`**; `ServiceManager` still **collects** live references and counters into the inputs struct (acceptable composition-root duty).

---

## 2) Functions that still contain deep business logic (prioritized)

| Priority | Function / area | Why it is “deep” | Suggested refactor |
|----------|-----------------|------------------|-------------------|
| ~~**P0**~~ | ~~`ExecuteProviderChatRuntimePath`~~ | — | ✅ **Done (Phase 4):** **`ChatProviderRuntimeService`** + **`ChatProviderRuntimeBindings`**. |
| ~~**P0**~~ | ~~`BindChatCallbacks` (body)~~ | — | ✅ **Done:** **`ChatRuntimeOrchestrationCoordinator.cpp`** — **`ExecuteChatRuntimeRequestBody`**, **`TryInlineToolInvocation`**, **`RunEmbeddedToolOrchestrationOrProvider`**, **`ResolveSkillsPromptForRun`**, **`OnChatRuntimeAborted`**; **`GatewayHostBindingCoordinator.cpp`** is thin registration + **`CChatRuntime::Execute`** handoff. |
| ~~**P1**~~ | ~~`ResolveSkillInvocationPromptRewrite`~~ | — | ✅ **Done (Phase 4):** **`SkillCommandInvocationService::RewriteInvocationPromptUtf8`**. |
| ~~**P1**~~ | ~~`InitializeModules` / `ConfigurePolicies`~~ | — | ✅ **Addressed:** sequencing and policy wiring moved to **`ServiceLifecycleStartupCoordinator`**; optional future step is a structured **`StartupReport`** DTO if reporting/testing needs it. |
| ~~**P1**~~ | ~~`ApplyManagedRuntimeConfigDiff`~~ | — | ✅ **Done:** **`ManagedRuntimeApplyPlan`** + **`EvaluateApplyPlan`** in **`ManagedRuntimeConfigDiffCoordinator`**; **`ServiceManager`** uses **`ApplyManagedRuntimeApplyPlan`** / **`ApplyManagedRuntimeAuthReject`**. Optional future: generated visitors if the apply body grows again. |
| ~~**P2**~~ | ~~`InvokeDeepSeekRemoteChat`~~ | — | ✅ **Done:** gateway → **`ChatRequest`** mapping moved to **`CDeepSeekClient::InvokeGatewayChat`**; **`ServiceManager`** only passes cancellation via **`BuildChatProviderRuntimeBindings`**. |
| ~~**P2**~~ | ~~`BindSkillsCallbacks` (skills.update seam)~~ | — | ✅ **Invariant:** **`SkillsGatewayMethodHandler::HandleSkillsUpdate`** is the only **`skills.update`** parse/validate/response implementation; **`GatewayHost`** forwards frames only (comments in **`GatewayHost.h`**, **`GatewayHost.Handlers.Runtime.cpp`**, **`GatewayHostBindingCoordinator.cpp`**, class doc on **`SkillsGatewayMethodHandler`**). |
| ~~**P2**~~ | ~~`BuildOperatorDiagnosticsReport`~~ | — | ✅ **Done (Phase 4):** **`OperatorDiagnosticsAssembler`** + **`OperatorDiagnosticsInputs`**; optional future shrink: dedicated **agents/features** projectors for the remaining scalars. |
| ~~**P3**~~ | ~~`BuildEmbeddedToolBindings`~~ | — | ✅ **Done:** mapping lives in **`SkillsCommandService::BuildEmbeddedToolBindings(const SkillsCommandSnapshot&)`**; **`ServiceManager`** delegates. |

---

## 3) Refactoring roadmap (consistency with service-management role)

### Phase 1–3 (completed per prior notes)

- Chat/skills delegation to `ChatRuntimeOrchestrationCoordinator`, `SkillsGatewayMethodHandler`, `SkillsGatewayProjectionService`.
- Startup seams: `SkillsStartupCoordinator`, `HooksStartupCoordinator`, `FixtureStartupValidatorFacade`.
- Managed reload: `ManagedRuntimeConfigDiffCoordinator`.
- Diagnostics: projector modules for gateway lifecycle, embedded, hooks, model/runtime, email.

### Phase 4 (completed)

1. **`ChatProviderRuntimeService`** implements multi-provider chat execution; **`ServiceManager::ExecuteProviderChatRuntimePath`** forwards through **`BuildChatProviderRuntimeBindings()`**.
2. **`SkillCommandInvocationService::RewriteInvocationPromptUtf8`** owns slash-command prompt template substitution; **`ResolveSkillInvocationPromptRewrite`** delegates to it.
3. **`OperatorDiagnosticsAssembler`** + **`OperatorDiagnosticsInputs`** own `DiagnosticsSnapshot` population and **`CDiagnosticsReportBuilder::BuildOperatorDiagnosticsReport`** emission; **`BuildOperatorDiagnosticsReport`** fills inputs and calls **`Build`**.
4. Contract strings for these seams live in **`ServiceManagerStartupPhaseContractTests`** (including a Phase 4 block); **`SkillCommandInvocationServiceTests`** covers **`RewriteInvocationPromptUtf8`**.

**Follow-up (completed):** chat runtime branching moved to **`ChatRuntimeOrchestrationCoordinator.cpp`** (see Phase 6b below).

### Phase 5 (lifecycle startup thin facade — completed)

- **`ServiceLifecycleStartupCoordinator`** (`ApplyConfigurePolicies`, `RunInitializeModules`) owns the former `ServiceManager` bodies for **`ConfigurePolicies`** and **`InitializeModules`**.
- **`ServiceManager`** declares **`friend class ServiceLifecycleStartupCoordinator`** so the coordinator can update private members while keeping a single composition root.
- Contract tests: **`ServiceManagerStartupPhaseContractTests`** includes **`ReadServiceLifecycleStartupCoordinatorSource()`** and asserts delegation strings plus coordinator-side bootstrap resolver usage.

### Phase 6 (gateway host binding — completed)

- **`GatewayHostBindingCoordinator`** (`WireAllGatewayServiceCallbacks`, `RegisterSkillsRelatedCallbacks`, `RegisterChatRuntimeCallbacks`) owns sequencing and the skills/chat callback bodies.
- **`ServiceManager`** declares **`friend class GatewayHostBindingCoordinator`**; private **`BindGatewayPolicyCallbacks`**, **`BindToolRuntimeCallbacks`**, **`BindEmbeddingsCallbacks`** are invoked from **`WireAllGatewayServiceCallbacks`** in order.
- **`skills.update`** / **`gateway.skills.update`**: **`SkillsGatewayMethodHandler::HandleSkillsUpdate`** is the **only** parse/validate/response implementation; **`GatewayHost`** forwards **`RequestFrame`** only (see source comments).
- Contract tests: **`ReadGatewayHostBindingCoordinatorSource()`**, gateway delegation case, and phase1/email strings updated to read the coordinator/assembler sources where behavior moved.

### Phase 6b (chat runtime strategies — completed)

- **`ChatRuntimeOrchestrationCoordinator.cpp`**: **`ExecuteChatRuntimeRequestBody`**, **`TryInlineToolInvocation`**, **`RunEmbeddedToolOrchestrationOrProvider`**, **`ResolveSkillsPromptForRun`**, **`OnChatRuntimeAborted`** (named strategies per branch; **`GatewayHostBindingCoordinator`** only wires **`SetChatRuntimeCallback`** / **`SetChatAbortCallback`** + **`m_chatRuntime.Execute`**).
- **`ServiceManager`** declares **`friend class ChatRuntimeOrchestrationCoordinator`** for the same private surface as gateway binding.
- Contract tests: phase1 asserts **`PrepareChatRequest`** in **`GatewayHostBindingCoordinator.cpp`** and **`ExecuteProviderChatRuntimePath`** in **`ChatRuntimeOrchestrationCoordinator.cpp`**; email contract asserts **`EvaluateEmbeddedFailure`** in **`ChatRuntimeOrchestrationCoordinator.cpp`**.

### Phase 7 (skills refresh policy + gateway publication — completed)

- **`SkillsAgentCommandDescriptorPolicy`**: **`BuildDescriptors`** (defaults + per-agent `config.agents.entries` skill overrides) and **`BuildReservedChatSlashCommandNamesNormalized`** (gateway reserved slash names).
- **`SkillsGatewayPublicationCoordinator`**: **`RefreshProjection`** / **`PublishProjection`** (bundle diagnostics → `m_gatewaySkillsStateProjection`, **`SetSkillsCatalogState`**).
- **`ServiceManager`** declares **`friend class SkillsGatewayPublicationCoordinator`**; **`RefreshSkillsState`** calls **`SkillsGatewayPublicationCoordinator::RefreshProjection`** after refresh (and thin **`Refresh*`** / **`Publish*`** wrappers delegate to the coordinator).
- Contract tests: **`ServiceManagerStartupPhaseContractTests`** skills case; **`SkillCommandsAggregationServiceContractTests`** reads **`SkillsAgentCommandDescriptorPolicy.cpp`** for policy strings.

### Phase 8 (DeepSeek gateway adapter — completed)

- **`CDeepSeekClient::InvokeGatewayChat`**: maps **`GatewayHost::ChatRuntimeRequest`** + model/API key into **`ChatRequest`**, then **`InvokeChat`** (HTTP/SSE transport unchanged).
- **`ServiceManager`**: removed **`InvokeDeepSeekRemoteChat`**; **`BuildChatProviderRuntimeBindings`** calls **`m_deepSeekClient.InvokeGatewayChat`** with **`IsDeepSeekRunCancelled`** only.
- Contract tests: Phase 4 block asserts **`m_deepSeekClient.InvokeGatewayChat(`** in **`ServiceManager.cpp`**.

### Phase 9 (embedded tool bindings mapping — completed)

- **`SkillsCommandService::BuildEmbeddedToolBindings(const SkillsCommandSnapshot&)`** holds the tool-dispatch → **`EmbeddedToolBinding`** mapping; **`ServiceManager::BuildEmbeddedToolBindings`** delegates.
- Contract tests: **`ServiceManagerStartupPhaseContractTests`** phase1 asserts delegation string in **`ServiceManager.cpp`**.

---

## 4) Summary conclusion

| Criterion | Status |
|-----------|--------|
| **Lifecycle / wiring / delegation** | Strong — `ServiceManager` remains the composition root. |
| **Pure getters** | Aligned. |
| **Deep runtime logic** | **Further reduced:** provider path in **`ChatProviderRuntimeService`**; gateway chat runtime branches in **`ChatRuntimeOrchestrationCoordinator`**; skills/config schema registration in **`GatewayHostBindingCoordinator`**; skills refresh **policy** + gateway **publication** in **`SkillsAgentCommandDescriptorPolicy`** / **`SkillsGatewayPublicationCoordinator`**; DeepSeek **protocol** in **`CDeepSeekClient`** (**`InvokeGatewayChat`** / **`InvokeChat`**). |
| **Diagnostics** | **Assembler path:** projector contexts + scalars → **`OperatorDiagnosticsAssembler`** → report builder. |

**Verdict:** `ServiceManager` remains the composition root; **provider execution**, **prompt rewrite**, **diagnostics assembly**, **startup policy/module sequencing**, **gateway callback registration** (**`GatewayHostBindingCoordinator`**) + **chat runtime strategies** (**`ChatRuntimeOrchestrationCoordinator`**), **skills refresh policy / gateway publication**, and **DeepSeek transport** (beyond cancel wiring) are **delegated** to dedicated types.

---

## 5) Related documents

- `blazeclaw/docs/blazeclaw-openclaw-architecture-framework-gap-analysis.md` — BlazeClaw vs OpenClaw architecture mapping and port optimization priorities (context for where `ServiceManager` sits in the stack).
- `blazeclaw/docs/SERVICE_LAYER_BOUNDARIES.md` — `ServiceManager` vs `GatewayHost`, **`GatewayHostBindingCoordinator::WireAllGatewayServiceCallbacks`** sequencing, rules for new behavior.

---

## 6) Tracking checklist

- [x] `ExecuteProviderChatRuntimePath` extracted or substantially delegated (**`ChatProviderRuntimeService`**).
- [x] `ResolveSkillInvocationPromptRewrite` moved out of `ServiceManager` (**`RewriteInvocationPromptUtf8`**).
- [x] `BindChatCallbacks` implementation moved out of **`ServiceManager.cpp`** (**`GatewayHostBindingCoordinator.cpp`** + **`ChatRuntimeOrchestrationCoordinator.cpp`** strategies).
- [x] `BuildOperatorDiagnosticsReport` reduced to projector inputs + **`OperatorDiagnosticsAssembler`**.
- [x] Contract tests updated for new seams (`ServiceManagerStartupPhaseContractTests` Phase 4–9 + skills gateway single-entry case + **`SkillCommandInvocationServiceTests`** + **`SkillCommandsAggregationServiceContractTests`** policy path).
- [x] DeepSeek gateway field mapping lives in **`CDeepSeekClient::InvokeGatewayChat`** (Phase 8).
- [x] `ConfigurePolicies` / `InitializeModules` thin facades over **`ServiceLifecycleStartupCoordinator`** (Phase 5).
- [x] **`SkillsGatewayMethodHandler`** documented as the **single** **`skills.update`** parse/validate/response entry (**`GatewayHost`** forwards only).
- [x] **`BuildEmbeddedToolBindings`** mapping in **`SkillsCommandService`** (Phase 9).

*Last updated: Phase 6b **`ChatRuntimeOrchestrationCoordinator`** chat runtime strategies; **`ServiceManager`** friend + **`GatewayHostBindingCoordinator`** thin registration; contract tests read **`ChatRuntimeOrchestrationCoordinator.cpp`** for phase1/email strings; BlazeClawMfc + BlazeClawMfc.Tests Debug\|x64 built with MSBuild (`PlatformToolset=v143` where v145 is unavailable).*
