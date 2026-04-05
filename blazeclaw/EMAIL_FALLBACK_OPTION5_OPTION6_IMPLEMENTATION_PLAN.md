# Option 5 + Option 6 Implementation Plan

## Scope
Implement a future-extensible fallback architecture using:

- **Option 5:** dependency preflight + runtime health index
- **Option 6:** configurable fallback policy profiles

Target outcome: email-capable task-delta flows automatically avoid unavailable backends/tools and deterministically apply policy without hardcoded flow-specific branching.

---

## Objectives

1. Detect backend/tool readiness before execution.
2. Route execution using profile-driven policy, not fixed if/else logic.
3. Keep embedded orchestration generic and capability-based.
4. Preserve backward compatibility for existing `email.schedule` flows.
5. Provide operator visibility via health/policy telemetry.

---

## High-Level Architecture

## A) Dependency Preflight Subsystem (Option 5)

### Responsibilities
- Probe runtime dependencies and credentials on startup and periodically.
- Produce a normalized health index by capability/tool/backend.
- Expose read APIs for resolver/orchestrator and diagnostics.

### Suggested readiness states
- `ready`: executable and required config available.
- `degraded`: executable available but non-blocking warning exists.
- `unavailable`: missing executable/skill/config for required operation.
- `unknown`: preflight not executed yet.

### Suggested probe dimensions
- binary availability (`himalaya`, `node`, optional others)
- skill script availability (`imap-smtp-email/scripts/smtp.js`)
- account/config presence
- transport smoke result cache (recent success/failure with TTL)

### Data model (proposed)
- `DependencyProbeResult`
  - `key` (for example `backend:himalaya`)
  - `state`
  - `reasonCode`
  - `reasonMessage`
  - `checkedAtEpochMs`
  - `expiresAtEpochMs`
- `RuntimeHealthIndex`
  - map of probe results
  - derived capability summary (for example `email.send=ready|degraded|unavailable`)

---

## B) Policy Profile Subsystem (Option 6)

### Responsibilities
- Load declarative fallback profiles.
- Resolve runtime decision for a capability/tool request.
- Define behavior for unavailable/auth/exec errors.

### Profile hierarchy
1. per-tool override
2. per-capability profile
3. global default profile

### Policy profile fields (proposed)
- `profileId`
- `capability` (for example `email.send`)
- `backendOrder` (for example `["himalaya","imap-smtp-email"]`)
- `onUnavailable` (`continue|stop`)
- `onAuthError` (`continue|stop`)
- `onExecError` (`retry_then_continue|stop`)
- `maxBackendAttempts`
- `retry`
  - `maxRetries`
  - `backoffMs`
- `approvalPolicy`
  - `requiresApproval`
  - `allowAutoApproveWhenImmediate`

### Runtime decision output (proposed)
- `PolicyDecision`
  - `selectedBackend`
  - `candidateBackends`
  - `fallbackAllowed`
  - `terminalOnErrorClass`
  - `decisionReason`

---

## C) Resolver Integration

Add `EmailDeliveryResolver` (or generic capability resolver):

Inputs:
- requested capability (for example `email.send`)
- health index snapshot
- policy profile
- runtime context (immediate/scheduled, approval state)

Outputs:
- ordered executable backend plan
- stop/continue semantics per failure class

Execution loop:
1. filter `backendOrder` by health (`ready`, optional `degraded`).
2. execute in order per policy.
3. classify failures (`unavailable`, `auth_error`, `exec_error`, `transient`).
4. apply profile action.
5. emit deterministic run/task-delta status and telemetry.

---

## File-Level Implementation Plan

## Executable Phase-by-Phase Coding Checklist (Exact C++ Targets)

This checklist is implementation-first and maps each phase to concrete C++ files,
target interfaces, and completion checks.

### Phase 0 — Baseline and Feature Flags

**Target files**
- `blazeclaw/BlazeClawMfc/src/config/ConfigModels.h`
- `blazeclaw/BlazeClawMfc/src/config/ConfigLoader.cpp`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.h`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`

**Interfaces / members to add or extend**
- `blazeclaw::config::AppConfig` / nested config structs:
  - `email.preflight.enabled`
  - `email.policyProfiles.enabled`
  - `email.policyProfiles.enforce`
- `ServiceManager` startup wiring to pass new config into gateway/runtime paths.

**Checklist**
1. Add Option 5/6 config fields to config models.
2. Parse new keys in config loader with safe defaults.
3. Expose effective flag state in operator diagnostics.
4. Keep legacy behavior when flags are disabled.

**Phase 0 implementation status (current): completed**

- Implemented config keys:
  - `email.preflight.enabled`
  - `email.policyProfiles.enabled`
  - `email.policyProfiles.enforce`
- Implemented startup wiring:
  - `ServiceManager -> GatewayHost` runtime flag propagation.
- Implemented diagnostics exposure:
  - `ServiceManager::BuildOperatorDiagnosticsReport()` includes `emailFallback` state.
- Implemented runtime visibility:
  - `gateway.config.get` and `gateway.config.snapshot` include `emailFallback` flags.

---

### Phase 1 — Dependency Preflight Models + Probes (Option 5)

**Target files**
- `blazeclaw/BlazeClawMfc/src/gateway/executors/EmailScheduleExecutor.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.h`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.cpp`

**New interface targets**
- Add health model types (new structs in gateway layer), for example:
  - `DependencyProbeResult`
  - `RuntimeHealthIndex`
- Add read APIs in gateway host (or equivalent internal accessors), for example:
  - `GetRuntimeHealthIndex()`
  - `RefreshRuntimeHealthIndex()`

**Checklist**
1. Extract current dependency checks (`HasHimalayaBinary`, `HasNodeBinary`,
   `HasImapSmtpSkill`) into reusable probe helpers.
2. Build cached health snapshot with TTL and timestamp fields.
3. Add capability-level derived readiness (`email.send`).
4. Ensure preflight can be forced on-demand after failed backend attempts.

---

### Phase 2 — Health Endpoints + Contract/Schema Updates

**Target files**
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolContract.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolSchemaValidator.Request.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolSchemaValidator.Response.cpp`

**Endpoints to add**
- `gateway.runtime.health.dependencies`
- `gateway.runtime.health.capabilities`

**Checklist**
1. Register new runtime health methods in runtime handlers.
2. Add request/response contract examples and fixtures.
3. Extend schema validator for new payload fields.
4. Keep response format deterministic for diagnostics and tests.

---

### Phase 3 — Policy Profile Models + Loader (Option 6)

**Target files**
- `blazeclaw/BlazeClawMfc/src/config/ConfigModels.h`
- `blazeclaw/BlazeClawMfc/src/config/ConfigLoader.cpp`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.h`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`

**New interface targets**
- Add policy profile structures, for example:
  - `EmailFallbackPolicyProfileConfig`
  - `EmailFallbackRetryPolicyConfig`
  - `EmailFallbackApprovalPolicyConfig`
- Add effective policy resolver entrypoint, for example:
  - `ResolveEmailFallbackPolicy(...)`

**Checklist**
1. Define profile hierarchy: tool > capability > default.
2. Add config parsing and validation rules for policy actions.
3. Normalize invalid entries to safe defaults and emit warnings.
4. Surface effective policy in operator diagnostics.

---

### Phase 4 — Resolver Introduction + Email Executor Migration

**Target files**
- `blazeclaw/BlazeClawMfc/src/gateway/executors/EmailScheduleExecutor.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.h`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`

**New/updated interface targets**
- Introduce resolver abstraction (new class or static helper), for example:
  - `EmailDeliveryResolver::ResolvePlan(...)`
  - `EmailDeliveryResolver::ClassifyFailure(...)`
- Keep executor API contract unchanged:
  - `EmailScheduleExecutor::Create()`
  - output envelope fields (`protocolVersion`, `ok`, `status`, `error`).

**Checklist**
1. Replace ad-hoc backend chain loop with resolver output plan.
2. Apply policy action matrix:
   - unavailable -> continue/stop
   - auth_error -> continue/stop
   - exec_error -> retry_then_continue/stop
3. Preserve approval-token prepare/approve semantics.
4. Preserve legacy envelope compatibility.

---

### Phase 5 — Runtime Orchestration Path Alignment

**Target files**
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.cpp`
- `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.h`
- `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.cpp`

**Interface targets**
- Remove backend-specific checks in weather shortcut path:
  - replace `himalaya_cli_missing` special handling with resolver output usage.
- Extend task-delta metadata contract fields via existing structures:
  - `ChatRuntimeResult::TaskDeltaEntry`
  - `EmbeddedTaskDelta`

**Checklist**
1. Consume normalized resolver decision in orchestration output.
2. Emit fallback attempt/backend info in `tool_result` deltas.
3. Ensure terminal statuses are policy-derived and deterministic.
4. Keep dynamic tool loop behavior generic and non-hardcoded.

---

### Phase 6 — Policy Resolution Endpoint + Diagnostics

**Target files**
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolContract.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolSchemaValidator.Response.cpp`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`

**Endpoint to add**
- `gateway.runtime.policy.resolve`

**Checklist**
1. Return selected profile id, effective backend order, and action rules.
2. Add telemetry events:
   - `gateway.email.preflight.snapshot`
   - `gateway.email.policy.decision`
   - `gateway.email.fallback.attempt`
   - `gateway.email.fallback.terminal`
3. Add operator diagnostics counters for fallback attempts/success/failure.

---

### Phase 7 — Tests and Validation Matrix

**Target files**
- `blazeclaw/BlazeClawMfc/tests/EmailScheduleFallbackTests.cpp`
- `blazeclaw/BlazeClawMfc/tests/OpsToolsExecutorTests.cpp`
- `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.cpp` (fixture scenarios)

**Checklist**
1. Add unit cases for policy precedence resolution.
2. Add unit cases for readiness classification (`ready/degraded/unavailable`).
3. Add integration-like executor cases:
   - node missing + himalaya ready
   - himalaya missing + node/skill ready
   - both unavailable (deterministic terminal behavior)
4. Verify task-delta fallback metadata and terminal status consistency.
5. Build validation command:
   - `msbuild blazeclaw/BlazeClaw.sln /t:Build /p:Configuration=Debug /p:Platform=x64`

---

### Phase 8 — Rollout and Migration Gate

**Target files**
- `blazeclaw/BlazeClawMfc/src/config/ConfigModels.h`
- `blazeclaw/BlazeClawMfc/src/config/ConfigLoader.cpp`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`

**Checklist**
1. Start monitor-only mode (`policyProfiles.enabled=true`,
   `policyProfiles.enforce=false`).
2. Validate telemetry and policy-resolution endpoint in canary sessions.
3. Switch to enforce mode after validation.
4. Keep rollback path via flags and maintain Option-2-compatible path as bridge.

---

## Core Interfaces Inventory (Implementation Contract)

### Existing interfaces to preserve
- `EmailScheduleExecutor::Create()`
- `GatewayHost::RegisterRuntimeHandlers()`
- `GatewayHost::RouteRequest(...)`
- `ServiceManager::Start(...)`
- `PiEmbeddedService::ExecuteRun(...)`

### New interfaces to introduce
- Health index access:
  - `GetRuntimeHealthIndex()`
  - `RefreshRuntimeHealthIndex()`
- Policy resolution:
  - `ResolveEmailFallbackPolicy(...)`
  - `gateway.runtime.policy.resolve`
- Resolver:
  - `EmailDeliveryResolver::ResolvePlan(...)`
  - `EmailDeliveryResolver::ClassifyFailure(...)`

## Phase 1 — Foundation (Preflight + Models)

### 1. Add health models
- Introduce health index structs/classes in gateway/runtime core.
- Add serialization helpers for gateway responses and telemetry.

### 2. Implement dependency probes
- Move dependency checks into reusable probe functions.
- Add startup preflight and periodic refresh (configurable interval).

### 3. Add health query endpoints
- `gateway.runtime.health.dependencies`
- `gateway.runtime.health.capabilities`

Deliverable:
- deterministic health snapshot APIs available before execution.

---

## Phase 2 — Policy Profiles

### 4. Add config schema for fallback profiles
- extend app config models and parser.
- support defaults + per-capability/per-tool overrides.

### 5. Add policy validation
- validate backend names, action enums, retry bounds.
- startup warnings for invalid policy entries with safe defaults.

### 6. Add effective policy endpoint
- `gateway.runtime.policy.resolve` (debug/ops visibility).

Deliverable:
- runtime can resolve effective profile deterministically.

---

## Phase 3 — Resolver + Email Executor Migration

### 7. Introduce resolver class
- use policy + health snapshot to build backend execution plan.

### 8. Refactor `EmailScheduleExecutor`
- replace ad-hoc backend chain with resolver-driven plan.
- keep existing response envelope contract.

### 9. Normalize failure taxonomy
- map backend failures to common classes and stable error codes.

Deliverable:
- `email.schedule` fallback behavior policy-driven and health-aware.

---

## Phase 4 — Embedded Orchestration Alignment

### 10. Surface fallback metadata in task deltas
- include fallback attempt index/backend in `tool_result` metadata.
- set terminal status/reason from policy outcome.

### 11. Remove flow-specific backend checks in weather shortcut path
- no direct `himalaya_cli_missing` special-case logic.
- rely on resolver outcome and policy profile.

Deliverable:
- no hardcoded backend-specific fallback in runtime orchestration path.

---

## Phase 5 — Observability + Rollout

### 12. Telemetry additions
- `gateway.email.preflight.snapshot`
- `gateway.email.policy.decision`
- `gateway.email.fallback.attempt`
- `gateway.email.fallback.terminal`

### 13. Rollout flags
- `email.preflight.enabled`
- `email.policyProfiles.enabled`
- `email.policyProfiles.enforce`

### 14. Canary strategy
- start in monitor mode (decision logging only)
- then enforce for selected providers/sessions
- finally default-on

Deliverable:
- safe progressive rollout with rollback toggles.

---

## Test Plan

## Unit tests
1. preflight state derivation for missing binaries/config.
2. policy precedence (tool > capability > default).
3. resolver selection with mixed ready/degraded/unavailable backends.
4. action matrix behavior:
   - unavailable + continue
   - auth_error + stop
   - exec_error + retry_then_continue

## Integration tests
1. weather/report/email prompt with:
   - only himalaya ready
   - only imap-smtp-email ready
   - both unavailable
2. verify deterministic output envelope and task-delta phases.

## Regression tests
- existing `email.schedule` approval prepare/approve flow contract unchanged.
- legacy behavior when feature flags disabled.

---

## Documentation Update Checklist

1. `EMAIL_FALLBACK_FIX_OPTIONS.md`
   - set recommendation to Option 5 + Option 6.
   - reference this detailed implementation plan.
2. `EMBEDDED_ORCHESTRATION_REFACTOR_PLAN.md`
   - add workstream for preflight health + policy-driven fallback.
3. `HARDCODED_WEATHER_REPLY_ANALYSIS.md`
   - add note to remove backend-specific hardcoded checks.
4. `PARITY_PORTING_ANALYSIS.md`
   - add follow-up for policy-driven backend resolution parity.
5. `PYTHON_SUPPORT_IMPLEMENTATION_PLAN.md`
   - add cross-runtime policy alignment note.

---

## Risks and Mitigations

- Risk: stale health snapshots causing wrong routing.
  - Mitigation: TTL + on-demand probe on first failure.
- Risk: policy misconfiguration blocks execution.
  - Mitigation: strict validation + safe fallback defaults + diagnostics endpoint.
- Risk: behavior drift between orchestration paths.
  - Mitigation: central resolver usage and shared failure taxonomy.

---

## Definition of Done

1. Resolver-based backend selection is active and policy-driven.
2. No prompt-flow-specific backend missing special-casing for email fallback.
3. Health/policy endpoints and telemetry available.
4. Unit + integration tests pass for fallback matrix.
5. Documentation set updated and internally consistent.