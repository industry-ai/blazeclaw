# Voice Stream to ASR Step 6: WebView State Streaming

## Goal
Stream speech lifecycle state from native recording/transcription execution boundaries back into WebView chat state so the voice UI reflects real progress (`recording`, `queued`, `transcribing`, terminal states) instead of relying only on optimistic local button text changes.

## Scope Implemented
- Added a dedicated native bridge topic for speech lifecycle events.
- Emitted recording and transcription lifecycle states from the WebView host boundary.
- Routed lifecycle events through the existing canonical transport + legacy compatibility channel path.
- Updated Web chat event/controller/UI code to consume streamed state and drive button labels/disabled state from `state.speechSessionState.stage`.

## Native Bridge Changes

### 1) New speech lifecycle event topic in transport
Files:
- `BlazeClawMfc/src/app/EventTransport.h`
- `BlazeClawMfc/src/app/EventTransport.cpp`

Changes:
- Added `BridgeEventTopic::SpeechLifecycle`.
- Added canonical topic mapping: `speech.lifecycle`.
- Added compatibility channel mapping: `blazeclaw.gateway.speech.lifecycle`.

This preserves the existing transport pattern:
- canonical envelope (`blazeclaw.transport.event.v1`) for modern consumers,
- compatibility channel fan-out for current web handlers.

### 2) WebView-host lifecycle emitter
Files:
- `BlazeClawMfc/src/app/BlazeClawMFCView.h`
- `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`

Changes:
- Added `EmitSpeechLifecycleEvent(const std::string& payloadJson)` helper.
- Added lifecycle payload builders:
  - `BuildSpeechLifecyclePayloadJson(...)`
  - `BuildSpeechLifecyclePayloadFromTranscribeResponse(...)`
- Added MFC message wiring for lifecycle dispatch (`kSpeechLifecycleDispatchMessage`) and handler (`OnSpeechLifecycleDispatched(...)`) for safe UI-thread emission.

### 3) Recording lifecycle emission
File:
- `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`

Changes:
- On successful `gateway.speech.startRecording` RPC response, emit lifecycle stage: `recording`.
- On successful `gateway.speech.stopRecording` RPC response, emit lifecycle stage: `stopped` and include `audioPath` when present.

### 4) Transcription lifecycle emission
File:
- `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`

Changes:
- Before detached `speech.transcribe` worker dispatch, emit:
  - `queued`
  - `transcribing`
- On async completion (`OnSpeechRpcCompleted`), extract terminal speech state from `speech.transcribe` response payload and emit lifecycle event before emitting the RPC result channel.

Terminal stage is sourced from response payload (`speechSession.stage`) and can be:
- `completed`
- `failed`
- `cancelled`

## WebView Consumer Changes

### 1) Event normalization + routing
File:
- `BlazeClawMfc/web/chat/chat-events.js`

Changes:
- Normalized canonical topic `speech.lifecycle` to channel `blazeclaw.gateway.speech.lifecycle`.
- Added dispatch branch for `blazeclaw.gateway.speech.lifecycle` that calls controller speech lifecycle updater and then refreshes composer state.

### 2) Controller speech lifecycle state merge
File:
- `BlazeClawMfc/web/chat/chat-controller.js`

Changes:
- Added `applySpeechLifecycleUpdate(payload)`:
  - normalizes lifecycle payload,
  - merges into `state.speechSessionState`,
  - updates timestamp.
- Exported `applySpeechLifecycleUpdate` so event router and UI code can reuse one merge path.
- Extended speech payload normalization to support both shapes:
  - RPC payload (`speechSession` nested object),
  - streamed lifecycle payload (flat fields).
- Added extra normalized fields used by UI/diagnostics:
  - `audioPath`, `language`, `latencyMs`, `cancelled`.

### 3) UI rendering driven by lifecycle state
File:
- `BlazeClawMfc/web/chat/index.js`

Changes:
- Updated `updateComposerState()` to derive speech busy state from streamed stage:
  - busy: `queued`, `recording`, `transcribing`
- Updated speech button label by stage:
  - `recording` -> `Recording... (click to stop)`
  - `queued` -> `Queued...`
  - `transcribing` -> `Transcribing...`
  - terminal/idle -> `Transcribe`
- Simplified click flow to rely on lifecycle state instead of local optimistic flags:
  - start path updates lifecycle to `recording`
  - stop path updates lifecycle to `stopped`, then calls `transcribeSpeech(...)`
  - error path updates lifecycle to `failed`

## Step 6 Contract Coverage
Minimum events requested in plan are now emitted and consumed:
- recording started
- recording stopped
- transcription queued
- transcription started
- transcription completed
- transcription failed
- transcription cancelled

## Notes
- The Step 3 async transcription boundary is preserved: heavy STT work still runs off the UI/native bridge thread.
- Step 6 adds state streaming only; it does not change model inference internals or handoff mode (still WAV-file boundary from Step 4).