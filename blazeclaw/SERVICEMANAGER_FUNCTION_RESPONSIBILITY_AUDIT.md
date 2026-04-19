# ServiceManager Function Responsibility Audit

## Scope
Audit of `blazeclaw/BlazeClawMfc/src/core/ServiceManager.h/.cpp` to:
1. List member functions grouped by functionality.
2. Check whether they stay within service-management/composition responsibility.
3. Identify deep business logic and recommend refactoring seams.

---

## 1) Member Functions Grouped by Functionality

## A. Lifecycle and top-level orchestration
- `ServiceManager()`
- `~ServiceManager()`
- `SetSkillsHostCallbacks(...)`
- `Start(...)`
- `Stop()`
- `IsRunning()`

Private lifecycle phases:
- `ConfigurePolicies(...)`
- `InitializeModules()`
- `WireGatewayCallbacks()`
- `FinalizeStartup(...)`

Assessment: Mostly service-management responsibility.

---

## B. Read facade / snapshots / state accessors
- `Registry()`
- `AgentsScope()`
- `AgentsWorkspace()`
- `SubagentRegistry()`
- `LastAcpDecision()`
- `ActiveEmbeddedRuns()`
- `ToolPolicy()`
- `ShellProcessCount()`
- `ModelRouting()`
- `AuthProfiles()`
- `Sandbox()`
- `Embeddings()`
- `LocalModelRuntime()`
- `LocalModelRolloutEligible()`
- `LocalModelActivationEnabled()`
- `LocalModelActivationReason()`
- `RetrievalMemory()`
- `SkillsCatalog()`
- `SkillsEligibility()`
- `SkillsPrompt()`
- `RunSkillsSnapshot()`

Assessment: Service-management responsibility.

---

## C. Gateway integration surface
- `InvokeGatewayMethod(...)`
- `RouteGatewayRequest(...)`
- `PumpGatewayNetworkOnce(...)`

Private gateway composition:
- `BindSkillsCallbacks()`
- `BindGatewayPolicyCallbacks()`
- `BindToolRuntimeCallbacks()`
- `BindChatCallbacks()`
- `BindEmbeddingsCallbacks()`
- `BuildConfigSchemaGatewayState()`
- `LookupConfigSchemaGatewayPath(...)`
- `WriteConfigSchemaDocumentationSnapshot(...)`

Assessment: Mixed. Some pure orchestration; some methods currently contain deep request/business processing.

---

## D. Skills/hooks orchestration and projection
- `BuildGatewaySkillsState()`
- `RefreshGatewaySkillsStateProjection()`
- `PublishGatewaySkillsStateProjection()`
- `BuildGatewaySkillEntry(...)`
- `BuildRuntimeSkillCommandSourceAdapters()`
- `RefreshSkillsState(...)`

Assessment: Mixed. `BuildGatewaySkillEntry(...)` and parts of callback wiring include domain mapping logic beyond thin composition.

---

## E. Chat runtime orchestration helpers
- `ResolveSkillInvocationToolTarget(...)`
- `ResolveSkillInvocationPromptRewrite(...)`
- `ShouldLoadSkillCommandsForInlineActions(...)`
- `BuildOrderedAllowedToolTargets(...)`
- `ExtractInlineToolResultText(...)`
- `TryExecuteInlineToolInvocation(...)`
- `ConvertEmbeddedTaskDeltas(...)`
- `ApplyEmbeddedExecutionTelemetry(...)`
- `IsEmbeddedDynamicLoopCanaryEligible(...)`
- `IsEmbeddedDynamicLoopPromotionReady()`
- `IsLocalModelRolloutEligible()`

Assessment: Mixed. Several are lightweight helpers; some implement policy/behavior decisions.

---

## F. Managed reload / cleanup / lifecycle internals
- `ApplyManagedRuntimeConfigDiff(...)`
- `ResetGatewayOwnedRuntimeCleanup()`
- `RegisterGatewayOwnedRuntimeCleanup(...)`
- `ExecuteGatewayOwnedRuntimeCleanup()`
- `ExecuteGatewayStartupFailureCleanup(...)`
- `ExecuteNonGatewayRuntimeCleanup()`
- `RecordGatewayLifecycleTransition(...)`
- `QueueManagedConfigInternalWriteHash(...)`
- `ConsumeManagedConfigInternalWriteHash()`

Assessment: Mostly orchestration with one heavy function (`ApplyManagedRuntimeConfigDiff(...)`).

---

## G. Provider credential/cancellation integration
- `SetActiveChatProvider(...)`
- `ActiveChatProvider()`
- `ActiveChatModel()`
- `ResolveDeepSeekCredentialUtf8()`
- `HasDeepSeekCredential()`
- `InvokeDeepSeekRemoteChat(...)`
- `IsDeepSeekRunCancelled(...)`
- `MarkDeepSeekRunCancelled(...)`
- `ClearDeepSeekRunCancelled(...)`
- `IsEmbeddedRunCancelled(...)`
- `MarkEmbeddedRunCancelled(...)`
- `ClearEmbeddedRunCancelled(...)`

Assessment: Mostly service-management responsibility.

---

## H. Diagnostics
- `BuildOperatorDiagnosticsReport()`

Assessment: Partially improved (email projection extracted), but still large snapshot assembly logic remains in `ServiceManager`.

---

## 2) Functions that still contain deep business logic

The following methods are the main outliers relative to the goal that `ServiceManager` should be a composition/lifecycle facade:

1. `BindChatCallbacks()`
- Why deep: Contains substantial runtime behavior branching (inline invocation auth, embedded fallback behavior, provider switching, local-model prompt/retry logic, retrieval memory side effects).
- Refactor target:
  - `ChatRuntimeOrchestrationCoordinator` (request flow and branch policy)
  - `InlineToolInvocationService` (auth + execution envelope)
  - `LocalModelResponseGuardService` (echo detection + retry policy)
  - Keep `BindChatCallbacks()` as only callback registration + delegate call.

2. `InitializeModules()`
- Why deep: Mixes startup wiring with domain bootstrap flows (skills refresh modes, hooks bootstrap event dispatch/remediation, fixture validation branching).
- Refactor target:
  - `ServiceStartupOrchestrator` with phase DTO results
  - `SkillsStartupCoordinator` for refresh/minimal load decisions
  - `HooksStartupCoordinator` for bootstrap+dispatch+governance flow
  - Keep `InitializeModules()` as phase invocation and state assignment only.

3. `ApplyManagedRuntimeConfigDiff(...)`
- Why deep: Includes complex policy decisions and rollback behavior (auth generation gating, local model runtime rebuild/fallback, email policy reload and gateway updates).
- Refactor target:
  - `ManagedRuntimeConfigDiffCoordinator`
  - sub-components: `AuthSessionGenerationGuard`, `LocalModelReloadCoordinator`, `GatewayManagedReloadProjector`.

4. `BuildGatewaySkillEntry(...)`
- Why deep: Contains metadata normalization, OpenClaw/BlazeClaw compatibility mapping, config hint transformations.
- Refactor target:
  - `GatewaySkillEntryProjector` / `SkillsGatewayProjectionService`
  - `SkillMetadataNormalizationService` (field resolution + mapping rules)
  - keep method as one-line delegation.

5. `BindSkillsCallbacks()`
- Why deep: Large inlined request parsing/persistence/error-envelope construction for skills update path.
- Refactor target:
  - `SkillsGatewayMethodHandler` (e.g., `HandleSkillsUpdateRequest(...)`)
  - callback binding should only forward request + return handler response.

6. `BuildOperatorDiagnosticsReport()` (partial)
- Why deep: Still assembles broad cross-domain snapshot population in one method.
- Refactor target:
  - retain `EmailRuntimeDiagnosticsProjector`
  - add `GatewayLifecycleDiagnosticsProjector`, `EmbeddedRuntimeDiagnosticsProjector`, `HooksDiagnosticsProjector`, `ModelRuntimeDiagnosticsProjector`.

---

## 3) Refactoring roadmap (consistency with service-management role)

## Phase 1 (high impact, low contract risk)
1. ✅ Extract chat runtime flow from `BindChatCallbacks()` to `ChatRuntimeOrchestrationCoordinator`.
2. ✅ Extract skills update request handling from `BindSkillsCallbacks()` to `SkillsGatewayMethodHandler`.
3. ✅ Extract skill entry projection from `BuildGatewaySkillEntry(...)` to projector service.

Phase 1 implementation notes:
- Added `ChatRuntimeOrchestrationCoordinator` to prepare inline/tool orchestration
  request context in `BindChatCallbacks`.
- Refactored provider/local fallback runtime branch handling into
  `ServiceManager::ExecuteProviderChatRuntimePath(...)` to keep callback wiring
  path thinner and more deterministic.
- Added `SkillsGatewayMethodHandler` and delegated `skills.update` request parsing,
  host persistence, and response shaping from `BindSkillsCallbacks`.
- Added `SkillsGatewayProjectionService` and delegated skill gateway entry mapping
  from `BuildGatewaySkillEntry(...)`.
- Added contract test coverage:
  - `ServiceManager phase1 contract: delegates chat orchestration, skills update handling, and skill projection`

## Phase 2 (medium risk)
4. ✅ Split `InitializeModules()` into startup coordinators (`SkillsStartupCoordinator`, `HooksStartupCoordinator`, `FixtureStartupValidatorFacade`).
5. ✅ Extract `ApplyManagedRuntimeConfigDiff(...)` into managed reload coordinator with explicit result DTO.

Phase 2 implementation notes:
- Added startup orchestration seams:
  - `SkillsStartupCoordinator`
  - `HooksStartupCoordinator`
  - `FixtureStartupValidatorFacade`
- `InitializeModules()` now delegates branch gating and execution sequencing for:
  - startup skills refresh mode,
  - hook bootstrap gating,
  - fixture validation gating.
- Added `ManagedRuntimeConfigDiffCoordinator` and delegated:
  - auth session generation guard evaluation,
  - local model reload decision envelope.
- `ServiceManager::ApplyManagedRuntimeConfigDiff(...)` remains the composition/state
  facade while coordinator handles reusable decision logic.
- Added contract test coverage:
  - `ServiceManager phase2 contract: delegates startup and managed config diff seams`

## Phase 3 (incremental quality)
6. ✅ Split diagnostics assembly in `BuildOperatorDiagnosticsReport()` into projector modules.
7. ✅ Keep `ServiceManager` method bodies mostly as:
   - resolve DTOs
   - delegate to coordinator/service
   - store snapshot/state
   - wire callbacks

Phase 3 implementation notes:
- Added diagnostics projector seams:
  - `GatewayLifecycleDiagnosticsProjector`
  - `EmbeddedRuntimeDiagnosticsProjector`
  - `ModelRuntimeDiagnosticsProjector`
  - `HooksDiagnosticsProjector`
  - (retained) `EmailRuntimeDiagnosticsProjector`
- `BuildOperatorDiagnosticsReport()` now delegates projection for gateway lifecycle,
  embedded runtime, hooks, and model/runtime diagnostics to dedicated projectors.
- ServiceManager diagnostics flow now remains focused on:
  - assembling high-level context DTOs,
  - invoking projector modules,
  - preserving output contract through `CDiagnosticsReportBuilder`.
- Added contract test coverage:
  - `ServiceManager phase3 contract: delegates diagnostics projection to projector modules`

---

## 4) Summary conclusion

`ServiceManager` has significantly improved and many email-domain concerns are already extracted. However, several methods still contain deep runtime/business logic and should be further delegated to dedicated coordinators/handlers/projectors.

Current status vs target statement (“should not be the place where deep business logic is implemented”):
- **Partially achieved**.
- Remaining high-priority deep-logic hotspots: `BindChatCallbacks`, `InitializeModules`, `ApplyManagedRuntimeConfigDiff`, `BuildGatewaySkillEntry`, `BindSkillsCallbacks`, and broad parts of `BuildOperatorDiagnosticsReport`.
