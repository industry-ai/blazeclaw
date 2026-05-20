# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-05-20 (Phase BV P4 task-ledger queued-manual hook + gateway runs E2E)

Authoritative detail: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` (§7.1 work packages WP-A..F)

## Summary

BlazeClaw exposes the full cron/wake RPC surface at the gateway level. P0–P4
execution priorities from §5 are now landed at baseline depth; remaining effort
is P5+ breadth (IANA tz, store depth, schema strictness, hook-consumer E2E).

## Priority status (§5)

| Priority | Status |
| --- | --- |
| P0 | **Complete** (WP-A runtime execution core) |
| P1 | **Complete** (WP-B outbound delivery + failure alert) |
| P2 | **Complete** (WP-C auto-disable workflow + schedule syntax breadth) |
| P3 | **Complete** (WP-D scheduler hardening baseline) |
| P4 | **Complete** (Step 4 task-ledger cross-layer baseline) |
| P5+ | **Open** |

## Work package status (2026-05-20)

| Package | Focus | Status |
| --- | --- | --- |
| WP-A | Production runtime execution default | **P0 complete** |
| WP-B | Outbound announce/webhook/failure-alert dispatch | **P1 complete** |
| WP-C | Auto-disable + schedule syntax breadth | **P2 complete** |
| WP-D | Scheduler hardening + realtime events | **P3 complete** (baseline) |
| WP-E | Store JSON5 + per-job `runs/<jobId>.jsonl` | **Baseline landed** |
| WP-F | Schema residuals + GatewayHost production E2E | **Baseline landed** |
| Step 4 | Task-ledger manual/queued terminal hooks + `cron.runs` projection | **P4 complete** (baseline) |

## Remaining high-impact gaps

- OpenClaw isolated-agent module parity (WP-A breadth).
- Dedicated channel/outbound plugin routing (WP-B breadth).
- Full IANA timezone database (P5).
- OpenClaw hook-consumer integration depth (P8).
- OpenClaw CLI/dashboard controller parity (deferred).

## Validation

Rebuild + rerun on build host after Phase BV:

```powershell
msbuild "E:\gitRepo\blazeClaw\blazeclaw\BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][ops][p4]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][gateway][wp-f][p4]"
```
