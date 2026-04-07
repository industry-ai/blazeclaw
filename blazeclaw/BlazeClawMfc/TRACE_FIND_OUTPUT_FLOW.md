# Find Output Skill-Path Trace (End-to-End)

## Purpose
Trace where skill-path information strings are created and how they reach the GUI list in **Output -> Find**.

## Runtime Paths That Can Emit Skill-Path Info
There are two relevant chat pipelines in the app:

1. `CChatView` native pipeline (`chat.send` + local polling)
2. `CBlazeClawMFCView` WebView bridge pipeline (active in shared WebView tab flow)

Current user scenario is primarily the WebView bridge path.

---

## A. String Creation Points (WebView path)
File: `blazeclaw/BlazeClawMfc/src/app/BlazeClawMFCView.cpp`

### A1. Immediate tool lifecycle lines
- `AppendFindSkillPathStatus(L"tools.execute.start", ...)`
- `AppendFindSkillPathStatus(L"tools.execute.result", ...)`
- `AppendFindSkillPathStatus(L"tools.execute.error", ...)`

These create formatted lines like:
- `[SkillPath] tools.execute.start - tool=... action=...`
- `[SkillPath] tools.execute.result - tool=... status=...`
- `[SkillPath] tools.execute.error - status=error code=...`

### A2. Terminal run summary from task deltas
Method: `CBlazeClawMFCView::ReportRunSkillPathsToFindOutput(const std::string& runId)`

Creates lines:
- `[SkillPath] runId=<id> tried paths:`
- `  - <toolName> [<status>] errorCode=<code>` (if present)

Data source:
- `gateway.runtime.taskDeltas.get` using params `{ "runId":"..." }`
- Parses `taskDeltas` and emits only `phase == "tool_result"` entries

### A3. Trigger points for terminal extraction
- Poll lifecycle path (`PumpBridgeLifecycle`) after `chat.events.poll`
- WebSocket request flow (`openclaw.ws.req`) when routed method is `chat.events.poll`
- RPC flow (`blazeclaw.gateway.rpc`) when routed method is `chat.events.poll`

Extraction helper:
- `ExtractTerminalRunIds(eventsRaw)` for states `final`, `error`, `aborted`

---

## B. Forwarding Path to Frame
From WebView view helpers:
- `AppendFindSkillPathLine(...)` -> `CMainFrame::AddFindStatusLine(...)`

File: `blazeclaw/BlazeClawMfc/src/app/MainFrame.cpp`
- `AddFindStatusLine` forwards to `m_wndOutput.AddFindStatusLine(line)`
- `AddFindStatusBlock` forwards to `m_wndOutput.AddFindStatusBlock(text)`

---

## C. Output Pane Path
File: `blazeclaw/BlazeClawMfc/src/app/OutputWnd.cpp`

1. `COutputWnd::OnCreate` creates `m_wndOutputFind`
2. `FillFindWindow` adds the 3 default placeholder rows
3. `AddFindStatusLine` -> `m_wndOutputFind.AppendLine(line)`
4. `AddFindStatusBlock` -> `m_wndOutputFind.AppendMultiline(text)`

List behavior in `COutputList`:
- `AppendLine`: normalizes text, `AddString`, auto-scrolls via `SetTopIndex`
- retention cap: 1200 rows

---

## D. Why You Might Still Only See 3 Default Rows
Even when forwarding works, these conditions can prevent runtime rows:

1. No terminal run in polled events (`final/error/aborted` missing)
2. `chat.events.poll` not routed through expected WebView path in current tab instance
3. `gateway.runtime.taskDeltas.get` returns empty `taskDeltas`
4. Current visible pane not the same `COutputWnd` instance bound to active frame
5. Terminal event dedup (`m_reportedSkillPathRunIds`) suppresses repeat output for same runId

---

## E. Fast Manual Verification Checklist
1. Send one new prompt (new runId)
2. Confirm chat reaches terminal state (answer shown)
3. Open **Output -> Find** and scroll to bottom
4. Expect at least one of:
   - `[SkillPath] tools.execute.start - ...`
   - `[SkillPath] runId=... tried paths:`
5. If absent, add temporary probe in `chat.events.poll` branch before extraction:
   - `mainFrame->AddFindStatusLine(L"[SkillPath][Probe] chat.events.poll reached")`

---

## F. Native CChatView Path (secondary)
File: `blazeclaw/BlazeClawMfc/src/app/ChatView.cpp`
- `MaybeReportRunSkillPaths(runId)`
- `ReportTriedSkillPathsToFindOutput(runId)`
- same `gateway.runtime.taskDeltas.get` parsing model

This path is useful only if the active UI is actually `CChatView`.

---

## Notes
- Build validation command:
  - `msbuild blazeclaw/BlazeClaw.sln /t:Build /p:Configuration=Debug /p:Platform=x64`
- Current implementation is event-driven; no custom MFC message-map entry is required for `AddFindStatusLine/AddFindStatusBlock` because they are direct method calls, not window message handlers.
