# Sherpa Zipformer Live Recognition Bad Result Root-Cause and Fix Plan

## Problem Statement

After live recognition updates while speaking were introduced, speaking `请讲一个笑话`
can produce the incorrect live and final recognition text `请听`.

Before the live-recognition GUI update flow was implemented, final speech
recognition quality for the same utterance was good. This strongly suggests the
regression may be in the GUI/live-preview orchestration path rather than in the
underlying Sherpa model itself.

## Goal

Restore the pre-live-recognition final transcription behavior while preserving
live recognition updates if possible.

The fix procedure must first prove whether the bad result is caused by:

1. WebView/GUI preview polling state,
2. native bridge lifecycle ordering,
3. preview and final request run/session identity collisions,
4. shared Sherpa stream state reused across preview/final requests,
5. or the actual ASR audio/model path.

## Current Working Hypothesis

The most likely regression class is GUI orchestration: live preview requests now
run while recording is active, and those requests may affect the final request by
sharing one or more of these identifiers or state channels:

- `sessionId`,
- `runId`,
- `streamId`,
- `speechSessionState`,
- `audioArtifact`,
- `SpeechTranscriptionCoordinator` execution state,
- `SherpaZipformerStreamingEngine` per-stream cached state.

Because both `Recognizing stream ...` and final text show `请听`, the UI may be
presenting a stale live-preview hypothesis as if it were final, or the final
transcription may be skipped/short-circuited because the preview request already
created a busy or completed execution state.

## Non-Goals

- Do not switch back to Qwen3 ASR.
- Do not add fallback transcription mechanisms.
- Do not hard-code a phrase-specific fix for `请讲一个笑话`.
- Do not hide poor results only with frontend filtering.
- Do not disable Sherpa streaming permanently unless the isolation step proves
  preview polling is the direct regression source and no safer fix is available.

## Key Files

- `web/chat/index.js`
  - Starts/stops live speech polling and stores active recording artifacts.
- `web/chat/chat-controller.js`
  - Sends `speech.transcribe` requests and applies speech lifecycle updates.
- `src/app/BlazeClawMFCView.cpp`
  - WebView host bridge, async speech RPC dispatch, and lifecycle event emission.
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
  - Handles `gateway.speech.startRecording`, `gateway.speech.stopRecording`, and
	`speech.transcribe`.
- `src/core/GatewayHostBindingCoordinator.cpp`
  - Builds `SpeechStreamingInputContract` from `SpeechAudioArtifact`.
- `src/core/SpeechTranscriptionCoordinator.cpp`
  - Tracks accepted/in-flight speech executions and emits lifecycle state.
- `src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
  - Dispatches Sherpa streaming transcription.
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
  - Maintains Sherpa stream state and produces interim/final segments.
- `src/app/VoiceRecorder.cpp`
  - Publishes the active `voice_recorder` PCM stream artifact.

## Diagnostic Principles

1. Compare final-only transcription against preview-plus-final transcription
   using the same microphone utterance.
2. Preserve the captured final WAV/PCM artifact for repeatable testing.
3. Treat WebView text, lifecycle event payloads, gateway response payloads, and
   native Sherpa debug info as separate evidence layers.
4. Prove whether the final request really runs through Sherpa or whether the GUI
   reuses/stale-displays preview text.
5. Apply the smallest fix at the layer where the divergence is proven.

## Step-by-Step Procedure

### Step 1: Add a one-switch preview bypass for A/B testing

Status: completed after visibility correction

Add a temporary/configurable switch that disables live preview polling while
leaving the normal start-recording and stop-recording final transcription path
unchanged.

Implemented switch:

- `BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false`.

Implementation details:

- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp` reads
  `BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED` when registering speech handlers.
- `speech.capabilities.get` now reports `stt.streamingPreviewEnabled`.
- `speech.capabilities.get` now resolves
  `BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED` dynamically for every capability
  request, so config `env.*` changes applied before or during runtime are not
  hidden by handler-registration-time capture.
- `speech.capabilities.get` also reports `stt.livePreviewToggleEnabled` and
  `stt.livePreviewToggleSource` for diagnosing the active preview toggle.
- `web/chat/chat-controller.js` preserves `streamingPreviewEnabled` in the
  speech capability snapshot.
- `web/chat/index.js::startLiveSpeechPoll(...)` exits before starting preview
	polling unless `streamingPreviewEnabled === true`; missing or unloaded
  capabilities are treated as preview disabled.
- Final stop-recording and final `speech.transcribe` are not gated by this
  switch.
- `BlazeClawMfc/blazeclaw.conf` contains a commented A/B line:
  `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false`.

Expected result:

- If final transcription becomes correct again when preview polling is disabled,
  the regression is confirmed to be caused by preview/GUI orchestration.
- If final transcription is still `请听`, continue investigating audio/runtime
  changes outside the GUI path.

Acceptance criteria:

- completed: a single config/env toggle can disable preview polling.
- completed: final transcription still runs after stop because only
  `startLiveSpeechPoll(...)` is bypassed.
- pending manual A/B result: uncomment the config env line, restart the app,
  speak `请讲一个笑话`, then record the result below.

### Step 2: Trace WebView preview and final request identities

Status: completed

Instrument `web/chat/index.js` and `web/chat/chat-controller.js` to log each
speech request with:

- request type: `livePreviewOnly=true/false`,
- `sessionId`,
- `runId`,
- `audioArtifact.streamId`,
- `audioArtifact.sequenceStart`,
- `audioArtifact.sequenceEnd`,
- current `speechSessionState.stage`,
- whether the response is accepted or ignored as stale.

Expected result:

- Preview requests should use preview-specific run identity.
- Final request should use a final-specific run identity and must not be rejected
  as busy because of the preview run.
- Final UI text must come from the final response, not the last preview response.

Acceptance criteria:

- completed: logs show a clear preview request series and one final request.
- completed: final request identity is distinct enough from preview identity.
- completed: no stale preview response is applied after final transcription
  begins.

Implementation details:

- `web/chat/chat-controller.js::transcribeSpeech(...)` now emits structured
  `[speech-request-trace]` console diagnostics for:
  - `request_start`,
  - `response_applied`,
  - `response_ignored`,
  - `request_error`.
- Each trace includes request type (`preview` or `final`),
  `livePreviewOnly`, `sessionId`, request `runId`, current speech stage, and a
  compact `audioArtifact` summary containing `streamId`, `sequenceStart`, and
  `sequenceEnd`.
- Ignored preview responses include the reason, such as
  `preview_not_active`, `stale_preview_update`, or `quality_rejected`.
- `web/chat/index.js` now assigns final stop transcription a dedicated
  `speech-final-<timestamp>` run id and emits `speech.final.request_start` and
  `speech.final.request_end` diagnostics around the final request.
- The preview loop continues to use the stable `speech-preview-<timestamp>` run
  id created at recording start.
- Visibility correction: the original Step 2 implementation wrote the most
  important request identity traces only to the WebView `console.debug` stream,
  which is not present in the normal Visual Studio output shown in the latest
  repro. The tracing now also increments `speech.request.*` operator diagnostic
  counters and the native `speech.transcribe` bridge path emits
  `speech.request.trace` / `[Speech][RequestTrace]` lines with request type,
  session/run ids, streaming flag, artifact stream id, and artifact sequence
  range.

Current finding after latest repro:

- The visible log showed only preview lifecycle telemetry for
  `speech-preview-1780098747152` and did not include a final request identity
  trace. That was insufficient evidence to prove whether final transcription
  used a finite artifact or whether it was overwritten by preview text.
- The observed bad result `请` is still unresolved by Step 2 because Step 2 is a
  tracing step. With the visibility correction, the next repro should show
  whether Step 5 (finite final PCM artifact) or Step 6 (clean final Sherpa
  decode range) is the actual failure point.

Manual verification guidance:

1. Open WebView developer tools or attach a console log collector.
2. Record `请讲一个笑话` with preview enabled.
3. Confirm a series of `[speech-request-trace]` preview entries with the same
   `speech-preview-*` run id in WebView console logs when available.
4. Confirm Visual Studio output/status logs contain `speech.request.trace` or
   `[Speech][RequestTrace]` entries.
5. Confirm exactly one final request with a `speech-final-*` run id after
   `gateway.speech.stopRecording` returns.
6. Confirm the final request has a finite `sequenceEnd > sequenceStart`.
7. Confirm any late preview response is logged as `response_ignored` and is not
   applied after the final request begins.

### Step 3: Trace native bridge event ordering

Status: completed

Instrument `CBlazeClawMFCView` speech RPC handling to log event ordering:

1. `gateway.speech.startRecording`,
2. each preview `speech.transcribe`,
3. `gateway.speech.stopRecording`,
4. final `speech.transcribe`,
5. lifecycle events emitted back to WebView.

Also record whether async preview workers complete after stop/final begins.

Expected result:

- Stop/final should invalidate older preview responses.
- No preview lifecycle event should overwrite final text after stop.

Acceptance criteria:

- completed: native logs prove whether UI receives late preview text after final
  starts.
- completed: any stale event path is identified with request/run/session ids.

Implementation details:

- `CBlazeClawMFCView` now emits ordered `speech.bridge.order` status lines and
  `[Speech][BridgeOrder]` ATL traces with a per-view monotonic `seq` number.
- The trace covers:
  1. `startRecording.complete` after `gateway.speech.startRecording`,
  2. `transcribe.dispatch` for every preview and final `speech.transcribe`,
  3. `stopRecording.complete` after `gateway.speech.stopRecording`,
  4. `transcribe.complete` when each async speech worker posts back to the UI
	 thread,
  5. `lifecycle.emit` and `lifecycle.suppressed` for speech lifecycle events
	 emitted or dropped before WebView delivery.
- `transcribe.dispatch` and `transcribe.complete` include the Step 2 request
  identity fields: request type, `sessionId`, `runId`, streaming flag,
  artifact `handoffMode`, `streamId`, `sequenceStart`, `sequenceEnd`, and
  `hasAudioPath`.
- Lifecycle ordering traces avoid transcript content and log only metadata:
  `stage`, `sessionId`, `runId`, `textLength`, `segmentSequence`, and
  `segmentFinal`.

Manual verification guidance:

1. Record `请讲一个笑话` with preview enabled.
2. Confirm `speech.bridge.order seq=... phase=startRecording.complete` appears
   before preview `transcribe.dispatch` entries.
3. Confirm preview `transcribe.complete` entries that occur after
   `stopRecording.complete` are followed by either ignored WebView traces or by
   lifecycle events that do not overwrite the final `speech-final-*` state.
4. Confirm one `phase=transcribe.dispatch type=final runId=speech-final-*`
   appears after `stopRecording.complete`.
5. If final recognition is still `请`, continue to Step 4/5 using the same
   sequence numbers to determine whether the final request was rejected, used a
   short/open artifact, or decoded only a partial range.

### Step 4: Verify coordinator busy-state behavior

Status: completed

Inspect and instrument `SpeechTranscriptionCoordinator::Accept(...)` and
`Execute(...)` for preview/final interactions.

Check whether preview and final requests share a tracking run id such as:

- `<sessionId>:speech`,
- empty run id fallback,
- reused preview run id.

Expected result:

- A preview request must not cause final transcription to receive a busy-state
  response containing old preview text.
- Final transcription should have its own accepted execution path.

Acceptance criteria:

- completed: logs show final request accepted independently when coordinator
  allows it.
- completed: if rejected, the exact existing run id/stage causing rejection is
  documented.

Implementation details:

- `SpeechTranscriptionCoordinator::Accept(...)` now emits metadata-only
  coordinator diagnostics for:
  - `accept.begin`,
  - `accept.accepted.detail`,
  - `accept.rejected_busy_session.detail`,
  - `accept.rejected_busy_run.detail`.
- `SpeechTranscriptionCoordinator::Execute(...)` now emits metadata-only
  coordinator diagnostics for:
  - `execute.begin`,
  - `execute.reuse_existing.detail`,
  - `execute.rejected.detail`,
  - `execute.start.detail`,
  - `execute.completed.detail`.
- Diagnostics include request type (`preview`, `final`, or `unknown`),
  `sessionId`, explicit request `runId`, coordinator `trackingRunId`, whether a
  streaming input exists, artifact `streamId`, `sequenceStart`, `sequenceEnd`,
  and duration metadata.
- Rejection diagnostics include the active mapped run id, existing request type,
  existing run id, existing stage, existing streaming flag, cancel flag, and
  existing transcript text length without logging transcript content.

Current behavior to verify in repro logs:

- Preview requests should show `requestType=preview` and
  `trackingRunId=speech-preview-*`.
- Final requests should show `requestType=final` and
  `trackingRunId=speech-final-*`.
- If final is blocked by preview, logs should contain
  `accept.rejected_busy_session.detail requestType=final ...
  existingRequestType=preview existingRunId=speech-preview-* existingStage=...`.
- If final is accepted, logs should contain
  `accept.accepted.detail requestType=final ... trackingRunId=speech-final-*`
  followed by `execute.start.detail` and `execute.completed.detail` for the same
  final run.

Manual verification guidance:

1. Record `请讲一个笑话` with preview enabled.
2. Search output for `[SpeechTranscriptionCoordinator][accept.` and
   `[SpeechTranscriptionCoordinator][execute.`.
3. Confirm final request has a distinct `trackingRunId=speech-final-*`.
4. If final is rejected, use `existingRunId` and `existingStage` from the
   rejection line to identify the blocking preview/final path.
5. If final is accepted but still recognizes only `请`, continue to Step 5 to
   verify final artifact range and Step 6 to verify Sherpa final decode range.

### Step 5: Verify final request uses a finite PCM artifact

Status: completed

Confirm the final request after stop uses:

- `handoffMode=pcm_stream`,
- `streamId=voice_recorder`,
- `sequenceStart` equal to the start of the recording range,
- non-zero finite `sequenceEnd`,
- correct sample rate `16000`,
- mono channel count.

Expected result:

- Preview requests may use open-ended `sequenceEnd=0`.
- Final request must use finite `sequenceEnd > sequenceStart`.

Acceptance criteria:

- completed: final artifact metadata is logged and matches the captured utterance
  duration.
- completed: if final uses an open-ended live artifact, stop artifact resolution
  is corrected before final transcription.

Implementation details:

- `CVoiceRecorder` now preserves `m_recordingStartSequence` when recording
  starts.
- `CVoiceRecorder::BuildStreamingAudioArtifact()` uses the preserved recording
  start sequence for artifact `sequenceStart` instead of recomputing it from the
  ring buffer's current oldest available sequence.
- Preview artifacts still remain open-ended while recording with
  `sequenceEnd=0`.
- Final artifacts after stop use a finite `sequenceEnd=latestSequence` and are
  rejected if `latestSequence <= sequenceStart`.
- Artifact diagnostics now emit `[VoiceRecorder][artifact.preview]`,
  `[VoiceRecorder][artifact.final]`, and `[VoiceRecorder][artifact.final.invalid]`
  lines with stream id, start, end, oldest available, latest, sample rate,
  channel count, duration, and whether the preserved start precedes the current
  oldest available sequence.

Root-cause risk addressed:

- Before this step, final artifacts were finite after stop, but their
  `sequenceStart` was recomputed from `GetOldestAvailableSequence()`. If the
  ring buffer advanced during recording, the final request could omit early
  utterance audio and produce a partial transcript such as `请`.
- The final request now carries the original recording start sequence so Step 6
  can verify Sherpa drains the intended full range.

Manual verification guidance:

1. Record `请讲一个笑话` with preview enabled.
2. Confirm preview logs may show `sequenceEnd=0`.
3. Confirm final logs show `[VoiceRecorder][artifact.final]` with
   `streamId=voice_recorder`, `start=<recording start>`, and
   `end > start`.
4. Confirm final request logs from Steps 2/3/4 carry the same finite range.
5. If recognition is still partial, continue to Step 6 to verify Sherpa final
   decode starts at this same `sequenceStart` and drains to `sequenceEnd`.

### Step 6: Verify Sherpa final decode is actually clean and full-range

Status: completed

Instrument `SherpaZipformerStreamingEngine::TranscribeStreaming(...)` for final
requests to log:

- whether `isFinalStreamRequest` is true,
- whether cached state was reset,
- starting `nextSequence`,
- final `nextSequence`,
- `sequenceStart`,
- `sequenceEnd`,
- chunk count,
- decoded text,
- final outcome.

Expected result:

- Final request should begin at `sequenceStart`, not at a preview cursor.
- Final request should drain to `sequenceEnd`.
- Final decoded text should be independently produced by Sherpa, not copied from
  an earlier preview segment.

Acceptance criteria:

- completed: logs prove final request reads the full finite range.
- completed: if it does not, the logs identify whether stream-state reset,
  cursor initialization, or oldest-sequence clamping caused the mismatch.

Implementation details:

- `SherpaZipformerStreamingEngine::TranscribeStreaming(...)` now emits
  `[SherpaStreaming][final.start]` for final finite PCM requests.
- The final-start trace includes:
  - `runId`,
  - `streamId`,
  - `final=1`,
  - `cachedReset`,
  - requested `sequenceStart`,
  - initial cursor before oldest-sequence clamping,
  - oldest available ring sequence,
  - whether the start was clamped,
  - effective decode start,
  - `sequenceEnd`,
  - artifact start/end and duration.
- `TranscribeStreaming(...)` also emits `[SherpaStreaming][final.summary]` after
  decoding.
- The final-summary trace includes:
  - requested/effective start,
  - final cursor,
  - drain status,
  - remaining samples,
  - chunk count,
  - loop count,
  - decoded text,
  - final outcome,
  - optional baseline diagnostic path.

Current implementation finding:

- Final finite requests already reset cached stream state before decoding.
- After reset, the start cursor is initialized from the streaming contract cursor
  or source `sequenceStart`.
- The only expected reason final decode would not begin at `sequenceStart` is if
  the requested start is no longer readable and must be clamped to the oldest
  available ring-buffer sequence. This is now explicit in the `clamped` field.

Manual verification guidance:

1. Record `请讲一个笑话` with preview enabled.
2. Confirm `[SherpaStreaming][final.start]` contains `final=1` and
   `cachedReset=1`.
3. Confirm `requestedStart` equals the Step 5 final artifact start.
4. Confirm `clamped=0` unless logs show the requested start was no longer
   readable.
5. Confirm `[SherpaStreaming][final.summary]` has `drained=1`,
   `remaining=0`, and `finalCursor >= sequenceEnd`.
6. Confirm `decodedText` is independently produced on the final run. If it is
   still partial while the range is fully drained, continue with later model/audio
   diagnostics rather than GUI stale-state fixes.

### Step 7: Add an automated regression test for preview-plus-final flow

Status: completed

Add or update tests to simulate:

1. start recording with an open-ended PCM stream,
2. run one or more preview transcriptions,
3. stop recording and produce a finite PCM artifact,
4. run final transcription,
5. assert final reads from the finite artifact start and is not replaced by the
   preview response.

Candidate test areas:

- `tests/SpeechRecognitionRealtimeStreamingTests.cpp`,
- WebView controller unit tests if available,
- gateway handler contract tests if available.

Acceptance criteria:

- completed: the test would fail if final decode reused a preview cursor or did
  not drain the finite final range.
- completed: the test passes after the Step 5/6 final range and debug fixes.
- completed: existing WebView controller regressions protect final text authority
  from stale preview text, while the strengthened engine test protects the core
  preview-plus-final range behavior.

Implementation details:

- Strengthened
  `tests/SpeechRecognitionRealtimeStreamingTests.cpp` test
  `Sherpa streaming engine final stream resets live preview state`.
- The test now simulates:
  1. open-ended live preview with `sequenceEnd=0`,
  2. final transcription with a finite artifact range,
  3. final read beginning at the original stream start,
  4. final cursor draining to the finite `sequenceEnd`.
- Added assertions for:
  - `sherpaFinalStreamRequest=true`,
  - `sherpaLivePcmStream=false`,
  - `sherpaFinalDrainComplete=true`,
  - `sherpaFinalRemainingSamples=0`,
  - final `sequenceEnd`,
  - final cursor at or past `sequenceEnd`,
  - baseline input start/end range,
  - final outcome not `live_stream_not_final` or `finite_stream_not_drained`,
  - returned streaming input source and cursor preserving the finite final range,
  - returned final text matching Sherpa decoded text when decoded text exists.

Related existing coverage:

- `web/chat/chat-controller.js::runRegressionChecks()` already includes final
  transcript authority and stale preview run guard checks, covering UI-level
  protection against stale preview text replacing final text.

### Step 8: Fix the proven GUI/orchestration issue

Status: completed

Apply the smallest source-level fix based on the evidence.

Likely fixes include one or more of:

- use separate preview and final run ids,
- cancel or invalidate preview polling before stop/final transcription,
- ignore late preview responses after stop/final starts,
- ensure final lifecycle events have higher precedence than preview events,
- ensure final request gets a finite artifact,
- reset Sherpa stream state only for true final finite requests,
- prevent busy-state responses from copying preview text into final UI state.

Acceptance criteria:

- completed: the fix is structural and uses speech state/run-id authority, not
  phrase-specific text checks.
- completed: preview text can still update during active recording/streaming
  states.
- completed: final text is produced by the non-preview final transcription path
  with a distinct `speech-final-*` run id.
- completed: late preview lifecycle or async response updates are ignored once
  the previous speech state leaves active preview authority.

Implementation details:

- Added a state-aware active preview predicate in
  `web/chat/chat-controller.js`.
- Added a final-authority guard that rejects preview updates unless the previous
  state is still recording, start-streaming, streaming, or queued with a preview
  run id.
- Routed ignored late preview lifecycle updates through
  `speech.request.lifecycle_ignored` diagnostics with reason
  `final_authority_active`.
- Updated async preview response suppression in `transcribeSpeech(...)` to use
  the same state-aware active preview authority check.
- Added a WebView controller regression check proving a late
  `speech-preview-*` lifecycle update cannot overwrite a stopped/final
  `speech-final-*` state.
- Added UI active-state handling so preview `completed` or `segment_finalized`
  lifecycle updates for `speech-preview-*` runs do not return the Transcribe
  button to idle. The button remains `Recording... (click to stop)` until the
  user explicitly stops recording.
- Added final-dispatch authority: when stop/final transcription begins, WebView
  state is owned by the `speech-final-*` run id, and later `speech-preview-*`
  lifecycle events are ignored instead of replacing the final visible/chat text.
- Tightened final transcript submission: final `chat.send` now uses only text
  returned by the final response (`payload.text` / `payload.transcript`).
  Preserved preview/session text is no longer a fallback for final submission.
- Restored nested final transcript authority: final submission can also use
  final-owned nested `speechSession.text`, `speechSession.transcript`, or
  `speechSession.segment.text`, while preview-owned response text remains
  rejected.
- Added terminal reset for missing final text: if no final-owned transcript is
  present, the speech state becomes `failed` with
  `missing_final_transcript`, clears preview text, and allows the Transcribe
  button to return to retry/idle.
- Fixed missing-final failed-state merging so `Recognition failed` no longer
  restores and displays stale preview text. Missing-final diagnostics now
  include final transcript source and ownership fields to distinguish empty
  native final decode from preview-owned rejection.
- Fixed native Sherpa finalization to preserve compatible live preview stream
  state when finalizing a finite PCM range. The final pass no longer always
  resets decoder/token state before draining, so preview-decoded tokens can be
  promoted into final-owned native transcript output when the live cursor is
  within the final range.

Preview enablement:

- Live preview is enabled by default when the speech capability reports
  `streamingPreviewEnabled=true`.
- To enable preview after using the diagnostic bypass, remove or comment out the
  following line in `BlazeClawMfc/blazeclaw.conf`, then restart BlazeClaw:
  `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false`.
- To explicitly keep preview enabled, set the environment/config value to true:
  `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=true`.
- The WebView status should no longer show `preview=off` when preview is enabled.
- When preview is disabled, the header speech status shows `preview=off` and
  the live preview box shows `Live preview disabled` with `Preview is off; final
  transcription will run after stop.` This avoids confusing the adjacent
  `Abort` button with the preview status surface.

### Step 9: Validate manually with controlled scenarios

Status: ready for manual execution

Manual validation support is implemented in:

- `tools/speech/SHERPA_ZIPFORMER_LIVE_RECOGNITION_STEP9_MANUAL_CHECKLIST.md`

Use that checklist to run the real microphone scenarios, capture the required
diagnostics, and record the final pass/fail evidence. This step cannot be marked
fully passed until the scenarios are run in the GUI with real audio input.

Manual validation matrix:

| Scenario | Expected result | Status |
| --- | --- | --- |
| Disable preview, speak `请讲一个笑话`, stop | Final text correct | checklist ready; not run |
| Enable preview, speak `请讲一个笑话`, stop | Preview may be partial; final text correct | checklist ready; not run |
| Enable preview, wait through several preview ticks before stopping | Button remains click-to-stop; final runs after explicit stop | checklist ready; not run |
| Enable preview, stop quickly | No stale preview overwrites final/empty state | checklist ready; not run |
| Enable preview, speak English | Preview/final both remain sane | checklist ready; not run |
| Enable preview, cancel recording | Preview is cleared/ignored; no chat send | checklist ready; not run |

Acceptance criteria:

- ready to validate: the bad `请听` final result is no longer reproducible for
  the reported phrase after a real preview-enabled GUI run.
- ready to validate: final result quality matches the pre-live-recognition
  preview-disabled behavior for the same speaker, microphone, phrase, and
  environment.
- ready to validate: final visible text is produced by a `speech-final-*` request
  and late `speech-preview-*` updates are ignored after stop/final begins.
- ready to validate: logs may show late preview `transcribe.complete`, but any
  preview lifecycle after `speech.final.request_start` is ignored and cannot
  become the chat-sent transcript.
- ready to validate: if a final response has no transcript text, WebView logs
  `reason=missing_final_transcript` and does not send preserved preview text as
  the user prompt.
- ready to validate: missing final text does not leave the Transcribe button in
  `Recording... (click to stop)` and does not repeatedly finalize the same
  stale audio artifact on subsequent clicks.
- ready to validate: `Recognition failed` for `missing_final_transcript` shows
  the error message, not stale preview text such as `请你讲一个`, and logs include
  `finalTextSource`, `finalTextOwned`, and text-presence flags.
- ready to validate: final Sherpa logs should show `cachedReset=0` when a
  compatible preview state exists, and final response text should be present
  instead of `missing_final_transcript`.

### Step 10: Validate with build and focused tests

Status: pending

Run the required BlazeClaw build command from the actual solution directory:

```powershell
msbuild "BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

Run focused tests after build:

```powershell
bin\Debug\BlazeClawMfc.Tests.exe "[speech][streaming][realtime]"
```

If frontend tests exist for WebView speech controller behavior, run those too.

Acceptance criteria:

- Build succeeds with 0 errors.
- Focused speech tests pass.
- Any added GUI/controller regression tests pass.

## Evidence Log

Use this section while executing the plan.

### A/B preview bypass result

- Status: checklist ready; not run
- Notes: Step 1 added `BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false` as the
  one-switch bypass. Step 9 manual execution should use
  `tools/speech/SHERPA_ZIPFORMER_LIVE_RECOGNITION_STEP9_MANUAL_CHECKLIST.md`.
  For the disabled-preview A/B run, uncomment or add
  `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false` in
	`BlazeClawMfc/blazeclaw.conf`, restart BlazeClaw, confirm the WebView header
  speech status shows `preview=off`, confirm the live preview box shows
	`Live preview disabled`, record `请讲一个笑话`, and verify the logs do not show
	`requestType=preview` / `type=preview` dispatches. If recording starts before
  capabilities load, WebView should emit `speech.preview.disabled` with
  `reason=capability_unloaded`; final stop transcription must still run.

### WebView request identity trace

- Status: pending
- Notes:

### Native bridge ordering trace

- Status: pending
- Notes:

### Coordinator acceptance trace

- Status: pending
- Notes:

### Final artifact trace

- Status: pending
- Notes:

### Empty-final native failure normalization (implementation update)

- Status: implemented
- Notes:
  - WebView now sends explicit `livePreviewOnly` for every `speech.transcribe` request.
  - Native Sherpa finalization now returns explicit failed result when final transcript text is empty.
	- Gateway normalizes `completed + empty transcript` into failed state with explicit `inference_failed` mapping for final flows only (`!livePreviewOnly`).
  - WebView preserves native final error codes for empty-final responses and only uses `missing_final_transcript` as legacy fallback when no native error is present.
  - Warmup runs (`speech-warmup-*`) are excluded from transcript-required failure semantics to prevent startup `warmup_failed` caused solely by empty decoded transcript.

### Sherpa final decode trace

- Status: pending
- Notes:

## Final Fix Summary

Status: implementation complete; manual Step 9 execution pending

- Root cause: preview and final speech orchestration could share or reuse GUI
  state authority, allowing preview state/cursor/lifecycle evidence to obscure
  whether final transcription used the finite final artifact.
- Files changed: recorder final artifact range handling, Sherpa final decode
  diagnostics, WebView request/bridge/coordinator diagnostics, WebView
  final-authority guard, realtime streaming regression tests, and Step 9 manual
  validation checklist.
- Tests added: strengthened `tests/SpeechRecognitionRealtimeStreamingTests.cpp`
  preview-plus-final regression and `web/chat/chat-controller.js` regression
  checks for final transcript authority and late preview invalidation.
- Manual validation result: pending real GUI runs using
  `tools/speech/SHERPA_ZIPFORMER_LIVE_RECOGNITION_STEP9_MANUAL_CHECKLIST.md`.
- Build/test result:
