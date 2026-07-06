# Agent Chat Native Bridge Timeout - Root Cause and Fix Tracker

**Status: Implemented (code + build + automated regression tests) - 2026-07-03**

## Issue Summary
- Symptom: In Agent Chat WebView (`CBlazeClawAgentChatView`), sending `路演h5` in default rooms (`炎图AI助手`, `我的 AI 工作空间`) returns:
  - `抱歉，AI 回复失败：Native bridge request failed (native_health_timeout)`
- Regression: Previously working, now native preflight health check times out.

## Root Cause (Confirmed from code)

### 1) Native health request is sent from WebView JS
- File: `BlazeClawMfc/web/agent-chat-vanilla/html/js/api/agentBridgeTransport.js`
- `_preflightNativeBridgeHealth()` posts:
  - `channel: 'agentchat.bridge.request'`
  - `kind: 'agent.health'`
- It waits for `'agentchat.bridge.response'` with same `requestId`.
- If no response arrives before timeout, it throws `native_health_timeout`.

### 2) C++ message pipeline drops incoming WebView messages
- File: `BlazeClawMfc/src/app/BlazeClawAgentChatView.cpp`
- `SetupWebViewEvents()` receives web messages and does:
  - invoke `m_messageHandler(message)` **only if handler exists**
  - `PostMessage(WM_AGENTCHAT_WEBMESSAGE_RECEIVED)`
- `OnWebMessageReceived()` only processes `m_pendingWebMessageJson`.
- But in current code path:
  - `m_pendingWebMessageJson` is never assigned in the event handler.
  - `m_messageHandler` has no assignment in this class.
- Result:
  - `OnWebMessageReceived()` sees empty payload and returns immediately.
  - `kind == "agent.health"` block is never reached.
  - WebView side waits until timeout -> `native_health_timeout`.

## Fix Strategy
- Restore deterministic handoff from WebView message event to `OnWebMessageReceived()` by persisting raw message JSON before posting WM message.
- Do not depend on optional `m_messageHandler` for bridge-critical path.

## Step-by-step Tracker

### Phase 1 - Apply fix in C++ bridge intake
- [x] 1. Update `CBlazeClawAgentChatView::SetupWebViewEvents()` in `BlazeClawAgentChatView.cpp`.
- [x] 2. In `add_WebMessageReceived` callback, write received message into `m_pendingWebMessageJson` under `m_webBridgeMutex`.
- [x] 3. Keep `PostMessage(WM_AGENTCHAT_WEBMESSAGE_RECEIVED)` after persisting payload.
- [x] 4. Keep `m_messageHandler` callback optional (non-blocking), but ensure native bridge path no longer relies on it.

### Phase 2 - Defensive compatibility hardening (recommended)
- [x] 5. Add fallback extraction for non-string web messages (`get_WebMessageAsJson`) and normalize to `m_pendingWebMessageJson`.
- [x] 6. Add trace lines for receive/parse/drop decisions (requestId, kind, channel) for faster future diagnosis.

### Phase 3 - Validate behavior
- [x] 7. Build with required command:
  - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
- [ ] 8. Run Agent Chat WebView and send `路演h5` in both default rooms. *(manual runtime verification pending in local UI session)*
- [ ] 9. Verify no `native_health_timeout` and confirm native stream events (`delta/final`) arrive. *(manual runtime verification pending in local UI session)*
- [ ] 10. Verify fallback policy still behaves correctly when native host is intentionally unavailable. *(manual runtime verification pending in local UI session)*

### Phase 4 - Regression guard
- [x] 11. Add/extend tests around web message intake -> `OnWebMessageReceived` dispatch for `agent.health` and `agent.turn`.
- [x] 12. Document the regression and final fix in release notes / internal troubleshooting notes.

## Implementation Delta
- Updated `BlazeClawMfc/src/app/BlazeClawAgentChatView.cpp`:
  - `SetupWebViewEvents()` now persists incoming WebView payload into `m_pendingWebMessageJson` under `m_webBridgeMutex` before `PostMessage(WM_AGENTCHAT_WEBMESSAGE_RECEIVED)`.
  - Added fallback payload extraction path using `get_WebMessageAsJson` when string extraction is unavailable.
  - Added native bridge diagnostics for dropped/ignored/invalid web messages and message receive size.
- Added regression contract test file:
  - `BlazeClawMfc/tests/AgentChatWebMessageIntakeDispatchContractTests.cpp`
- Added test project entry:
  - `BlazeClawMfc.Tests/BlazeClawMfc.Tests.vcxproj`

## Validation Record
- Build validation: **PASS**
  - `msbuild "E:/gitRepo/blazeClaw/blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
- Automated regression tests: **PASS**
  - `E:/gitRepo/blazeClaw/blazeclaw/bin/Debug/BlazeClawMfc.Tests.exe "[agentchat][native][webview][intake][contract]"`
  - Result: `All tests passed (13 assertions in 2 test cases)`

## Expected Outcome
- Native bridge health preflight receives response reliably.
- Agent Chat no longer fails with `Native bridge request failed (native_health_timeout)` in normal runtime.
- Native transport remains primary, HTTP fallback remains policy-driven.
