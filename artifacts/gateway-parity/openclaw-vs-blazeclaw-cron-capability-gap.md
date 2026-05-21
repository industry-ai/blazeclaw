# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-05-21 (Phase CW Step 4 / P4+ hook-consumer depth uplift + docs sync)

Authoritative detail: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` (§7.1 work packages, §7.3–§7.5 uplift governance)

## Summary

BlazeClaw exposes the full cron/wake RPC surface at the gateway level and
maintains strong baseline parity through P0–P9. OpenClaw `jobs.ts` schedule/state
core parity remains strong and §7.5 high-uplift closure is complete for all
tracked Section 2/3 rows. Phase CO/CP/CQ completed CronTimerService
(`jobs.ts` + `timer.ts`), CronOpsService runtime-surface (`server-methods/cron.ts` + `ops.ts`),
and CronStoreService (`store.ts`) row uplift to `High`. Remaining work is P10+ breadth:
isolated-runtime, long-tail capability breadth, tool-surface depth, and deferred CLI.

## §7.5 post-high closure targets (active)

Section 2/3 high-uplift rows are closed; active targets are P10+ depth themes:

- runtime isolated-agent execution breadth,
- outbound/channel delivery execution depth,
- timezone and advanced cron syntax breadth,
- task-ledger hook-consumer integration depth,
- tool/controller deferred surface depth.

Mandatory gate order for promotion tranches:

1. `[cron][timer]`
2. `[cron][ops]`
3. `[cron][store]`
4. `[cron][schema][response]`
5. `[cron][gateway][wp-f]`
6. `[cron]`
7. `msbuild` (`BlazeClaw.sln` Debug x64, CodePage 65001)

## Priority status (§5)

| Priority | Status |
| --- | --- |
| P0 | **Complete** (WP-A runtime execution core) |
| P1 | **Complete** (WP-B outbound delivery + failure alert) |
| P2 | **Complete** (WP-C auto-disable workflow + schedule syntax breadth) |
| P3 | **Complete** (WP-D scheduler hardening baseline) |
| P4 | **Complete** (High uplift: Step 4 task-ledger hook-consumer source-linkage depth) |
| P5 | **Complete** (High uplift: IANA Etc aliases + six-field + shorthand cron + military timezone aliases) |
| P6 | **Complete** (Store JSON5 + per-job jsonl + migration baseline) |
| P7 | **Complete** (Residual schema strictness baseline) |
| P8 | **Complete** (Production GatewayHost E2E baseline) |
| P9 | **Complete** (Tool surface baseline) |
| P10+ | **Active** (runtime/hook-consumer/tool breadth; CLI deferred) |

## jobs.ts row status (Section 2)

| Baseline | Counterpart | Status |
| --- | --- | --- |
| `src/cron/service/jobs.ts` | `CronTimerService.cpp` + `CronOpsService.cpp` | **High** (Phase CO) |

Evidence: `JOBS-1..JOBS-5` pass in acceptance matrix; Phase CO timer/jobs uplift
gates green (`[cron][timer][wp-a]` 35/6, `[cron][timer][step5]` 39/7,
`[cron][gateway][wp-f]` 47/4, `[cron]` 1528/240, `msbuild` pass).

## runtime-surface + ops rows status (Section 2)

| Baseline | Counterpart | Status |
| --- | --- | --- |
| `src/gateway/server-methods/cron.ts` | `CronOpsService.cpp` + `GatewayHost.Handlers.Runtime.Surface.cpp` | **High** (Phase CP) |
| `src/cron/service/ops.ts` | `CronOpsService.cpp` | **High** (Phase CP) |

Evidence: Phase CP ops/runtime-surface uplift gates green (`[cron][ops]` 210/18,
`[cron][schema][response]` 92/33, `[cron][gateway][wp-f]` 47/4,
`[cron]` 1543/240 sustained x2, `msbuild` pass).

## store row status (Section 2)

| Baseline | Counterpart | Status |
| --- | --- | --- |
| `src/cron/service/store.ts` | `CronStoreService.cpp` | **High** (Phase CQ) |

Evidence: Phase CQ store uplift gates green (`[cron][store]` 44/12,
`[cron][schema][response]` 92/33, `[cron][gateway][wp-f]` 47/4,
`[cron]` 1559/242, `msbuild` pass).

## Section 3 capability status

| Capability | Status | Evidence lock |
| --- | --- | --- |
| `Persistence` | **High** (Phase CR) | `UPLIFT-6` pass (`UPLIFT-3` + `[cron][store]` + full `[cron]`) |
| `Scheduling` | **High** (Phase CR reaffirmed) | `UPLIFT-7` pass (`UPLIFT-5` + `[cron][timer]` + `[cron][gateway][wp-f]` + full `[cron]`) |

Phase CR gate refresh: `[cron][store]` 60/14, `[cron][timer]` 718/103,
`[cron][schema][response]` 92/33, `[cron][gateway][wp-f]` 47/4,
`[cron]` 1559/242, `msbuild` pass.

## Work package status (2026-05-21)

| Package | Focus | Status |
| --- | --- | --- |
| WP-A | Production runtime execution default | **P0 complete** (Phase CT hardening applied) |
| WP-B | Outbound announce/webhook/failure-alert dispatch | **P1 complete** (Phase CU metadata projection depth landed) |
| WP-C | Auto-disable + schedule syntax breadth | **P2 complete** |
| WP-D | Scheduler hardening + realtime events | **P3 complete** (baseline) |
| WP-E | Store JSON5 + per-job `runs/<jobId>.jsonl` + recovery/repair precedence | **High (Phase CQ)** |
| WP-F | Schema residuals + GatewayHost production E2E | **Baseline landed** |
| jobs.ts core | Schedule cursor, every/at, schedule-error, maintenance, projection | **High (Phase CO)** |
| Step 8 | Tool surface + wake + contextMessages | **P9 baseline + P10 follow-ups** |
| Step 9 | Schema taxonomy + projection consistency | **P7 complete** (baseline) |
| Step 10 | Production GatewayHost E2E matrix | **P8 complete** (baseline) |

## Remaining high-impact gaps

- OpenClaw isolated-agent module parity (WP-A breadth).
- Dedicated channel/outbound plugin routing (WP-B breadth).
- Full IANA timezone database parity depth beyond current `Etc/*` + military alias support (P5 long-tail).
- OpenClaw hook-consumer long-tail integration breadth (beyond current row lock).
- Persistence capability-level closure linkage across Section 2 + Section 3 uplift locks.
- MFC CLI/dashboard controller parity (P10 deferred).

## Validation

```powershell
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][timer]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][schema][response]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][gateway][wp-f]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron]"
msbuild "E:\gitRepo\blazeClaw\blazeclaw\BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

Evidence (2026-05-21): `[cron][ops][p4]` 19/2; `[cron]` 1618/247; `msbuild` pass.
