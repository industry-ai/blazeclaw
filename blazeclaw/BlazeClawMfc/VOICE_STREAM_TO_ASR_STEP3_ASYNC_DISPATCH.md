# Voice Stream to ASR Step 3 Async Dispatch

## Scope
This document captures Step 3 of `BlazeClawMfc/VOICE_STREAM_TO_ASR_WIRING_PLAN.md`: moving speech transcription off the UI/native bridge thread so WebView-triggered STT no longer blocks the MFC GUI.

## Step 3 Goal
Remove the blocking `speech.transcribe` execution path from `CBlazeClawMFCView::HandleWebMessageJson(...)` while keeping the existing WAV-file handoff and synchronous low-level ASR runtime intact underneath.

## Implementation Summary
Step 3 was implemented as an async bridge-dispatch rollout at the WebView host boundary.

### What changed
Files:
- `BlazeClawMfc/src/app/BlazeClawMFCView.h`
- `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`
- `BlazeClawMfc/web/chat/index.js`

### Native bridge change
`speech.transcribe` is no longer executed inline inside `HandleWebMessageJson(...)`.

Instead:
1. the WebView host view detects `method == "speech.transcribe"`
2. it captures the request frame and correlation id
3. it dispatches `RouteGatewayRequest(request)` on a detached worker thread
4. the worker posts a completion payload back to the view window using `CMgrMessage::PostOwnedPayloadToHwnd(...)`
5. the view handles that completion on the UI thread and emits the final `blazeclaw.gateway.rpc.result`

## Why this fixes the observed freeze
Before Step 3:
- the WebView `speech.transcribe` RPC was routed inline on the native bridge path
- that path stayed blocked during WAV parsing, preprocessing, ONNX inference, token decode, and synchronous transcript forwarding
- the MFC UI thread became unresponsive until the RPC returned

After Step 3:
- the expensive gateway/runtime work runs on a background worker thread
- the UI/native bridge thread is released immediately after scheduling the worker
- the final RPC result is delivered back to WebView once the worker completes

## Existing synchronous runtime preserved
Step 3 does **not** yet change the low-level inference implementation.

These pieces remain synchronous internally:
- `GatewayHost::TranscribeSpeech(...)`
- `SpeechRecognitionRuntime::Transcribe(...)`
- the current file-based WAV handoff
- the current nested transcript forwarding behavior in the speech handler

The difference is that those synchronous steps now execute under a worker-thread bridge wrapper rather than directly on the UI/native bridge thread.

## UI updates added in Step 3
In `BlazeClawMfc/web/chat/index.js`:
- the speech session state is set to `stage: "transcribing"` before awaiting the transcription result
- the button label changes to `Transcribing...`
- the transcribe button is disabled while the session state reports `transcribing`
- once the async RPC resolves, the normal state snapshot refresh path restores the final session state

## Design Notes
This Step 3 rollout intentionally chooses the narrowest safe boundary:
- keep the existing `speech.transcribe` API shape
- keep the existing WAV-path runtime input
- change the WebView host bridge from synchronous dispatch to background dispatch

This reduces risk while directly addressing the root cause discovered in Step 1: blocking work was running on the native bridge/UI path.

## What Step 3 Does Not Yet Do
Step 3 does **not** yet provide:
- a dedicated speech coordinator service
- queue admission/rejection semantics using `SpeechExecutionAccepted`
- incremental lifecycle event streaming back to WebView beyond the local `transcribing` UI state
- shutdown drain/join hardening for active speech workers
- explicit background cancellation wiring from WebView to worker-managed jobs

Those remain follow-up work for later plan steps, especially Step 5, Step 6, and Step 8.

## Validation Outcome
Expected validation target for Step 3:
- clicking `Transcribe` should no longer freeze the MFC UI while speech inference is running
- the WebView should show a visible `Transcribing...` state while the RPC is pending
- the final result should still arrive through the existing RPC result path
