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
| `ConfigurePolicies(const AppConfig&)` | Apply config to policy/registry/email/hooks/chat state. |
| `InitializeModules()` | Bootstrap catalogs, skills, hooks, fixtures, coordinators. |
| `WireGatewayCallbacks()` | Bind gateway to `Bind*` methods. |
| `FinalizeStartup(const AppConfig&)` | Last startup steps after modules wired. |

**Assessment:** Appropriate for a composition root; `InitializeModules` / `ConfigurePolicies` remain **large** and should stay thin facades over coordinators (see §3).

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
| `BindSkillsCallbacks()` | Register skills-related gateway handlers. |
| `BindGatewayPolicyCallbacks()` | Policy hooks on gateway. |
| `BindToolRuntimeCallbacks()` | Tool registry callbacks. |
| `BindChatCallbacks()` | **Large:** chat runtime, inline tools, embedded, providers. |
| `BindEmbeddingsCallbacks()` | Embeddings lifecycle on gateway. |
| `BuildConfigSchemaGatewayState()` | Expose config schema state for gateway. |
| `LookupConfigSchemaGatewayPath(path)` | Schema lookup helper. |
| `WriteConfigSchemaDocumentationSnapshot(path, error)` | Doc export. |

**Assessment:** **Mixed.** Routing/pump are appropriate. `BindChatCallbacks` and parts of `BindSkillsCallbacks` remain **high-line-count** and behavior-heavy.

---

### D. Skills / hooks orchestration and projection

| Function | Role |
|----------|------|
| `BuildGatewaySkillsState()` | Build skills catalog state for gateway. |
| `RefreshGatewaySkillsStateProjection()` | Refresh cached projection. |
| `PublishGatewaySkillsStateProjection()` | Publish to gateway host. |
| `BuildGatewaySkillEntry(...)` | Map catalog + eligibility + command + install → gateway entry. |
| `BuildRuntimeSkillCommandSourceAdapters()` | Adapters for runtime skill commands. |
| `RefreshSkillsState(config, force, reason)` | Reload skills state from config. |

**Assessment:** **Mixed.** Delegation to `SkillsGatewayProjectionService` helps; `RefreshSkillsState` and gateway state publishing still mix orchestration with policy.

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
| **P0** | `BindChatCallbacks` | Still the main **wall of lambdas**: inline auth, embedded orchestration, fallbacks, coordinator calls — high cyclomatic surface. | Continue extracting **named strategies** per branch; target file-size &lt; N lines by delegating each lambda body to `ChatRuntimeOrchestrationCoordinator` methods. |
| ~~**P1**~~ | ~~`ResolveSkillInvocationPromptRewrite`~~ | — | ✅ **Done (Phase 4):** **`SkillCommandInvocationService::RewriteInvocationPromptUtf8`**. |
| **P1** | `InitializeModules` | Multi-phase startup (skills modes, hooks, fixtures) — even with coordinators, sequencing + error aggregation can grow. | **`ServiceStartupOrchestrator`** returning a structured `StartupReport`; `ServiceManager` only applies report to members. |
| **P1** | `ApplyManagedRuntimeConfigDiff` | May still imperative-patch many subsystems after coordinator. | Ensure coordinator returns **`ManagedRuntimeApplyPlan`**; `ServiceManager` executes plan via small private `ApplyPlan(...)` or generated visitors. |
| **P2** | `InvokeDeepSeekRemoteChat` | SSE parsing, delta callbacks — **transport**. | Already near `CDeepSeekClient`; ensure **no additional protocol logic** in `ServiceManager` beyond argument mapping. |
| **P2** | `BindSkillsCallbacks` | Can regrow if new JSON shapes added inline. | Keep **`SkillsGatewayMethodHandler`** as single entry for parse/validate/response. |
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

**Follow-up (not Phase 4):** reduce **`BindChatCallbacks`** surface area / line count via further coordinator extraction.

---

## 4) Summary conclusion

| Criterion | Status |
|-----------|--------|
| **Lifecycle / wiring / delegation** | Strong — `ServiceManager` remains the composition root. |
| **Pure getters** | Aligned. |
| **Deep runtime logic** | **Further reduced:** provider path lives in **`ChatProviderRuntimeService`**; **`BindChatCallbacks`** remains the main **high-line-count** integration surface. |
| **Diagnostics** | **Assembler path:** projector contexts + scalars → **`OperatorDiagnosticsAssembler`** → report builder. |

**Verdict:** `ServiceManager` remains the composition root; **provider execution**, **prompt rewrite**, and **diagnostics assembly** are now **delegated** to dedicated types. The next shrink target is **`BindChatCallbacks`** (strategies / coordinator methods), not the Phase 4 extractions.

---

## 5) Tracking checklist

- [x] `ExecuteProviderChatRuntimePath` extracted or substantially delegated (**`ChatProviderRuntimeService`**).
- [x] `ResolveSkillInvocationPromptRewrite` moved out of `ServiceManager` (**`RewriteInvocationPromptUtf8`**).
- [ ] `BindChatCallbacks` line count reduced (measurable threshold, e.g. &lt; 400 lines or split file) — **follow-up**.
- [x] `BuildOperatorDiagnosticsReport` reduced to projector inputs + **`OperatorDiagnosticsAssembler`**.
- [x] Contract tests updated for new seams (`ServiceManagerStartupPhaseContractTests` Phase 4 + **`SkillCommandInvocationServiceTests`**).

*Last updated: Phase 4 completion; `ServiceManager.cpp`/`ServiceManager.h` delegation to `ChatProviderRuntimeService`, `SkillCommandInvocationService`, `OperatorDiagnosticsAssembler`; Debug\|x64 build validated with VS 18 MSBuild.*
