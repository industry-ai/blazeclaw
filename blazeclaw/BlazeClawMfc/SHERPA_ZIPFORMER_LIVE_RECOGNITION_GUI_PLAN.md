# Sherpa Zipformer Live Recognition GUI Plan

## Goal
Modify the Sherpa Zipformer speech recognition GUI so users see live recognition updates while speaking, instead of waiting until recording stops and final transcription completes.

## Current State Summary
- The app already supports Sherpa Zipformer streaming contracts and ring-buffer audio handoff.
- `SpeechTranscriptSegment` already models interim vs. final transcript segments with `text`, `final`, and `sequence`.
- `SpeechExecutionStage` already includes live-friendly stages such as `StartStream`, `Streaming`, and `SegmentFinalized`.
- `SherpaZipformerStreamingEngine::TranscribeStreaming(...)` maintains `partialText` and returns a non-final segment for live PCM streams.
- `SpeechTranscriptionCoordinator` emits execution lifecycle callbacks, but it currently only emits segment-finalized updates when a streaming request returns a final segment.
- `speech.capabilities.get` advertises `supportsInterim: true`, but the GUI path does not yet continuously invoke or surface interim recognition while the microphone is still recording.
- WebView chat state already has `speechSessionState`, `applySpeechLifecycleUpdate(...)`, and a `livePreviewOnly` transcription mode that can update state without sending chat messages.
- The active native speech-recognition result surface is `CBlazeClawMFCView`, not `CChatView`; live interim/final speech results should be routed through `CBlazeClawMFCView` and its existing WebView/native bridge output path.

## Desired User Experience
1. User clicks the voice/transcribe button.
2. Recording starts and the UI immediately enters a recording/listening state.
3. While the user speaks, the GUI shows the latest recognized interim transcript.
4. Interim text updates in place and does not send a chat message.
5. When speech is finalized or recording stops, the final transcript replaces the interim text.
6. Only final accepted transcript text is injected into chat/send flow.
7. Cancel, stop, timeout, and failure states clear or preserve the live preview predictably.

## Proposed Architecture

### Data Flow
```text
Microphone / VoiceRecorder
  -> AudioRingBuffer / StreamingAudioSourceRegistry
  -> speech.transcribe with pcm_stream artifact and live preview mode
  -> SpeechTranscriptionCoordinator
  -> SpeechRecognitionRuntime
  -> SherpaZipformerStreamingEngine partial segment
  -> speech.lifecycle event / response payload
  -> WebView chat-controller speechSessionState
  -> GUI interim transcript display
```

### Contract Direction
Use the existing contract surface first:
- `SpeechAudioArtifact.handoffMode = PcmStream`
- `SpeechStreamingInputContract`
- `SpeechTranscriptSegment.final = false` for interim updates
- `SpeechExecutionStage::Streaming` for interim updates
- `SpeechExecutionStage::SegmentFinalized` for finalized utterance segments
- `speech.lifecycle` / `blazeclaw.gateway.speech.lifecycle` as the UI update channel

Avoid introducing a second transcript-preview model unless the existing segment contract cannot carry enough metadata.

## Implementation Steps

### Step 1: Confirm the active GUI entry points
Status: completed

Detailed findings:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP1_ENTRY_POINTS.md`

Confirmed voice UI paths:
- MFC/WebView host view: `src/app/BlazeClawMFCView.cpp`, `src/app/BlazeClawMFCView.h`
- WebView chat UI: `web/chat/index.js`, `web/chat/chat-controller.js`, `web/chat/chat-events.js`
- Gateway speech handler: `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- Native recording provider path: `src/gateway/GatewayHost.cpp`, `src/app/ChatView.cpp`, `src/app/VoiceRecorder.cpp`

Step 1 findings:
- the primary button path is the WebView chat UI button in `web/chat/index.js`, hosted by `CBlazeClawMFCView`
- the speech-recognition output surface is `CBlazeClawMFCView` / WebView bridge lifecycle output, not `CChatView`
- `CChatView` may still supply the active `CVoiceRecorder` through `GatewayHost::StartNativeRecording()`, `StopNativeRecording()`, and `ResolveNativeRecordingArtifact(...)`
- recording starts through `gateway.speech.startRecording`, routed by `CBlazeClawMFCView::HandleWebMessageJson(...)` to `GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`, then `GatewayHost::StartNativeRecording()`
- recording stops through `gateway.speech.stopRecording`, routed by the same bridge path to `GatewayHost::StopNativeRecording()`
- final transcription is invoked by `web/chat/chat-controller.js::transcribeSpeech(...)` via `speech.transcribe`; `CBlazeClawMFCView` special-cases this method, emits lifecycle stages, and dispatches gateway work asynchronously
- an existing live preview polling hook is already present in `web/chat/index.js` as `startLiveSpeechPoll(...)`, using `livePreviewOnly: true` and a `liveSpeechPollBusy` guard

Acceptance criteria result:
- completed: primary button path, native host output path, recording start/stop path, and `speech.transcribe` invocation path are documented

### Step 2: Refine the live preview polling loop while recording
Status: completed

Detailed findings and implementation notes:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP2_LIVE_PREVIEW_POLLING.md`

Refine the existing lightweight live preview loop after recording starts.

Current starting point from Step 1:
- `web/chat/index.js` already has `startLiveSpeechPoll(...)`.
- the existing loop calls `controller.transcribeSpeech(...)` with `livePreviewOnly: true`.
- the existing loop uses `liveSpeechPollBusy` to suppress overlapping preview calls.
- the existing interval is 1200 ms and should be tuned only after correctness is confirmed.

Implemented refinements:
- `startLiveSpeechPoll(...)` now starts when either `audioPath` or the live `audioArtifact` is available, so preview can run before the final WAV path is available.
- each recording session gets a stable `speech-preview-<timestamp>` preview run id that is reused by all preview ticks.
- `controller.transcribeSpeech(...)` now accepts an optional `runId` for preview requests instead of always generating a new run id.
- `liveSpeechPollGeneration` invalidates late preview responses after stop/failure-like transitions.
- `livePreviewOnly` calls no longer emit a local queued update that clears interim transcript text before every poll.
- stale live preview responses are ignored after the speech state leaves active preview stages.
- low-quality interim preview text is ignored instead of failing the whole speech session; final transcription keeps the quality gate.

Recommended refinement approach:
- In WebView, keep the timer active while `speechSessionState.stage === "recording"` or `speechSessionState.stage === "streaming"`.
- At a conservative interval, call `controller.transcribeSpeech(...)` or an equivalent internal helper with:
  - current `audioArtifact` from the active native recording
  - `livePreviewOnly: true`
  - a short timeout
  - the same `sessionId`
  - a stable preview-specific `runId` or correlation id
- Do not send chat messages from preview calls.

Native MFC alternative:
- Add an MFC timer or async preview dispatch in `CBlazeClawMFCView` while `CVoiceRecorder` is recording.
- Call a preview-only speech request with the current `PcmStream` artifact.
- Post interim updates back to the UI thread.

Acceptance criteria:
- completed: interim recognition requests occur while recording is active through the existing WebView preview loop.
- completed: preview requests can run from the live artifact even before a final WAV path exists.
- completed: requests stop before final stop transcription and stale preview results are suppressed by generation checks.
- completed: preview inference remains asynchronous through the `CBlazeClawMFCView` speech RPC worker path and does not block the UI thread.

### Step 3: Ensure live preview requests use the ring-buffer PCM stream
Status: completed

Detailed findings and implementation notes:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP3_RING_PCM_STREAM.md`

Ensure preview requests do not wait for the final WAV file.

Files to review/update:
- `src/app/VoiceRecorder.h`
- `src/app/VoiceRecorder.cpp`
- `src/app/AudioRingBuffer.h`
- `src/app/AudioRingBuffer.cpp`
- `src/core/runtime/SpeechRecognition/StreamingAudioSourceRegistry.*`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

Implementation notes:
- Use `CVoiceRecorder::BuildStreamingAudioArtifact()` during recording.
- Ensure the artifact has `handoffMode: "pcm_stream"`, `streamId`, sample rate, channel count, bit depth, and sequence range.
- For live preview, keep `sequenceEnd` open-ended or use the latest available sequence according to the existing streaming contract.
- Resolve native recording artifacts through `GatewayHost::ResolveNativeRecordingArtifact(...)` when possible.

Implemented behavior:
- `CVoiceRecorder::BuildStreamingAudioArtifact()` now returns an open-ended live `PcmStream` artifact while recording by setting `sequenceEnd` to `0`.
- the live artifact is available immediately after recording starts, even before a final WAV path or captured sample range is available.
- stopped/final artifacts continue to use a finite `sequenceEnd`, preserving final transcription behavior.
- `GatewayHost::ResolveNativeRecordingArtifact(...)` now accepts both finite PCM ranges and open-ended live PCM ranges.
- `SpeechRecognitionRuntime::Transcribe(...)` now infers Sherpa `SpeechStreamingInputContract` from the provided PCM artifact, including `streamId`, sample rate, channels, bit depth, and open-ended `sequenceEnd`.

Acceptance criteria:
- completed: live preview requests can read from the ring-buffer PCM stream artifact rather than an incomplete WAV file.
- completed: preview can start before the user clicks stop because the recording start path can return an open-ended artifact.
- completed: WAV-file fallback and final finite-range transcription remain available after stop.

### Step 4: Emit interim segments from the coordinator
Update `SpeechTranscriptionCoordinator::Execute(...)` so streaming requests with a non-empty non-final segment update the tracked execution state and emit callbacks.

Current gap:
- The coordinator emits `SegmentFinalized` only when `result.sessionState.segment->final` is true.
- Non-final `SpeechTranscriptSegment` results are not promoted into a lifecycle callback as a live transcript update.

Planned behavior:
- If `isStreamingRequest` and `result.sessionState.segment` exists:
  - copy `segment` into current execution state
  - copy partial text into `transcriptText` when appropriate
  - emit `SpeechExecutionStage::Streaming` for `segment.final == false`
  - emit `SpeechExecutionStage::SegmentFinalized` for `segment.final == true`
- Preserve the existing terminal completion callback.

Acceptance criteria:
- Every preview decode with new text can produce a lifecycle update.
- Non-final text is distinguishable from final text.
- Duplicate unchanged interim text can be suppressed either here or in the UI layer.

### Step 5: Include segment data in gateway lifecycle payloads
Ensure lifecycle and response JSON include segment data for interim updates.

Files to review/update:
- `src/gateway/GatewayHost.cpp`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- any event fanout code used by `SpeechTranscriptionCoordinator::SetExecutionUpdateCallback(...)`

Payload shape should include:
```json
{
  "stage": "streaming",
  "sessionId": "...",
  "runId": "...",
  "text": "latest partial transcript",
  "segment": {
	"text": "latest partial transcript",
	"final": false,
	"sequence": 3
  },
  "latencyMs": 123,
  "audioPath": "...",
  "language": "..."
}
```

Acceptance criteria:
- `speech.lifecycle` events carry interim segment text.
- `speech.transcribe` preview responses carry the same normalized segment data.
- Final responses remain backward compatible with existing chat send behavior.

### Step 6: Render live recognition in the WebView GUI
Update the WebView chat UI to show interim transcript text clearly while recording.

Files to review/update:
- `web/chat/chat-controller.js`
- `web/chat/index.js`
- `web/chat/index.css`

Recommended UX:
- Add a dedicated live transcript preview area near the composer or voice button.
- Show states such as:
  - `Listening...`
  - `Recognizing... <interim text>`
  - `Finalizing...`
- Style interim text differently from final text, for example muted/italic.
- Keep preview text out of the message list until final transcript acceptance.

Acceptance criteria:
- Users see changing transcript text while speaking.
- Interim text is replaced in place rather than appended repeatedly.
- Empty interim results do not flicker the UI.
- Final transcript can still be sent through the existing chat path.

### Step 7: Render live recognition through CBlazeClawMFCView
Route native live recognition updates through `CBlazeClawMFCView`, which is the current output surface for speech recognition results.

Files to review/update:
- `src/app/BlazeClawMFCView.h`
- `src/app/BlazeClawMFCView.cpp`

Recommended native behavior:
- Track the last live segment sequence/text in `CBlazeClawMFCView`.
- Reuse the existing WebView/native bridge output path to emit live `speech.lifecycle` or equivalent payloads.
- Update the existing speech-recognition result display in place for interim text.
- Replace the interim text with final text on completion.
- Avoid appending one output item per partial update.

Acceptance criteria:
- Users see interim recognition from the current `CBlazeClawMFCView` speech output path while recording.
- Interim updates are coalesced instead of appended repeatedly.
- UI updates are posted to the MFC UI thread and ignored after the view is destroyed.

### Step 8: Prevent overlapping preview inference from piling up
Add concurrency protection for preview requests.

Implementation options:
- Allow only one preview request in flight per speech session.
- If a preview is still running when the next timer fires, skip that tick.
- Drop stale preview responses based on `runId`, `sequence`, or a monotonically increasing preview generation.

Acceptance criteria:
- Slow inference does not create an unbounded backlog.
- Stale partial text cannot overwrite newer text.
- Stop/final transcription cancels or ignores outstanding preview results.

### Step 9: Finalize cleanly on stop
When the user stops recording:
- Stop the preview loop.
- Keep the last interim text visible as `Finalizing...`.
- Run the existing final transcription path using the final artifact/WAV fallback.
- Replace interim text with the accepted final transcript.
- Clear preview state after successful chat send or after a short terminal-state delay.

Acceptance criteria:
- The final transcript is authoritative.
- Interim-only text is not sent if final transcription fails quality checks.
- The UI does not regress to blank while final transcription is running.

### Step 10: Add diagnostics and tests
Add focused coverage for live updates.

Recommended tests:
- Coordinator emits `Streaming` callback for non-final streaming segment.
- Coordinator emits `SegmentFinalized` for final streaming segment.
- WebView controller applies `speech.lifecycle` interim payload without sending chat.
- Preview loop suppresses overlapping requests.
- Stop path ignores stale preview responses and sends only final text.

Existing relevant tests to extend or mirror:
- `tests/SpeechRecognitionRealtimeStreamingTests.cpp`
- `tests/VoiceRecorderRingArtifactIntegrationTests.cpp`
- `tests/SherpaStep8BaselineToolTests.cpp`

Diagnostics:
- Add trace logs for preview request start/end, skipped ticks, segment sequence, and final replacement.
- Avoid logging full transcript content unless existing diagnostics already allow it.

## Rollout Strategy

### Phase 1: Minimal live preview
- Use WebView polling with `livePreviewOnly`.
- Use existing `PcmStream` artifact and `SpeechTranscriptSegment` contract.
- Render interim text in one preview area.

### Phase 2: Coordinator event improvements
- Promote non-final streaming segments to lifecycle callbacks.
- Reduce reliance on response polling if event fanout is sufficient.

### Phase 3: Native MFC parity
- Add or refine native `CBlazeClawMFCView` interim display if required.
- Share behavior and state naming with the WebView path.

### Phase 4: Performance tuning
- Tune preview interval.
- Suppress duplicate text updates.
- Add adaptive backoff if inference is slower than the polling interval.

## Risks and Mitigations

- **Risk: Preview requests contend with final transcription.**
  - Mitigation: stop preview loop before final transcription; enforce one in-flight request per session.

- **Risk: Existing coordinator duplicate-session protection rejects preview calls.**
  - Mitigation: use a dedicated preview execution path or update coordinator semantics to allow short preview requests for the active recording session while still rejecting overlapping final runs.

- **Risk: Interim Sherpa output may be unstable.**
  - Mitigation: render it as interim/muted text and only send final accepted transcript.

- **Risk: UI flicker from empty or repeated partials.**
  - Mitigation: preserve previous text on empty updates and ignore unchanged segment text/sequence.

- **Risk: Ring-buffer retention window is exceeded for long utterances.**
  - Mitigation: use current sequence cursors and VAD/finalization boundaries; keep preview cadence below retention limits.

## Definition of Done
- While speaking, users see live Sherpa Zipformer recognition text update in the GUI.
- Interim text is visibly distinct from final text.
- Final transcript behavior remains compatible with the existing chat send flow.
- No UI-thread blocking is introduced.
- Preview requests are bounded and do not pile up.
- Stop/cancel/destroy paths do not leave background work updating destroyed UI.
- Relevant coordinator, gateway, and UI tests pass.
