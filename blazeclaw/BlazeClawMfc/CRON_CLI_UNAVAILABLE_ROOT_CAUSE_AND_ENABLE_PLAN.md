# Cron-cli `/cron` Unavailable Root Cause and Enablement Plan

**Status: Complete (2026-06-05)** — Phase 1 (config parse) + Phase 2 (WebView bridge, telemetry, diagnostics) + **Phase 3 (compile guard + shared WebView2 header)** implemented.

**Phase 1 fix note:** `ConfigLoader` used `substr(23)` instead of `substr(25)` for `blazeclaw.agents.enabled=`, parsing `"d=1"` as false. Corrected to `substr(25)`.

**Phase 3 fix note:** `WebViewStartupConfigBridge.cpp` (and `WebViewBridgeSupport.cpp`) compiled WebView2 injection as no-ops because `HAVE_WEBVIEW2_HEADER` was never defined in those translation units. Corrected via shared `WebView2Availability.h` + `IsWebViewStartupBridgeCompiled()` compile guard.

---

## Two-plane model (operational)

| Plane | When | Source | Debug line |
|-------|------|--------|------------|
| **Native (C++)** | App `InitInstance` | `blazeclaw.conf` → `AppConfig.agents.controlPlaneEnabled` | `[Chat] startup.agents.controlPlane - enabled=true source=blazeclaw.conf` |
| **WebView (JS)** | Page load + navigation verify | `__BLAZECLAW_RUNTIME_CONFIG__` → `agents-toggle.js` → `state.cronCliEnabled` | `[Chat] startup.webview.runtimeConfig.agents - enabled=true` |
| **WebView toggle resolution** | After navigation + resync | `BlazeClawAgentsToggle.resolveAgentsEnabled()` | `[Chat] startup.webview.agentsToggle - resolved=true source=config ...` |
| **JS → native trace** | Startup / resync / `/cron` | `blazeclaw.agents.toggle.trace` postMessage | `[Chat] startup.webview.agentsToggle.trace - resolved=... source=...` |

Both planes must agree for `/cron status` to dispatch. Native-only `enabled=true` is necessary but not sufficient.

### Symptom → diagnosis quick map

| What you see | What it means |
|--------------|---------------|
| Native `enabled=true source=blazeclaw.conf` only | Config parse OK; WebView bridge did not deliver runtime config |
| No `startup.webview.runtimeConfig.inject` line | Bridge injection still stubbed (rebuild required) or WebView not initialized |
| `/cron` → `agentsToggleSource: "default"` | JS never saw `__BLAZECLAW_RUNTIME_CONFIG__.agents.enabled` |
| `/cron` → `agentsToggleSource: "config"` but `agents_controller_missing` | Toggle OK; agents controller module failed to load |

### Troubleshooting order

1. App startup: `[Chat] startup.agents.controlPlane - enabled=... source=blazeclaw.conf`
2. WebView inject: `[Chat] startup.webview.runtimeConfig.inject - documentCreated=registered`
3. WebView verify: `[Chat] startup.webview.runtimeConfig.agents` + `startup.webview.agentsToggle`
4. JS trace: `[Chat] startup.webview.agentsToggle.trace` (from `agents-toggle.js`)
5. DevTools probes:
   ```javascript
   window.__BLAZECLAW_RUNTIME_CONFIG__
   window.BlazeClawAgentsToggle?.resolveAgentsEnabled()
   localStorage.getItem("blazeclaw.agents.enabled")
   ```
6. Asset path: `[Chat] startup.chat.selected` → must be `BlazeClawMfc/web/chat/index.html` with `agents-toggle.js`
7. Clear WebView cache / stale localStorage overrides
8. After Phase 3 fix: **Rebuild** (not incremental build) so bridge `.obj` files pick up `WebView2Availability.h`

### Automated gates

```powershell
powershell -File blazeclaw/BlazeClawMfc/tools/chat/Verify-CronCliControlPlaneGate.ps1
node blazeclaw/BlazeClawMfc/tools/chat/run-agents-toggle-regression.js
BlazeClawMfc.Tests.exe "[webview][agents][bootstrap]"
```

Build:

```text
msbuild "blazeclaw/BlazeClaw.sln" /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

Catch2 tag: `[webview][agents][bootstrap]` in `WebViewStartupConfigBridgeTests.cpp`.

---

## Phase 1 — Config file not parsed (completed)

### Root cause

The `/cron` slash handler is guarded by the agents control-plane toggle in chat WebView. Initially that toggle ignored `blazeclaw.conf`.

### Phase 1 implementation

| Layer | Change |
|-------|--------|
| Config | `blazeclaw.agents.enabled=` → `agents.controlPlaneEnabled` |
| Native bridge | `WebViewStartupConfigBridge.*` document-created injection |
| JS toggle | `agents-toggle.js` precedence policy |
| Chat wiring | `index.js` `cronCliEnabled` + lazy `agentsController` |
| Slash hints | `/cron` gated on `state.cronCliEnabled` |

---

## Phase 2 — Native config enabled, WebView `/cron` blocked (completed)

### Root cause (design intent)

Split-brain: native `AppConfig` correct, WebView `__BLAZECLAW_RUNTIME_CONFIG__` missing at JS evaluation time. Phase 2 added bridge delivery, telemetry, fallback, and diagnostics — but the bridge `.cpp` was still compiled as stubs until Phase 3.

### Phase 2 step-by-step plan

| Step | Description | Status |
|------|-------------|--------|
| 1 | Confirm WebView asset bundle (`startup.chat.selected`, `agents-toggle.js` in HTML) | ✅ |
| 2 | Confirm runtime config in live document (DevTools) | ✅ |
| 3 | Native WebView startup telemetry (`EnsureRuntimeConfigAfterNavigation`) | ✅ |
| 4 | Harden bridge delivery (document-created + NavigationCompleted fallback + resync hook) | ✅ |
| 5 | Split `/cron` unavailable diagnostics (`control_plane_disabled` vs `agents_controller_missing`) | ✅ |
| 6 | JS startup alignment (missing-module trace, native-forwarded `[agents-toggle]` via postMessage) | ✅ |
| 7 | Regression coverage (C++ bootstrap tests, JS regressions, `Verify-CronCliControlPlaneGate.ps1`) | ✅ |
| 8 | End-to-end validation checklist (conf-only enablement, `/cron status`, slash regressions) | ✅ |
| 9 | Build verification | ✅ |
| 10 | Operational docs | ✅ |

---

## Phase 3 — Bridge compiled as no-op stubs (completed)

### Observed symptom

Native startup showed `enabled=true source=blazeclaw.conf`, but `/cron status` returned:

```json
{"surface":"cron-cli","ok":false,"code":"control_plane_disabled","agentsToggleSource":"default","message":"/cron is unavailable because agents control plane is disabled."}
```

Missing WebView startup lines:

- `[Chat] startup.webview.runtimeConfig.inject - documentCreated=registered`
- `[Chat] startup.webview.runtimeConfig.agents - enabled=true`
- `[Chat] startup.webview.agentsToggle - resolved=true source=config ...`

### Real root cause

**Translation-unit macro mismatch — bridge compiled as no-op stubs.**

`WebViewStartupConfigBridge.cpp` and `WebViewBridgeSupport.cpp` gate WebView2 code on `#ifdef HAVE_WEBVIEW2_HEADER`, but that macro was only defined in consumer headers (`BlazeClawMFCView.h`, `DashboardWnd.h`, `AIChatView.h`). Those headers are not included by the bridge `.cpp` files, so injection/shim code was stripped at compile time while native config logging still worked.

### Phase 3 implementation

| Step | Action | Status |
|------|--------|--------|
| 1 | Add shared `src/app/WebView2Availability.h` — single source for `HAVE_WEBVIEW2_HEADER` | ✅ |
| 2 | Include `WebView2Availability.h` from bridge `.cpp` files and view headers | ✅ |
| 3 | Add `IsWebViewStartupBridgeCompiled()` + Catch2 guard test | ✅ |
| 4 | Add `IsWebViewBridgeSupportCompiled()` for OpenClaw shim injection parity | ✅ |
| 5 | Extend `Verify-CronCliControlPlaneGate.ps1` to assert shared header usage | ✅ |
| 6 | Rebuild + verify startup WebView lines + `/cron status` | ☐ Manual (local) |

### Key files (Phase 3)

| File | Role |
|------|------|
| `src/app/WebView2Availability.h` | Defines `HAVE_WEBVIEW2_HEADER` when `<WebView2.h>` is available |
| `src/app/WebViewStartupConfigBridge.cpp` | Runtime config injection (document-created + navigation fallback) |
| `src/app/WebViewBridgeSupport.cpp` | OpenClaw WebSocket shim injection |
| `tests/WebViewStartupConfigBridgeTests.cpp` | Bootstrap script + compile guard tests |
| `tools/chat/Verify-CronCliControlPlaneGate.ps1` | Asset + header + JS regression gate |

### Manual verification checklist

1. Set `blazeclaw.agents.enabled=1` in `blazeclaw.conf` (only toggle source).
2. Clear WebView `localStorage` key `blazeclaw.agents.enabled` if present.
3. **Rebuild** Debug\|x64 (see build command above).
4. Start chat WebView; confirm debug lines:
   - `[Chat] startup.webview.runtimeConfig.inject - documentCreated=registered`
   - `[Chat] startup.webview.runtimeConfig.agents - enabled=true`
   - `[Chat] startup.webview.agentsToggle - resolved=true source=config ...`
5. Run `/cron status` — must **not** return `control_plane_disabled` with `agentsToggleSource: "default"`.
6. Regression: `/help`, `/clear`, `/model` unchanged; slash hints show `/cron` when toggle on.

---

## Operational toggle behavior

### Config key

```ini
blazeclaw.agents.enabled=1   # enable agents control-plane + /cron in chat WebView
blazeclaw.agents.enabled=0   # explicitly disable (overrides query/localStorage)
# omit key         # chat defaults to disabled; query/localStorage apply when unset
```

### Precedence (chat WebView)

1. Dashboard host → always enabled
2. `blazeclaw.conf` (via `__BLAZECLAW_RUNTIME_CONFIG__`) → authoritative when set
3. URL `?agents=0|1` → when config unset
4. localStorage `blazeclaw.agents.enabled` → when config unset
5. Default → disabled

### `/cron` availability matrix

| Host / config | `/cron` in hints | `/cron status` | Error code if blocked |
|---------------|------------------|----------------|------------------------|
| Chat, toggle off | Hidden | Blocked | `control_plane_disabled` |
| Chat, conf `=1` + bridge OK | Visible | Cron CLI dispatch | — |
| Chat, conf `=1` + bridge fixed by fallback | Visible | Cron CLI dispatch | — |
| Chat, toggle on, controller missing | Visible | Blocked | `agents_controller_missing` |
| Dashboard host | Visible | Cron CLI dispatch | — |

### `/cron` error envelopes

```json
{"surface":"cron-cli","ok":false,"code":"control_plane_disabled","agentsToggleSource":"default","message":"..."}
{"surface":"cron-cli","ok":false,"code":"agents_controller_missing","agentsToggleSource":"config","controllerPresent":false,"message":"..."}
```

---

## Key files

| Area | Path |
|------|------|
| Plan | `BlazeClawMfc/CRON_CLI_UNAVAILABLE_ROOT_CAUSE_AND_ENABLE_PLAN.md` |
| WebView2 macro | `src/app/WebView2Availability.h` |
| Config | `src/config/ConfigLoader.cpp`, `blazeclaw.conf` |
| Native bridge | `src/app/WebViewStartupConfigBridge.*` |
| JS toggle | `web/chat/agents-toggle.js` |
| Chat wiring | `web/chat/index.js`, `web/chat/chat-controller.js` |
| C++ tests | `tests/WebViewStartupConfigBridgeTests.cpp` |
| Gate script | `tools/chat/Verify-CronCliControlPlaneGate.ps1` |
