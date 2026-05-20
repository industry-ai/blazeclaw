# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-05-19 (Phase BG WP-F contract + integration gate)

Authoritative detail: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` (§7.1 work packages WP-A..F)

## Summary

BlazeClaw exposes the full cron/wake RPC surface at the gateway level. Runtime
behavior parity is tracked in the step/work-package plan; method-surface parity is
documented in `openclaw-vs-blazeclaw-method-diff.md`.

## Work package status (2026-05-19)

| Package | Focus | Status |
| --- | --- | --- |
| WP-A | Production runtime execution default | **Baseline landed**; chat-pipeline model routing + live E2E open |
| WP-B | Outbound announce/webhook/failure-alert dispatch | **In Progress** |
| WP-C | Auto-disable notification delivery | **Baseline landed**; IANA/cron syntax open |
| WP-D | Scheduler hardening + realtime events | **Baseline landed** |
| WP-E | Store JSON5 + per-job `runs/<jobId>.jsonl` | **Baseline landed**; prune/doctor/chmod open |
| WP-F | Schema residuals + GatewayHost production E2E | **Baseline landed** (Phase BG); chat-runtime E2E open |

## WP-F landed evidence (Phase BG)

- `GatewayProtocolSchemaValidator.Response`: manual-edge `taskLedgerDisposition`
  taxonomy (`already_running`, `not_due`, `missing_terminal_run`).
- `CronOpsServiceTestHooks`: isolated on-disk cron store for gateway tests.
- `GatewayHost::WireCronProductionIntegration`: handler-stack E2E for
  `cron.add` → `cron.run` (force) → `wake` → `cron.runs` with schema validation.
- Tests: `[cron][gateway][wp-f]` in `BlazeClawMfc/tests/CronParityContractTests.cpp`.

## Remaining high-impact gaps

- Chat-runtime-backed production E2E (configured `m_chatRuntimeCallback` in harness).
- WP-B outbound delivery dispatch depth (Steps 3, 6).
- WP-E jsonl prune / doctor repair / secure file-mode parity.
- OpenClaw CLI/dashboard controller parity (deferred unless product requires MFC CLI).

## Validation

```powershell
msbuild "blazeclaw/BlazeClawMfc.Tests/BlazeClawMfc.Tests.vcxproj" /p:Configuration=Debug /p:Platform=x64
BlazeClawMfc.Tests.exe "[cron]"
BlazeClawMfc.Tests.exe "[cron][gateway][wp-f]"
```
