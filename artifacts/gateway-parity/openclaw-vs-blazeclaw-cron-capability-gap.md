# OpenClaw vs BlazeClaw Cron Capability Gap Report

Generated: 2026-05-19 (Phase BC WP-A: production runtime gating landed; closure sequence WP-A baseline done → WP-B next; see `blazeclaw/docs/cron-parity-gap-and-port-plan.md` §7.1)

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

1. **Runtime execution fidelity** — production startup now wires runtime adapters via `GatewayHost::WireCronProductionIntegration()`, heartbeat busy-lane runtime outcomes now suppress delivery/failure-destination transport simulation attempts, explicit adapter `handled=false` outcomes preserve simulation-fallback intent instead of being overridden by implicit handled inference, latest transport handling applies independent primary/failure lane simulation bypass for partial runtime-projected transport outcomes, runtime transport projection now infers explicit status defaults (`deliveryStatus` / `failureDestinationStatus`) when adapters omit status fields, webhook failure-destination suppression resolves primary route fallback from `delivery.url` alias when runtime-projected primary transport omits explicit `deliveryTarget`, announce suppression account-equivalence prefers runtime-projected primary `deliveryAccountId` with config fallback, announce suppression target-equivalence now resolves primary-target fallback from runtime/session context when explicit announce target is omitted, webhook same-target suppression infers primary mode as webhook when runtime projects an HTTP/HTTPS primary target without explicit `deliveryMode`, failure-destination routing/suppression infers webhook mode from HTTP/HTTPS `failureDestination.url` when explicit `failureDestination.mode` is omitted, normalization persists these url-alias inferences as both `delivery.mode=webhook` and `failureDestination.mode=webhook`, runtime projection canonicalization infers omitted runtime `deliveryMode` / `failureDestinationMode` as `webhook` from HTTP/HTTPS projected targets, patch-path normalization infers omitted `delivery.mode` / nested `failureDestination.mode` as `webhook` from HTTP/HTTPS url aliases, Step 4 task-ledger terminal hook payload projection carries both primary and failure-destination transport observability metadata (including attempt/http/error and route fields) in manual linkage lanes, Step 5 schedule computation now treats invalid non-empty timezone offsets as explicit schedule errors for deterministic `scheduleErrorCount` auto-disable behavior with persisted `scheduleAutoDisabled*` plus `scheduleAutoDisableNotification*`/`scheduleAutoDisableHeartbeatWake*` signaling metadata, Step 6 recurring policy now suppresses immediate failure-alert triggering in retry-scheduled error lanes (`failureAlertSuppressedReason=retry_pending`), and Step 6 recovery lanes now clear stale failure-alert route snapshots on non-error outcomes so recurring carry-forward state does not leak stale mode/target/channel/account metadata; non-adapter lanes still retain simulation fallback and OpenClaw execution-core depth remains higher.
2. **Transport execution** — announce paths and default webhook policy still lag OpenClaw; opt-in WinHTTP dispatch exists for webhooks.
3. **Shared task-ledger transition depth** — production hook wiring is active, create-running payload phasing emits active/running non-terminal semantics, hook payload timing metadata projection (`queuedAtMs`/`startedAtMs`/`endedAtMs`) is landed, immediate manual terminal edges (`already_running`/`not_due`) emit terminal hooks in `cron.run`, and latest Step 4 integration coverage now validates carry-forward of runtime-projected inferred transport statuses (`deliveryStatus=unknown` / `failureDestinationStatus=unknown`) through terminal hook payloads; broader retry/cooldown/consumer depth remains open.
4. **Gateway normalization flow** — `cron.add`/`cron.update` normalize inside `CronOpsService` after gateway request validation, and `GatewayHost::HandleInboundText` now applies pre-validator canonicalization for legacy/flat payloads across `cron.add`/`cron.update`/`cron.run`/`cron.runs`/`wake`; latest increments deepen `cron.update` flat schedule/payload/delivery alias lifting into nested patch structure, canonicalize flat `failureAlert*` aliases into nested `failureAlert` objects for add/update patch paths, canonicalize legacy wake-mode alias values to strict `next-heartbeat`, canonicalize trimmed/case-variant strict wake `mode` values to canonical taxonomy before validation, and canonicalize singular `cron.runs` CSV aliases (`status`, `deliveryStatus`) into plural array filters for multi-value forms. Remaining gap is broader tool-surface action depth, not absence of gateway normalization bridge.
5. **Tool-surface parity** — WebView shaping (`agents-controller.js`) covers much of `cron-tool.ts` ergonomics; dedicated agent cron tool adapter and CLI/controller/bootstrap parity remain open.

## Closure sequence (Phase BC, 2026-05-19)

**WP-A baseline landed:** `preferRuntimeExecution`, wake-now busy-wait, `runtime_unavailable` gating.

**Next:** **WP-B** outbound delivery → **WP-C** → **WP-D** → **WP-E** → **Step 4** → **WP-F**. See `blazeclaw/docs/cron-parity-gap-and-port-plan.md` §7.1.

Parity tests in source: **205+** `TEST_CASE`s in `CronParityContractTests.cpp` (full `[cron]` rerun required for sign-off).

## Capability Matrix (reconciled 2026-05-19)

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
| Schedule error isolation | Auto-disable + user notification | `scheduleErrorCount` auto-disable after 3 errors with persisted `scheduleAutoDisabled*` plus notification/wake signaling metadata; runtime delivery path still lighter | Medium |
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

- BlazeClaw cron matcher now includes 5-field depth (minute/hour/day-of-month/month/day-of-week), and schedule auto-disable threshold paths now persist `scheduleAutoDisabled*` plus `scheduleAutoDisableNotification*`/`scheduleAutoDisableHeartbeatWake*` signaling metadata; broader OpenClaw cron syntax/timezone semantics and runtime auto-disable user-notification delivery workflow still lag.

**Suggestion**

- Port richer cron field matching and OpenClaw-equivalent auto-disable user-notification workflow (system-event enqueue + heartbeat wake request equivalents).

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

### 2026-05-19

- Added Phase BA Step 8-11 note: gateway pre-validator normalization now
  canonicalizes flat webhook URL aliases into nested target fields for
  add/update patch payloads:
  - `deliveryUrl` -> `delivery.to`,
  - `failureDestinationUrl` -> `delivery.failureDestination.to`,
  - `failureAlertUrl` -> `failureAlert.to`.
- Added gateway normalization parity coverage notes:
  - `cron.add canonicalizes flat webhook url aliases to nested to fields`,
  - `cron.update patch canonicalizes flat webhook url aliases to nested to fields`.
- Validation evidence updated for this tranche:
  - `BlazeClawMfc.Tests.exe "[cron][gateway][normalize]"` passed
	(95 assertions / 2 test cases),
  - required build gate passed via
	`msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`.

- Added Phase AZ Step 7-11 note: `CronStoreService::ParseArrayPayload` now
  accepts kind-matched legacy envelope array keys
  (`kind=jobs` -> `jobs`, `kind=runs` -> `runs`) when
  `values`/`items`/`data` aliases are absent.
- Added store parity coverage note:
  `Cron store loads envelope with legacy kind-matched array keys`.
- Validation evidence updated for this tranche:
  - `BlazeClawMfc.Tests.exe "[cron][store]"` passed
	(21 assertions / 5 test cases),
  - `BlazeClawMfc.Tests.exe "[cron][parity]"` passed
	(91 assertions / 2 test cases),
  - required build gate passed via
	`msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`.

- Added Phase AY Step 6-11 note: `CronTimerService` now suppresses
  failure-alert triggering for retry-scheduled error lanes with explicit
  `failureAlertSuppressedReason=retry_pending`, preventing premature alerts
  while retries are active.
- Added timer parity coverage note:
  `Cron timer suppresses failure alert while retry is pending`.
- Validation evidence updated for this tranche:
  - `BlazeClawMfc.Tests.exe "[cron][timer]"` passed
	(593 assertions / 82 test cases),
  - `BlazeClawMfc.Tests.exe "[cron][ops]"` passed
	(170 assertions / 16 test cases),
  - `BlazeClawMfc.Tests.exe "[cron][normalize]"` passed
	(156 assertions / 20 test cases),
  - required build gate passed via
	`msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`.

- Added Phase AX Step 0-4 note: `CronOpsService` terminal hook mapping now
  preserves recognized manual queued-edge `taskLedgerDisposition` values
  (`unknown_job`, `already_running`, `not_due`, `missing_terminal_run`) in
  hook payload projection instead of flattening to generic mapped
  dispositions.
- Added Phase AX Step 0-4 taxonomy-alignment note:
  `GatewayProtocolSchemaValidator.Response` now accepts
  `taskLedgerDisposition=unknown_job` in cron run-entry response taxonomy.
- Added Phase AX Step 0-4 parity coverage note:
  - updated ops integration assertion for
	`Cron ops emits task-ledger completion hook for manual unknown-job terminal edge`
	to require `disposition=unknown_job` and
	`taskLedgerDisposition=unknown_job`,
  - expanded response taxonomy positive-path coverage to include
	`unknown_job` in
	`Cron runs response validator accepts expanded taskLedgerDisposition taxonomy values`.
- Validation evidence updated for this tranche:
  - `BlazeClawMfc.Tests.exe "[cron][response][schema]"` passed
	(74 assertions / 30 test cases),
  - `BlazeClawMfc.Tests.exe "[cron][ops]"` passed
	(170 assertions / 16 test cases),
  - required build gate passed via
	`msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`.

- Added Phase AW Step 9-10 note: response validator strictness now rejects
  `cron.run` queued-reason terminal-state drift
  (`reason=queued` with `runState=terminal`) and enforces
  `cron.runs` action-to-task-ledger phase alignment when
  `taskLedgerPhase` is present (`queued->queued`, `started->active`,
  `finished->terminal`).
- Added schema response negative-path parity coverage notes for:
  - `Cron run response validator rejects queued reason with terminal runState`,
  - `Cron runs response validator rejects inconsistent action to task-ledger phase semantics`.
- Validation evidence updated for this tranche:
  - `BlazeClawMfc.Tests.exe "[cron][response][schema]"` passed
	(66 assertions / 28 test cases),
  - `BlazeClawMfc.Tests.exe "[cron][ops]"` passed
	(169 assertions / 16 test cases),
  - required build gate passed via
	`msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`.

### 2026-05-18

- Reconciled capability matrix with current `CronTimerService` / `CronOpsService` / `CronStoreService` behavior.
- Added Phase K docs-sync note: remaining-gap priority sequence and tranche wording aligned with `blazeclaw/docs/cron-parity-gap-and-port-plan.md` and companion docs.
- Reconciled production wiring status: adapters/hooks are registered from `GatewayHost` startup, with remaining work moved to execution-depth parity.
- Updated schedule/maintenance rows (`tz`, stagger, `preserveDueSlots`, `scheduleErrorCount` auto-disable), and added Step 5 matcher breadth notes for range/stepped-range token support, named month/weekday alias support (including alias ranges), and question-mark wildcard semantics in day-field lanes.
- Pointed authoritative plan to `blazeclaw/docs/cron-parity-gap-and-port-plan.md`.
- Added Phase G Step 4 note: `CronOpsService::Run` now emits terminal task-ledger hooks for immediate manual `already_running` and `not_due` transitions, with targeted ops parity coverage for `not_due` terminal-edge completion-hook emission.
- Added Phase H Step 6 note: failure-alert recurring error-branch evaluation now clears stale route snapshot state (`lastFailureAlertTarget` plus mode/channel/account snapshots) in disabled suppression lanes, with targeted timer parity coverage.
- Added Phase I Step 8/9 note: gateway pre-validator normalization now canonicalizes flat `failureAlert*` aliases into nested `failureAlert` fields for `cron.add` and `cron.update.patch`, with targeted gateway normalization regression coverage.
- Updated Phase J Step 11 note: gate-freeze revalidation command evidence remains valid; latest full `[cron]` suite reruns are now green and sign-off remains open only for remaining parity-depth items.
- Added Phase M Step 6 note: failure-alert webhook target fallback now reuses runtime-resolved delivery target and `delivery.url` alias routes when `failureAlert.to` / `delivery.to` are omitted.
- Added Phase N Step 6/10 note: cross-layer ops integration now validates Step 6 fallback-route carry-forward through `CronOpsService::Wake` terminal hook payload assertions (`failureAlertMode`/`failureAlertTarget`/`failureAlertAtMs`) with green `[cron]` suite evidence (169 tests / 949 assertions).
- Added Phase O Step 2/4 note: runtime adapter `aborted=true` outcomes now carry through timer state/run projections (`lastRunAborted`, run-log `aborted`) and ops aborted terminal-hook coverage now asserts `aborted=true` payload projection.
- Added Phase P Step 6 note: webhook-mode failure-alert cooldown route-change gating now ignores announce-only channel/account snapshot drift and clears stale webhook account snapshots, with dedicated timer parity coverage and green `[cron]` suite evidence (171 tests / 964 assertions).
- Added Phase AD Step 3/10 note: webhook same-target suppression now infers primary mode as webhook when runtime projects HTTP/HTTPS primary target without explicit `deliveryMode`, with targeted timer parity coverage and validation gate pass.
- Added Phase AR Step 5/10 note: schedule-error threshold auto-disable paths now persist explicit signaling metadata (`scheduleAutoDisabled`, `scheduleAutoDisabledAtMs`, `scheduleAutoDisabledReason`) and clear stale signaling metadata on successful recompute, with targeted timer parity coverage and validation gate pass.

### 2026-05-17

- Initial artifact generated from eight-file OpenClaw baseline comparison.
