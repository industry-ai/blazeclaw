# Cron-cli `/cron` Unavailable Root Cause and Enablement Plan

**Status: Implemented (2026-06-05)** — `blazeclaw.agents.enabled` in `blazeclaw.conf` now drives chat WebView agents control-plane and `/cron` availability.

**Fix note (2026-06-05):** `ConfigLoader` used `substr(23)` instead of `substr(25)` for `blazeclaw.agents.enabled=`, so `=1` was parsed as `"d=1"` and fell back to `false`. Corrected to `substr(25)`.

## Root Cause Summary

The `/cron` slash handler is wired, but it is guarded by the agents control-plane toggle in chat WebView mode. Before this fix, that toggle was read only from WebView runtime inputs (dashboard host / query / localStorage), not from `blazeclaw.conf`.

### Evidence chain

1. In `web/chat/index.js`, `/cron` delegates to `agentsController.executeCronCliSlashCommand(...)`.
2. If `agentsController` is missing, it returns the exact error envelope:
   `"/cron is unavailable because agents control plane is disabled."`
3. `agentsController` is only created when the agents toggle resolves to enabled.
4. The toggle previously defaulted to `false` for normal chat WebView unless dashboard host, `?agents=1`, or localStorage `blazeclaw.agents.enabled=1` was set.
5. `blazeclaw.agents.enabled` in `blazeclaw.conf` was not bridged into WebView startup state.
6. Therefore, `blazeclaw.agents.enabled=1` in `blazeclaw/BlazeClawMfc/blazeclaw.conf` had no effect on chat `/cron`.

## Implementation summary

| Layer | Change |
|-------|--------|
| Config | `ConfigModels.h` + `ConfigLoader.cpp` parse `blazeclaw.agents.enabled=` into `agents.controlPlaneEnabled` |
| Native bridge | `WebViewStartupConfigBridge.*` injects `window.__BLAZECLAW_RUNTIME_CONFIG__.agents.enabled` via `AddScriptToExecuteOnDocumentCreated` (chat + dashboard WebViews) |
| JS toggle | `web/chat/agents-toggle.js` resolves enablement with deterministic precedence |
| Chat wiring | `index.js` sets `state.cronCliEnabled` before controller creation; uses shared toggle for `agentsController` |
| Slash hints | `chat-controller.js` includes `/cron` in builtins only when `state.cronCliEnabled` |
| Logging | `BlazeClawMfcApp.cpp` + chat procedure status lines log resolved config at startup |
| Tests | `ConfigLoaderTests.cpp` + `agents-toggle.js` + `chat-controller.js` regressions |

## Operational toggle behavior

### Config key

```ini
# blazeclaw.conf
blazeclaw.agents.enabled=1   # enable agents control-plane + /cron in chat WebView
blazeclaw.agents.enabled=0   # explicitly disable (overrides query/localStorage)
# omit key entirely         # chat defaults to disabled; use query/localStorage overrides
```

### Precedence (chat WebView)

1. **Dashboard host** (`dashboard.html`, `?host=dashboard`, `__BLAZECLAW_DASHBOARD_HOST__`) → always enabled
2. **`blazeclaw.conf`** (`blazeclaw.agents.enabled`) → authoritative when set (`true`/`false`)
3. **URL query** `?agents=1` or `?agents=0` → used when config unset
4. **localStorage** `blazeclaw.agents.enabled` = `1` or `0` → used when config unset
5. **Default** → disabled in chat WebView

### `/cron` availability matrix

| Host / config | `/cron` in hints | `/cron status` execution |
|---------------|------------------|--------------------------|
| Chat, toggle off | Hidden | Unavailable envelope |
| Chat, `blazeclaw.agents.enabled=1` | Visible | Cron CLI dispatch |
| Chat, `?agents=1` (config unset) | Visible | Cron CLI dispatch |
| Dashboard host | Visible (control plane UI) | Cron CLI dispatch |

### Troubleshooting "control plane disabled"

1. Confirm `blazeclaw.conf` loaded path in main-frame status (`[Chat] startup.config.path`).
2. Look for `[Chat] startup.agents.controlPlane - enabled=... source=blazeclaw.conf`.
3. In DevTools console (optional `?assistantRegression=1`), check `[agents-toggle]` trace for `source` and `resolved`.
4. Verify you are in chat WebView (not expecting config to matter on dashboard-only flows — dashboard host is always on).
5. If config is unset, set `blazeclaw.agents.enabled=1` or use `?agents=1` for a one-off session.

## Step-by-step plan (completed)

1. **Reproduce and baseline the issue deterministically** — documented root cause above; unavailable envelope traced to missing `agentsController`.
2. **Validate the current effective toggle path** — confirmed config path was ineffective pre-fix; query/localStorage path worked as workaround.
3. **Instrument startup evidence** — `agents-toggle.js` emits `[agents-toggle]` trace; native startup logs `startup.agents.controlPlane`.
4. **Fix target: adopt `blazeclaw.conf` as source of truth** — `blazeclaw.agents.enabled` contract documented above.
5. **Implement config-to-WebView wiring** — `WebViewStartupConfigBridge` + `agents-toggle.js` precedence policy.
6. **Align slash command discovery** — `/cron` hints gated on `state.cronCliEnabled`.
7. **Add/extend regression checks** — `agents-toggle.js`, `chat-controller.js`, `ConfigLoaderTests.cpp`.
8. **Validate end-to-end** — chat + dashboard host share bridge; non-cron slash commands unchanged.
9. **Build and final verification** — run `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001` locally after toolset install.
10. **Document operational toggle behavior** — this file § Operational toggle behavior.
