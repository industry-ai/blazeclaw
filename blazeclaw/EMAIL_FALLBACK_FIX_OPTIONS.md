# Email Sending Fallback Bug: Fix Options and Recommendation

## Problem Summary

Current behavior can fail with:

- `node_cli_missing`

when executing weather-report + email task-delta flows.

Expected behavior is:

- If one backend/tool is unavailable, automatically select an alternative email-capable tool/skill and continue.

Observed architecture notes:

- `email.schedule` already has backend chain logic (`himalaya -> imap-smtp-email`) in `EmailScheduleExecutor`.
- Prompt orchestration in `GatewayHost.Handlers.Runtime.cpp` still contains flow-specific logic and only soft-handles `himalaya_cli_missing` in one path.
- Embedded dynamic loop (`PiEmbeddedService`) currently stops run on tool execution failure and does not perform capability-based alternative tool selection.

---

## Possible Fix Ways

## 1) Minimal Hotfix in Runtime Orchestration (Fast)

### Idea
Handle `node_cli_missing` similarly to `himalaya_cli_missing` in weather-email orchestration path.

### Changes
- In `TryOrchestrateWeatherEmailPrompt(...)`, broaden backend-missing detection to include:
  - `node_cli_missing`
  - `imap_smtp_skill_missing`
  - `himalaya_cli_missing`
- Return a deterministic pending/needs-approval style response instead of hard error.

### Pros
- Smallest change.
- Immediate user-visible improvement.

### Cons
- Still flow-specific and hardcoded.
- Not extensible to future tools/skills.

---

## 2) Strengthen `email.schedule` Internal Fallback (Short Term)

### Idea
Keep orchestration unchanged but harden `EmailScheduleExecutor` backend policy and failure taxonomy.

### Changes
- Ensure backend chain always attempts all configured backends before final error.
- Add explicit backend availability classification:
  - `backend_unavailable` (missing binary/skill)
  - `backend_exec_error` (runtime failure)
  - `backend_auth_error` (credentials/config)
- Continue fallback on `backend_unavailable`; stop only for hard terminal policy errors.

### Pros
- Centralized in one executor.
- Backward compatible.

### Cons
- Still limited to `email.schedule` only.
- No cross-tool fallback selection.

---

## 3) Capability-Based Alternative Tool Selection in Embedded Loop (Medium)

### Idea
When a tool fails with availability errors, dynamically choose another runtime tool with equivalent capability.

### Changes
- Add capability metadata for tools (example: `capabilities: ["email.send"]`).
- Add optional fallback metadata (example: `fallbacks: ["email.schedule", "outlook.send", "smtp.send"]`).
- In `PiEmbeddedService::ExecuteRun(...)`:
  - On `embedded_tool_execution_failed`, check if error is availability-related.
  - Query candidate tools from runtime catalog by capability.
  - Retry same step with ranked alternatives.
  - Emit task-delta transitions with explicit fallback markers.

### Pros
- Generic and reusable for all domains, not only email.
- Aligns with dynamic task-delta orchestration direction.

### Cons
- Requires schema/metadata expansion.
- Needs more tests and telemetry updates.

---

## 4) Gateway-Level Tool Resolver/Router Before Execution (Medium-Long)

### Idea
Introduce a resolver layer that maps intent/capability to best available tool before each execute call.

### Changes
- Add a resolver service (priority + health + policy aware).
- Replace direct fixed tool IDs in orchestration with resolver calls.
- Resolver considers:
  - tool enabled state
  - dependency readiness (node/himalaya/etc.)
  - credential readiness
  - historical success score

### Pros
- Single policy point for fallback behavior.
- Better long-term maintainability.

### Cons
- More invasive than local executor changes.

---

## 5) Dependency Preflight + Runtime Health Index (Long Term)

### Idea
Build a preflight matrix that marks each tool backend as ready/degraded/unavailable, used by orchestration and resolver.

### Changes
- Add periodic probes for required dependencies and credentials.
- Expose health endpoints/fields for tool readiness.
- Use health state to avoid selecting unavailable tools.

### Pros
- Prevents avoidable failures before execution.
- Improves UX and observability.

### Cons
- Additional subsystem complexity.

---

## 6) Configurable Fallback Policy Profiles (Long Term)

### Idea
Support declarative fallback policy in config instead of hardcoding.

### Example
- `email.send.policy = ["himalaya", "imap-smtp-email", "outlook-graph"]`
- `onUnavailable = continue`
- `onAuthError = stop`

### Pros
- Enterprise-friendly and tunable.
- No recompilation for strategy changes.

### Cons
- Needs config schema and migration support.

---

## Best Option for Future Extend (Recommended)

## Recommendation: **Option 5 + Option 6** as primary track, with Option 2 as migration bridge

### Why
- Option 5 prevents avoidable failures by making dependency readiness explicit before execution.
- Option 6 removes hardcoded fallback behavior and enables declarative, configurable routing policies.
- Together they create a generic resolver foundation that scales across current and future email tools/skills.
- Option 2 can be used as a compatibility bridge while resolver/policy layers are rolled out.

### Suggested rollout
1. **Phase A (stabilizer):** keep Option 2 behavior, but classify failures using shared error taxonomy.
2. **Phase B (Option 5):** introduce dependency preflight and runtime health index APIs.
3. **Phase C (Option 6):** introduce policy profile schema and effective policy resolution.
4. **Phase D (enforcement):** switch `email.schedule` to resolver-driven execution using health + policy.
5. **Phase E (generalization):** propagate the same resolver/policy model to broader capability routing.

### Current implementation progress
- Phase 0 scaffolding is implemented:
  - config keys for preflight/policy flags are available,
  - startup wiring passes flags into gateway runtime state,
  - diagnostics and gateway config endpoints expose effective flag state.
- Phase 1 preflight is implemented:
  - dependency probes for himalaya/node/imap-smtp skill are cached in a
    runtime health index,
  - email delivery fallback can short-circuit on preflight unavailability
    when preflight is enabled,
  - operator diagnostics include preflight capability/probe summary fields.
- Phase 2 health API surface is implemented:
  - runtime endpoints `gateway.runtime.health.dependencies` and
    `gateway.runtime.health.capabilities` are available,
  - request/response schema validation and protocol contract coverage were
    added for deterministic gateway behavior,
  - gateway fixtures were added for both health responses.
- Phase 3 policy profile foundation is implemented:
  - config models now support default/capability/tool policy profile hierarchy,
  - loader now parses and normalizes action/retry/approval profile fields,
  - service startup resolves effective policy (`tool > capability > default`) and
    diagnostics now expose resolved policy details.
- Phase 4 resolver migration is implemented:
  - email fallback execution now follows resolver output plan instead of ad-hoc
    backend loop ordering,
  - policy action matrix is enforced for unavailable/auth/exec failure classes
    with bounded retry behavior,
  - runtime-resolved policy values are propagated through gateway runtime state
    into executor consumption while preserving legacy output envelope shape.
- Phase 5 runtime orchestration alignment is implemented:
  - weather shortcut path now uses normalized fallback handling rather than
    backend-specific `himalaya_cli_missing` checks,
  - runtime and embedded task-delta contracts now include fallback backend,
    action, and attempt metadata,
  - orchestration terminal statuses are now deterministic and policy-derived
    (`completed` / `needs_approval` / `failed`).
- Phase 6 policy endpoint and diagnostics are implemented:
  - runtime endpoint `gateway.runtime.policy.resolve` exposes effective profile,
    backend order, policy actions, retry settings, and approval settings,
  - telemetry now emits policy and fallback lifecycle events
    (`gateway.email.preflight.snapshot`, `gateway.email.policy.decision`,
    `gateway.email.fallback.attempt`, `gateway.email.fallback.terminal`),
  - operator diagnostics now include fallback attempts/success/failure counters.
- Phase 7 tests and validation matrix are implemented:
  - readiness-state tests now cover `ready` / `degraded` / `unavailable`,
  - fallback matrix tests now cover both directional backend failover paths plus
    deterministic all-backend-unavailable terminal behavior,
  - embedded fixture validation now asserts fallback metadata and terminal
    status consistency in task-delta chains.

### Detailed implementation reference
- `blazeclaw/EMAIL_FALLBACK_OPTION5_OPTION6_IMPLEMENTATION_PLAN.md`

### Executable coding checklist reference
- See section:
  - `Executable Phase-by-Phase Coding Checklist (Exact C++ Targets)`
- Includes:
  - exact C++ files to modify per phase,
  - interface inventory (existing + new),
  - rollout and validation command (`msbuild BlazeClaw.sln`).

---

## Required Validation for Any Fix

- Unit tests:
  - Missing node + himalaya present -> success via himalaya.
  - Missing himalaya + node present -> success via imap-smtp-email.
  - Missing both -> deterministic fallback/pending behavior (not brittle hard error in chat UX).
- Embedded task-delta tests:
  - Verify fallback transition deltas and terminal status consistency.
- Integration tests:
  - Weather-report + email prompt path in `chat.send` with dynamic orchestration enabled.
- Telemetry:
  - Count fallback attempts, fallback success, backend unavailability reasons.

---

## Notes

This recommendation also aligns with the project direction to prefer task-delta decomposition and LLM-driven dynamic tool sequencing over hardcoded flow-specific orchestration logic.