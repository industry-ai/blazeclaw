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
Status: completed

Inspected and documented the exact execution path across these components:
- `BlazeClawMfc/web/chat/index.js`
- `BlazeClawMfc/web/chat/chat-controller.js`
- `BlazeClawMfc/src/gateway/GatewayHost.cpp`
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `BlazeClawMfc/src/core/GatewayHostBindingCoordinator.cpp`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`

Detailed trace output:
- `BlazeClawMfc/VOICE_STREAM_TO_ASR_STEP1_TRACE.md`

Step 1 findings:
- the WebView `Transcribe` click path awaits `speech.transcribe` synchronously from the browser layer
- the native WebView bridge routes `blazeclaw.gateway.rpc` inline and does not return `rpc.result` until `RouteGatewayRequest(...)` completes
- `speech.transcribe` calls `GatewayHost::TranscribeSpeech(...)` synchronously, which is bound directly to `SpeechRecognitionRuntime::Transcribe(...)`
- `SpeechRecognitionRuntime::Transcribe(...)` performs file I/O, preprocessing, ONNX inference, and decode in one blocking call while holding the runtime mutex
- on success, the speech handler also performs a nested synchronous `chat.send` dispatch before returning the RPC response
- cancellation exists inside the runtime, but the traced WebView path has no async boundary, so shutdown and UI responsiveness are still exposed to long-running STT work

Output of this step:
- identified which calls run on the UI/native bridge thread
- identified where the WebView request waits synchronously
- identified where cancellation and shutdown do not unwind cleanly

### Step 2: Define the speech execution contract
Status: completed

Introduced a clear contract for future non-blocking speech work while preserving the existing synchronous runtime inference API.

Implemented contract shape:
- kept the existing synchronous `Transcribe(...)` method for low-level runtime logic
- added orchestration-facing contract types above it instead of driving ONNX directly from the future WebView execution path
- defined a lifecycle with:
  - `Queued`
  - `Recording`
  - `Stopped`
  - `Transcribing`
  - `Completed`
  - `Failed`
  - `Cancelled`

Implemented runtime contract types:
- `SpeechExecutionStage`
- `SpeechExecutionState`
- `SpeechExecutionRequest`
- `SpeechExecutionAccepted`
- `SpeechExecutionStatus`
- `SpeechExecutionUpdateCallback`

Detailed contract output:
- `BlazeClawMfc/VOICE_STREAM_TO_ASR_STEP2_CONTRACT.md`

Files updated in this step:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/ISpeechRecognitionRuntime.h`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `BlazeClawMfc/src/gateway/GatewayHost.h`

Step 2 outcome:
- the codebase now has explicit contract types for admission, execution state, and lifecycle reporting
- the current synchronous speech runtime path remains intact for later reuse by the Step 3 background coordinator
- the new execution contract is separate from the existing `SpeechSessionStage` and `SpeechTranscribeResult`, reducing migration risk while enabling the next threading/orchestration step

### Step 3: Move transcription off the UI thread
Status: completed

Implemented a background execution path for STT at the WebView host bridge boundary.

Implemented rollout:
- `speech.transcribe` is no longer executed inline inside `CBlazeClawMFCView::HandleWebMessageJson(...)`
- the WebView host now routes `speech.transcribe` on a detached worker thread
- the worker posts completion back to the view window through existing async message infrastructure
- the view emits the final `blazeclaw.gateway.rpc.result` from the UI thread after the worker completes
- the WebView now shows a visible `Transcribing...` state while the async RPC is pending

Important constraint satisfied:
- `SpeechRecognitionRuntime::Transcribe(...)` is no longer invoked on the WebView/native bridge UI path

Detailed implementation output:
- `BlazeClawMfc/VOICE_STREAM_TO_ASR_STEP3_ASYNC_DISPATCH.md`

Files updated in this step:
- `BlazeClawMfc/src/app/BlazeClawMFCView.h`
- `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`
- `BlazeClawMfc/web/chat/index.js`

Step 3 outcome:
- the blocking speech transcription work is moved off the MFC UI/native bridge thread
- the existing synchronous gateway/runtime speech path is preserved underneath the new worker-thread wrapper
- the WebView receives the final speech RPC result through the same bridge result mechanism after background completion
- the UI now shows a transcribing state instead of appearing frozen during long-running STT work

### Step 4: Decide the audio handoff boundary
Status: completed

Formalized the current audio boundary as an explicit WAV-file handoff contract so the existing recording artifact remains the first stable input to background STT.

Implemented rollout:
1. kept the current WAV-file boundary first
   - stop recording
   - hand the saved WAV path to background STT
   - validate end-to-end responsiveness first
2. deferred true in-memory streaming/chunking until a later step

Implemented contract shape:
- added `SpeechAudioHandoffMode` with `WavFile` and future-facing `PcmStream`
- added `SpeechAudioArtifact` for path, MIME/container, sample rate, channels, bit depth, and duration metadata
- attached `audioArtifact` to execution/session state and to gateway/runtime speech request models
- advertised the active handoff boundary from `speech.capabilities.get` using:
  - `audioHandoffMode: wav_file`
  - `audioMimeType: audio/wav`
  - `audioContainer: wav`
  - `streamingSupported: false`

Detailed implementation output:
- `BlazeClawMfc/VOICE_STREAM_TO_ASR_STEP4_HANDOFF_BOUNDARY.md`

Files updated in this step:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/ISpeechRecognitionRuntime.h`
- `BlazeClawMfc/src/gateway/GatewayHost.h`
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

Step 4 outcome:
- the codebase now models the current audio artifact explicitly instead of relying only on a bare `audioPath`
- the current runtime remains file-based and compatible with the existing recorder output
- capabilities now make it explicit that WAV-file handoff is supported today and live streaming is not yet enabled
- future streaming work can be added as a new handoff mode without reinterpreting the current stable boundary

### Step 5: Add a speech transcription coordinator
Status: completed

Implemented a dedicated speech transcription coordinator that now owns admission, active-run tracking, duplicate-session protection, and runtime-backed orchestration for speech transcription requests.

Implemented rollout:
- added `SpeechTranscriptionCoordinator` under `BlazeClawMfc/src/core/`
- accepted `SpeechExecutionRequest` separately from low-level runtime execution
- tracked execution state by coordinator-owned `runId`
- mapped in-flight `sessionId -> runId`
- rejected duplicate in-flight transcription for the same session
- delegated actual inference to the existing `SpeechRecognitionRuntime::Transcribe(...)`
- forwarded cancellation to the existing runtime cancel path

Gateway/runtime wiring added in this step:
- `GatewayHost` now exposes coordinator-facing methods for:
  - `AcceptSpeechTranscription(...)`
  - `GetSpeechExecutionStatus(...)`
  - `CancelSpeechTranscription(...)`
- `GatewayHostBindingCoordinator.cpp` now binds speech transcription through the coordinator instead of binding directly to `SpeechRecognitionRuntime::Transcribe(...)`
- `speech.transcribe` now performs admission before execution and returns coordinator-managed execution metadata in the response payload

Detailed implementation output:
- `BlazeClawMfc/VOICE_STREAM_TO_ASR_STEP5_TRANSCRIPTION_COORDINATOR.md`

Files updated in this step:
- `BlazeClawMfc/src/core/SpeechTranscriptionCoordinator.h`
- `BlazeClawMfc/src/core/SpeechTranscriptionCoordinator.cpp`
- `BlazeClawMfc/src/core/ServiceManager.h`
- `BlazeClawMfc/src/core/GatewayHostBindingCoordinator.cpp`
- `BlazeClawMfc/src/gateway/GatewayHost.h`
- `BlazeClawMfc/src/gateway/GatewayHost.cpp`
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `BlazeClawMfc/BlazeClawMfc.vcxproj`

Step 5 outcome:
- the codebase now has an explicit service dedicated to speech execution orchestration
- duplicate in-flight transcription for the same session is blocked consistently
- run/session ownership is modeled above the runtime instead of being implicit in handlers
- later lifecycle streaming and shutdown hardening now have a coordinator-level source of truth to build on

### Step 6: Stream state back to WebView
Status: completed

Expose stage updates so the user sees progress instead of a hung button.

Implemented rollout:
- added a dedicated speech lifecycle bridge topic (`speech.lifecycle`) and compatibility channel (`blazeclaw.gateway.speech.lifecycle`)
- emitted recording lifecycle updates from the WebView-host RPC boundary for:
  - `gateway.speech.startRecording` -> `recording`
  - `gateway.speech.stopRecording` -> `stopped`
- emitted transcription lifecycle updates from the async transcription boundary for:
  - queued before detached dispatch
  - transcribing before runtime execution
  - terminal stage (`completed`/`failed`/`cancelled`) from the final `speech.transcribe` response payload on UI-thread completion
- normalized canonical transport speech lifecycle events in `chat-events.js` and routed them into controller state updates
- added controller-level `applySpeechLifecycleUpdate(...)` to merge streamed lifecycle payloads into `state.speechSessionState`
- updated the WebView speech button/UI behavior to render from streamed lifecycle state:
  - `Recording... (click to stop)` while recording
  - `Queued...` while queued
  - `Transcribing...` while transcribing
  - restored `Transcribe` on terminal states

Minimum events now emitted:
- recording started
- recording stopped
- transcription queued
- transcription started
- transcription completed
- transcription failed
- transcription cancelled

WebView behavior updates completed:
- disable/relabel `Transcribe` button while background STT is running
- show status text for queued/transcribing states
- restore controls on completion/failure/cancel

Detailed implementation output:
- `BlazeClawMfc/VOICE_STREAM_TO_ASR_STEP6_WEBVIEW_STATE_STREAMING.md`

Files updated in this step:
- `BlazeClawMfc/src/app/EventTransport.h`
- `BlazeClawMfc/src/app/EventTransport.cpp`
- `BlazeClawMfc/src/app/BlazeClawMFCView.h`
- `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`
- `BlazeClawMfc/web/chat/chat-events.js`
- `BlazeClawMfc/web/chat/chat-controller.js`
- `BlazeClawMfc/web/chat/index.js`

Step 6 outcome:
- speech lifecycle now streams through the native bridge into WebView state
- the speech button no longer relies on optimistic local toggles only
- queued/transcribing/terminal progress is visible in Web chat state while preserving the Step 3 async transcription boundary

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
