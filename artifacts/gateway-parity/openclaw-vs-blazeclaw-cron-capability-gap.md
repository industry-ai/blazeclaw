# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-05-20 (Phase CB P10 tool-surface wake-action follow-up + P0-P9 closure verification)

Authoritative detail: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` (§7.1 work packages WP-A..F)

## Summary

BlazeClaw exposes the full cron/wake RPC surface at the gateway level. P0–P9
execution priorities from §5 are now landed at baseline depth; P10 follow-up
controller parity has started with wake-action routing/refresh behavior, while
remaining effort is deeper tool breadth plus deferred CLI/controller parity.

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
| P9 | **Complete** (Tool surface `jobId`/`id` alias + flat-shape guard baseline) |
| P10+ | **Active** (wake-action follow-up landed; deferred CLI + deeper tool/hook-consumer parity remain) |

## Work package status (2026-05-20)

| Package | Focus | Status |
| --- | --- | --- |
| WP-A | Production runtime execution default | **P0 complete** |
| WP-B | Outbound announce/webhook/failure-alert dispatch | **P1 complete** |
| WP-C | Auto-disable + schedule syntax breadth | **P2 complete** |
| WP-D | Scheduler hardening + realtime events | **P3 complete** (baseline) |
| WP-E | Store JSON5 + per-job `runs/<jobId>.jsonl` | **P6 complete** (baseline) |
| WP-F | Schema residuals + GatewayHost production E2E | **Baseline landed** |
| Step 4 | Task-ledger manual/queued terminal hooks + `cron.runs` projection | **P4 complete** (baseline) |
| Step 8 | Tool-surface `jobId`/`id` aliases + flat recovery guard + wake action follow-up | **P9 baseline + P10 follow-up landed** |
| Step 9 | `lifecycleState`/`deliveryMode` taxonomy + projection consistency | **P7 complete** (baseline) |
| Step 10 | Production GatewayHost E2E matrix | **P8 complete** (baseline) |

## Remaining high-impact gaps

- OpenClaw isolated-agent module parity (WP-A breadth).
- Dedicated channel/outbound plugin routing (WP-B breadth).
- Full IANA timezone database parity (P5 breadth).
- OpenClaw hook-consumer integration depth (beyond P8 baseline).
- `contextMessages` on manual run and broader synthetic-job recovery depth.
- OpenClaw CLI/dashboard controller parity (P10 deferred).

## Validation

Validation rerun after Phase CB:

```powershell
msbuild "E:\gitRepo\blazeClaw\blazeclaw\BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][gateway][normalize]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron]"
```

Evidence (2026-05-20, Phase CB): `[cron][gateway][normalize]` 132 assertions / 2 test cases; `[cron]` 1409 assertions / 223 test cases.
