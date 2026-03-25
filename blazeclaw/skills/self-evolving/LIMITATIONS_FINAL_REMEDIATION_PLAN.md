# Self-Evolving Final Remediation Plan for Limitations & Follow-Ups

## Current Check Result
Not all items are cleared.

### Remaining items
1. **Known Limitation still active**
   - Hook execution uses a constrained handler-content adapter, not full TypeScript runtime execution.
2. **Follow-Up Enhancements still active**
   - Explicit gateway telemetry for reminder transitions is not fully implemented as dedicated transition events.
   - Reminder verbosity policy toggles are not implemented.
3. **Documentation cleanup still needed**
   - Follow-up list still includes PowerShell script variants, but `.ps1` parity is already completed.
   - "Shell helper parity is available" should not remain under `Known Limitations`.

## Remediation Plan

## Phase 1 — Documentation Correction (Immediate)
- Update `SKILL.md`:
  - remove completed PowerShell follow-up item
  - move shell parity statement out of `Known Limitations`
  - leave only unresolved technical limitations
- Update `references/hooks-setup.md` and `EVENT_HOOK_ENGINE_PLAN.md` to match final state.

## Phase 2 — Reminder Policy Controls
- Extend hooks engine config:
  - `hooks.engine.reminderEnabled` (bool)
  - `hooks.engine.reminderVerbosity` (`minimal|normal|detailed`)
- Parse in `ConfigLoader` and apply in `ServiceManager` + hook execution path.
- Add diagnostics fields for effective policy values.

## Phase 3 — Reminder Transition Telemetry
- Emit explicit reminder transition telemetry states:
  - `reminder_triggered`
  - `reminder_injected`
  - `reminder_skipped` (with reason)
  - `reminder_fallback_used`
- Surface counters/last-state in operator diagnostics and gateway-facing state if applicable.

## Phase 4 — Execution Adapter Hardening
- Replace loose path extraction with constrained contract parsing:
  - allowlisted mutation operations only
  - deterministic parse errors with diagnostics
  - strict validation before mutation
- Keep full TypeScript runtime execution as optional future step if required by product scope.

## Phase 5 — Validation and Closure
- Add/extend fixture validations for:
  - policy toggle behavior
  - telemetry transitions
  - adapter guardrails and failure modes
- Build validation:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
- Update docs to mark resolved items and keep only truly unresolved limitations.

## Exit Criteria
- `Known Limitations` contains only unresolved constraints.
- `Follow-Up Enhancements` excludes completed PowerShell parity.
- Telemetry and verbosity controls are implemented, validated, and documented.
