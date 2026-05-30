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
- `web/chat/chat-controller.js` preserves `streamingPreviewEnabled` in the
  speech capability snapshot.
- `web/chat/index.js::startLiveSpeechPoll(...)` exits before starting preview
  polling when `streamingPreviewEnabled === false`.
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

Status: pending

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

- Logs show final request accepted independently.
- If rejected, the exact existing run id/stage causing rejection is documented.

### Step 5: Verify final request uses a finite PCM artifact

Status: pending

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

- Final artifact metadata is logged and matches the captured utterance duration.
- If final uses an open-ended live artifact, fix stop artifact resolution first.

### Step 6: Verify Sherpa final decode is actually clean and full-range

Status: pending

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

- Logs prove final request reads the full finite range.
- If it does not, fix stream-state reset/cursor handling.

### Step 7: Add an automated regression test for preview-plus-final flow

Status: pending

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

- The test fails before the fix.
- The test passes after the fix.
- The test protects against stale preview text replacing final text.

### Step 8: Fix the proven GUI/orchestration issue

Status: pending

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

- The fix is structural and not phrase-specific.
- Preview text can update during recording.
- Final text is produced only by final transcription.
- Late preview responses cannot overwrite final text.

### Step 9: Validate manually with controlled scenarios

Status: pending

Manual validation matrix:

| Scenario | Expected result | Status |
| --- | --- | --- |
| Disable preview, speak `请讲一个笑话`, stop | Final text correct | pending |
| Enable preview, speak `请讲一个笑话`, stop | Preview may be partial; final text correct | pending |
| Enable preview, stop quickly | No stale preview overwrites final/empty state | pending |
| Enable preview, speak English | Preview/final both remain sane | pending |
| Enable preview, cancel recording | Preview is cleared/ignored; no chat send | pending |

Acceptance criteria:

- The bad `请听` final result is no longer reproducible for the reported phrase.
- Final result quality matches the pre-live-recognition behavior.

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

- Status: ready for manual validation
- Notes: Step 1 added `BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false` as the
  one-switch bypass. To run the A/B test, uncomment
  `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false` in
  `BlazeClawMfc/blazeclaw.conf`, restart BlazeClaw, record
  `请讲一个笑话`, and verify the final stop transcription still runs without live
  preview polling.

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

### Sherpa final decode trace

- Status: pending
- Notes:

## Final Fix Summary

Status: pending

To be filled after implementation:

- Root cause:
- Files changed:
- Tests added:
- Manual validation result:
- Build/test result:
