# OpenClaw vs BlazeClaw Cron Capability Gap (Post-Closure Snapshot)

Last refreshed: 2026-06-09 (Phase DI gap-closing procedure execution)

Phase 3 update (2026-06-05): chat `/cron` enablement bridge fix — shared
`WebView2Availability.h` ensures runtime config injection is compiled into
`WebViewStartupConfigBridge.obj` (fixes native `enabled=true` vs JS
`agentsToggleSource: "default"` split-brain). See
`BlazeClawMfc/CRON_CLI_UNAVAILABLE_ROOT_CAUSE_AND_ENABLE_PLAN.md` Phase 3.

## Closure verdict

Cron parity is **closed** for product-scoped BlazeClaw surfaces. Section 2 core
rows and Section 3 capability baselines remain **High** / **Pass**-linked.

Phase DI update (2026-06-09): landed isolated-runtime adapter shim, transport-dispatch
test hooks, native CLI gateway verb shim, and targeted parity tests while keeping
MFC native dashboard UI explicitly deferred.

## Sustained pass-locks (do not re-open without §7.8 trigger)

| Area | Status |
| --- | --- |
| Gateway RPC + schema | **Pass** |
| `Cron*` services (jobs/timer/ops/store) | **Pass** |
| WebView tool + `/cron` CLI surface (§7.7) | **Pass** (Phase DH dispatch + Phase 2 chat enablement via `blazeclaw.agents.enabled` + Phase 3 `WebView2Availability.h` bridge compile guard; see `BlazeClawMfc/CRON_CLI_UNAVAILABLE_ROOT_CAUSE_AND_ENABLE_PLAN.md`) |
| Production `GatewayHost` E2E (wp-f) | **Pass** |

## Gate evidence (2026-05-23 replay)

| Gate | Result |
| --- | --- |
| `[cron][gateway][normalize]` | 136 assertions / 2 test cases |
| `[cron][schema][response]` | 92 assertions / 33 test cases |
| `[cron][gateway][wp-f]` | 83 assertions / 7 test cases |
| `[cron]` | 1664 assertions / 254 test cases |
| `msbuild` Debug x64 | pass |

## Product-scoped deferrals (non-blocking)

- Native MFC dashboard UI (WebView `/cron` + `CronCliGatewayShim` verb mapping are active; full native dashboard remains deferred).
- Full OpenClaw `isolated-agent/*` submodule graph beyond adapter-backed production metadata (Phase DI closed adapter/test depth; module graph still deferred).
- Exotic `cron-tool.ts` synthetic-job recovery shapes unless regressions appear.

## Re-open policy (§7.8)

Re-open only when:

1. A targeted or full cron gate fails in an affected lane,
2. OpenClaw baseline behavior changes and product requires parity uplift, or
3. Product explicitly promotes a deferred surface.

Authoritative plan: `blazeclaw/docs/job/cron-parity-gap-and-port-plan.md` (Phase DI + §7.8) and `blazeclaw/docs/job/cron-gap-closure-plan.md`.
