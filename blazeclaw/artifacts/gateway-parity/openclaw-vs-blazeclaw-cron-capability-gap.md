# OpenClaw vs BlazeClaw Cron Capability Gap (Post-Closure Snapshot)

Last refreshed: 2026-05-23 (Phase DF §7.8 Steps 1–5 governance execution)

## Closure verdict

Cron parity is **closed** for product-scoped BlazeClaw surfaces. Section 2 core
rows and Section 3 capability baselines remain **High** / **Pass**-linked.

## Sustained pass-locks (do not re-open without §7.8 trigger)

| Area | Status |
| --- | --- |
| Gateway RPC + schema | **Pass** |
| `Cron*` services (jobs/timer/ops/store) | **Pass** |
| WebView tool + `/cron` CLI surface (§7.7) | **Pass** |
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

- Native MFC `cron-cli` / macOS dashboard UI (WebView `/cron` is the active path).
- Full OpenClaw `isolated-agent/*` submodule graph beyond production adapter metadata.
- Exotic `cron-tool.ts` synthetic-job recovery shapes unless regressions appear.

## Re-open policy (§7.8)

Re-open only when:

1. A targeted or full cron gate fails in an affected lane,
2. OpenClaw baseline behavior changes and product requires parity uplift, or
3. Product explicitly promotes a deferred surface.

Authoritative plan: `blazeclaw/docs/cron-parity-gap-and-port-plan.md` §7.8.
