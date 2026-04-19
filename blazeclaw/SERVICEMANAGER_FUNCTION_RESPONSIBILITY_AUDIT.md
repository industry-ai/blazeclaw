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
| `BindSkillsCallbacks()` | Thin: delegates to **`GatewayHostBindingCoordinator::RegisterSkillsRelatedCallbacks`**. |
| `BindGatewayPolicyCallbacks()` | Policy hooks on gateway. |
| `BindToolRuntimeCallbacks()` | Tool registry callbacks. |
| `BindChatCallbacks()` | Thin: delegates to **`GatewayHostBindingCoordinator::RegisterChatRuntimeCallbacks`** (implementation lives in **`GatewayHostBindingCoordinator.cpp`**). |
| `BindEmbeddingsCallbacks()` | Embeddings lifecycle on gateway. |
| `BuildConfigSchemaGatewayState()` | Expose config schema state for gateway. |
| `LookupConfigSchemaGatewayPath(path)` | Schema lookup helper. |
| `WriteConfigSchemaDocumentationSnapshot(path, error)` | Doc export. |

**Assessment:** **Mixed.** Routing/pump are appropriate. Skills/chat **gateway wiring** bodies live in **`GatewayHostBindingCoordinator`** (friend of `ServiceManager`); `BindChatCallbacks` / `BindSkillsCallbacks` stay thin entry points. Remaining complexity is **nested runtime branching** inside the coordinator (candidates for further named strategies on `ChatRuntimeOrchestrationCoordinator` or helpers).

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
| `BuildEmbeddedToolBindings()` | Map `m_skillsCommands` → `EmbeddedToolBinding` list (filter tool dispatches). |
| `ExecuteProviderChatRuntimePath(...)` | Thin: builds `ChatProviderRuntimeBindings` via `BuildChatProviderRuntimeBindings()` and delegates to `ChatProviderRuntimeService::ExecuteProviderPath`. |

**Assessment:** Provider branching and provider-local helpers live in **`ChatProviderRuntimeService`**; `ServiceManager` remains composition glue (callbacks + config snapshots passed through bindings).

---

### G. Managed reload / cleanup / lifecycle internals

| Function | Role |
|----------|------|
| `ApplyManagedRuntimeConfigDiff(nextConfig, warningMessage)` | Hot reload: delegate to coordinator + apply gateway/email/model side effects. |
| `ResetGatewayOwnedRuntimeCleanup()` | Clear cleanup registry. |
| `RegisterGatewayOwnedRuntimeCleanup(name, action)` | Register LIFO cleanup. |
| `ExecuteGatewayOwnedRuntimeCleanup()` | Run registered cleanups. |
| `ExecuteGatewayStartupFailureCleanup(config, startupResult)` | Failure path cleanup. |
| `ExecuteNonGatewayRuntimeCleanup()` | Non-gateway teardown. |
| `RecordGatewayLifecycleTransition(transition)` | Append transition string. |
| `QueueManagedConfigInternalWriteHash(hash)` | Pending write coordination. |
| `ConsumeManagedConfigInternalWriteHash()` | Consume one pending hash. |

**Assessment:** Registry/cleanup helpers are **orchestration**. `ApplyManagedRuntimeConfigDiff` can still be **deep** if coordinator returns and `ServiceManager` performs many imperative updates — prefer **single DTO apply** from `ManagedRuntimeConfigDiffCoordinator`.

---

### H. Provider credential / cancellation / active model

| Function | Role |
|----------|------|
| `SetActiveChatProvider(provider, model)` | Updates active provider/model; auth generation side effects on certain mutations. |
| `ActiveChatProvider()` / `ActiveChatModel()` | Getters. |
| `ResolveDeepSeekCredentialUtf8()` | Credential resolution from config/store. |
| `HasDeepSeekCredential()` | Boolean helper. |
| `InvokeDeepSeekRemoteChat(request, modelId, apiKey)` | **HTTP/SSE path via `CDeepSeekClient`** — provider I/O. |
| `IsDeepSeekRunCancelled` / `Mark` / `Clear` | Per-run cancel map. |
| `IsEmbeddedRunCancelled` / `Mark` / `Clear` | Embedded cancel map. |

**Assessment:** **Mixed.** Getters and cancel maps are **integration glue**. `InvokeDeepSeekRemoteChat` is **provider client logic**; acceptable behind a dedicated `DeepSeekChatTransport` or `RemoteLlmClient` owned by `ServiceManager` but **not** implemented as hundreds of lines inside `ServiceManager` long term.

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
| **P0** | `BindChatCallbacks` (body) | **Moved** to **`GatewayHostBindingCoordinator::RegisterChatRuntimeCallbacks`** — still a **large nested lambda** for chat runtime + embedded + provider handoff. | Continue extracting **named strategies** per branch; delegate inner bodies to `ChatRuntimeOrchestrationCoordinator` (or similar) methods to shrink **`GatewayHostBindingCoordinator.cpp`**. |
| ~~**P1**~~ | ~~`ResolveSkillInvocationPromptRewrite`~~ | — | ✅ **Done (Phase 4):** **`SkillCommandInvocationService::RewriteInvocationPromptUtf8`**. |
| ~~**P1**~~ | ~~`InitializeModules` / `ConfigurePolicies`~~ | — | ✅ **Addressed:** sequencing and policy wiring moved to **`ServiceLifecycleStartupCoordinator`**; optional future step is a structured **`StartupReport`** DTO if reporting/testing needs it. |
| **P1** | `ApplyManagedRuntimeConfigDiff` | May still imperative-patch many subsystems after coordinator. | Ensure coordinator returns **`ManagedRuntimeApplyPlan`**; `ServiceManager` executes plan via small private `ApplyPlan(...)` or generated visitors. |
| **P2** | `InvokeDeepSeekRemoteChat` | SSE parsing, delta callbacks — **transport**. | Already near `CDeepSeekClient`; ensure **no additional protocol logic** in `ServiceManager` beyond argument mapping. |
| **P2** | `BindSkillsCallbacks` (body) | Wiring lives in **`GatewayHostBindingCoordinator`**; can regrow if new callbacks are added without a handler seam. | Keep **`SkillsGatewayMethodHandler`** as single entry for parse/validate/response. |
| ~~**P2**~~ | ~~`BuildOperatorDiagnosticsReport`~~ | — | ✅ **Done (Phase 4):** **`OperatorDiagnosticsAssembler`** + **`OperatorDiagnosticsInputs`**; optional future shrink: dedicated **agents/features** projectors for the remaining scalars. |
| **P3** | `BuildEmbeddedToolBindings` | Straightforward mapping — low risk. | Optional: `SkillsCommandService::BuildEmbeddedToolBindings()` if reuse needed elsewhere. |

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

**Follow-up (not Phase 4):** reduce nested chat-runtime lambda surface inside **`GatewayHostBindingCoordinator`** via further strategy extraction.

### Phase 5 (lifecycle startup thin facade — completed)

- **`ServiceLifecycleStartupCoordinator`** (`ApplyConfigurePolicies`, `RunInitializeModules`) owns the former `ServiceManager` bodies for **`ConfigurePolicies`** and **`InitializeModules`**.
- **`ServiceManager`** declares **`friend class ServiceLifecycleStartupCoordinator`** so the coordinator can update private members while keeping a single composition root.
- Contract tests: **`ServiceManagerStartupPhaseContractTests`** includes **`ReadServiceLifecycleStartupCoordinatorSource()`** and asserts delegation strings plus coordinator-side bootstrap resolver usage.

### Phase 6 (gateway host binding — completed)

- **`GatewayHostBindingCoordinator`** (`RegisterSkillsRelatedCallbacks`, `RegisterChatRuntimeCallbacks`) owns the former bodies of **`BindSkillsCallbacks`** and **`BindChatCallbacks`**.
- **`ServiceManager`** declares **`friend class GatewayHostBindingCoordinator`**; **`BindSkillsCallbacks`** / **`BindChatCallbacks`** delegate with one call each.
- Contract tests: **`ReadGatewayHostBindingCoordinatorSource()`**, gateway delegation case, and phase1/email strings updated to read the coordinator/assembler sources where behavior moved.

### Phase 7 (skills refresh policy + gateway publication — completed)

- **`SkillsAgentCommandDescriptorPolicy`**: **`BuildDescriptors`** (defaults + per-agent `config.agents.entries` skill overrides) and **`BuildReservedChatSlashCommandNamesNormalized`** (gateway reserved slash names).
- **`SkillsGatewayPublicationCoordinator`**: **`RefreshProjection`** / **`PublishProjection`** (bundle diagnostics → `m_gatewaySkillsStateProjection`, **`SetSkillsCatalogState`**).
- **`ServiceManager`** declares **`friend class SkillsGatewayPublicationCoordinator`**; **`RefreshSkillsState`** calls **`SkillsGatewayPublicationCoordinator::RefreshProjection`** after refresh (and thin **`Refresh*`** / **`Publish*`** wrappers delegate to the coordinator).
- Contract tests: **`ServiceManagerStartupPhaseContractTests`** skills case; **`SkillCommandsAggregationServiceContractTests`** reads **`SkillsAgentCommandDescriptorPolicy.cpp`** for policy strings.

---

## 4) Summary conclusion

| Criterion | Status |
|-----------|--------|
| **Lifecycle / wiring / delegation** | Strong — `ServiceManager` remains the composition root. |
| **Pure getters** | Aligned. |
| **Deep runtime logic** | **Further reduced:** provider path in **`ChatProviderRuntimeService`**; gateway callback wiring in **`GatewayHostBindingCoordinator`**; skills refresh **policy** + gateway **publication** in **`SkillsAgentCommandDescriptorPolicy`** / **`SkillsGatewayPublicationCoordinator`**. |
| **Diagnostics** | **Assembler path:** projector contexts + scalars → **`OperatorDiagnosticsAssembler`** → report builder. |

**Verdict:** `ServiceManager` remains the composition root; **provider execution**, **prompt rewrite**, **diagnostics assembly**, **startup policy/module sequencing**, **skills/chat gateway binding**, and **skills refresh policy / gateway publication** are **delegated** to dedicated types. The next shrink target is **nested chat runtime logic** inside **`GatewayHostBindingCoordinator`** (strategies / coordinator methods).

---

## 5) Tracking checklist

- [x] `ExecuteProviderChatRuntimePath` extracted or substantially delegated (**`ChatProviderRuntimeService`**).
- [x] `ResolveSkillInvocationPromptRewrite` moved out of `ServiceManager` (**`RewriteInvocationPromptUtf8`**).
- [x] `BindChatCallbacks` implementation moved out of **`ServiceManager.cpp`** (**`GatewayHostBindingCoordinator.cpp`**) — further line-count reduction inside the coordinator is **follow-up**.
- [x] `BuildOperatorDiagnosticsReport` reduced to projector inputs + **`OperatorDiagnosticsAssembler`**.
- [x] Contract tests updated for new seams (`ServiceManagerStartupPhaseContractTests` Phase 4–7 + **`SkillCommandInvocationServiceTests`** + **`SkillCommandsAggregationServiceContractTests`** policy path).
- [x] `ConfigurePolicies` / `InitializeModules` thin facades over **`ServiceLifecycleStartupCoordinator`** (Phase 5).

*Last updated: Phase 7 skills policy + gateway publication — `SkillsAgentCommandDescriptorPolicy`, `SkillsGatewayPublicationCoordinator`; contract tests updated; BlazeClawMfc + BlazeClawMfc.Tests Debug\|x64 built with MSBuild (`PlatformToolset=v143` where v145 is unavailable).*
