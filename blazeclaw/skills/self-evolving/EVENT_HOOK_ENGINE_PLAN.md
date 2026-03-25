# Dedicated Event-Hook Execution Engine Plan (Self-Evolving)

## Problem
Current behavior injects self-evolving reminder content via **skills prompt lifecycle**.
Target behavior is a **dedicated event-hook execution engine** in BlazeClaw runtime.

## Goal
Implement a native BlazeClaw hook engine that executes self-evolving hook handlers on runtime lifecycle events, without relying on prompt injection as the primary dispatch path.

## Scope
- Runtime: BlazeClaw C++ MFC / Gateway integration
- Skill: `blazeclaw/skills/self-evolving/hooks/blazeclaw/*`
- Events: bootstrap/session lifecycle events required for self-evolving reminder flow

## Non-Goals
- Reintroducing any OpenClaw runtime dependency
- Expanding to all possible hook types in first iteration
- Replacing existing script helpers (`activator.sh`, `error-detector.sh`)

## Architecture Target
1. **Hook Catalog Loader**
   - Discover hook metadata from skill path (`HOOK.md`)
   - Resolve hook handler source (`handler.ts` contract)
   - Validate required fields and event subscriptions

2. **Hook Event Dispatcher**
   - Emit normalized runtime events from BlazeClaw lifecycle transitions
   - Route events to subscribed hooks
   - Enforce isolation, error capture, and timeout boundaries

3. **Hook Execution Adapter**
   - Execute handler contract against normalized event object
   - Support context mutation (`bootstrapFiles` append) in controlled way
   - Preserve safety guards (invalid shape rejection, subagent skip)

4. **Observability & Diagnostics**
   - Report hook registration/execution status in diagnostics
   - Add counters for dispatches, successes, failures, skips, timeouts

## Implementation Plan

### Phase A — Contract and Loader
- Define BlazeClaw-native hook metadata schema for `HOOK.md`
- Add loader service in core runtime to parse and validate hooks
- Add fixture coverage for valid/invalid hook metadata and path safety

### Phase B — Event Emission
- Introduce normalized lifecycle events (starting with `agent.bootstrap`)
- Wire event emission from runtime/session bootstrap path
- Add schema validation for emitted events

### Phase C — Execution Engine
- Add hook dispatcher + execution pipeline with:
  - event filtering
  - deterministic execution order
  - timeout + exception handling
- Add context mutation guardrails for `bootstrapFiles`

### Phase D — Self-Evolving Integration
- Register `self-evolving` hook in engine
- Move reminder flow trigger from prompt-only path to event-hook path
- Keep prompt injection as temporary fallback behind feature flag

### Phase E — Rollout Hardening
- Add diagnostics fields:
  - `hookEngineEnabled`
  - `hooksLoaded`
  - `hookDispatchCount`
  - `hookFailureCount`
  - `selfEvolvingHookTriggered`
- Add toggle/rollout config:
  - `hooks.engine.enabled`
  - `hooks.engine.fallbackPromptInjection`

## Validation Plan

## 1) Build Validation
- Run:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`

## 2) Fixture/Service Validation
- Add/extend fixture scenarios for:
  - hook metadata parsing
  - lifecycle event dispatch
  - self-evolving reminder hook execution
  - failure/timeout handling

## 3) Runtime Verification
- Confirm reminder injection occurs after hook dispatch on bootstrap event
- Confirm diagnostics show hook execution metrics
- Confirm fallback behavior when hook engine disabled

## 4) Regression Checks
- Ensure existing skill prompt generation still works
- Ensure no OpenClaw runtime imports/files are required

## Risks and Mitigations
- **Risk:** Handler runtime integration complexity (TS contract execution from C++ path)
  - **Mitigation:** Introduce a narrow adapter boundary and start with constrained contract.
- **Risk:** Event ordering issues during bootstrap
  - **Mitigation:** Define explicit lifecycle sequence and deterministic dispatch order.
- **Risk:** Silent hook failures
  - **Mitigation:** Add mandatory diagnostics counters and warning surfaces.

## Deliverables
- Hook loader + dispatcher runtime components
- Lifecycle event emission for bootstrap
- Self-evolving hook execution path wired to event engine
- Config flags for enablement/fallback
- Fixture/test updates and passing solution build
- Updated docs (`SKILL.md`, `references/hooks-setup.md`, `PORTING_PLAN.md`) reflecting dedicated event-hook execution engine

## Definition of Done
- Self-evolving reminder is triggered through dedicated event-hook execution engine.
- Prompt lifecycle injection is no longer the primary mechanism.
- Build/tests pass and diagnostics prove hook engine activity.
- BlazeClaw remains fully independent from OpenClaw runtime.

## Phase A Execution Results (Completed)

### Contract and loader implemented
- Added `HookCatalogService` with:
  - `HOOK.md` frontmatter parsing contract
  - required fields validation (`name`, `description`, `blazeclaw.event`, `blazeclaw.handler`)
  - handler path safety checks and file existence checks
- Added runtime diagnostics counters:
  - `hooksLoaded`
  - `invalidMetadataFiles`
  - `unsafeHandlerPaths`
  - `missingHandlerFiles`

### Runtime integration applied
- Hook catalog snapshot is now refreshed from current skills catalog in `ServiceManager` lifecycle.
- Hook fixture validation is now executed during startup fixture validation pass.
- Operator diagnostics report now includes a `hooks` block with loader metrics.

### Fixture coverage added
- Added `fixtures/skills-catalog/s8-hooks/workspace/skills/*` scenarios:
  - `hook-valid`
  - `hook-invalid-metadata`
  - `hook-unsafe-handler`
- Added `HookCatalogService::ValidateFixtureScenarios()` assertions for valid/invalid/unsafe paths.

### Build validation
- Ran:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
- Result: success, 0 errors, 0 warnings.

## Phase B Execution Results (Completed)

### Normalized lifecycle event model added
- Added `HookEventService` with normalized event schema:
  - `type`
  - `action`
  - `sessionKey`
  - `bootstrapFiles[]`
- Added schema validation for emitted events with explicit checks for:
  - `type == agent`
  - `action == bootstrap`
  - non-empty `sessionKey`
  - non-empty `bootstrapFiles[*].path`

### Runtime event emission wired
- Integrated `HookEventService` into `ServiceManager` lifecycle.
- Emitted `agent.bootstrap` normalized event from startup lifecycle path.
- Captured event snapshot in runtime state for diagnostics.

### Fixture and validation coverage added
- Added `HookEventService::ValidateFixtureScenarios()` assertions for:
  - successful emission of valid bootstrap event
  - rejection of invalid bootstrap event
  - emitted/failed counter expectations
- Hook-event fixture validation is now executed during startup fixture validation pass.

### Observability updates
- Extended diagnostics report `hooks` block with event metrics:
  - `eventsEmitted`
  - `eventValidationFailed`
  - `eventsDropped`
- Added feature registry entry:
  - `hooks-event-emission` = implemented

### Build validation
- Ran:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
- Result: success, 0 errors, 0 warnings.

## Phase C Execution Results (Completed)

### Hook execution engine implemented
- Added `HookExecutionService` with:
  - event filtering (`agent.bootstrap`)
  - deterministic dispatch ordering (skill/hook name sort)
  - per-hook timeout accounting
  - exception handling and warning capture
- Added context mutation guardrails for `bootstrapFiles`:
  - safe relative path enforcement
  - rejection of traversal/absolute/drive-style paths
  - bounded bootstrap file count

### Runtime integration applied
- Integrated execution engine into startup lifecycle after normalized
  `agent.bootstrap` event emission.
- Persisted execution snapshot in runtime state for operator visibility.
- Added fixture validation call for hook execution scenarios in startup pass.

### Fixture coverage added
- Added `fixtures/skills-catalog/s9-hooks-exec/workspace/skills/*` scenarios:
  - `self-evolving` (dispatch success + reminder mutation)
  - `hook-unsafe-mutation` (guardrail rejection path)
- Added `HookExecutionService::ValidateFixtureScenarios()` assertions for:
  - successful dispatch for normalized bootstrap event
  - reminder file mutation presence
  - guard rejection count > 0 for unsafe mutation attempts

### Observability updates
- Extended diagnostics `hooks` block with execution metrics:
  - `dispatches`
  - `dispatchSuccess`
  - `dispatchFailures`
  - `dispatchSkipped`
  - `dispatchTimeouts`
  - `guardRejected`
- Added feature registry entry:
  - `hooks-execution-engine` = implemented

### Build validation
- Ran:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
- Result: success, 0 errors, 0 warnings.

## Phase D Execution Results (Completed)

### Self-evolving hook path promoted to primary
- Runtime now treats event-hook execution as the primary trigger for
  self-evolving reminder flow.
- Reminder prompt content is appended from hook execution outcome when
  `SELF_EVOLVING_REMINDER.md` is produced by dispatch.

### Prompt injection converted to explicit fallback
- Updated `SkillsPromptService::BuildSnapshot(...)` contract with
  `enableSelfEvolvingPromptFallback` flag.
- Default behavior keeps fallback disabled.
- Prompt-side reminder insertion occurs only when fallback flag is enabled.

### Runtime control flags added
- Added environment-driven toggles in `ServiceManager`:
  - `BLAZECLAW_HOOKS_ENGINE_ENABLED` (default: enabled)
  - `BLAZECLAW_HOOKS_FALLBACK_PROMPT_INJECTION` (default: disabled)
- Added runtime state tracking:
  - `m_selfEvolvingHookTriggered`

### Diagnostics and feature signaling updates
- Extended diagnostics `hooks` block with:
  - `hookEngineEnabled`
  - `fallbackPromptInjection`
  - `selfEvolvingHookTriggered`
- Added feature registry entry:
  - `hooks-self-evolving-integration` = implemented

### Validation adjustments
- Extended `SkillsPromptService::ValidateFixtureScenarios()` to verify:
  - reminder absent when fallback disabled
  - reminder present when fallback enabled

### Build validation
- Ran:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
- Result: success, 0 errors, 0 warnings.

## Phase E Execution Results (Completed)

### Rollout config implemented
- Added config model support:
  - `hooks.engine.enabled`
  - `hooks.engine.fallbackPromptInjection`
- Added parsing in `ConfigLoader` and default model values in `ConfigModels`.
- Added documented keys in `src/config/blazeclaw.conf` template comments.

### Runtime toggle behavior hardened
- `ServiceManager` now resolves rollout toggles from config first, with
  environment variables as optional override layer:
  - `BLAZECLAW_HOOKS_ENGINE_ENABLED`
  - `BLAZECLAW_HOOKS_FALLBACK_PROMPT_INJECTION`

### Diagnostics alignment completed
- Diagnostics `hooks` block already exposed:
  - `hookEngineEnabled`
  - `hooksLoaded`
  - `selfEvolvingHookTriggered`
- Added Phase E naming aliases:
  - `hookDispatchCount`
  - `hookFailureCount`

### Build validation
- Ran:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
- Result: success, 0 errors, 0 warnings.

## Final Cleanup Pass (Completed)

### Remaining code gap closed
- Replaced hardcoded skill-name mutation behavior in `HookExecutionService`
  with handler-contract-driven extraction from `handler.ts` content.
- Hook execution now derives proposed bootstrap mutations from handler
  `path` fields before guardrails are applied.

### Runtime behavior alignment confirmed
- `selfEvolvingHookTriggered` remains tied to executed bootstrap mutation
  output (`SELF_EVOLVING_REMINDER.md`).
- Prompt-based reminder path remains fallback-only via
  `hooks.engine.fallbackPromptInjection`.

### Documentation alignment completed
- `SKILL.md` updated to reflect event-hook primary path and revised
  diagnostics verification.
- `references/hooks-setup.md` updated to reflect event-hook lifecycle
  primary behavior and Phase E diagnostics fields.

### Accomplishment status
- Dedicated event-hook engine plan is now materially closed for the
  scoped adapter architecture, with build-validated runtime integration,
  diagnostics, config rollouts, and aligned docs.

## Limitations & Follow-Up Remediation Pass (Completed)

### Phase 1 — Documentation correction completed
- Removed completed PowerShell parity follow-up from `SKILL.md`.
- Updated limitation wording to keep only unresolved adapter constraint.
- Updated `references/hooks-setup.md` with current telemetry/policy guidance.

### Phase 2 — Reminder policy controls completed
- Added config toggles:
  - `hooks.engine.reminderEnabled`
  - `hooks.engine.reminderVerbosity` (`minimal|normal|detailed`)
- Wired parsing in `ConfigLoader` and runtime resolution in `ServiceManager`.
- Exposed effective policy in diagnostics: `reminderEnabled`, `reminderVerbosity`.

### Phase 3 — Reminder transition telemetry completed
- Added reminder transition counters/states in hook execution diagnostics:
  - `reminderTriggered`
  - `reminderInjected`
  - `reminderSkipped`
  - `reminderState`
  - `reminderReason`
- Added fallback transition state support (`reminder_fallback_used`).

### Phase 4 — Adapter hardening completed
- Replaced loose path extraction with constrained parsing of
  `bootstrapFiles.push({...})` handler contract blocks.
- Added deterministic parse error signaling and reasoned skip telemetry.

### Phase 5 — Validation and closure completed
- Updated execution fixtures to exercise constrained contract parser and
  unsafe-path guardrail behavior.
- Build and fixture validation executed successfully.

### Final limitations status after remediation
- Completed:
  - PowerShell helper parity
  - reminder policy controls
  - reminder transition telemetry
  - TypeScript hook runtime support expanded beyond self-evolving scope
    (additional package fixtures validated)
  - full TypeScript runtime execution path implemented for self-evolving
    hook scope (`bun` / `tsx` / `node-ts-node` bridge)
  - governance and rollout policy templates added for tenant-level
    multi-package hook ecosystem management
- Remaining (intended):
  - broader multi-package runtime orchestration and operational hardening.
  - governance automation for policy enforcement and drift detection.
