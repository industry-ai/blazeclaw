# Step 8: Preview Inference Concurrency Protection

## Goal
Prevent live speech preview inference from piling up while the user records, and ensure stale preview responses cannot overwrite newer speech session state after stop/final transcription begins.

## Files Updated
- `web/chat/index.js`
- `web/chat/chat-controller.js`

## Implemented Behavior
- The WebView live preview loop continues to use a single `liveSpeechPollBusy` guard so only one preview transcription request can be in flight for the active polling generation.
- Added explicit `liveSpeechPollInFlightRunId` tracking so the polling loop clears ownership when a generation stops or a late in-flight preview completes.
- `stopLiveSpeechPoll()` increments `liveSpeechPollGeneration`, clears the interval, clears the busy flag, and clears the tracked in-flight run id before final stop transcription starts.
- `chat-controller.js` now has shared helpers for active preview stages and preview run ids.
- `applySpeechLifecycleUpdate(...)` now rejects stale non-final `streaming` updates when:
  - the previous state is no longer an active preview stage,
  - the payload belongs to an older `speech-preview-*` run id,
  - the session id changes,
  - the segment sequence regresses,
  - or the same sequence/text repeats.
- `transcribeSpeech({ livePreviewOnly: true })` applies the same stale-update filter before replacing state from a preview response.
- Live-preview timeout/failure responses no longer mark the whole speech session as failed; explicit stop/final transcription remains responsible for surfacing final errors.

## Acceptance Criteria Result
- Completed: slow inference does not create an unbounded backlog because the poll loop skips ticks while a preview request is busy.
- Completed: stale partial text cannot overwrite newer state because preview lifecycle and preview response merges are filtered by stage, run id, session id, sequence, and duplicate text.
- Completed: stop/final transcription invalidates preview polling through generation changes and clears in-flight preview ownership; late preview results are ignored when the session has left active preview stages.

## Validation
- JavaScript diagnostics were checked for `web/chat/index.js` and `web/chat/chat-controller.js` after the change.
- Full syntax/build validation is recorded in the master plan update for this step.
