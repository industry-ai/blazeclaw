# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-05-20 (Phase BR P2/WP-C schedule breadth + production auto-disable gateway E2E)

Authoritative detail: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` (§7.1 work packages WP-A..F)

## Summary

BlazeClaw exposes the full cron/wake RPC surface at the gateway level. P0/P1/P2
execution priorities from §5 are now landed; remaining effort is P3+ breadth.

## Priority status (§5)

| Priority | Status |
| --- | --- |
| P0 | **Complete** (WP-A runtime execution core) |
| P1 | **Complete** (WP-B outbound delivery + failure alert) |
| P2 | **Complete** (WP-C auto-disable workflow + schedule syntax breadth) |
| P3+ | **Open** |

## Work package status (2026-05-20)

| Package | Focus | Status |
| --- | --- | --- |
| WP-A | Production runtime execution default | **P0 complete** |
| WP-B | Outbound announce/webhook/failure-alert dispatch | **P1 complete** |
| WP-C | Auto-disable + schedule syntax breadth | **P2 complete** |
| WP-D | Scheduler hardening + realtime events | **Baseline landed** |
| WP-E | Store JSON5 + per-job `runs/<jobId>.jsonl` | **Baseline landed** |
| WP-F | Schema residuals + GatewayHost production E2E | **Baseline landed** |

## Remaining high-impact gaps

- OpenClaw isolated-agent module parity (WP-A breadth).
- Dedicated channel/outbound plugin routing (WP-B breadth).
- Full IANA timezone database (P5).
- Task-ledger cross-layer depth (Step 4 / P4).
- OpenClaw CLI/dashboard controller parity (deferred).

## Validation

```powershell
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
BlazeClawMfc.Tests.exe "[cron]"
BlazeClawMfc.Tests.exe "[cron][timer][wp-c]"
BlazeClawMfc.Tests.exe "[cron][gateway][wp-f][wp-c]"
```
