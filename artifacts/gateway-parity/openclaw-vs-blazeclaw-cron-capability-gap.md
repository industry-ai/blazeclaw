# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-05-20 (Phase CF Step 6-11 failure-alert observability + schema strictness + production E2E follow-up + docs sync)

Authoritative detail: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` (§7.1 work packages WP-A..F)

## Summary

BlazeClaw exposes the full cron/wake RPC surface at the gateway level and
maintains strong baseline parity through P0–P9. Step 2-4 depth now also
includes explicit runtime execution provenance projection
(`runtimeExecutionPath` + runtime adapter/simulation fallback booleans)
through timer state/run snapshots and terminal task-ledger hooks. Remaining
work is concentrated in P10+ breadth: execution/runtime permutations,
hook-consumer side-effect depth, and tool-surface breadth (plus deferred CLI
parity decision).

Phase CF Step 6-11 follow-up tightened failure-alert and schema observability
parity by projecting `failureAlertStatus=not-requested` for not-configured
suppression lanes, enforcing `failureAlertStatus` taxonomy in
`cron.runs` response validation, and adding production
`[cron][gateway][wp-f][step6][step10]` E2E coverage for handler-stack
`cron.runs` projection.

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
| P10+ | **Active** (execution-depth + hook-consumer depth + tool-surface breadth; deferred CLI decision) |

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
| Step 8 | Tool-surface `jobId`/`id` aliases + flat recovery guard + wake action + `contextMessages` shaping | **P9 baseline + P10 follow-ups landed** |
| Step 9 | `lifecycleState`/`deliveryMode` taxonomy + projection consistency | **P7 complete** (baseline) |
| Step 10 | Production GatewayHost E2E matrix | **P8 complete** (baseline) |

## Remaining high-impact gaps

- OpenClaw isolated-agent runtime behavior breadth parity (WP-A depth).
- Cross-layer hook-consumer/runtime integration depth (task-ledger/retry/cooldown side-effects).
- Broader synthetic-job recovery depth beyond landed `contextMessages` tool-surface shaping.
- OpenClaw CLI/dashboard controller parity only if MFC CLI becomes product scope.

## Validation

Validation rerun after Phase CC:

```powershell
msbuild "E:\gitRepo\blazeClaw\blazeclaw\BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron]"
```

Evidence (2026-05-20, Phase CF): `[cron][timer]` 672 assertions / 96 test cases, `[cron][schema][response]` 79 assertions / 30 test cases, `[cron][gateway][wp-f]` 152 assertions / 14 test cases, `[cron]` 1574 assertions / 240 test cases; required `msbuild` gate passed.
