# Step 9: Clean Stop Finalization

## Goal
Finalize live speech recognition cleanly when the user stops recording: preserve the last interim preview while final transcription runs, treat the final transcription as authoritative, avoid sending interim-only text on final quality failure, and clear terminal preview state after successful final send.

## Files Updated
- `web/chat/index.js`
- `web/chat/chat-controller.js`

## Implemented Behavior
- The stop-recording path still calls `stopLiveSpeechPoll()` before requesting `gateway.speech.stopRecording`, invalidating outstanding preview work from earlier steps.
- `web/chat/index.js` now captures the last visible interim transcript from `speechSessionState.segmentText` or `speechSessionState.text` and passes it into the `stopped` lifecycle update.
- `chat-controller.js::applySpeechLifecycleUpdate(...)` now preserves previous interim text through `queued`, `stopped`, `transcribing`, and `failed` state transitions when an incoming status payload has no new text.
- Final transcription remains authoritative through the existing non-preview `transcribeSpeech(...)` path.
- Existing final transcript quality checks still block rejected final transcripts before `sendPayload(...)`; preserved interim preview text is not sent as a fallback.
- After a successful final speech send, the controller marks speech state as `completed` with the cleaned final transcript, updates the preview briefly, then clears the speech preview state after a short delay.
- Terminal preview cleanup checks the expected run id and terminal stage before clearing, so newer speech sessions are not cleared by an older timeout.

## Acceptance Criteria Result
- Completed: the final transcript is authoritative because only the final non-preview transcription path calls `sendPayload(...)`.
- Completed: interim-only text is not sent when final transcription fails quality checks; failures preserve preview/status text but return before chat send.
- Completed: the UI does not regress to blank while final transcription is running because `stopped`/`queued`/`transcribing` states preserve the last interim text and render as `Finalizing...`.
