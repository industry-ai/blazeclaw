# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-05-21 (Phase CY full counterpart re-review)

Authoritative detail: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` (§7.6)

## Summary

BlazeClaw cron parity is **strong on the service/gateway contract surface**: all
Section 2 core rows are **High** with 247 `[cron]` tests (1618 assertions) green.
Remaining work is **behavioral depth** under §7.6: isolated-agent runtime module,
true outbound delivery, cron session hygiene, store doctor/prune ops, schema
residual edges, and expanded production E2E.

## OpenClaw inventory (non-test, ~45 modules)

- Domain: `src/cron/{types,store,schedule,parse,normalize,delivery*,session-*}`
- Service: `src/cron/service/{jobs,timer,ops,store,*}`
- Isolated runtime: `src/cron/isolated-agent/*` (15+ files)
- Gateway: `server-cron.ts`, `server-methods/cron.ts`, `protocol/schema/cron.ts`
- Surfaces: `cron-tool.ts`, `cron-cli/*`, UI controller

## BlazeClaw inventory

- `BlazeClawMfc/src/cron/{CronModels,CronJsonCompat,CronNormalize,CronStoreService,CronTimerService,CronOpsService}`
- Gateway: `GatewayHost` pre-validator + handlers + validators + schema catalog
- UX: `web/chat/agents-controller.js`
- Tests: `BlazeClawMfc/tests/CronParityContractTests.cpp`

## Priority status

| Priority | Status |
| --- | --- |
| P0–P9 + P10 tool depth | **Complete** |
| §7.6 depth (P11+) | **Active** |

## Remaining gaps (Phase CY)

| Theme | OpenClaw | BlazeClaw gap |
| --- | --- | --- |
| Isolated runtime | Full `isolated-agent/*` orchestration | Adapter callbacks; module depth lighter |
| Outbound delivery | Default runtime dispatch | Simulation + opt-in `transportDispatch` |
| Session hygiene | `session-reaper.ts` | Not yet in cron subsystem |
| Active job dedupe | `active-jobs.ts` | Lighter process-level guard |
| Past `at` guard | `validate-timestamp.ts` | Normalization only |
| Store ops | Doctor migrations + runLog prune config | Backup/jsonl landed; doctor depth open |
| CLI/UI | `cron-cli`, native UI | WebView-first; CLI deferred |

## Validation (2026-05-21)

```powershell
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][timer]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][ops]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][store]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][schema][response]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][gateway][wp-f]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron]"
```

Evidence: timer 759/106; ops 220/19; store 60/14; schema 92/33; wp-f 55/5; cron 1618/247.
