# OpenClaw vs BlazeClaw Cron Capability Gap Report

Generated: 2026-05-17

## Scope

This report compares OpenClaw cron behavior from:

- `openclaw/src/agents/tools/cron-tool.ts`
- `openclaw/src/gateway/server-methods/cron.ts`
- `openclaw/src/cron/normalize.ts`
- `openclaw/src/gateway/protocol/schema/cron.ts`
- `openclaw/src/cron/service/jobs.ts`
- `openclaw/src/cron/service/store.ts`
- `openclaw/src/cron/service/ops.ts`
- `openclaw/src/cron/service/timer.ts`

Against BlazeClaw counterparts in:

- `blazeclaw/BlazeClawMfc/src/cron/CronNormalize.cpp`
- `blazeclaw/BlazeClawMfc/src/cron/CronStoreService.cpp`
- `blazeclaw/BlazeClawMfc/src/cron/CronOpsService.cpp`
- `blazeclaw/BlazeClawMfc/src/cron/CronTimerService.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolSchemaValidator.Request.cpp`

## Executive Summary

BlazeClaw has method-surface parity for core cron RPC endpoints (`cron.status/list/add/update/remove/run/runs`, `wake`) and has implemented foundational persistence, scheduling, run logs, retry, and delivery.

Main parity gaps are in behavior-level fidelity, not endpoint presence:

1. **Gateway normalization and validation flow parity is incomplete** (OpenClaw does normalize-then-validate per method; BlazeClaw currently relies on stricter front validation and generic handler dispatch).
2. **Schedule semantics diverge**, especially for cron expression handling (`tz`, richer expression support, and stable stagger behavior).
3. **Recovery and maintenance semantics diverge** (OpenClaw maintenance recompute avoids silently advancing due jobs; BlazeClaw recompute loop can advance state more aggressively).
4. **Tool-surface parity is missing** (OpenClaw has an agent-facing cron tool shim with recovery behavior and context-aware shaping).
5. **Store and run-log model parity differs** (OpenClaw uses dedicated run-log reading paths and restart-safe reload behavior; BlazeClaw currently uses in-memory arrays persisted in envelope files).

## Capability Matrix

| Area | OpenClaw | BlazeClaw | Gap Level |
|---|---|---|---|
| RPC method surface | Full cron RPC handlers with typed validation in server-method layer | RPC handlers present and wired | Low |
| Method param normalization before validation | `cron.add`/`cron.update` normalize inputs and patches before validator checks | Strict request validator requires canonical shape earlier | Medium |
| Agent tool surface (`cron` tool) | Rich flattened/object payload recovery and session-aware defaults | No equivalent BlazeClaw cron tool adapter found | High |
| Schedule kinds (`at/every/cron`) | Supported | Supported | Low |
| Cron expression behavior | Delegated to schedule engine; supports `tz`, stagger defaults, richer cron semantics | Cron parser currently minute/hour-focused; `tz` not used in next-run compute | High |
| Stable stagger behavior | Per-job stable offset hash for stagger windows | Adds `candidate % staggerMs`; not equivalent to OpenClaw window alignment | High |
| One-shot re-arm behavior | Defensive `at` handling with legacy compatibility and re-arm logic | `at` supported with simpler last-run logic | Medium |
| Maintenance recompute semantics | `recomputeNextRunsForMaintenance` preserves due slots unless executed | `RecomputeSchedules` runs broadly in sync loop | High |
| Schedule error isolation | Repeated schedule errors auto-disable after threshold + user notification | No equivalent schedule-error count + auto-disable flow observed | High |
| Startup catch-up behavior | Explicit missed-job planning, staggered replay, and recovery controls | Startup catch-up loop present but less policy-rich | Medium |
| Delivery + failure destination | Announce/webhook + failure destination semantics with strict target checks | Announce/webhook + failure destination supported | Low |
| Failure alerts | Threshold/cooldown with config layering and suppression rules | Threshold/cooldown and suppression present | Low |
| Run ledger semantics | Integrates task ledger and run-log shaping in service flow | Manual-run lifecycle entries and terminal mapping present | Low |
| Store reload discipline | Tracks file mtime and force-reload semantics with guarded recompute options | Reload by file write-time change; simpler reload semantics | Medium |

## Highest-Impact Parity Gaps and Suggestions

### 1) Normalize-then-validate gateway flow

**Gap**
- OpenClaw server methods normalize request shape before validating and executing.
- BlazeClaw request validator currently requires canonical add/update shape at request boundary.

**Suggestion**
- Add a cron pre-validation normalization stage in BlazeClaw gateway handling for `cron.add` and `cron.update`.
- Keep request validator strict after normalization to preserve safety.
- Align nullability handling for `agentId` and `sessionKey` with OpenClaw behavior.

### 2) Cron expression, timezone, and stagger parity

**Gap**
- OpenClaw schedule path supports richer cron semantics with timezone and stable stagger offset by job identity.
- BlazeClaw `ComputeNextCron` currently uses a simpler minute/hour parse and does not apply timezone behavior.

**Suggestion**
- Port OpenClaw schedule-compute semantics into BlazeClaw scheduler core.
- Add stable per-job stagger offset logic equivalent to OpenClaw behavior.
- Add parity tests for top-of-hour, daily rollover, timezone offsets, and stagger windows.

### 3) Maintenance recompute and due-slot preservation

**Gap**
- OpenClaw distinguishes maintenance recompute from due-run progression to avoid skipping unexecuted due slots.
- BlazeClaw sync loop recomputes schedule and pumps due runs with fewer safeguards.

**Suggestion**
- Introduce a BlazeClaw maintenance-only recompute mode that does not advance due slots unless execution is confirmed.
- Mirror OpenClaw read-path protections used for status/list/runs read operations.

### 4) Schedule error auto-disable and user visibility

**Gap**
- OpenClaw tracks consecutive schedule compute errors and auto-disables jobs after threshold, with explicit user/system notification.
- Equivalent auto-disable signal path is not present in BlazeClaw scheduler.

**Suggestion**
- Add `scheduleErrorCount` state tracking in BlazeClaw cron state.
- Add thresholded auto-disable policy and emit runtime/system notification when tripped.

### 5) Agent-facing cron tool facade parity

**Gap**
- OpenClaw `cron-tool.ts` provides flattened-object recovery, session-target inference, and context message enrichment for reminder flows.
- No BlazeClaw equivalent façade currently bridges these ergonomics to gateway cron RPC.

**Suggestion**
- Add a BlazeClaw cron tool adapter layer that mirrors OpenClaw shaping behavior and keeps gateway contract parity.
- Include explicit compatibility handling for flattened payload keys and patch recovery logic.

## Step-by-Step Porting Plan

1. **Add cron parity normalization bridge in BlazeClaw gateway runtime**
   - Normalize `cron.add` and `cron.update` payloads before strict schema validation.
2. **Align cron add/update schema nullability and compatibility behavior**
   - Match OpenClaw semantics for nullable `agentId`/`sessionKey` and patch coercion.
3. **Port OpenClaw schedule compute semantics into BlazeClaw timer core**
   - Replace simplified cron parsing path with parity-compatible next-run behavior.
4. **Implement timezone-aware cron execution path**
   - Honor `schedule.tz` consistently in `ComputeNextRunAtMs` and tests.
5. **Implement stable job-id-based stagger offsets**
   - Port deterministic stagger strategy and edge-case fallback behavior.
6. **Add maintenance-only recompute mode in BlazeClaw cron ops/timer**
   - Preserve overdue slots until confirmed execution.
7. **Add schedule error isolation and auto-disable policy**
   - Introduce error counters, threshold disable, and notification emission.
8. **Align startup catch-up policy with OpenClaw safeguards**
   - Include replay limits, staggered replay, and one-shot restart behavior parity.
9. **Add BlazeClaw cron tool adapter parity layer**
   - Support flattened input recovery, session-aware defaults, and gateway call shaping.
10. **Expand parity test suite and regression fixtures**
	- Add tests for timezone, stagger, maintenance recompute, auto-disable, and tool-surface coercion.
11. **Run full BlazeClaw build/test validation and parity docs refresh**
	- Validate with project-standard build command and update parity docs after implementation.

## Suggested Validation Targets

- `blazeclaw/BlazeClawMfc/tests/CronParityContractTests.cpp`
- New focused tests for:
  - cron timezone and daily rollover
  - stable stagger offset repeatability
  - maintenance recompute no-skip guarantee
  - schedule error auto-disable + notification
  - tool adapter input coercion parity

## Documentation Follow-up

- Keep this report as the cron behavior parity source of truth.
- Keep method/event parity docs focused on surface coverage and link to this report for behavior parity status.

## Implementation Progress Update (One-shot execution)

Completed in this execution:

- Gateway cron request compatibility updates:
  - nullable `agentId` / `sessionKey` accepted in `cron.add`
  - flattened payload fields accepted in `cron.add`
  - `cron.add` no longer hard-requires `name`/`payload` when normalization can derive payload
- Added `CronNormalize::NormalizePatchInput` and routed `ApplyPatch` through normalized patch input.
- Added flattened payload compatibility normalization in cron normalization:
  - top-level `message` / `text` / `model` / `fallbacks` / `toolsAllow` / `thinking` /
    `timeoutSeconds` / `lightContext` / `allowUnsafeExternalContent` merge into `payload`
  - flattened compatibility fields stripped after normalization.
- Added maintenance recompute options (`CronRecomputeOptions`) in scheduler API and wired ops paths:
  - maintenance refresh preserves due slots
  - execution sync recompute does not preserve due slots.
- Upgraded cron compute behavior:
  - deterministic per-job stagger offset from job id
  - timezone offset support (`UTC/GMT`, signed offsets like `+08`, `+08:00`, `-0500`)
  - cron minute/hour token matcher supports `*`, comma lists, and `*/N`.
- Added schedule recompute error isolation:
  - `scheduleErrorCount` tracking
  - `lastError` update
  - auto-disable after repeated schedule compute failures.
- Expanded parity tests in `blazeclaw/BlazeClawMfc/tests/CronParityContractTests.cpp` for:
  - nullable/flattened add validation
  - flattened add normalization
  - deterministic stagger repeatability
  - maintenance recompute due-slot preservation.
- Validated with required build command:
  - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`

Remaining parity work:

- Full OpenClaw cron-expression semantic parity (beyond current minute/hour focus).
- Full IANA timezone/DST parity.
- Agent-facing cron tool façade parity (`cron-tool` equivalent).
- Deeper startup catch-up policy alignment with OpenClaw safeguards.