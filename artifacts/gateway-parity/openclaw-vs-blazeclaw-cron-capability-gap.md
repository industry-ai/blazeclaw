# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-05-21 (Phase DE §7.7 Step 7-9 production E2E + validation/docs closure baseline)

Authoritative detail: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` (§7.7)

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
| §7.6 depth (P11+) | **Complete** |
| §7.7 `/cron` cli parity (Step 1-9) | **In Progress** (Phase DE baseline landed) |

## Remaining gaps (Phase DD)

| Theme | OpenClaw | BlazeClaw gap |
| --- | --- | --- |
| Isolated runtime | Full `isolated-agent/*` orchestration | Adapter callbacks; module depth lighter |
| Outbound delivery | Default runtime dispatch | Simulation + opt-in `transportDispatch` |
| Session hygiene | `session-reaper.ts` | Not yet in cron subsystem |
| Active job dedupe | `active-jobs.ts` | Lighter process-level guard |
| Past `at` guard | `validate-timestamp.ts` | Normalization only |
| Store ops | Doctor migrations + runLog prune config | Backup/jsonl landed; doctor depth open |
| CLI/UI | `cron-cli`, native UI | WebView-first `/cron` parser + planner + structured UX + execution bridge + regression baseline landed (Step 1-6); production E2E permutations + ordered gate/doc closure baseline landed (Step 7-9) |

## Validation (2026-05-21, Phase DE)

```powershell
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][gateway][normalize]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][schema][response]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][gateway][wp-f]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron]"
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

Evidence: normalize 136/2; schema 92/33; wp-f 83/7; cron 1664/254; msbuild pass.
