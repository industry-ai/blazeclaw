# Step 6: WebView Live Recognition Preview

## Goal
Render live Sherpa Zipformer recognition text in the WebView chat UI while recording, without appending interim text into the chat message list.

## Files Updated
- `web/chat/index.html`
- `web/chat/index.js`
- `web/chat/index.css`

## Implemented Behavior
- Added a dedicated `speechLivePreview` surface between the message list and composer.
- Rendered the current speech session state from `state.speechSessionState` into the preview on every composer/state refresh.
- Used `segmentText` first, then `text`, so interim segment payloads from gateway lifecycle and preview responses are shown immediately.
- Replaced the preview text in place instead of appending one DOM node per partial recognition result.
- Kept empty idle states hidden, while active recording/finalizing states show stable labels such as `Listening...`, `Recognizing...`, and `Finalizing...`.
- Styled interim/listening/finalizing text as muted italic and final/error states with distinct colors.
- Left accepted final transcript injection on the existing `transcribeSpeech(...)` / chat composer path; preview text is not added to the message list.

## UI State Mapping
- `recording`, `start_stream`, `streaming` without text: `Listening... Speak now`
- `streaming` with segment text: `Recognizing... <interim text>`
- `queued`, `stopped`, `transcribing`: `Finalizing... <last available text>`
- `segment_finalized`, `completed`: `Recognized <final text>`
- `failed`, `cancelled`: terminal preview with error styling when text or an error message is available

## Acceptance Criteria Result
- Completed: users see changing transcript text while speaking when preview or lifecycle state carries segment text.
- Completed: interim text updates in one preview area instead of being appended repeatedly.
- Completed: idle empty interim results keep the preview hidden, and active empty states use stable `Speak now` text instead of flickering blank content.
- Completed: final transcript send behavior remains on the existing chat path because the preview is separate from `messages` and the composer injection flow.
