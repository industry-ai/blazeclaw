# Voice Stream to ASR Step 4 Audio Handoff Boundary

## Scope
This document captures Step 4 of `BlazeClawMfc/VOICE_STREAM_TO_ASR_WIRING_PLAN.md`: formalizing the current audio handoff boundary between WebView recording and the ASR runtime.

## Step 4 Goal
Keep the existing WAV-file artifact boundary as the first stable integration point for speech transcription, make that boundary explicit in shared contracts, and defer true in-memory PCM streaming to a later step.

## Implementation Summary
Step 4 was implemented as a contract-and-capability clarification pass.

### What changed
Files:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/ISpeechRecognitionRuntime.h`
- `BlazeClawMfc/src/gateway/GatewayHost.h`
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

### Shared speech contract change
The speech domain model now represents the audio handoff explicitly.

Added types:
- `SpeechAudioHandoffMode`
  - `WavFile`
  - `PcmStream`
- `SpeechAudioArtifact`
  - `handoffMode`
  - `path`
  - `mimeType`
  - `container`
  - `sampleRate`
  - `channels`
  - `bitsPerSample`
  - `durationMs`

These types were added to `SpeechRecognitionContracts.h` and then wired into:
- `SpeechSessionState::audioArtifact`
- `SpeechExecutionState::audioArtifact`
- `ISpeechRecognitionRuntime::SpeechExecutionRequest::audioArtifact`
- `ISpeechRecognitionRuntime::SpeechTranscribeRequest::audioArtifact`
- `GatewayHost::SpeechExecutionRequest::audioArtifact`
- `GatewayHost::SpeechTranscribeRequest::audioArtifact`

### Capability advertisement change
`speech.capabilities.get` now reports the current Step 4 boundary directly in the STT payload:
- `audioHandoffMode: "wav_file"`
- `audioMimeType: "audio/wav"`
- `audioContainer: "wav"`
- `streamingSupported: false`

This keeps the current runtime behavior honest: recording still produces a WAV file, and that saved artifact remains the object handed to background transcription.

## Why Step 4 keeps the WAV boundary
The current runtime already accepts `audioPath` and the recent Step 3 fix proved the primary blocking issue was thread placement, not the file boundary itself.

Keeping WAV-file handoff first provides these benefits:
- no new low-level streaming decoder path is needed yet
- existing recorder output remains reusable without conversion redesign
- troubleshooting stays focused on orchestration and lifecycle rather than PCM transport details
- later streaming support can be added on top of an explicit artifact contract instead of replacing an implicit one

## Design Notes
Step 4 intentionally does **not** switch the runtime to true live chunk ingestion.

The codebase now distinguishes between:
- today's implemented mode: persisted WAV-file artifact handoff
- a future possible mode: PCM stream handoff

That distinction is modeled in contracts now, but only the WAV-file path is advertised and supported by capabilities.

## What Step 4 Does Not Yet Do
Step 4 does **not** yet provide:
- in-memory PCM chunk transport from recorder to ASR
- live partial transcript streaming from the runtime
- a coordinator that populates and publishes artifact metadata for each lifecycle transition
- cancellation or shutdown hardening beyond the async bridge changes from Step 3

Those remain follow-up work for later plan steps, especially Step 5, Step 6, and Step 8.

## Validation Outcome
Expected validation target for Step 4:
- the build should succeed with the new handoff contract fields
- speech capabilities should explicitly report WAV-file handoff metadata
- current record -> stop -> transcribe behavior should remain compatible because `audioPath` is preserved
