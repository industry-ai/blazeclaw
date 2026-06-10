# OpenClaw vs BlazeClaw Cron Capability Gap

Last refreshed: 2026-06-09 (Phase DI gap-closing procedure + dashboard hydration fix + docs sync)

Authoritative detail: `blazeclaw/docs/job/cron-parity-gap-and-port-plan.md` (Phase DI + §7.7) and `blazeclaw/docs/job/cron-gap-closure-plan.md`

## Summary

BlazeClaw cron parity is **closed and sustained** on the service/gateway/tool
surface: all Section 2 core rows remain **High**, §7.6 and §7.7 baselines remain
pass-linked, and Phase DF replayed ordered gates green (`[cron]` 1664 assertions /
254 test cases).

Phase DI update (2026-06-09): isolated-runtime outcome adapter shim, injectable
transport-dispatch test hooks, native CLI gateway verb shim, and targeted parity
tests landed; MFC native dashboard UI remains product-deferred.

Phase DH update (2026-06-05): WebView `/cron` slash execution refactored to a
non-blocking pending/settled pipeline with sequence-token stale suppression while
preserving 7.7 parser/planner/help/error contracts.

Follow-up tranche (2026-06-09): WebView cron dashboard content fix applied —
controller hydration gate was moved to allow global cron hydration when no
agent is selected. Regression coverage and validation evidence recorded in
`blazeclaw/docs/job/cron-dashboard-content-fix-plan.md` and `blazeclaw/docs/cron.md`.

Phase DG update: §7.8 Step 1-5 governance execution is completed with no reopen
trigger conditions met; closure remains in pass-lock mode.

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
| §7.7 `/cron` cli parity (Step 1-9) | **Complete** (Phase DE baseline, sustained in Phase DG; non-blocking dispatch Phase DH) |
| §7.8 post-closure re-open governance | **Active policy** |

## Remaining gaps (Phase DG)

| Theme | OpenClaw | BlazeClaw gap |
| --- | --- | --- |
| Isolated runtime submodule graph | Full `isolated-agent/*` orchestration breadth | Adapter-backed runtime outcomes/metadata are landed; deep module graph remains product-scoped deferred |
| Native CLI/UI | `cron-cli`, native UI | WebView-first `/cron` parity is closed and non-blocking (Phase DH); native MFC dashboard remains deferred unless product scope changes |

## Validation (2026-05-22, Phase DG)

```powershell
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][gateway][normalize]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][schema][response]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron][gateway][wp-f]"
E:\gitRepo\blazeClaw\blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe "[cron]"
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

Evidence: normalize 136/2; schema 92/33; wp-f 83/7; cron 1664/254; msbuild pass.
