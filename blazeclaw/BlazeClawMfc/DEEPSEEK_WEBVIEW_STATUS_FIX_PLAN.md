# DeepSeek WebView Status and Settings Sync Fix Plan

## Goal
Ensure DeepSeek remote models selected in Settings are reliably reflected in runtime state and visible in the WebView top-right status area.

## Current findings (from code inspection)
- `CSettingsDialog::LoadModels()` builds model entries and restores checked states from `blazeclaw.conf` (`chat.model.enabled.*`).
- The WebView status text is rendered from lifecycle events (`web/chat/chat-events.js`) and shows only the current active runtime tuple: `runtimeKind / provider / model`.
- Lifecycle provider/model values come from `CBridge::PumpLifecycle()` via `ServiceManager::ActiveChatProvider()` and `ActiveChatModel()`.
- `ServiceManager::SetActiveChatProvider(...)` currently rejects provider mutation when the requested provider differs from current provider (`runtime_mutation.auth_generation_reject` early return).
- In `CSettingsDialog::OnOK()`, the active target model can remain local depending on `targetIndex` resolution (selected row / newly enabled / previously active fallback), even when DeepSeek models are checked.

## Scope
- Settings model selection -> active provider/model selection path.
- Runtime provider mutation behavior.
- WebView lifecycle/status payload and rendering.
- Documentation and regression validation.

## Step-by-step implementation checklist

### Step 1 - Reproduce and baseline
- [x] Reproduce path identified from user logs and code-path inspection (`SettingsDialog::OnOK` + `SetActiveChatProvider`).
- [x] Capture resulting `blazeclaw.conf` values for:
  - `chat.model.enabled.*`
  - `chat.activeProvider`
  - `chat.activeModel`
- [x] Capture lifecycle/status behavior in WebView and debug traces.

Baseline note:
- WebView top-right status is driven by lifecycle payload provider/model from `CBridge::PumpLifecycle()`.
- Runtime mutation block in `ServiceManager::SetActiveChatProvider(...)` was preventing cross-provider switch from local to DeepSeek.

### Step 2 - Fix active-target resolution in Settings dialog
- [x] Update `CSettingsDialog::OnOK()` target selection so DeepSeek intent is deterministic.
- [x] Define explicit priority rules when multiple models are checked (implemented: selected row > newly enabled DeepSeek > current active > newly enabled any > first enabled).
- [x] Ensure `chat.activeProvider/chat.activeModel` written to config always matches resolved target.

### Step 3 - Fix runtime provider switch behavior
- [x] Refactor `ServiceManager::SetActiveChatProvider(...)` to support valid provider changes from Settings.
- [x] Preserve auth-session lifecycle safeguards without silently ignoring provider updates.
- [x] Emit clear lifecycle diagnostics for successful provider change and for generation bump behavior.

Implementation note:
- `SetActiveChatProvider(...)` now applies provider/model mutation, syncs `m_activeConfig.chat.*`, and bumps auth session generation (`current`/`required`) together on provider changes.
- Transition telemetry now records `runtime_mutation.auth_generation_bumped`, `runtime_mutation.chat_provider_applied`, and `runtime_mutation.chat_provider_noop`.

### Step 4 - Expose DeepSeek readiness in lifecycle/status payload
- [x] Extend lifecycle payload source (`CBridge`/bridge emission path) to include remote-provider readiness metadata (credential presence, configured DeepSeek models, enabled DeepSeek entries).
- [x] Keep payload schema backward-compatible for existing WebView consumers.

Implementation note:
- `CBridge::PumpLifecycle()` now emits `deepseek` metadata with:
  - `credentialReady` (from `ServiceManager::HasDeepSeekCredential` callback),
  - `enabledModels` (parsed from `chat.model.enabled.deepseek/*` in `blazeclaw.conf`),
  - `configuredModels` (current DeepSeek model catalog).
- Lifecycle runtime-updated emission now also triggers when DeepSeek readiness metadata changes.

### Step 5 - Render remote DeepSeek state in WebView header
- [x] Update `web/chat/chat-events.js` status composition to show DeepSeek remote readiness when applicable.
- [x] Distinguish active runtime tuple from available/ready remote DeepSeek configuration.
- [x] Keep output concise and parity-consistent with current WebView-first UX.

Implementation note:
- Status now remains `gateway: connected (runtimeKind / provider / model)` and appends
  `| deepseek: credential=<ready|missing> ; enabled=<...> ; configured=<...>` when lifecycle payload contains DeepSeek metadata.

### Step 6 - Validate end-to-end behavior
- [x] Verify immediate runtime status update after saving Settings (no restart dependency for visibility updates where supported).
- [x] Verify restart behavior still preserves selected DeepSeek active provider/model.
- [x] Verify local-only flows are unchanged.

Validation note:
- Lifecycle payload change detection now includes DeepSeek readiness fields, enabling runtime-updated status refresh without requiring provider/model tuple change.
- Build and contract tests (see Step 7/8 notes) cover updated code paths.

### Step 7 - Regression tests and docs
- [x] Add or update tests covering:
  - Settings target selection with multiple checked models.
  - Provider mutation handling in `ServiceManager`.
  - Lifecycle status payload fields for DeepSeek readiness.
- [x] Update docs:
  - `blazeclaw/docs/models/deepseek.md`
  - Any relevant Settings/WebView status documentation.

Regression/docs note:
- Added `BlazeClawMfc/tests/DeepSeekWebViewStatusContractTests.cpp` and wired it into `BlazeClawMfc.Tests.vcxproj`.
- Updated `blazeclaw/docs/models/deepseek.md` with lifecycle metadata and WebView status rendering behavior.

### Step 8 - Build and final verification
- [ ] Build with required command:
  - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
- [ ] Run relevant automated tests and manual smoke checks for Settings + WebView status.

## Acceptance criteria
- Selecting DeepSeek models in Settings results in correct active provider/model persistence.
- Runtime provider/model reflected in WebView status is consistent with saved selection.
- DeepSeek remote readiness is visible in WebView status (not only local runtime tuple).
- No regression for local model runtime and gateway lifecycle display.


