# ServiceManager Email Business Logic Decoupling Plan

## Goal
Remove email-specific business logic from `ServiceManager` and keep it as a lifecycle/composition facade.

## Problem Statement
`ServiceManager` currently owns multiple email-domain responsibilities (policy resolution details, runtime fallback wiring decisions, state shaping, and gateway-level email-specific handling). This increases coupling and makes `ServiceManager` harder to reason about, test, and evolve.

## Target Architecture
`ServiceManager` should only:
- request resolved email policy/runtime DTOs from specialized modules,
- wire callbacks and delegates,
- expose summary snapshots.

Email business behavior should move to dedicated email-domain services under `src/core` and/or `src/gateway/executors` composition boundaries.

---

## Scope

### In Scope
- Extract email business rules from `ServiceManager` into dedicated modules.
- Keep Option 5 + Option 6 behavior parity (dependency preflight health index + configurable fallback policy profiles).
- Preserve runtime behavior and gateway contract compatibility.
- Add/extend tests for decoupled orchestration seams.

### Out of Scope
- Rewriting the full gateway architecture.
- Changing external protocol contracts unless strictly necessary.
- Broad refactors unrelated to email runtime/fallback orchestration.

---

## Design Principles
- `ServiceManager` remains a composition root and lifecycle facade.
- Business rules live in domain services, not in startup/wiring orchestrators.
- DTO-in/DTO-out contracts between orchestration and domain modules.
- Backward-compatible policy semantics and diagnostics.
- OpenClaw workflow parity preserved without hardcoded flow-specific logic.

---

## Proposed Module Split

## 1) `EmailPolicyOrchestrationService`
**Responsibilities**
- Resolve effective policy profile selection (default/capability/tool).
- Normalize and validate runtime policy actions.
- Produce resolved DTO for runtime usage.

**ServiceManager interaction**
- `ServiceManager` calls a single `ResolveRuntimePolicy(...)` method and consumes returned DTO.

## 2) `EmailFallbackRuntimeCoordinator`
**Responsibilities**
- Runtime fallback decisioning for unavailable/auth/exec-error scenarios.
- Retry strategy interpretation.
- Approval-required gate handling (pre-execution decision output).

**ServiceManager interaction**
- `ServiceManager` delegates fallback outcome requests and consumes result envelopes.

## 3) `EmailPreflightHealthService`
**Responsibilities**
- Option 5 preflight dependency probing and health index shaping.
- Capability readiness state production.

**ServiceManager interaction**
- `ServiceManager` consumes health snapshot only for publication/diagnostics.

## 4) `EmailRuntimeDiagnosticsProjector`
**Responsibilities**
- Build email-focused diagnostics projection from domain snapshots.
- Keep report-field mapping out of `ServiceManager`.

**ServiceManager interaction**
- `ServiceManager` includes prebuilt projection into global diagnostics snapshot.

---

## Tracking Plan

## Phase A — Baseline and Seam Inventory
- [x] Enumerate all email-related fields and methods in `ServiceManager.h/.cpp`.
- [x] Map each usage site to one of: policy resolution, fallback decisioning, preflight, diagnostics projection, gateway wiring.
- [x] Produce dependency graph of current email orchestration calls.

Phase A notes:
- Inventory identified primary email seams in `ServiceManager`:
  - policy resolution (`ResolveEmailFallbackPolicy` + config/profile rollover state),
  - gateway wiring (`SetEmailFallbackRuntimeFlags`, `SetEmailFallbackResolvedPolicy`),
  - diagnostics projection (`BuildOperatorDiagnosticsReport` email fields),
  - state carrier fields (`m_emailFallbackResolvedPolicy`, `m_state.emailPolicy`).
- Dependency flow baseline captured:
  - `AppConfig.email` + `StartupPolicyResolver::EmailPolicySettings`
  - → email fallback resolution
  - → gateway policy binding
  - → diagnostics projection.

## Phase B — Contract Extraction
- [x] Define DTOs/interfaces for extracted services.
- [x] Add adapter layer where necessary to preserve current call signatures.
- [x] Mark deprecated direct email logic paths in `ServiceManager` with migration TODO markers.

Phase B notes:
- Added `EmailPolicyOrchestrationService` with explicit DTO contracts:
  - `ResolvedEmailFallbackPolicy`
  - `GatewayEmailPolicyBinding`
- Extracted service API:
  - `ResolveFallbackPolicy(const AppConfig&, toolName, capabilityName)`
  - `BuildGatewayPolicyBinding(const EmailFallbackConfig&, runtimeEnabled, runtimeEnforce, resolvedPolicy)`
- Removed in-class resolver declaration/struct ownership from `ServiceManager` and replaced with service-owned DTO usage.

## Phase C — Policy Resolution Extraction
- [x] Move remaining email policy business logic into `EmailPolicyOrchestrationService`.
- [x] Replace `ServiceManager` inline logic with single service call.
- [x] Verify policy profile behavior parity for default/capability/tool paths.

Phase C notes:
- `ServiceManager` now delegates email fallback resolution and gateway policy projection to `EmailPolicyOrchestrationService` in:
  - startup policy configuration path,
  - gateway callback binding path,
  - managed config reload apply path.
- Added service-focused parity tests in `EmailScheduleFallbackTests.cpp` for:
  - fallback policy resolution behavior,
  - runtime-gated gateway binding behavior.

## Phase D — Runtime Fallback Extraction (Option 6)
- [x] Move fallback decisioning/retry/approval behavior to `EmailFallbackRuntimeCoordinator`.
- [x] Replace `ServiceManager` branching with coordinator delegation.
- [x] Validate unavailable/auth/exec-error action outcomes parity.

Phase D notes:
- Added `EmailFallbackRuntimeCoordinator` as extracted runtime fallback decision service.
- `ServiceManager` now delegates embedded failure fallback eligibility decisions via
  `EmailFallbackRuntimeCoordinator::EvaluateEmbeddedFailure(...)`.
- Replaced direct fallback decision helper method in `ServiceManager`.
- Added explicit email fallback attempt/success/failure state counters driven by delegated decision outcomes.

## Phase E — Preflight/Health Extraction (Option 5)
- [x] Move dependency probe/health-index business shaping to `EmailPreflightHealthService`.
- [x] Keep service-level publication in `ServiceManager` as thin projection wiring.
- [x] Validate startup/runtime health state reporting parity.

Phase E notes:
- Added `EmailPreflightHealthService` to encapsulate runtime health index retrieval.
- `ServiceManager` diagnostics now consume preflight health via service abstraction,
  removing direct `EmailScheduleExecutor` health call coupling.

## Phase F — Diagnostics Decoupling
- [x] Move email diagnostics projection assembly out of `ServiceManager`.
- [x] Keep `ServiceManager` report builder invocation unchanged from caller perspective.
- [x] Validate diagnostics fields for email rollout mode, policy state, and fallback counters.

Phase F notes:
- Added `EmailRuntimeDiagnosticsProjector` to assemble email diagnostics fields into
  `DiagnosticsSnapshot`.
- `ServiceManager::BuildOperatorDiagnosticsReport()` now delegates email-specific field
  projection to the projector while preserving caller-facing report flow.
- Diagnostics now project explicit email fallback attempt/success/failure counters from
  dedicated email fallback telemetry state.

## Phase G — Test Hardening
- [x] Add unit tests for extracted email services.
- [x] Add orchestration seam tests proving `ServiceManager` delegates instead of computes.
- [x] Add scenario tests for fallback + approval + retry transitions.
- [x] Run parity-focused test suites and contract checks.

Phase G notes:
- Added service unit tests in `EmailScheduleFallbackTests.cpp` for:
  - `EmailFallbackRuntimeCoordinator` fallback classification,
  - `EmailPreflightHealthService` health index retrieval,
  - `EmailRuntimeDiagnosticsProjector` diagnostics field mapping.
- Added ServiceManager orchestration seam contract coverage in
  `ServiceManagerStartupPhaseContractTests.cpp` to verify delegation to:
  - `EmailFallbackRuntimeCoordinator`,
  - `EmailPreflightHealthService`,
  - `EmailRuntimeDiagnosticsProjector`.
- Added fallback+approval+retry scenario tests for policy action transitions:
  - exec_error + `stop` blocks backend fallback,
  - exec_error + `retry_then_continue` transitions to fallback backend.

## Phase H — Cleanup and Finalization
- [x] Remove obsolete email helper branches from `ServiceManager.cpp`.
- [x] Reduce `ServiceManagerState::EmailPolicyState` surface to summary-only data.
- [x] Update docs (`ServiceManager.md`, architecture docs, rollout notes).

Phase H notes:
- Removed obsolete in-class email helper logic from `ServiceManager.cpp` in favor of
  extracted coordinator/service/projector delegation.
- `EmailPolicyState` now remains summary-oriented runtime policy state used only for
  orchestration and reporting projections.
- Updated architecture/tracking docs to reflect full Phase A-H extraction progress.

---

## Candidate File Touchpoints
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.h`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`
- `blazeclaw/BlazeClawMfc/src/core/bootstrap/StartupPolicyResolver.*`
- `blazeclaw/BlazeClawMfc/src/core/bootstrap/CServiceBootstrapCoordinator.*`
- `blazeclaw/BlazeClawMfc/src/gateway/executors/EmailScheduleExecutor.*`
- `blazeclaw/BlazeClawMfc/tests/*` (new service and orchestration tests)

---

## Milestones
- [x] M1: Email policy logic fully delegated (no direct computation in `ServiceManager`).
- [x] M2: Fallback coordinator integrated with Option 5/6 parity.
- [x] M3: Diagnostics projection decoupled.
- [x] M4: Test coverage and parity checks green.

---

## Risks and Mitigations
- **Risk:** Behavior drift in fallback policy handling.
  - **Mitigation:** Golden-case tests for unavailable/auth/exec-error paths.
- **Risk:** Hidden coupling to gateway state surfaces.
  - **Mitigation:** Introduce explicit projection DTOs and seam tests.
- **Risk:** Regression in startup/runtime diagnostics.
  - **Mitigation:** Snapshot-based diagnostics regression comparison updates.

---

## Validation Checklist
- [x] Required build command passes:
  - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
- [x] Email fallback behavior parity validated for Option 5 + Option 6.
- [x] No email-domain branching remains in `ServiceManager` except composition/delegation.
- [x] `ServiceManager` remains deterministic and lifecycle-focused.

---

## Definition of Done
- Email business logic is implemented in dedicated services.
- `ServiceManager` contains only orchestration/composition and summary projection usage.
- Existing runtime behavior and protocol outputs are parity-compatible.
- Tests and docs are updated and passing.
