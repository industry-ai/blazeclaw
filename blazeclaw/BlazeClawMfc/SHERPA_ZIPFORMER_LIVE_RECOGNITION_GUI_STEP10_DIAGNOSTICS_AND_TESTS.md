# Step 10: Diagnostics and Tests

## Goal
Add focused diagnostics and regression coverage for live Sherpa Zipformer speech recognition updates without logging transcript contents.

## Files Updated
- `web/chat/index.js`
- `web/chat/chat-controller.js`
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_PLAN.md`

## Diagnostics Added
- `web/chat/index.js` now emits metadata-only preview diagnostics through `console.debug("[speech-preview-diagnostic]", ...)` when diagnostics are available:
  - `speech.preview.request_start`
  - `speech.preview.request_end`
  - `speech.preview.skip_busy`
  - `speech.preview.skip_stale_generation`
  - `speech.preview.response_stale_generation`
  - `speech.preview.stop_inactive_stage`
  - `speech.preview.request_error`
- Preview diagnostics include safe metadata only:
  - run id
  - polling generation
  - lifecycle stage
  - segment sequence
  - whether audio path/artifact exists
  - whether recognized text exists
- `web/chat/chat-controller.js` now emits `speech.final.replacement` operator diagnostics when final speech replacement succeeds.
- Final replacement diagnostics include transcript length and metadata, but not transcript content.

## Regression Coverage Added
Extended `window.BlazeClawChatController.runRegressionChecks()` with speech-specific checks:
- Interim `speech.lifecycle` streaming updates update `speechSessionState` without appending chat messages.
- Stale preview streaming updates do not overwrite newer interim segment text/sequence.
- Stop/final transcription sends only the authoritative final transcript through `chat.send`.
- Interim-only text is not sent as a fallback during finalization.
- Final speech replacement increments the `speech.final.replacement` diagnostic counter.

## Existing Native Coverage Reviewed
Existing native streaming coverage remains in `tests/SpeechRecognitionRealtimeStreamingTests.cpp`:
- Sherpa streaming engine emits segment data for speech-energy input when runtime/model loading is available.
- Sherpa streaming engine handles sequence catch-up after wrap-window scenarios.
- Sherpa streaming engine reports cancellation.

The coordinator callback path is exercised indirectly by the earlier implementation and build validation. The added Step 10 work focuses on WebView state and finalization regressions where the recent live preview behavior is concentrated.

## Acceptance Criteria Result
- Completed: WebView controller applies interim lifecycle payloads without sending chat messages.
- Completed: stale preview responses cannot overwrite newer text in regression coverage.
- Completed: stop/final transcription sends only final text in regression coverage.
- Completed: diagnostics record preview start/end/skipped/stale/error and final replacement metadata without transcript contents.
