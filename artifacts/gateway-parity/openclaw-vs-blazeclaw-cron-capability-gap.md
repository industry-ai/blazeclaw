# OpenClaw vs BlazeClaw Cron Capability Gap Report

Generated: 2026-05-18 (reconciled against current BlazeClaw cron sources; includes latest Step 4 manual queued-edge immediate terminal-hook increment)

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

Authoritative parity plan: `blazeclaw/docs/cron-parity-gap-and-port-plan.md`

## Executive Summary

BlazeClaw has method-surface parity for core cron RPC endpoints (`cron.status/list/add/update/remove/run/runs`, `wake`) and has implemented foundational persistence, scheduling, run logs, retry, delivery metadata, and extensive validator/test coverage.

Main parity gaps are in **execution fidelity** and **cross-layer depth**, not endpoint presence:

1. **Runtime execution fidelity** — production startup now wires runtime adapters via `GatewayHost::WireCronProductionIntegration()`, and heartbeat busy-lane runtime outcomes now suppress delivery/failure-destination transport simulation attempts; non-adapter lanes still retain simulation fallback and OpenClaw execution-core depth remains higher.
2. **Transport execution** — announce paths and default webhook policy still lag OpenClaw; opt-in WinHTTP dispatch exists for webhooks.
3. **Shared task-ledger transition depth** — production hook wiring is active, create-running payload phasing emits active/running non-terminal semantics, hook payload timing metadata projection (`queuedAtMs`/`startedAtMs`/`endedAtMs`) is landed, and immediate manual terminal edges (`already_running`/`not_due`) now emit terminal hooks in `cron.run`; broader retry/cooldown/consumer depth remains open.
4. **Gateway normalization flow** — `cron.add`/`cron.update` normalize inside `CronOpsService` after gateway request validation, and `GatewayHost::HandleInboundText` now applies pre-validator canonicalization for legacy/flat payloads across `cron.add`/`cron.update`/`cron.run`/`cron.runs`/`wake`; latest increment deepens `cron.update` flat schedule/payload/delivery alias lifting into nested patch structure. Remaining gap is broader tool-surface action depth, not absence of gateway normalization bridge.
5. **Tool-surface parity** — WebView shaping (`agents-controller.js`) covers much of `cron-tool.ts` ergonomics; dedicated agent cron tool adapter and CLI/controller/bootstrap parity remain open.

## Capability Matrix (reconciled 2026-05-18)

| Area | OpenClaw | BlazeClaw | Gap Level |
|---|---|---|---|
| RPC method surface | Full cron RPC handlers with typed validation in server-method layer | RPC handlers present and wired via `CronOpsService` | Low |
| Production runtime adapter wiring | Heartbeat/isolated runtime invoked by default | `GatewayHost::WireCronProductionIntegration()` registers adapters/hooks before scheduler startup; simulation fallback remains for non-adapter lanes | Medium |
| Method param normalization before validation | Server methods normalize before validator checks | Ops-layer normalize after gateway validator plus gateway pre-validator canonicalization in `GatewayHost` for legacy/flat cron payloads | Medium-Low |
| Agent tool surface (`cron` tool) | Rich flattened/object payload recovery and session-aware defaults | WebView parity helpers; no dedicated agent cron tool adapter | Medium |
| Schedule kinds (`at/every/cron`) | Supported | Supported | Low |
| Cron expression behavior | Richer cron semantics (Croner-grade syntax, IANA-style `tz`) | 5-field matcher (minute/hour/day-of-month/month/day-of-week) with `tz` offset parsing; broader syntax/timezone semantics still lighter | Medium |
| Stable stagger behavior | Per-job stable offset hash for stagger windows | `ResolveStableCronOffsetMs` (job-id hash) landed | Low-Medium |
| One-shot re-arm behavior | Defensive `at` handling with legacy compatibility | `at` supported with last-run guard semantics | Medium |
| Maintenance recompute semantics | Maintenance recompute preserves due slots | `preserveDueSlots` wired for read refresh vs execution sync | Low-Medium |
| Schedule error isolation | Auto-disable + user notification | `scheduleErrorCount` auto-disable after 3 errors; notification path lighter | Medium |
| Startup catch-up behavior | Explicit missed-job planning and recovery controls | Bounded startup catch-up loop present | Medium |
| Delivery + failure destination | Full transport execution | Metadata + suppression + opt-in webhook WinHTTP dispatch | Medium |
| Failure alerts | Full policy behavior | Threshold/cooldown/suppression landed; recurring carry-forward depth open | Medium |
| Run ledger / task-ledger hooks | Integrated shared ledger transitions | Hook contracts + emission in ops with production wiring and timing metadata projection; deeper retry/cooldown/consumer depth remains | Medium |
| Store reload discipline | mtime-aware reload + recovery policy | mtime reload, force-reload, `.bak` fallback landed | Low-Medium |

## Highest-Impact Parity Gaps and Suggestions

### 0) Production wiring baseline (completed)

**Status**

- `GatewayHost::WireCronProductionIntegration()` registers runtime adapters and task-ledger hooks before scheduler start and clears them on host stop.

**Next depth**

- Add explicit production integration smoke coverage and extend cross-layer parity assertions for retry/cooldown/phasing behavior.
- Continue extending cross-layer assertions for hook payload consistency across queued/retry/cooldown transitions.

### 1) Normalize-then-validate gateway flow

**Gap**

- OpenClaw server methods normalize before validating; BlazeClaw now combines gateway pre-validator canonicalization for legacy/flat cron payloads with ops-layer normalization for canonical add/update shapes.

**Suggestion**

- Keep expanding canonicalization coverage only where additional legacy aliases are discovered; preserve strict validator behavior.

### 2) Cron expression and notification parity

**Gap**

- BlazeClaw cron matcher now includes 5-field depth (minute/hour/day-of-month/month/day-of-week), but broader OpenClaw cron syntax/timezone semantics and schedule-error user notification still lag.

**Suggestion**

- Port richer cron field matching and user-visible auto-disable signaling.

### 3) Runtime and transport execution fidelity

**Gap**

- Production execution remains simulation-first; announce transport and default webhook policy lag OpenClaw.

**Suggestion**

- Continue runtime/transport depth closure per `cron-parity-gap-and-port-plan.md` Steps 2–4.

## Step-by-Step Porting Plan (summary)

See `blazeclaw/docs/cron-parity-gap-and-port-plan.md` §7 for the full ordered plan. Current sequence:

0. Freeze parity matrix (completed)
1. Runtime execution fidelity
2. Transport execution fidelity
3. Task-ledger cross-layer depth
4. Normalization/add-contract depth
5. Failure-alert policy depth
6. Store migration/recovery depth
7. Tool-facing + broader surface parity
8. Contract strictness depth
9. Integration + negative-path coverage
10. Docs + gate freeze

## Validation

`msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`

Parity tests: `blazeclaw/BlazeClawMfc/tests/CronParityContractTests.cpp`

## Changelog

### 2026-05-18

- Reconciled capability matrix with current `CronTimerService` / `CronOpsService` / `CronStoreService` behavior.
- Reconciled production wiring status: adapters/hooks are registered from `GatewayHost` startup, with remaining work moved to execution-depth parity.
- Updated schedule/maintenance rows (`tz`, stagger, `preserveDueSlots`, `scheduleErrorCount` auto-disable).
- Pointed authoritative plan to `blazeclaw/docs/cron-parity-gap-and-port-plan.md`.
- Added Phase G Step 4 note: `CronOpsService::Run` now emits terminal task-ledger hooks for immediate manual `already_running` and `not_due` transitions, with targeted ops parity coverage for `not_due` terminal-edge completion-hook emission.

### 2026-05-17

- Initial artifact generated from eight-file OpenClaw baseline comparison.
