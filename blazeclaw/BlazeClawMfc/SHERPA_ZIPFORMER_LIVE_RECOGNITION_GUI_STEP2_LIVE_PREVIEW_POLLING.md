# Sherpa Zipformer Live Recognition GUI Step 2 - Live Preview Polling

## Status
Completed after visibility correction.

## Scope
This step refines the existing WebView live preview polling loop so interim speech recognition requests can run while recording is active without blocking the UI, without sending chat messages, and without allowing stale preview responses to overwrite stopped/finalizing state.

## Files Updated
- `web/chat/index.js`
- `web/chat/chat-controller.js`
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_PLAN.md`
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_BAD_RESULT_FIX_PLAN.md`

## Implementation Summary

### Preview polling ownership
The live preview loop remains in `web/chat/index.js` near the speech transcribe button handler.

The loop still uses:
- `startLiveSpeechPoll(...)` to begin preview polling after recording starts.
- `stopLiveSpeechPoll()` to stop preview polling before final stop transcription.
- `liveSpeechPollBusy` to prevent overlapping preview requests.

Step 1 of `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_BAD_RESULT_FIX_PLAN.md` added
an A/B bypass switch for this loop:

- set `BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false` to keep final
  record-then-transcribe behavior while disabling interim preview polling.
- the native `speech.capabilities.get` response reports this as
  `stt.streamingPreviewEnabled=false`.
- `startLiveSpeechPoll(...)` exits before scheduling preview requests when that
  capability is false.

### Artifact-only preview support
`startLiveSpeechPoll(...)` now allows polling when either `audioPath` or `audioArtifact` is present.

Reason:
- `gateway.speech.startRecording` can return a live `audioArtifact` before a final WAV `audioPath` is available.
- Live preview should use the active PCM stream artifact during recording instead of waiting for the final WAV path.

### Stable preview run id
A stable preview run id is created when recording starts:

```text
speech-preview-<timestamp>
```

That id is:
- stored in the recording lifecycle state as `runId`
- passed into every preview `controller.transcribeSpeech(...)` call for that recording session

`chat-controller.js::transcribeSpeech(...)` now accepts an optional `runId` in its options and uses it instead of always generating a new id.

### Final request identity trace
Final stop transcription now gets its own run id:

```text
speech-final-<timestamp>
```

That id is passed into the non-preview `controller.transcribeSpeech(...)` call
after `gateway.speech.stopRecording` returns. This makes the final request
distinguishable from the earlier `speech-preview-*` polling series in WebView
logs and native bridge traces.

### Request identity diagnostics
`chat-controller.js::transcribeSpeech(...)` now emits structured
`[speech-request-trace]` console diagnostics for:

- `request_start`
- `response_applied`
- `response_ignored`
- `request_error`

Each trace includes:

- request type: `preview` or `final`
- `livePreviewOnly`
- `sessionId`
- request `runId`
- current `speechSessionState.stage`
- `audioArtifact.streamId`
- `audioArtifact.sequenceStart`
- `audioArtifact.sequenceEnd`

Ignored preview responses also include a reason, such as
`preview_not_active`, `stale_preview_update`, or `quality_rejected`, so late
preview responses can be distinguished from accepted final text updates.

`web/chat/index.js` also emits `speech.final.request_start` and
`speech.final.request_end` diagnostics around the final transcription request,
including the final `speech-final-*` run id, the previous preview run id, the
current stage, and the audio artifact summary.

The request traces are now visible through two paths:

- WebView console: `[speech-request-trace]` and
  `[speech-preview-diagnostic]` entries.
- Visual Studio/native status output: `speech.request.trace` and
  `[Speech][RequestTrace]` entries emitted by the `speech.transcribe` bridge
  path.

The native trace line includes request type, `sessionId`, `runId`, streaming
flag, artifact `handoffMode`, `streamId`, `sequenceStart`, `sequenceEnd`, and
whether an `audioPath` was supplied. This is the preferred evidence source when
WebView console output is not available.

### Stale preview response suppression
`web/chat/index.js` now uses a `liveSpeechPollGeneration` counter.

Behavior:
- `stopLiveSpeechPoll()` increments the generation.
- every poll captures the current generation.
- a poll result is ignored if its captured generation no longer matches the current generation.
- the busy flag is only cleared by the still-current generation.

This prevents late preview responses from continuing the old preview loop after stop/failure/destroy-like state transitions.

### Preview state preservation
`chat-controller.js::transcribeSpeech(...)` no longer emits a local `queued` lifecycle update for `livePreviewOnly` requests.

Reason:
- repeated preview calls should not clear the current interim transcript text.
- queued/transcribing state is still emitted for normal final transcription.
- native bridge lifecycle events may still report queued/start_stream/streaming, but preview calls avoid local text-clearing before each request.

### Stop/finalize guard
`chat-controller.js::transcribeSpeech(...)` now ignores live preview responses if the current speech session state is no longer live-preview active.

Allowed active preview stages:
- `recording`
- `start_stream`
- `streaming`
- `queued`

If the state has moved to `stopped`, `transcribing`, `completed`, `failed`, or another terminal/finalizing stage, the preview response returns without applying transcript state.

### Interim quality rejection behavior
Low-quality interim preview text no longer fails the whole speech session.

Behavior:
- final transcription still uses the existing transcript quality gate.
- live preview requests that produce rejected interim text are ignored and polling continues.

Reason:
- interim Sherpa output can be unstable.
- only the final accepted transcript should decide whether to send or block chat input.

## Current Polling Parameters
- interval: `150` ms
- per-preview timeout: `8000` ms

The interval is active only when `streamingPreviewEnabled` is true. Later
performance tuning can adjust it after coordinator/gateway correctness is
complete.

## Acceptance Criteria Results
- Interim recognition requests occur while recording is active through the existing WebView preview loop.
- Preview requests can run from the active `audioArtifact` even when no final WAV path is available yet.
- Requests stop when `stopLiveSpeechPoll()` is called before final stop transcription.
- Generation checks suppress stale preview responses after the loop stops.
- `liveSpeechPollBusy` continues to prevent overlapping preview requests.
- `speech.transcribe` preview requests remain asynchronous through `CBlazeClawMFCView`, so the UI thread is not blocked by preview inference.
- Preview requests do not send chat messages because they use `livePreviewOnly: true`.
- WebView diagnostics now show preview requests as `speech-preview-*` and final
  transcription as one `speech-final-*` request.
- Stale or rejected preview responses are logged as ignored instead of silently
  competing with the final response.
- Normal Visual Studio logs now expose native request identity lines, so Step 2
  evidence no longer depends on WebView developer tools being open.

## Follow-up Notes for Step 3
Step 3 should verify that the `audioArtifact` used by preview calls is consistently a live `pcm_stream` artifact with a usable `streamId`, sequence range, sample rate, channel count, and bit depth.
