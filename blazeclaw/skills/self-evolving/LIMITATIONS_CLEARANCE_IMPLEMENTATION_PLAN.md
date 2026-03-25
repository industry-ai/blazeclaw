# Self-Evolving Limitations & Follow-Up Full Clearance Plan

## Current Status Check
The document is **not fully cleared**.

### Remaining `Known Limitations`
- Hook execution still uses a constrained contract parser for `bootstrapFiles.push(...)` instead of full TypeScript runtime execution.

### Remaining `Follow-Up Enhancements`
- Evaluate optional full TypeScript hook runtime execution as a future expansion.

## Goal
Fully clear the remaining limitation and follow-up by implementing a production-ready TypeScript hook runtime path (for the self-evolving hook scope) and aligning all related documentation.

## Phase 1 — Runtime Design Finalization
- Define the execution boundary between C++ runtime and TypeScript hook runner.
- Choose hook runner strategy (embedded engine or sidecar process) with deterministic timeout/error behavior.
- Define input/output contract for hook execution:
  - Input: normalized hook event (`type`, `action`, `sessionKey`, `context.bootstrapFiles`).
  - Output: validated mutation operations and execution status metadata.

## Phase 2 — TypeScript Hook Runtime Integration
- Add a TypeScript execution adapter service for hook handlers.
- Execute `hooks/blazeclaw/handler.ts` through the runtime adapter instead of parser-based extraction.
- Preserve safety controls:
  - subagent filtering
  - operation allowlist
  - path guardrails
  - timeout and failure isolation

## Phase 3 — Mutation Contract & Guardrails
- Enforce mutation allowlist at runtime (append-only for `bootstrapFiles`).
- Validate all returned mutations before applying to runtime state.
- Add deterministic rejection reasons and diagnostics mapping.

## Phase 4 — Diagnostics & Telemetry Completion
- Ensure diagnostics include:
  - engine mode (`parser` vs `ts-runtime`)
  - hook dispatch/execution counters
  - reminder transition telemetry fields
  - failure reasons and timeout counts
- Keep operator report and gateway-facing state consistent.

## Phase 5 — Tests and Validation
- Add/extend fixtures for:
  - successful TypeScript handler execution
  - invalid handler runtime failures
  - timeout path
  - mutation guardrail rejection
- Run build validation:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`

## Phase 6 — Documentation Closure
- Update `blazeclaw/skills/self-evolving/SKILL.md`:
  - remove remaining `Known Limitations` item once runtime is live
  - remove remaining `Follow-Up Enhancements` item once complete
- Update associated docs:
  - `blazeclaw/skills/self-evolving/references/hooks-setup.md`
  - `blazeclaw/skills/self-evolving/references/openclaw-integration.md`
  - `blazeclaw/skills/self-evolving/EVENT_HOOK_ENGINE_PLAN.md`
  - `blazeclaw/skills/self-evolving/PORTING_PLAN.md` (if lifecycle status section needs final closure)

## Exit Criteria
- No unresolved entries remain under `Known Limitations` and `Follow-Up Enhancements` in `SKILL.md`.
- TypeScript runtime execution is the active hook execution path for self-evolving.
- Build/tests pass and docs are fully aligned with shipped behavior.
