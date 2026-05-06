# Message references in BlazeClaw

This document lists locations that produce, post, receive, or handle Windows messages and bridge/webview messages across the BlazeClaw codebase. Used for audit and future processing.

> Phase 0 baseline is frozen in: `docs/UI/CMgrMessage.phase0-message-contract.md`

## 1. Custom WM_USER / WM_APP message IDs

- BlazeClawMfc/src/app/MainFrame.h
  - kMsgCreateMdiGroup = WM_USER + 0x100
  - kMsgAppendToolStatusLine = WM_USER + 0x101

- BlazeClawMfc/src/app/BlazeClawMFCView.cpp (local constants)
  - kBridgePollCompletedMessage = WM_APP + 0x2A1
  - kSkillPathLookupCompletedMessage = WM_APP + 0x2A2


## 2. Message handlers (ON_MESSAGE / message map)

- BlazeClawMfc/src/app/MainFrame.cpp
  - ON_MESSAGE(kMsgCreateMdiGroup, &CMainFrame::OnCreateMdiGroup)
  - ON_MESSAGE(kMsgAppendToolStatusLine, &CMainFrame::OnAppendToolStatusLine)

- BlazeClawMfc/src/app/BlazeClawMFCView.cpp
  - LRESULT CBlazeClawMFCView::OnBridgePollCompleted(WPARAM wParam, LPARAM lParam)
  - LRESULT CBlazeClawMFCView::OnSkillPathLookupCompleted(WPARAM wParam, LPARAM lParam)


## 3. Posting messages from background threads or other contexts

- Canonical producer layer: `BlazeClawMfc/src/app/CMgrMessage.h/.cpp`
  - `PostOwnedToolStatusLine(...)`
  - `PostOwnedPayloadToHwnd(...)`
  - `PostOwnedPayloadToMainFrame(...)`

- BlazeClawMfc/src/app/BlazeClawMFCView.cpp
  - `AppendFindSkillPathLine(...)` now posts tool-status lines via `CMgrMessage::PostOwnedToolStatusLine(...)`
  - `ReportRunSkillPathsToToolOutput(...)` now posts `kSkillPathLookupCompletedMessage` via `CMgrMessage::PostOwnedPayloadToHwnd(...)`

- BlazeClawMfc/src/app/CBridge.cpp
  - `StartEventsPollAsync(...)` now posts poll completion payload via `CMgrMessage::PostOwnedPayloadToHwnd(...)`

- BlazeClawMfc/src/app/MainFrame.cpp / CMainFrame::OnAppendToolStatusLine
  - Handler remains unchanged; still consumes heap-allocated `CString*` posted as `LPARAM`


## 4. WebView2 / bridge messages (JS <-> native)

- BlazeClawMfc/src/app/BlazeClawMFCView.cpp
  - PostBridgeMessageJson -> m_webView->PostWebMessageAsJson(json)
  - PostOpenClawWsFrameJson used to send frames back to the WebView shim
  - HandleWebMessageJson receives messages from WebView2 and dispatches based on `channel` field
	- channels observed:
	  - blazeclaw.gateway.chat.push.event
	  - blazeclaw.gateway.rpc
	  - blazeclaw.gateway.lifecycle.subscribe
	  - openclaw.ws.shim.ready
	  - openclaw.ws.req
	  - openclaw.ws.frame / openclaw.ws.close (in JS shim mapping)


## 5. Bridge / transport emitters (not Win32 messages but logical event topics)

- m_eventTransport.EmitTopic(BridgeEventTopic::..., payload)
  - Locations:
	- BlazeClawMfc/src/app/BlazeClawMFCView.cpp (multiple: EmitTopic calls for lifecycle, tools lifecycle, rpc result, etc.)
	- BlazeClawMfc/src/gateway/GatewayHost.cpp (EmitTelemetryEvent / other transports)


## 6. PostMessage / SendMessage scan hits (other notable places)

- Various code references discovered (by search):
  - PostMessage occurrences found in BlazeClawMFCView.cpp for kSkillPathLookupCompletedMessage
  - AfxGetApp()->m_pMainWnd->PostMessage(...) used in AppendFindSkillPathLine helper
  - ON_MESSAGE usage in MainFrame.cpp for kMsgAppendToolStatusLine


## 7. Diagnostics and lifecycle notes (current canonical state)

- `CMgrMessage` diagnostics now track:
  - `posted`, `failedPost`, `handled`, `dropped`, `payloadCleanup`
- Lifecycle order:
  - init: `CMainFrame::OnCreate` -> `CMgrMessage::Initialize(...)`
  - shutdown: `CMainFrame::~CMainFrame` -> `CMgrMessage::Shutdown()`
- Optional channel dispatch path:
  - `CBlazeClawMFCView::HandleWebMessageJson(...)` can register+dispatch selected channels via `CMgrMessage` while preserving the original orchestrator and fallback behavior.

## 8. Recommendations for further analysis

- Expand delegated channel coverage gradually with per-channel metrics baselines.
- Keep payload ownership contracts explicit (message payload type + deleter pairing).
- Continue mapping WebView2 channels to bridge topics for end-to-end tracing.


---

(Generated automatically from code search for PostMessage/ON_MESSAGE/WM_USER/AfxGetMainWnd/etc.)
