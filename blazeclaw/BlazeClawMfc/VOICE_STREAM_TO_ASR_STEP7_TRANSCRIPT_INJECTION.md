# Voice Stream to ASR Step 7: Transcript Injection Into Chat Pipeline

## Goal
When speech transcription completes successfully, forward the transcript into the existing chat orchestration path as a voice-derived user message while preserving speech provenance metadata.

## Scope Implemented
- Completed speech transcripts are sent through the existing `chat.send` path.
- Speech metadata is preserved and forwarded with the request.
- The transcript is still traceable as voice input instead of becoming an indistinguishable typed message.

## WebView Changes

### `BlazeClawMfc/web/chat/chat-controller.js`

#### 1) Chat send metadata support
`sendPayload(...)` now accepts an optional `speechContext` object.

When present, the controller merges the following into the `chat.send` request:
- `transcriptInjection`
- `speechArtifact`
- `sessionId`
- `runId`
- `audioPath`
- `language`
- `latencyMs`
- `source`

This keeps the existing orchestration path intact while preserving voice provenance for downstream logic.

#### 2) Speech transcript forwarding
`transcribeSpeech(...)` now forwards successful transcription results by calling `sendPayload(...)` directly instead of copying text into the input box and invoking the normal typed-input send path.

That means:
- transcript text still reaches chat as the user message,
- voice metadata is preserved,
- the request remains compatible with the existing chat pipeline.

## Native/Gateway Compatibility
The gateway speech handler already emits transcript-oriented payload shapes that include:
- `speechSession`
- `transcriptInjection`
- `speechArtifact`

The chat pipeline already recognizes those fields and uses them to track voice-derived input. Step 7 makes the WebView path use the same metadata shape so voice input and typed input converge in the same orchestration surface.

## Validation Intent
A successful speech transcript should now:
- appear in the chat pipeline as user input,
- preserve provenance metadata,
- continue through the same skill/tool-routing/orchestration logic as typed messages.

## Notes
- Step 6 remains the UI/state streaming layer; Step 7 builds on it by handing the final transcript into chat processing.
- This does not change the ASR runtime itself or the WAV handoff boundary.
