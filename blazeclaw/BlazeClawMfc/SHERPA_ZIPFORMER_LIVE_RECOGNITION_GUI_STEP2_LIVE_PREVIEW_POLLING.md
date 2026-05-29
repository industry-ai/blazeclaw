# Sherpa Zipformer Live Recognition GUI Step 2 - Live Preview Polling

## Status
Completed.

## Scope
This step refines the existing WebView live preview polling loop so interim speech recognition requests can run while recording is active without blocking the UI, without sending chat messages, and without allowing stale preview responses to overwrite stopped/finalizing state.

## Files Updated
- `web/chat/index.js`
- `web/chat/chat-controller.js`
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_PLAN.md`

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

## Follow-up Notes for Step 3
Step 3 should verify that the `audioArtifact` used by preview calls is consistently a live `pcm_stream` artifact with a usable `streamId`, sequence range, sample rate, channel count, and bit depth.
