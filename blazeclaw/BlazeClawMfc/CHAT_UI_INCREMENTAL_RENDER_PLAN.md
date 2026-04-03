# Chat UI Incremental Render Plan (BlazeClawMfc)

## Objective
Eliminate full chat-list rebuilds on every poll/update and switch to incremental append/update behavior in `ChatView` to reduce redraw churn and improve UI responsiveness.

## Scope
- `CChatView` list synchronization path (`SyncItemsFromState`)
- Streaming assistant row updates
- Error row updates
- History load behavior alignment with render-state tracking

## Implementation Summary

### Phase 1 — Incremental state model
- Added render-state tracking fields in `ChatView`:
  - rendered message count
  - stream row index and last stream text
  - error row index and last error text

### Phase 2 — Incremental synchronization
- Refactored `SyncItemsFromState` to:
  - append only new history messages,
  - update or remove stream row in-place,
  - update or remove error row in-place,
  - fall back to rebuild only when source history shrinks.

### Phase 3 — Item-level update helpers
- Updated `AppendMessage` to return row index.
- Added targeted helpers for row update/removal:
  - `UpdateItemAt`
  - `RemoveItemAt`
  - `ResetRenderedItemsTracking`
  - `RebuildItemsFromState`

### Phase 4 — History reload consistency
- Updated `LoadChatHistoryNative` to force a tracked rebuild path after full history fetch.

## Validation
- Build validation:
  - `msbuild blazeclaw/BlazeClaw.sln /t:Build /p:Configuration=Debug /p:Platform=x64`
- Regression validation:
  - `blazeclaw/bin/Debug/BlazeClawMfc.Tests.exe "[parity][chat]"`

## Completion Audit
Current state: **completed**.

- Full list reset path removed from routine polling sync.
- Incremental append/update behavior is active for history, stream, and error rows.
- Existing chat runtime/event regressions remain passing.
