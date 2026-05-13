# Voice Stream to ASR Step 2 Contract

## Scope
This document captures Step 2 of `BlazeClawMfc/VOICE_STREAM_TO_ASR_WIRING_PLAN.md`: defining the speech execution contract for a future non-blocking speech orchestration layer while preserving the existing synchronous low-level runtime entry point.

## Contract Goal
Separate two layers of speech behavior:

1. **Low-level synchronous runtime inference**
   - kept as `SpeechRecognitionRuntime::Transcribe(...)`
   - remains responsible for file-based WAV preprocessing, ONNX inference, decode, and final transcript production

2. **Higher-level speech execution orchestration**
   - will later own queueing, worker dispatch, lifecycle updates, cancellation coordination, and UI-friendly status reporting
   - must not require the WebView RPC path to remain blocked for the full inference duration

## Contract Additions Implemented in Step 2

### Runtime contract additions
Files:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/ISpeechRecognitionRuntime.h`

Added:
- `SpeechExecutionStage`
- `SpeechExecutionState`
- `SpeechExecutionRequest`
- `SpeechExecutionAccepted`
- `SpeechExecutionStatus`
- `SpeechExecutionUpdateCallback`

### Gateway contract additions
File:
- `BlazeClawMfc/src/gateway/GatewayHost.h`

Added gateway-facing mirrors for:
- `SpeechExecutionRequest`
- `SpeechExecutionAccepted`
- `SpeechExecutionStatus`
- `SpeechExecutionUpdateCallback`

## Lifecycle Model
`SpeechExecutionStage` defines the orchestration-facing lifecycle introduced by Step 2:
- `Queued`
- `Recording`
- `Stopped`
- `Transcribing`
- `Completed`
- `Failed`
- `Cancelled`

This lifecycle is intentionally separate from the existing `SpeechSessionStage` so current synchronous code paths remain stable while Step 3 introduces background execution.

## Execution State Model
`SpeechExecutionState` is the orchestration status payload intended for queue/worker coordination and UI-facing progress updates.

Fields:
- `sessionId`
- `runId`
- `stage`
- `audioPath`
- `transcriptText`
- `language`
- `prompt`
- `latencyMs`
- `cancelRequested`
- `segment`
- `error`

Why these fields were chosen:
- they preserve the identifiers already used by the WebView and gateway speech flow
- they can represent both pre-inference and post-inference phases
- they support future progress emission without overloading the synchronous result contract

## Accepted/Status Contract Shape

### `SpeechExecutionRequest`
Represents an orchestration-layer speech job submission:
- `runId`
- `sessionId`
- `audioPath`
- `language`
- `prompt`

### `SpeechExecutionAccepted`
Represents the result of queue/admission logic:
- `accepted`
- `executionState`
- optional `error`

This is intended for the future Step 3 boundary where the UI request can be accepted quickly without waiting for full STT completion.

### `SpeechExecutionStatus`
Represents lookup/poll state for an existing speech job:
- `found`
- `executionState`

This is intended for future coordinator/status lookup paths and event/state sync.

## Compatibility Decision
Step 2 deliberately does **not** replace these existing synchronous types:
- `SpeechTranscribeRequest`
- `SpeechTranscribeResult`
- `SpeechSessionStage`
- `SpeechRecognitionRuntime::Transcribe(...)`

Reason:
- Step 2 is contract preparation only
- Step 3 will consume these new contract types when transcription is moved off the UI/native bridge thread
- keeping the synchronous path intact reduces implementation risk and avoids changing working model-load/inference behavior prematurely

## Architectural Outcome of Step 2
After Step 2, the codebase now has an explicit vocabulary for:
- work submission
- acceptance vs completion
- execution lifecycle reporting
- future coordinator callbacks

That contract was missing in Step 1, where all speech behavior was forced through the final synchronous transcribe result.

## What Step 2 Does Not Yet Do
Step 2 does **not** yet:
- queue transcription work in the background
- return early from `speech.transcribe`
- emit execution-stage events to the WebView
- provide status polling or asynchronous cancellation plumbing
- replace nested synchronous `chat.send` forwarding

Those changes belong to later plan steps, primarily Step 3 and Step 6.

## Next Step
Step 3 should consume this contract by introducing a background orchestration layer that:
- accepts a `SpeechExecutionRequest`
- returns a `SpeechExecutionAccepted`
- updates `SpeechExecutionState` as work progresses
- invokes the existing synchronous `Transcribe(...)` only from a worker/background execution context
