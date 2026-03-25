# Self-Evolving Limitations & Follow-Up Cleanup Plan

## Status Check Summary

Not all `Known Limitations` and `Follow-Up Enhancements` are cleared.

### Cleared
- PowerShell script parity is implemented:
  - `scripts/activator.ps1`
  - `scripts/error-detector.ps1`
  - `scripts/extract-skill.ps1`

### Not Cleared
1. **Known Limitation**
   - Hook execution still uses a constrained adapter that derives mutation paths from handler content, not full TypeScript runtime execution.
2. **Follow-Up Enhancement**
   - Explicit gateway event telemetry for reminder injection transitions is not fully implemented/documented as dedicated events.
3. **Follow-Up Enhancement**
   - Optional policy toggles to control reminder verbosity are not implemented.
4. **Documentation drift**
   - Follow-up list still includes PowerShell variants even though they are completed.

## Remediation Plan

## Phase 1 — Documentation Cleanup (Immediate)
- Remove completed PowerShell enhancement item from `SKILL.md` follow-ups.
- Keep only unresolved enhancements in `Known Limitations` and `Follow-Up Enhancements`.
- Align `references/hooks-setup.md` wording with current actual behavior.

## Phase 2 — Verbosity Policy Toggles
- Add config model fields under hooks engine settings, for example:
  - `hooks.engine.reminderEnabled`
  - `hooks.engine.reminderVerbosity` (`minimal|normal|detailed`)
- Parse these values in `ConfigLoader`.
- Apply toggles in hook execution/reminder injection path.
- Add diagnostics fields to show effective policy values.

## Phase 3 — Gateway Telemetry for Reminder Transitions
- Emit explicit gateway/runtime telemetry for reminder lifecycle transitions:
  - reminder-triggered
  - reminder-injected
  - reminder-skipped (with reason)
  - reminder-fallback-used
- Extend diagnostics and (if available) gateway event payloads for operator visibility.
- Add fixture/service assertions for telemetry presence and correctness.

## Phase 4 — Hook Execution Adapter Hardening
- Replace string-pattern extraction with a stricter handler contract adapter:
  - parse a constrained contract shape
  - validate allowed mutation operations
  - reject unsupported operations deterministically
- Keep full TypeScript runtime execution as optional future phase if required.

## Phase 5 — Validation and Closure
- Run fixture validations covering:
  - policy toggle behavior
  - telemetry transitions
  - adapter guardrails
- Build validation:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
- Update `SKILL.md` and `EVENT_HOOK_ENGINE_PLAN.md` to mark resolved items.

## Exit Criteria
- `Known Limitations` contains only truly unresolved technical constraints.
- `Follow-Up Enhancements` no longer lists completed PowerShell parity work.
- Telemetry and verbosity controls are implemented, tested, and documented.
