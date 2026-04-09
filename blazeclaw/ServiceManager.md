# ServiceManager Class Overview

## What `ServiceManager` Is
`ServiceManager` is the **application orchestration facade** for BlazeClaw core runtime behavior. In the current modular architecture, it is intended to act as a **composition root**: it wires core services together, manages lifecycle (`Start`/`Stop`), and delegates domain logic to specialized modules.

It should not be the place where deep business logic is implemented.

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

Builds snapshot data and delegates report shaping to `CDiagnosticsReportBuilder`.

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
   - Improves readability and failure isolation.

2. **Move remaining env/policy resolvers out of `ServiceManager.cpp`**
   - Keep all env parsing in bootstrap/policy resolver modules.
   - `ServiceManager` should consume resolved DTOs only.
   - ✅ Implemented for hooks policy/env resolver cluster.
   - Added `StartupPolicyResolver::HooksPolicySettings` DTO.
   - `ServiceManager::ConfigurePolicies(...)` now consumes
     `CServiceBootstrapCoordinator::ResolveHooksPolicySettings(...)`.
   - Removed migrated `ResolveHooks*` helper cluster from `ServiceManager.cpp`.
   - ✅ Implemented for non-hooks email policy resolver cluster.
   - Added `StartupPolicyResolver::EmailPolicySettings` DTO.
   - `ServiceManager::ConfigurePolicies(...)` now consumes
     `CServiceBootstrapCoordinator::ResolveEmailPolicySettings(...)`.
   - Removed inline email rollout/enforcement policy branch logic from `ServiceManager.cpp`.
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

4. **Use constructor-injected module interfaces where practical**
   - Enables easier testing/mocking and clearer dependency boundaries.

## B) Runtime/Behavior Optimizations

5. **Harden startup error model with structured status object**
   - Return richer startup diagnostics (phase, reason, recoverability) instead of bool-only path.

6. **Consolidate cancellation handling strategy**
   - Consider a unified cancellation registry abstraction instead of multiple maps.

7. **Reduce duplicated state projections**
   - Build snapshot DTOs once per report/tick where possible.
   - Reuse immutable snapshots across diagnostics and gateway publication.

## C) Maintainability Optimizations

8. **Narrow `ServiceManager.h` include footprint**
   - Use forward declarations when possible in header; move heavy includes to cpp.
   - Improves compile times and coupling.

9. **Introduce internal wiring helpers with strict naming**
   - Example: `BindChatCallbacks`, `BindSkillsCallbacks`, `BindEmbeddingsCallbacks`.
   - Easier code navigation and onboarding.

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
3. Add startup/wiring contract tests (phase-level orchestration coverage).
   - ✅ Implemented for startup phase ordering + failure-path assertions.
4. Keep diagnostics regression gate configurable (non-blocking local, strict CI).

---

## Final Assessment
`ServiceManager` is now close to its intended architecture role: a **composition and lifecycle façade**. The next optimization wave should focus on reducing residual orchestration complexity, tightening boundaries, and strengthening wiring-level test coverage, while preserving current runtime behavior parity.