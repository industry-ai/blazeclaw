# Step 7: Native View Speech Lifecycle Coalescing

## Goal
Route live speech-recognition updates through `CBlazeClawMFCView`, the active native output surface, while avoiding repeated duplicate interim updates from being forwarded to the WebView bridge.

## Files Updated
- `src/app/BlazeClawMFCView.h`
- `src/app/BlazeClawMFCView.cpp`

## Implemented Behavior
- Added per-view live speech preview tracking fields to `CBlazeClawMFCView`:
  - active speech session key
  - active speech run id
  - last live segment text
  - last live segment sequence
- Centralized native speech lifecycle filtering in `CBlazeClawMFCView::ShouldEmitSpeechLifecycleEvent(...)`.
- Kept all outward speech lifecycle delivery on the existing bridge path:
  - `CBlazeClawMFCView::EmitSpeechLifecycleEvent(...)`
  - `m_eventTransport.EmitTopic(BridgeEventTopic::SpeechLifecycle, ...)`
- Suppressed duplicate non-final `streaming` lifecycle payloads when they repeat the same segment text and, when present, the same segment sequence for the same session/run.
- Preserved empty/status lifecycle events such as `recording`, `queued`, `start_stream`, and `transcribing` so the WebView can show stable listening/finalizing states.
- Preserved final and terminal lifecycle events such as `segment_finalized`, `stopped`, `completed`, `failed`, and `cancelled` and reset native coalescing state at terminal boundaries.
- Existing asynchronous speech RPC completion still posts back to the view window before emission through `OnSpeechRpcCompleted(...)`, so UI-facing dispatch remains on the view/message path and stale posts are owned by the existing `CMgrMessage::PostOwnedPayloadToHwnd(...)` cleanup behavior.

## Coalescing Rules
- `recording`, `queued`, `start_stream`, and `transcribing` always emit and initialize or update active session/run tracking.
- `streaming` with no text always emits to preserve state transitions.
- `streaming` with non-final segment text emits only when text or segment sequence changes for the active session/run.
- `streaming` with `segment.final == true` always emits.
- terminal stages reset the tracked interim text/sequence so the next recording session starts cleanly.

## Acceptance Criteria Result
- Completed: interim recognition updates continue to flow through `CBlazeClawMFCView` using the existing speech lifecycle bridge output path.
- Completed: duplicate interim segment payloads are coalesced in the native view instead of repeatedly forwarding identical partial text.
- Completed: UI-facing speech RPC results still return via the posted MFC view message handler, and existing owned-payload cleanup prevents stale posted completions from dereferencing destroyed view state.
