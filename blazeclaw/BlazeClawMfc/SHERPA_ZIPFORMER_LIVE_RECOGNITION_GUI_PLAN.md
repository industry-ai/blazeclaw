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
- `BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false` now provides a one-switch A/B
  bypass for the WebView live preview polling loop while preserving final
  stop-recording transcription.
- `startLiveSpeechPoll(...)` now starts when either `audioPath` or the live `audioArtifact` is available, so preview can run before the final WAV path is available.
- each recording session gets a stable `speech-preview-<timestamp>` preview run id that is reused by all preview ticks.
- `controller.transcribeSpeech(...)` now accepts an optional `runId` for preview requests instead of always generating a new run id.
- final stop transcription now uses a distinct `speech-final-<timestamp>` run id, so WebView diagnostics can separate the final request from the earlier preview poll series.
- `chat-controller.js::transcribeSpeech(...)` emits `[speech-request-trace]` diagnostics for request start, accepted responses, ignored stale preview responses, and request errors, including request type, session/run ids, stage, and audio artifact sequence metadata.
- the native `speech.transcribe` bridge path emits visible `speech.request.trace` / `[Speech][RequestTrace]` lines so preview/final identity evidence appears in normal Visual Studio logs, not only in WebView developer tools.
- `CBlazeClawMFCView` now emits ordered `speech.bridge.order` / `[Speech][BridgeOrder]` diagnostics for start-recording completion, preview/final transcribe dispatch, stop-recording completion, async transcribe completion, and speech lifecycle emit/suppress decisions.
- `SpeechTranscriptionCoordinator` now emits metadata-only accept/execute busy-state diagnostics showing request type, tracking run id, session id, active mapped run id, active stage, streaming flag, artifact sequence range, and rejection reason without logging transcript content.
- `liveSpeechPollGeneration` invalidates late preview responses after stop/failure-like transitions.
- `livePreviewOnly` calls no longer emit a local queued update that clears interim transcript text before every poll.
- stale live preview responses are ignored after the speech state leaves active preview stages.
- `chat-controller.js` now uses a state-aware final-authority guard so
  `speech-preview-*` lifecycle or async response updates are accepted only while
  the previous state is still actively recording/streaming, or queued with a
  preview run id. After stop/final transcription starts, late preview updates are
  ignored and traced as `speech.request.lifecycle_ignored` with
  `final_authority_active`.
- low-quality interim preview text is ignored instead of failing the whole speech session; final transcription keeps the quality gate.

Preview enablement:
- Preview is on by default when `streamingPreviewEnabled` is true.
- If the A/B bypass was enabled, remove/comment
  `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false` in
  `BlazeClawMfc/blazeclaw.conf` and restart BlazeClaw.
- Optionally set `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=true` to make the
  enablement explicit.
- The WebView speech status shows `preview=off` only when preview is disabled.
- `speech.capabilities.get` resolves the live-preview toggle dynamically from
  `BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED` and exposes
  `livePreviewToggleEnabled` / `livePreviewToggleSource` so disabled-preview
  runs can be verified without relying only on UI text.
- WebView preview polling is fail-closed: `startLiveSpeechPoll(...)` starts
  preview only when capabilities explicitly report `streamingPreviewEnabled=true`.
  Missing/unloaded capability state is treated as preview disabled so final-only
  recording remains safe.
- Preview terminal lifecycle events are scoped to the preview RPC and no longer
  make the UI leave recording mode: `speech-preview-*` `completed` or
  `segment_finalized` states keep the Transcribe button in
  `Recording... (click to stop)` until the user explicitly stops recording.
- Final dispatch now establishes `speech-final-*` authority in WebView state
  before calling final transcription. Any later `speech-preview-*` lifecycle
  event is ignored, so stale preview text cannot replace or send as final text
  after stop/final begins.
- Final chat submission is now final-response-owned: only `payload.text` or
  `payload.transcript` from the final speech response can be sent to chat.
  Preview/session fallback text remains valid for live preview rendering, but
  is not reused as the final user prompt.
- Final-response ownership includes nested final fields
  (`speechSession.text`, `speechSession.transcript`, and
  `speechSession.segment.text`) when the response run is not preview-owned.
  If no final-owned transcript is present, the UI enters a terminal
  `missing_final_transcript` failed state so the Transcribe button can reset.
- The `missing_final_transcript` failed state explicitly clears prior preview
  text and reports extraction diagnostics (`finalTextSource`, ownership, and
  top-level/nested/segment text flags), preventing stale preview text from
  appearing under `Recognition failed`.
- Native Sherpa finalization now preserves compatible live preview stream state
  instead of unconditionally resetting decoder/token state before final drain.
  This allows preview-decoded tokens to become final-owned response text while
  still resetting empty or out-of-range cached state.
- When preview is disabled, the dedicated live preview box now shows
  `Live preview disabled` and `Preview is off; final transcription will run
  after stop.` during recording/finalizing, so users do not need to infer the
  preview state from the adjacent `Abort` button.

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
- completed: WebView diagnostics expose preview/final request identities and stale-response decisions without logging transcript content.
- completed: native bridge diagnostics expose request type, session/run ids, streaming flag, and artifact sequence range for each `speech.transcribe` request.
- completed: native bridge ordering diagnostics expose whether preview workers complete after stop/final begins and whether any lifecycle event is emitted or suppressed before WebView delivery.
- completed: coordinator diagnostics expose whether final transcription is accepted independently or rejected because a preview/final run is still mapped as in-flight for the session.

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
- stopped/final artifacts use a preserved recording start sequence and finite `sequenceEnd`, preventing final transcription from starting at a later ring-buffer oldest sequence after the buffer advances.
- `CVoiceRecorder` now emits `[VoiceRecorder][artifact.preview]` and `[VoiceRecorder][artifact.final]` diagnostics with stream id, sequence range, oldest/latest ring sequence, sample rate, channel count, duration, and start-before-oldest risk metadata.
- `GatewayHost::ResolveNativeRecordingArtifact(...)` now accepts both finite PCM ranges and open-ended live PCM ranges.
- `SpeechRecognitionRuntime::Transcribe(...)` now infers Sherpa `SpeechStreamingInputContract` from the provided PCM artifact, including `streamId`, sample rate, channels, bit depth, and open-ended `sequenceEnd`.
- `SherpaZipformerStreamingEngine::TranscribeStreaming(...)` now emits `[SherpaStreaming][final.start]` and `[SherpaStreaming][final.summary]` diagnostics for finite final requests, including cached reset status, requested/effective start, oldest-sequence clamp status, final cursor, drain status, chunk/loop counts, decoded text, and final outcome.

Acceptance criteria:
- completed: live preview requests can read from the ring-buffer PCM stream artifact rather than an incomplete WAV file.
- completed: preview can start before the user clicks stop because the recording start path can return an open-ended artifact.
- completed: final stop artifacts preserve the recording start sequence and expose a finite PCM range for final transcription.
- completed: Sherpa final decode diagnostics prove whether final transcription starts at the finite artifact range and drains to `sequenceEnd` independently of preview cursor state.
- completed: realtime streaming regression coverage now asserts preview-plus-final flow drains the finite final range, advances the final cursor to `sequenceEnd`, and keeps final decoded text aligned with the Sherpa final result when present.
- completed: WAV-file fallback and final finite-range transcription remain available after stop.

### Step 4: Emit interim segments from the coordinator
Status: completed

Detailed findings and implementation notes:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP4_COORDINATOR_INTERIM_SEGMENTS.md`

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

Implemented behavior:
- `SpeechTranscriptionCoordinator::Execute(...)` now promotes every streaming result with `result.sessionState.segment` into an execution update.
- non-final segments emit a `SpeechExecutionStage::Streaming` callback with the segment copied into `SpeechExecutionState::segment`.
- final segments emit `SpeechExecutionStage::SegmentFinalized`, then preserve the existing `Stopped` update and terminal completion callback behavior.
- segment text, transcript text, audio artifact, streaming input, language, latency, and error fields flow through the existing `BuildState(...)` mapping.

Acceptance criteria:
- completed: every streaming result containing a segment can produce a coordinator execution callback.
- completed: non-final text is distinguishable by `SpeechExecutionStage::Streaming` and `segment.final == false`.
- completed: final text is distinguishable by `SpeechExecutionStage::SegmentFinalized` and `segment.final == true`.
- completed: duplicate suppression is left to downstream gateway/UI logic so no valid interim callback opportunity is lost.

### Step 5: Include segment data in gateway lifecycle payloads
Status: completed

Detailed findings and implementation notes:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP5_GATEWAY_SEGMENT_PAYLOADS.md`

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
- completed: `speech.lifecycle` events emitted from `CBlazeClawMFCView` can carry interim segment text through normalized `speechSession.segment` data.
- completed: `speech.transcribe` preview responses carry normalized segment data at top level, under `speechSession`, and under `executionState`.
- completed: busy/in-flight speech responses preserve segment data when available.
- completed: gateway lifecycle, segment, and coordinator execution-update telemetry include segment text and metadata.
- completed: final responses remain backward compatible with existing `speechSession`, `speechArtifact`, `transcriptInjection`, and chat send behavior.

### Step 6: Render live recognition in the WebView GUI
Status: completed

Detailed findings and implementation notes:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP6_WEBVIEW_LIVE_PREVIEW.md`

Update the WebView chat UI to show interim transcript text clearly while recording.

Files to review/update:
- `web/chat/chat-controller.js`
- `web/chat/index.js`
- `web/chat/index.css`
- `web/chat/index.html`

Implemented behavior:
- added a dedicated `speechLivePreview` region between the message list and composer.
- `web/chat/index.js` now renders the current `speechSessionState.segmentText` or `speechSessionState.text` into that region whenever speech/composer state refreshes.
- live text is replaced in the same DOM node, so interim updates do not append duplicate messages.
- recording, recognizing, finalizing, final, failure, and cancellation states have explicit labels and CSS classes.
- interim/listening/finalizing text is styled as muted italic, while final and error states have distinct border/background colors.
- the preview remains separate from the message list and does not alter the existing final transcript send path.

Recommended UX:
- Add a dedicated live transcript preview area near the composer or voice button.
- Show states such as:
  - `Listening...`
  - `Recognizing... <interim text>`
  - `Finalizing...`
- Style interim text differently from final text, for example muted/italic.
- Keep preview text out of the message list until final transcript acceptance.

Acceptance criteria:
- completed: users see changing transcript text while speaking when preview or lifecycle state carries segment text.
- completed: interim text is replaced in place rather than appended repeatedly.
- completed: empty idle interim results do not flicker the UI; active empty states use stable `Speak now` text.
- completed: final transcript can still be sent through the existing chat path because preview rendering is separate from composer injection and `messages`.

### Step 7: Render live recognition through CBlazeClawMFCView
Status: completed

Detailed findings and implementation notes:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP7_NATIVE_VIEW_LIFECYCLE.md`

Route native live recognition updates through `CBlazeClawMFCView`, which is the current output surface for speech recognition results.

Files to review/update:
- `src/app/BlazeClawMFCView.h`
- `src/app/BlazeClawMFCView.cpp`

Implemented behavior:
- `CBlazeClawMFCView` now tracks the active live speech session key, run id, last segment text, and last segment sequence.
- `EmitSpeechLifecycleEvent(...)` now routes payloads through `ShouldEmitSpeechLifecycleEvent(...)` before forwarding to `BridgeEventTopic::SpeechLifecycle`.
- duplicate non-final `streaming` segment payloads for the same session/run/text/sequence are suppressed in the native view.
- status, empty streaming, final, and terminal payloads remain emitted so the WebView can show `Listening...`, `Recognizing...`, `Finalizing...`, and final/error states.
- terminal stages reset native coalescing state so the next recording starts cleanly.
- asynchronous speech RPC results still post to `CBlazeClawMFCView::OnSpeechRpcCompleted(...)` before bridge emission, preserving the existing UI-thread/native view output path.

Recommended native behavior:
- Track the last live segment sequence/text in `CBlazeClawMFCView`.
- Reuse the existing WebView/native bridge output path to emit live `speech.lifecycle` or equivalent payloads.
- Update the existing speech-recognition result display in place for interim text.
- Replace the interim text with final text on completion.
- Avoid appending one output item per partial update.

Acceptance criteria:
- completed: users see interim recognition from the current `CBlazeClawMFCView` speech lifecycle bridge output path while recording.
- completed: duplicate interim updates are coalesced instead of appended or forwarded repeatedly.
- completed: speech RPC UI updates remain posted through the MFC view message path, with existing owned-payload cleanup for stale view posts.

### Step 8: Prevent overlapping preview inference from piling up
Status: completed

Detailed findings and implementation notes:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP8_PREVIEW_CONCURRENCY.md`

Add concurrency protection for preview requests.

Implemented behavior:
- the WebView preview loop keeps the existing one-in-flight `liveSpeechPollBusy` guard and now tracks the active preview run id with `liveSpeechPollInFlightRunId`.
- `stopLiveSpeechPoll()` increments the preview generation, clears the timer, clears busy state, and clears in-flight preview ownership before final transcription starts.
- `chat-controller.js` now identifies active preview stages and `speech-preview-*` run ids consistently.
- stale non-final `streaming` updates are rejected when they arrive after active preview stages, belong to an older preview run id, switch session ids, regress segment sequence, or repeat the same sequence/text.
- live-preview transcription timeout/failure no longer marks the whole speech session failed; explicit final transcription remains authoritative for user-visible errors.

Implementation options:
- Allow only one preview request in flight per speech session.
- If a preview is still running when the next timer fires, skip that tick.
- Drop stale preview responses based on `runId`, `sequence`, or a monotonically increasing preview generation.

Acceptance criteria:
- completed: slow inference does not create an unbounded backlog because preview ticks are skipped while a request is busy.
- completed: stale partial text cannot overwrite newer text because lifecycle and preview response merges are filtered by active stage, preview run id, session id, sequence, and duplicate text.
- completed: stop/final transcription cancels or ignores outstanding preview results through generation invalidation, in-flight run clearing, and active-stage stale response checks.

### Step 9: Finalize cleanly on stop
Status: completed

Detailed findings and implementation notes:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP9_CLEAN_STOP_FINALIZATION.md`

When the user stops recording:
- Stop the preview loop.
- Keep the last interim text visible as `Finalizing...`.
- Run the existing final transcription path using the final artifact/WAV fallback.
- Replace interim text with the accepted final transcript.
- Clear preview state after successful chat send or after a short terminal-state delay.

Implemented behavior:
- stop handling still invalidates the preview loop before requesting the final recording artifact.
- the last interim transcript is carried into the `stopped` lifecycle update so the preview remains visible as `Finalizing...`.
- `applySpeechLifecycleUpdate(...)` preserves previous interim text during `queued`, `stopped`, `transcribing`, and `failed` status transitions when no newer text is available.
- the non-preview final transcription path remains the only path that calls `sendPayload(...)` for voice text.
- final transcript quality checks still block rejected final text before chat send, with no interim fallback send.
- after successful final send, the preview is marked `completed` with the accepted transcript, then cleared after a short run-id-guarded delay.

Acceptance criteria:
- completed: the final transcript is authoritative because only the final non-preview transcription path sends voice text.
- completed: interim-only text is not sent if final transcription fails quality checks.
- completed: the UI does not regress to blank while final transcription is running because finalizing states preserve the last interim text.

### Step 10: Add diagnostics and tests
Status: completed

Detailed findings and implementation notes:
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_STEP10_DIAGNOSTICS_AND_TESTS.md`

Add focused coverage for live updates.

Implemented behavior:
- `web/chat/index.js` now emits metadata-only preview diagnostics for request start/end, skipped busy ticks, stale generations, inactive-stage stops, and preview errors.
- preview diagnostics include run id, generation, stage, segment sequence, audio path/artifact presence, text presence, effective speech execution provider, CUDA availability/enabled flags, and CUDA fallback reason without logging transcript content.
- preview diagnostics now also include first-token timing checkpoints for click,
  start-recording response, preview request/response, native first readable and
  accepted audio, encoder/decoder/joiner start, first partial text, native
  payload readiness, gateway payload readiness, and first WebView render.
- `web/chat/chat-controller.js` now emits `speech.final.replacement` operator diagnostics after successful authoritative final transcript replacement.
- `window.BlazeClawChatController.runRegressionChecks()` now covers interim lifecycle updates without chat append, stale preview suppression, final-only speech send, and final replacement diagnostics.
- existing `tests/SpeechRecognitionRealtimeStreamingTests.cpp` coverage was reviewed for Sherpa streaming segment, sequence catch-up, and cancellation behavior.
- live speech gateway payloads now carry `speechRuntime` provider diagnostics through `gateway.speech.startRecording`, `gateway.speech.stopRecording`, `speech.transcribe`, nested speech session payloads, and speech lifecycle telemetry so WebView and logs can identify CPU vs CUDA use for each live session.
- live speech gateway payloads and startup status logs now also carry Sherpa hot
  warmup and model-load diagnostics, including warmup completion/success,
  provider, stage, latency, error, and model-load stage/latency. Warmup uses an
  isolated dummy PCM stream through the same Sherpa streaming engine path as live
  preview recognition.
- live preview PCM stream contracts now use configurable low-latency preview
  chunk policy only for `livePreviewOnly=true` requests. The first CPU tuning
  trial uses a 500 ms preview chunk with 320 ms lookback while final
  transcription keeps the broader 1500 ms chunk and 320 ms overlap policy.
- non-final Sherpa preview segments now remain in the native `streaming`
  lifecycle stage after the coordinator emits them, and WebView normalization
  treats `speech-preview-*` streaming updates as interim so partial text can be
  shown while the Transcribe button remains in the active recording state.
- WebView live-preview polling now uses a 150 ms recursive timeout fallback with
  a 50 ms busy retry instead of the previous fixed 1200 ms interval. The
  scheduler avoids overlapping preview requests while preserving stale-generation
  and inactive-stage guards.
- CUDA-vs-CPU live-preview benchmark summaries are now supported by
  `tools/speech/Invoke-SherpaProviderBenchmarkSummary.ps1`, which converts
  captured startup and WebView speech diagnostic logs into JSON and Markdown
  reports for provider, CUDA readiness, model-load, warmup, first-token,
  click-to-render, and steady-state partial interval comparison.
- CPU fallback tuning is now supported by
  `tools/speech/New-SherpaCpuFallbackTuningMatrix.ps1`, which generates trial
  overlays for preview chunk size, preview lookback, `speech.threads`, and
  `speech.execution_mode` while forcing CPU mode with `speech.cuda.enabled=false`.
  The summary reports include thread count and execution mode so fallback tuning
  decisions are tied to measured first-token and click-to-render latency.
- Step 9 manual validation is now supported by
  `tools/speech/New-SherpaFirstTokenValidationChecklist.ps1` and
  `tools/speech/Test-SherpaFirstTokenBenchmarkSummary.ps1`, so CUDA active, CPU
  fallback, cold first utterance, warm second utterance, Chinese short utterance,
  and English short utterance runs can be checked against the same required
  provider, warmup, first-token, and WebView timing fields.
- Bad-result Step 9 manual validation is now supported by
  `tools/speech/SHERPA_ZIPFORMER_LIVE_RECOGNITION_STEP9_MANUAL_CHECKLIST.md`.
  It covers preview-disabled A/B behavior, preview-enabled Chinese final quality,
  quick-stop stale preview suppression, English sanity validation, and
  cancel/failure handling without sending preview-only chat messages.

Recommended tests:
- Coordinator emits `Streaming` callback for non-final streaming segment.
- Coordinator emits `SegmentFinalized` for final streaming segment.
- WebView controller applies `speech.lifecycle` interim payload without sending chat.
- Coordinator keeps non-final preview segment updates in `Streaming` instead of
  sending a terminal completed update.
- Preview loop suppresses overlapping requests.
- Preview loop runs with the bounded low-latency recursive scheduler and keeps
  stale generation protection.
- Stop path ignores stale preview responses and sends only final text.

Existing relevant tests to extend or mirror:
- `tests/SpeechRecognitionRealtimeStreamingTests.cpp`
- `tests/VoiceRecorderRingArtifactIntegrationTests.cpp`
- `tests/SherpaStep8BaselineToolTests.cpp`
- `tests/GatewaySpeechPhase56ParityTests.cpp`

Diagnostics:
- Add trace logs for preview request start/end, skipped ticks, segment sequence, and final replacement.
- Include runtime provider diagnostics with live speech lifecycle payloads: `provider`, `effectiveExecutionProvider`, `cudaExecutionProviderAvailable`, `cudaExecutionProviderEnabled`, `cudaExecutionProviderReason`, chunk/lookback, threads, and execution mode.
- Include first-token timing diagnostics with live speech lifecycle payloads and
  WebView preview diagnostics through the `firstTokenTiming` object and the
  correlated `speech-preview-*` run id.
- Include hot runtime warmup diagnostics in `speechRuntime` payloads so live
  preview traces can distinguish first-token latency from model load, session
  creation, graph optimization, CUDA initialization, and dummy streaming warmup.
- Include streaming latency profile diagnostics in `speechRuntime` payloads:
  `streamingLatencyProfile`, `streamingPreviewChunkMs`, and
  `streamingPreviewLookbackMs`, alongside existing chunk/lookback, provider,
  thread, and execution-mode fields.
- Include WebView poll cadence diagnostics through `speech.preview.poll_config`,
  request `intervalMs`, and busy `retryMs` so render delay can be correlated with
  native first-partial timing.
- Summarize CPU/CUDA benchmark captures with
  `tools/speech/Invoke-SherpaProviderBenchmarkSummary.ps1`; pass
  `-RequireCudaActive` for CUDA runs so mixed or inactive CUDA runtime stacks are
  flagged before the results are used for provider policy decisions.
- Generate CPU fallback tuning overlays with
  `tools/speech/New-SherpaCpuFallbackTuningMatrix.ps1`, then summarize each
  captured trial with `Invoke-SherpaProviderBenchmarkSummary.ps1` so the chosen
  fallback profile records `speechThreads`, `speechExecutionMode`, preview chunk,
  first partial, click-to-render, and steady-state interval metrics.
- Generate Step 9 manual checklists with
  `tools/speech/New-SherpaFirstTokenValidationChecklist.ps1` and validate
  captured benchmark summaries with
  `tools/speech/Test-SherpaFirstTokenBenchmarkSummary.ps1` before accepting a
  first-token latency regression or improvement claim.
- Use
  `tools/speech/SHERPA_ZIPFORMER_LIVE_RECOGNITION_STEP9_MANUAL_CHECKLIST.md`
  for the bad-result fix validation matrix. Capture preview/final run ids,
  final artifact sequence range, Sherpa final drain status, final visible
  transcript, chat-send state, and any late-preview ignored reason.
- Avoid logging full transcript content unless existing diagnostics already allow it.

Acceptance criteria:
- completed: WebView controller applies `speech.lifecycle` interim payloads without sending chat messages in regression coverage.
- completed: preview stale/duplicate behavior is covered by run/sequence-based regression checks.
- completed: stop/finalization sends only authoritative final text in regression coverage.
- completed: diagnostics cover preview request start/end/skips/stale/error and final replacement without logging transcript content.
- completed: diagnostics expose the actual speech runtime provider and CUDA fallback reason in WebView status/preview diagnostics and native gateway speech payloads.
- completed: diagnostics expose first-token latency checkpoints across native
  Sherpa streaming, gateway payload emission, and WebView first-render events.
- completed: diagnostics expose hot runtime warmup completion/success/failure
  and model-load stage/latency in startup logs and gateway speech payloads.
- completed: live preview chunk tuning is visible in startup logs and gateway
  speech payloads without changing the final transcription chunk/overlap policy.
- completed: non-final partial preview text is propagated as active streaming
  state so the WebView can render interim text without resetting the recording
  button.
- completed: live preview polling uses a 150 ms low-latency fallback scheduler
  with stale-session protection and no overlapping preview requests.
- completed: CPU/CUDA live-preview benchmark summaries can be generated from
  captured diagnostics without assuming that CUDA is faster or active.
- completed: CPU fallback tuning has a repeatable trial matrix and summaries that
  include thread count and execution mode for first-token latency comparison.
- completed: provider, CUDA fallback, startup, first-token, and WebView timing
  diagnostic contracts are guarded by regression tests and manual validation
  scripts.

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
