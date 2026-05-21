# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-05-21 (Phase CM §7.4 Step 1-6 tranche #2 + jobs.ts row uplift)

Authoritative detail: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` (§7.1 work packages, §7.3–§7.4 jobs.ts closure)

## Summary

BlazeClaw exposes the full cron/wake RPC surface at the gateway level and
maintains strong baseline parity through P0–P9. OpenClaw `jobs.ts` schedule/state
core parity is now **Medium-High** after §7.4 Step 1-6 sustained soak and ordered
gate replay (Phase CM). Remaining work is P10+ breadth: isolated-runtime,
hook-consumer, tool-surface depth, and deferred CLI.

## Priority status (§5)

| Priority | Status |
| --- | --- |
| P0 | **Complete** (WP-A runtime execution core) |
| P1 | **Complete** (WP-B outbound delivery + failure alert) |
| P2 | **Complete** (WP-C auto-disable workflow + schedule syntax breadth) |
| P3 | **Complete** (WP-D scheduler hardening baseline) |
| P4 | **Complete** (Step 4 task-ledger cross-layer baseline) |
| P5 | **Complete** (IANA Etc aliases + six-field cron baseline) |
| P6 | **Complete** (Store JSON5 + per-job jsonl + migration baseline) |
| P7 | **Complete** (Residual schema strictness baseline) |
| P8 | **Complete** (Production GatewayHost E2E baseline) |
| P9 | **Complete** (Tool surface baseline) |
| P10+ | **Active** (runtime/hook-consumer/tool breadth; CLI deferred) |

## jobs.ts row status (Section 2)

| Baseline | Counterpart | Status |
| --- | --- | --- |
| `src/cron/service/jobs.ts` | `CronTimerService.cpp` + `CronOpsService.cpp` | **Medium-High** (Phase CM) |

Evidence: `JOBS-1..JOBS-5` pass in acceptance matrix; §7.4 tranche #2 sustained soak;
ordered gates green (timer 691/98, schema 92/33, wp-f 47/4, cron 1501/235 x2).

## Work package status (2026-05-21)

| Package | Focus | Status |
| --- | --- | --- |
| WP-A | Production runtime execution default | **P0 complete** |
| WP-B | Outbound announce/webhook/failure-alert dispatch | **P1 complete** |
| WP-C | Auto-disable + schedule syntax breadth | **P2 complete** |
| WP-D | Scheduler hardening + realtime events | **P3 complete** (baseline) |
| WP-E | Store JSON5 + per-job `runs/<jobId>.jsonl` | **P6 complete** (baseline) |
| WP-F | Schema residuals + GatewayHost production E2E | **Baseline landed** |
| jobs.ts core | Schedule cursor, every/at, schedule-error, maintenance, projection | **§7.3–7.4 complete (Medium-High)** |
| Step 8 | Tool surface + wake + contextMessages | **P9 baseline + P10 follow-ups** |
| Step 9 | Schema taxonomy + projection consistency | **P7 complete** (baseline) |
| Step 10 | Production GatewayHost E2E matrix | **P8 complete** (baseline) |

## Remaining high-impact gaps

- OpenClaw isolated-agent module parity (WP-A breadth).
- Dedicated channel/outbound plugin routing (WP-B breadth).
- Full IANA timezone database parity (P5 breadth).
- OpenClaw hook-consumer integration depth.
- MFC CLI/dashboard controller parity (P10 deferred).

## Validation

```powershell
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][timer]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][schema][response]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][gateway][wp-f]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron]"
msbuild "E:\gitRepo\blazeClaw\blazeclaw\BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

Evidence (2026-05-21): timer 691/98; schema 92/33; wp-f 47/4; cron 1501/235 (sustained x2).
