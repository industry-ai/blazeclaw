# Sherpa Zipformer Live Recognition GUI Step 1 - Active Entry Points

## Status
Completed.

## Scope
This step confirms which GUI path owns live speech-recognition output and documents where recording start, recording stop, and `speech.transcribe` are currently invoked.

## Findings

### Primary GUI path
The primary speech-recognition GUI path is the WebView chat UI hosted by `CBlazeClawMFCView`.

Relevant files:
- `src/app/BlazeClawMFCView.cpp`
- `src/app/BlazeClawMFCView.h`
- `web/chat/index.js`
- `web/chat/chat-controller.js`
- `web/chat/chat-events.js`

`CBlazeClawMFCView` owns the WebView/native bridge output path for speech lifecycle and speech RPC results:
- `HandleWebMessageJson(...)` routes WebView `blazeclaw.gateway.rpc` requests.
- `speech.transcribe` is special-cased and dispatched asynchronously from `CBlazeClawMFCView`.
- `OnSpeechRpcCompleted(...)` emits the final speech lifecycle payload and bridge RPC result.
- `EmitSpeechLifecycleEvent(...)` sends `speech.lifecycle` events back to the WebView.
- `chat-events.js` normalizes `speech.lifecycle` to `blazeclaw.gateway.speech.lifecycle` and applies it through `controller.applySpeechLifecycleUpdate(...)`.

### Native recorder ownership
`CChatView` is not the active speech-recognition result output surface for this feature. It can still be used by `GatewayHost` as a native recorder provider when an active chat view exists.

Current recorder selection:
- `GatewayHost::StartNativeRecording()` tries `CMainFrame::GetActiveChatView()` first.
- If an active chat view exists, it calls `CChatView::StartRecordingToPath(...)`.
- Otherwise it uses the fallback `CVoiceRecorder` owned by gateway fallback recording state.
- `GatewayHost::StopNativeRecording()` mirrors that selection and returns the recording path/artifact.
- `GatewayHost::ResolveNativeRecordingArtifact(...)` returns the active recorder artifact from either `CChatView` or fallback recorder.

Design consequence:
- Future live recognition display work should target `CBlazeClawMFCView` / WebView bridge output.
- Recorder plumbing may still read artifacts from `CChatView` or fallback recorder, but `CChatView` should not be treated as the GUI output owner for this feature.

## Current Start Recording Flow

```text
web/chat/index.js speechTranscribeBtn click
  -> controller.request("gateway.speech.startRecording", { sessionId })
  -> CBlazeClawMFCView::HandleWebMessageJson(...)
  -> CBlazeClawMFCApp::RouteGatewayRequest(...)
  -> GatewayHost.Handlers.Runtime.SpeechRecognition.cpp handler "gateway.speech.startRecording"
  -> GatewayHost::StartNativeRecording()
  -> CChatView recorder if active, otherwise fallback CVoiceRecorder
  -> GatewayHost::ResolveNativeRecordingArtifact(...)
  -> response returns ok plus audioArtifact when available
  -> CBlazeClawMFCView emits "recording" speech.lifecycle
  -> WebView controller applies speechSessionState update
```

Key implementation points:
- WebView start button path is in `web/chat/index.js` around `speechTranscribeBtn`.
- Native bridge routing is in `CBlazeClawMFCView::HandleWebMessageJson(...)`.
- Gateway start handler is `gateway.speech.startRecording` in `GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`.
- Native recording starts in `GatewayHost::StartNativeRecording()`.

## Current Stop Recording Flow

```text
web/chat/index.js speechTranscribeBtn click while recording/streaming
  -> stopLiveSpeechPoll()
  -> controller.request("gateway.speech.stopRecording", { sessionId })
  -> CBlazeClawMFCView::HandleWebMessageJson(...)
  -> CBlazeClawMFCApp::RouteGatewayRequest(...)
  -> GatewayHost.Handlers.Runtime.SpeechRecognition.cpp handler "gateway.speech.stopRecording"
  -> GatewayHost::StopNativeRecording()
  -> returns audioPath and audioArtifact
  -> CBlazeClawMFCView emits "stopped" speech.lifecycle
  -> WebView controller applies speechSessionState update
  -> web/chat/index.js calls controller.transcribeSpeech({ audioPath, audioArtifact, prompt })
```

Key implementation points:
- Stop is initiated by the same WebView button when `speechSessionState.stage` is `recording` or `streaming`.
- `stopLiveSpeechPoll()` is called before final transcription.
- The final transcription remains authoritative after stop.

## Current speech.transcribe Flow

```text
web/chat/chat-controller.js transcribeSpeech(...)
  -> requestWithOverride("speech.transcribe", transcriptRequest, requestOverride)
  -> CBlazeClawMFCView::HandleWebMessageJson(...)
  -> detects method == "speech.transcribe"
  -> emits queued/start_stream/streaming or queued/transcribing speech.lifecycle events
  -> dispatches RouteGatewayRequest(...) on a detached worker thread
  -> GatewayHost.Handlers.Runtime.SpeechRecognition.cpp handler "speech.transcribe"
  -> host.AcceptSpeechTranscription(...)
  -> host.TranscribeSpeech(...)
  -> response includes speechSession, executionState, audioArtifact, and segment when present
  -> CBlazeClawMFCView::OnSpeechRpcCompleted(...)
  -> BuildSpeechLifecyclePayloadFromTranscribeResponse(...)
  -> EmitSpeechLifecycleEvent(...)
  -> emit blazeclaw.gateway.rpc.result
  -> WebView updates speechSessionState and final chat-send behavior
```

Key implementation points:
- `CBlazeClawMFCView` is the async boundary for `speech.transcribe`; runtime work is not executed inline on the WebView UI bridge thread.
- Streaming requests are detected from `audioArtifact.handoffMode == "pcm_stream"`.
- The gateway response already includes `speechSession.segment` when the runtime returns a segment.

## Existing Live Preview Hook
A live preview polling hook already exists in `web/chat/index.js`:
- `startLiveSpeechPoll(audioPath, audioArtifact, prompt)` starts a periodic preview loop.
- `liveSpeechPollBusy` prevents overlapping preview calls.
- Each tick calls `controller.transcribeSpeech({ audioPath, audioArtifact, prompt, timeoutMs: 8000, livePreviewOnly: true })`.
- Polling currently runs every 1200 ms while `speechSessionState.stage` is `recording` or `streaming`.
- `stopLiveSpeechPoll()` clears the timer before final stop transcription.

Design consequence:
- Later steps should refine and harden the existing preview loop rather than assuming no polling loop exists.
- Coordinator/gateway behavior must support repeated preview transcribe calls for the active recording session without causing stale updates or duplicate-session rejection.

## Step 1 Acceptance Criteria Results
- Primary button path: WebView chat UI button in `web/chat/index.js`, hosted by `CBlazeClawMFCView`.
- Native output surface: `CBlazeClawMFCView` / WebView bridge speech lifecycle and RPC result path.
- Recorder provider: `GatewayHost` uses active `CChatView` recorder when available, otherwise fallback `CVoiceRecorder`.
- Recording starts: `gateway.speech.startRecording` -> `GatewayHost::StartNativeRecording()`.
- Recording stops: `gateway.speech.stopRecording` -> `GatewayHost::StopNativeRecording()`.
- Transcription invoked: `web/chat/chat-controller.js::transcribeSpeech(...)` -> `speech.transcribe` -> async `CBlazeClawMFCView` worker -> gateway speech handler.
