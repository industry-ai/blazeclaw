# Voice Stream to ASR Step 5 Speech Transcription Coordinator

## Scope
This document captures Step 5 of `BlazeClawMfc/VOICE_STREAM_TO_ASR_WIRING_PLAN.md`: introducing a dedicated speech transcription coordinator to own admission, execution tracking, and runtime-backed orchestration for STT requests.

## Step 5 Goal
Move speech session orchestration into a dedicated service so the codebase has an explicit coordinator for request admission, run/session tracking, duplicate in-flight protection, and lifecycle state ownership while preserving the existing WAV-file runtime inference path.

## Implementation Summary
Step 5 was implemented as a coordinator-first orchestration pass.

### What changed
Files:
- `BlazeClawMfc/src/core/SpeechTranscriptionCoordinator.h`
- `BlazeClawMfc/src/core/SpeechTranscriptionCoordinator.cpp`
- `BlazeClawMfc/src/core/ServiceManager.h`
- `BlazeClawMfc/src/core/GatewayHostBindingCoordinator.cpp`
- `BlazeClawMfc/src/gateway/GatewayHost.h`
- `BlazeClawMfc/src/gateway/GatewayHost.cpp`
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `BlazeClawMfc/BlazeClawMfc.vcxproj`

### New coordinator service
`SpeechTranscriptionCoordinator` now owns speech execution orchestration above the low-level runtime.

Implemented responsibilities:
- accept `SpeechExecutionRequest`
- assign or normalize a coordinator tracking `runId`
- track active execution state by `runId`
- map active `sessionId -> runId`
- reject duplicate in-flight transcription for the same session
- publish `Queued`, `Transcribing`, and terminal execution states through an update callback
- delegate inference work to `ISpeechRecognitionRuntime::Transcribe(...)`
- mark `cancelRequested` and forward cancellation to `ISpeechRecognitionRuntime::Cancel(...)`

### Gateway binding change
`GatewayHostBindingCoordinator.cpp` no longer binds speech transcription directly to `SpeechRecognitionRuntime::Transcribe(...)`.

Instead, it now wires these coordinator-backed gateway surfaces:
- speech execution admission callback
- speech execution status callback
- speech cancellation callback
- speech transcription execution callback

This preserves the current low-level runtime implementation while moving orchestration ownership into a dedicated service.

### Gateway host contract change
`GatewayHost` now exposes explicit coordinator-facing methods for speech orchestration:
- `AcceptSpeechTranscription(...)`
- `GetSpeechExecutionStatus(...)`
- `CancelSpeechTranscription(...)`

These methods sit beside the existing `TranscribeSpeech(...)` call so handlers can separate admission from execution.

### Speech RPC handler change
`speech.transcribe` now performs coordinator admission before invoking transcription.

Current behavior:
1. build a `SpeechExecutionRequest`
2. ask the coordinator to accept or reject it
3. if a session already has an in-flight speech request, return a non-blocking response that includes the active execution snapshot
4. otherwise run the existing transcription path through the coordinator
5. include `executionState` and `executionRunId` in the RPC response payload

This gives the speech pipeline an explicit orchestration owner without yet adding streaming lifecycle events to WebView.

## Why Step 5 matters
Before Step 5, the codebase had execution contract types but no dedicated service owning those contracts.

After Step 5:
- request admission is centralized
- active runs are tracked explicitly
- duplicate session conflicts are handled consistently
- runtime execution is wrapped by a dedicated orchestration service
- later lifecycle event streaming has a stable source of truth

## Design Notes
Step 5 intentionally keeps Step 3's async boundary in the WebView host and Step 4's WAV-file handoff intact.

The new coordinator does **not** replace the runtime inference implementation. It sits above it and manages orchestration concerns that were previously implicit.

## What Step 5 Does Not Yet Do
Step 5 does **not** yet provide:
- WebView push events for intermediate speech lifecycle transitions
- a public RPC for polling execution status directly
- shutdown drain/cancel hardening for coordinator-owned work
- true live PCM streaming into the runtime

Those remain follow-up work for later plan steps, especially Step 6 and Step 8.

## Validation Outcome
Expected validation target for Step 5:
- the build should succeed with the new coordinator service wired into gateway speech handling
- duplicate in-flight transcription for the same session should be rejected cleanly
- successful transcription should still return the existing transcript result shape with added execution metadata
