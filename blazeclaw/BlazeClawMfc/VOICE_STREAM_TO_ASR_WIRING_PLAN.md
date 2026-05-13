# Voice Stream to ASR Wiring Plan

## Goal
Wire live microphone audio captured from the WebView `Transcribe` flow into the `qwen3-asr-1.7b-onnx` speech runtime so the UI does not block, the app remains responsive, and completed transcripts are returned to the WebView chat flow.

## Current Observations
- Recording already works and writes a WAV file under `bin/Debug/BlazeClawRecordings`.
- The ASR model now loads successfully at startup.
- The current transcribe path can block the UI thread, causing the WebView to appear stuck and the app to hang on exit.
- The existing `SpeechRecognitionRuntime::Transcribe(...)` implementation is file-based and synchronous.

## Desired End State
1. Voice capture starts from the WebView `Transcribe` button.
2. PCM/WAV audio is handed off to a background speech pipeline.
3. `qwen3-asr-1.7b-onnx` performs STT without blocking the UI thread.
4. Session/progress/error state is reported back to the WebView.
5. Final transcript is injected into the existing chat send/orchestration flow.
6. Cancel/stop/exit paths cleanly shut down without hanging the process.

## Step-by-Step Plan

### Step 1: Trace the current record -> stop -> transcribe flow
Inspect and document the exact execution path across these components:
- `BlazeClawMfc/web/chat/index.js`
- `BlazeClawMfc/web/chat/chat-controller.js`
- `BlazeClawMfc/src/gateway/GatewayHost.cpp`
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `BlazeClawMfc/src/core/GatewayHostBindingCoordinator.cpp`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`

Output of this step:
- identify which calls run on the UI thread
- identify where the WebView request waits synchronously
- identify where cancellation and shutdown do not unwind cleanly

### Step 2: Define the speech execution contract
Introduce a clear runtime contract for non-blocking speech work.

Recommended shape:
- keep the existing synchronous `Transcribe(...)` method for low-level runtime logic
- add an async orchestration layer above it instead of driving ONNX directly from the WebView request path
- define a request/result lifecycle with:
  - `queued`
  - `recording`
  - `stopped`
  - `transcribing`
  - `completed`
  - `failed`
  - `cancelled`

Files likely involved:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/ISpeechRecognitionRuntime.h`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `BlazeClawMfc/src/gateway/GatewayHost.h`

### Step 3: Move transcription off the UI thread
Create a background execution path for STT.

Recommended implementation:
- queue speech transcription work onto a worker thread or runtime work queue
- return control to the WebView immediately after the request is accepted
- publish progress/completion back through the existing gateway event/bridge mechanism

Important constraint:
- do not run `SpeechRecognitionRuntime::Transcribe(...)` directly on the WebView/UI call path

Files likely involved:
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `BlazeClawMfc/src/core/GatewayHostBindingCoordinator.cpp`
- existing async posting/event helper infrastructure already used by chat runtime flows

### Step 4: Decide the audio handoff boundary
Choose how recorded audio reaches the ASR runtime.

Preferred rollout order:
1. keep the current WAV-file boundary first
   - stop recording
   - hand the saved WAV path to background STT
   - validate end-to-end responsiveness first
2. only after that, add true in-memory streaming/chunking if still needed

Reason:
- the runtime already accepts `audioPath`
- this minimizes risk and isolates the hang to threading/orchestration rather than model math

### Step 5: Add a speech transcription coordinator
Create a coordinator/service dedicated to speech session orchestration.

Responsibilities:
- accept transcribe requests from gateway/UI
- own worker-thread dispatch
- map `runId` / `sessionId`
- propagate stage transitions
- collect completion/error state
- handle cancellation during decode/inference
- avoid duplicate in-flight transcription for the same session unless explicitly allowed

Suggested location:
- `BlazeClawMfc/src/core/` or `BlazeClawMfc/src/gateway/` beside other orchestration coordinators

### Step 6: Stream state back to WebView
Expose stage updates so the user sees progress instead of a hung button.

Minimum events to emit:
- recording started
- recording stopped
- transcription queued
- transcription started
- transcription completed
- transcription failed
- transcription cancelled

WebView behavior updates:
- disable/relabel `Transcribe` button while background STT is running
- show status text for queued/transcribing states
- restore controls on completion/failure/cancel

Files likely involved:
- `BlazeClawMfc/web/chat/index.js`
- `BlazeClawMfc/web/chat/chat-controller.js`
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

### Step 7: Inject final transcript into the existing chat pipeline
When STT completes successfully:
- publish the transcript as the user message
- preserve `sessionId`, `runId`, audio path, language, and latency metadata
- reuse the existing `chat.send` / orchestration entry path already used today

Validation target:
- a completed transcript should flow through the same skills/tool-routing/chat logic as typed input

### Step 8: Harden cancellation and shutdown
Ensure app exit and debug stop do not hang.

Required safeguards:
- cancel in-flight speech jobs on shutdown
- avoid waiting forever on worker completion from the UI thread
- ensure any background thread checks cancellation before preprocessing, inference, and decode loops
- make sure recorder shutdown and speech worker shutdown happen in deterministic order

Files likely involved:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
- gateway/service shutdown wiring in app/core bootstrap code
- recorder shutdown path in gateway fallback recording flow

### Step 9: Expand diagnostics
Add targeted debug logging for speech orchestration.

Recommended log lines:
- request accepted with `sessionId`, `runId`, `audioPath`
- background transcription started
- preprocessing completed with sample rate / duration / frames
- inference completed with latency
- final transcript length / language
- cancellation source
- shutdown drain/cancel summary

This should make it obvious whether the stall is:
- recording
- file handoff
- queue dispatch
- ONNX inference
- callback/event delivery
- UI state restoration

### Step 10: Validate incrementally
Validate in small passes:

1. **Recording only**
   - confirm WAV is produced
   - confirm UI remains responsive
2. **Background file-based STT**
   - manually trigger transcription from saved WAV
   - confirm final transcript returns
3. **WebView integrated STT**
   - click `Transcribe`
   - confirm status updates and final transcript
4. **Cancellation**
   - cancel during transcription
   - ensure UI recovers
5. **Shutdown**
   - close app during/after transcription
   - ensure no hang

## Recommended Implementation Order
1. Fix threading first: make STT background-only.
2. Keep WAV-path handoff first.
3. Add progress events.
4. Add shutdown/cancel hardening.
5. Only then consider true live audio chunk streaming into ASR.

## Why this order
The current failure mode looks primarily like a blocking orchestration problem, not a model-loading problem. Using the already-working WAV capture path as the first handoff to `qwen3-asr-1.7b-onnx` gives the fastest path to a stable end-to-end STT flow before attempting lower-level streaming changes.
