# Voice Stream to ASR Step 1 Trace

## Scope
This document captures Step 1 of `BlazeClawMfc/VOICE_STREAM_TO_ASR_WIRING_PLAN.md`: tracing the current record -> stop -> transcribe path, identifying UI-thread execution, synchronous wait points, and shutdown/cancellation gaps.

## Traced Files
Primary scope:
- `BlazeClawMfc/web/chat/index.js`
- `BlazeClawMfc/web/chat/chat-controller.js`
- `BlazeClawMfc/src/gateway/GatewayHost.cpp`
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `BlazeClawMfc/src/core/GatewayHostBindingCoordinator.cpp`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`

Supporting bridge reference used to identify the synchronous wait boundary:
- `BlazeClawMfc/src/app/BlazeClawMFCView.cpp`

## Current End-to-End Flow

### 1. WebView button starts recording
File: `BlazeClawMfc/web/chat/index.js:2497-2511`
- Clicking `speechTranscribeBtn` enters the click handler.
- The first click awaits `controller.request("gateway.speech.startRecording", {})`.
- If successful, the button label changes to `Recording... (click to stop)`.

### 2. WebView button stops recording and immediately requests STT
File: `BlazeClawMfc/web/chat/index.js:2514-2533`
- The second click awaits `controller.request("gateway.speech.stopRecording", {})`.
- The returned `audioPath` is read from the payload.
- The handler then immediately awaits `controller.transcribeSpeech({ audioPath, prompt })`.
- Because this `await` stays inside the click handler, the UI state restoration is deferred until STT returns or throws.

### 3. WebView speech controller issues a synchronous RPC-style request
File: `BlazeClawMfc/web/chat/chat-controller.js:1886-1926`
- `transcribeSpeech(...)` builds a `speech.transcribe` request with `sessionId`, `runId`, `prompt`, and optional `audioPath`/`language`.
- It then awaits `requestWithOverride("speech.transcribe", transcriptRequest, requestOverride)`.

Supporting request helper:
- `BlazeClawMfc/web/chat/chat-controller.js:690-705`
- `requestWithOverride(...)` falls through to `request(...)` unless an override is injected.
- `request(...)` stores a pending promise resolver and posts `channel: "blazeclaw.gateway.rpc"` to the native host.

### 4. Native WebView message handling performs gateway routing inline
Supporting reference: `BlazeClawMfc/src/app/BlazeClawMFCView.cpp:3495-3592`
- `HandleWebMessageJson(...)` receives `blazeclaw.gateway.rpc` messages from WebView2.
- It constructs a `RequestFrame` and calls `app->RouteGatewayRequest(request)` inline.
- Only after `RouteGatewayRequest(...)` returns does it emit `blazeclaw.gateway.rpc.result` back to the WebView.

## Threading implication
This is the main synchronous wait boundary discovered in Step 1:
- the WebView promise is not resolved until `RouteGatewayRequest(...)` completes
- `speech.transcribe` therefore occupies the native bridge call path end-to-end
- any long-running STT work prevents the WebView-side `await` from completing

## Gateway recording flow

### 5. Start recording
File: `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp:133-143`
- `gateway.speech.startRecording` dispatches to `host.StartNativeRecording()`.

File: `BlazeClawMfc/src/gateway/GatewayHost.cpp:307-357`
- `StartNativeRecording()` locates the main frame.
- It builds a WAV output path in `bin/Debug/BlazeClawRecordings`.
- If an active `CChatView` exists, it uses that recorder.
- Otherwise it uses the fallback `CVoiceRecorder` attached to the main frame HWND.
- This path completes quickly and returns only `{ ok: true }`.

### 6. Stop recording
File: `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp:145-155`
- `gateway.speech.stopRecording` dispatches to `host.StopNativeRecording()`.

File: `BlazeClawMfc/src/gateway/GatewayHost.cpp:359-400`
- `StopNativeRecording()` stops either the active chat recorder or the fallback recorder.
- It returns the saved WAV path as UTF-8 `audioPath`.
- This path is also short-lived compared with STT.

## Gateway STT flow

### 7. speech.transcribe handler performs STT inline
File: `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp:345-613`
- The handler reads `audioPath`, `language`, `prompt`, `sessionId`, and `runId`.
- It immediately calls `host.TranscribeSpeech(...)`.
- After transcription returns, it normalizes lifecycle/error data, emits telemetry, optionally forwards the transcript into `chat.send`, and then builds the RPC response payload.

### 8. Gateway callback binding is synchronous
File: `BlazeClawMfc/src/core/GatewayHostBindingCoordinator.cpp:237-262`
- `SetSpeechTranscribeCallback(...)` binds gateway STT directly to `manager.m_speechRecognitionRuntime.Transcribe(...)`.
- The callback executes synchronously and returns only after the runtime finishes.
- The gateway snapshot is refreshed only after `Transcribe(...)` returns.

### 9. Runtime transcription is file-based and synchronous
File: `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
Key sections:
- lock acquisition and request validation: `Transcribe(...)`
- WAV parse / resample / feature extraction: preprocessing block
- ONNX encoder/decoder inference loop: synchronous `decoderInit->Run(...)` path
- transcript decode and result building: final section of `Transcribe(...)`

Observed behavior from the implementation:
- `Transcribe(...)` holds `m_mutex` for the full method duration.
- It performs file I/O (`std::ifstream`), WAV parsing, resampling, log-mel generation, ONNX inference, token decode, and result assembly in one call.
- The decoder loop can run for up to `kMaxDecodeSteps = 512`.
- No background dispatch exists between gateway request handling and runtime inference.

## Nested synchronous work after STT
File: `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp:464-524`
- If transcription succeeds and returns text, the handler performs an additional inline dispatch to `chat.send`.
- That means the `speech.transcribe` RPC does not complete after STT alone; it may also wait for chat-send orchestration work.

## Findings Required by Step 1

### A. Calls running on the UI/native bridge thread
Confirmed on the current path:
- WebView button click handler waits on recording and transcription promises.
- `BlazeClawMfc/src/app/BlazeClawMFCView.cpp:3551-3557` routes the RPC inline before responding.
- `speech.transcribe` gateway handling, runtime callback binding, runtime preprocessing/inference/decode, and transcript forwarding all execute before the RPC result is returned.

### B. Where the WebView waits synchronously
Primary synchronous waits:
1. `index.js` awaits `controller.transcribeSpeech(...)`
2. `chat-controller.js` awaits `requestWithOverride("speech.transcribe", ...)`
3. `BlazeClawMFCView.cpp` waits for `RouteGatewayRequest(...)`
4. `GatewayHost.Handlers.Runtime.SpeechRecognition.cpp` waits for `host.TranscribeSpeech(...)`
5. `GatewayHostBindingCoordinator.cpp` waits for `SpeechRecognitionRuntime::Transcribe(...)`
6. `SpeechRecognitionRuntime.cpp` blocks through file I/O, preprocessing, ONNX inference, and decode
7. on success, `speech.transcribe` also synchronously dispatches `chat.send`

## Why the WebView appears stuck
The button click path does not regain control until the entire synchronous RPC finishes. Because STT and transcript forwarding both run inline, the WebView cannot update to a progress state beyond the pending promise, and the native UI can appear frozen if the inference path is long-running.

## Cancellation and shutdown gaps
Current issues observed from the traced code:
- `SpeechRecognitionRuntime::Cancel(...)` exists, but the WebView transcribe path traced in Step 1 does not expose a dedicated cancellation route for an in-flight `speech.transcribe` request.
- `SpeechRecognitionRuntime::Transcribe(...)` checks cancellation internally, but only after the synchronous request is already in flight.
- The WebView request path has no queue/worker boundary, so shutdown can intersect active STT on the same native bridge path.
- Successful STT also chains into synchronous `chat.send`, extending the time before the request unwinds.
- This combination explains the observed symptom set: recorded WAV file exists, but UI does not return and process exit can hang until forced.

## Step 1 Conclusion
Step 1 confirms that the current voice pipeline is not yet wired as a background speech pipeline. The recorded WAV file is produced correctly, but the `speech.transcribe` request remains a synchronous, inline bridge call that performs:
1. file-based STT
2. ONNX inference and decode
3. optional synchronous `chat.send` forwarding
before the WebView receives a response.

## Immediate Implication for Step 2+
The highest-priority fix is architectural, not model-loading:
- introduce a background orchestration layer above `SpeechRecognitionRuntime::Transcribe(...)`
- decouple WebView RPC completion from long-running inference
- emit lifecycle/progress back through events rather than holding the bridge request open for the full STT+chat-send duration
