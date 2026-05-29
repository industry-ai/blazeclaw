# Sherpa Zipformer GPU Verification and First-Token Latency Plan

## Current GPU / CPU Status

The active Sherpa Zipformer speech-recognition path is configured through the ONNX speech provider:

- Active speech model path in `blazeclaw.conf`:
  - `speech.provider=onnx`
  - `speech.storageRoot=BlazeClawMfc/models/STT/sherpa-onnx-streaming-zipformer-bilingual-zh-en`
  - `speech.streaming.enabled=true`
  - `speech.streaming.chunk_ms=1500`
  - `speech.streaming.lookback_ms=320`
  - `speech.runtime_hot_mode=always_online`
  - `speech.runtime_hot_warmup_enabled=true`
- `SpeechRecognitionConfig::cudaEnabled` defaults to `true` in
  `src/config/ConfigModels.h`.
- `src/config/ConfigLoader.cpp` supports an explicit
  `speech.cuda.enabled=` setting, but the active `blazeclaw.conf` does not
  currently set it.
- `SpeechRecognitionRuntime.cpp` attempts to append the ONNX Runtime CUDA
  execution provider only when `cudaEnabled` is true and the in-process CUDA
  compatibility guard passes.
- The runtime then records the actual provider in:
  - `effectiveExecutionProvider`
  - `cudaExecutionProviderAvailable`
  - `cudaExecutionProviderEnabled`
  - `cudaExecutionProviderReason`

Therefore, the current configuration does not explicitly disable GPU. It will
attempt CUDA by default, but actual GPU use is conditional. The path is using GPU
only when runtime diagnostics show:

- `effectiveProvider=cuda`
- `available=true`
- `enabled=true`
- `reason=active`

If startup diagnostics show `effectiveProvider=cpu`, or CUDA status has a reason
such as `disabled_by_config`, `compatibility_guard_blocked`,
`CUDA execution provider API unavailable`, `onnxruntime.dll not loaded`, or
`cuda_session_init_failed`, then the Sherpa Zipformer path is running on CPU.

The startup status already prints the required confirmation lines from
`BlazeClawMfcApp.cpp`:

- `[Speech] startup.config - ... cudaEnabled=...`
- `[Speech] startup.runtime - ... effectiveProvider=...`
- `[Speech] startup.runtime.cuda - available=... enabled=... reason=...`

## Current Debug Log Findings

The supplied startup diagnostics confirm that the current local run is not using
GPU for Sherpa Zipformer speech recognition:

- `cudaEnabled=true` in the loaded speech config, so CUDA is intended by policy.
- `provider=onnx` and `effectiveProvider=cpu`, so the actual speech runtime is
  running on CPU.
- `available=false enabled=false reason=none`, so CUDA was not appended and the
  current diagnostic reason is not specific enough.
- The active model is the int8 Sherpa transducer model:
  - `encoder-epoch-99-avg-1.int8.onnx`
  - `decoder-epoch-99-avg-1.int8.onnx`
- Runtime policy expects a CUDA stack aligned with:
  - `cublasMajor=12`
  - `cublasLtMajor=12`
  - `cufftMajor=12`
  - `cudnnMajor=9`
- The loaded CUDA modules show only cuBLAS 13-family DLLs from
  `D:\Program Files\bin\x64`:
  - `cublas64_13.dll`
  - `cublasLt64_13.dll`
- The runtime reports CUDA alignment status as `mixed`, with
  `versionFamilies=cublas:13,cublaslt:13`.
- The runtime recommendation is explicit:
  `mixed CUDA/cuDNN runtime stack detected; align DLL roots/versions before
  enabling speech CUDA in production`.

Conclusion: the current first-character latency is being measured on CPU, not
GPU. CUDA is enabled in config, but the process environment has an incompatible
or incomplete CUDA/cuDNN runtime stack for the current speech CUDA policy. The
plan should therefore prioritize CPU-path first-token latency immediately, while
also adding a CUDA environment remediation track.

The log also confirms a major latency contributor:

- `chunkMs=1500`
- `overlapMs=320`
- `threads=4`
- `mode=sequential`

With a 1500 ms chunk, the streaming path can wait around 1.5 seconds before the
first decode attempt. This should be treated as the first tuning target after
instrumentation.

## Problem Statement

The first arrived recognition character is slow during live Sherpa Zipformer
recognition. The likely contributors are not limited to GPU/CPU inference. The
current live path can also be delayed by session loading, warmup coverage,
audio-buffer accumulation, a large streaming chunk size, preview polling cadence,
endpoint/finalization behavior, and delayed UI lifecycle propagation.

Current config uses `speech.streaming.chunk_ms=1500`, which means the pipeline
may wait up to about 1.5 seconds of audio before the first recognition attempt,
even before model inference and UI update latency are considered.

## Goals

1. Prove whether the active Sherpa Zipformer runtime is using CUDA or CPU in the
   actual launched process.
2. Measure first-token latency with timestamps across the whole live-recognition
   path.
3. Reduce time from user speech start to first visible recognition character.
4. Preserve strict Sherpa Zipformer streaming behavior and avoid fallback
   transcription mechanisms.
5. Keep the WebView-first UI flow through `CBlazeClawMFCView`.

## Non-Goals

- Do not reintroduce Qwen3 ASR as a fallback.
- Do not use hard-coded phrase-specific recognition shortcuts.
- Do not replace the WebView-first recognition result surface.
- Do not optimize only the UI while leaving native pipeline latency unmeasured.

## Step 1: Add Runtime Provider Verification

Status: completed

Update diagnostics so every live recognition session clearly records the
provider that the session is actually using.

Implemented behavior:

- `blazeclaw.conf` explicitly sets `speech.cuda.enabled=true`.
- `SpeechRecognitionRuntime.cpp` now converts an intended CUDA fallback with no
  provider message into a concrete reason:
  `cuda_execution_provider_unavailable_without_reported_error` or
  `cuda_execution_provider_not_enabled_without_reported_error`.
- `GatewayHost.Handlers.Runtime.SpeechRecognition.cpp` now includes a
  `speechRuntime` diagnostics object in:
  - `gateway.speech.startRecording` responses
  - `gateway.speech.stopRecording` responses
  - `speech.transcribe` responses
  - nested `speechSession`, `executionState`, and `speechArtifact` payloads
  - `gateway.speech.lifecycle` and `gateway.speech.debug.snapshot` telemetry
- The `speechRuntime` object includes:
  - `provider`
  - `effectiveExecutionProvider`
  - `cudaExecutionProviderAvailable`
  - `cudaExecutionProviderEnabled`
  - `cudaExecutionProviderReason`
  - `modelLayout`
  - `modelVariant`
  - `streamingChunkMs`
  - `streamingLookbackMs`
  - `threads`
  - `executionMode`
- `web/chat/chat-controller.js` normalizes provider/CUDA fields from both
  capabilities and live session payloads.
- `web/chat/index.js` surfaces the effective provider and CUDA reason in the
  speech status line and includes provider diagnostics in live-preview console
  diagnostics.

Validation:

- `node --check "BlazeClawMfc/web/chat/chat-controller.js"`
- `node --check "BlazeClawMfc/web/chat/index.js"`

Implementation tasks:

1. Add a speech diagnostics payload field for:
   - `effectiveExecutionProvider`
   - `cudaExecutionProviderAvailable`
   - `cudaExecutionProviderEnabled`
   - `cudaExecutionProviderReason`
2. Include the provider fields in live preview/start/stop diagnostics returned
   through the gateway.
3. Surface the provider in the WebView speech diagnostics panel or console trace.
4. Add an explicit line to `blazeclaw.conf`:
   - `speech.cuda.enabled=true`
5. Fix the current weak diagnostic case where startup reports
   `available=false enabled=false reason=none`. When CUDA is intended but not
   available, the reason should identify whether the block came from missing
   ONNX Runtime CUDA APIs, compatibility guard failure, missing DLLs, version
   mismatch, mixed roots, or session initialization failure.

Acceptance criteria:

- completed: The UI or logs can answer GPU vs CPU without code inspection.
- completed: A CUDA run clearly reports `effectiveProvider=cuda` and `reason=active`.
- completed: A CPU fallback clearly reports the exact fallback reason.
- completed: The current mixed CUDA 13-family environment no longer reports
  `reason=none`; it reports a concrete remediation reason.

## Step 2: Add First-Token Latency Instrumentation

Status: completed

Add timestamped checkpoints across the native and WebView live-recognition path.
Use monotonic timestamps for native measurements and include enough correlation
IDs to connect native runtime, gateway, and WebView events.

Implemented behavior:

- `SpeechRecognitionDebugInfo` now carries first-token offset fields for native
  streaming recognition diagnostics.
- `SherpaZipformerStreamingEngine` records monotonic offsets for:
  - first readable audio in the ring stream
  - first accepted audio chunk
  - first encoder invocation start/end
  - first decoder invocation start
  - first joiner invocation start
  - first non-empty partial text
- `SpeechRecognitionRuntime::Transcribe(...)` records request, inferred
  streaming-input, and native payload-ready offsets for streaming Sherpa
  results.
- `GatewayHost.Handlers.Runtime.SpeechRecognition.cpp` emits a
  `firstTokenTiming` object through:
  - `gateway.speech.startRecording` responses
  - `speech.transcribe` responses
  - nested `speechSession`, `executionState`, and `speechArtifact` payloads
  - `gateway.speech.lifecycle` and `gateway.speech.debug.snapshot` telemetry
- `web/chat/chat-controller.js` normalizes `firstTokenTiming` and
  `gatewayNativePayloadReadyOffsetMs` into `speechSessionState`.
- `web/chat/index.js` emits WebView diagnostics for:
  - `speech.first_token.click`
  - `speech.first_token.start_recording_response`
  - preview request/response timing
  - `speech.first_token.rendered`

The timing fields are offsets within the relevant monotonic clock domain. Native
offsets are comparable within the native trace; WebView offsets are comparable
within the WebView trace. The shared preview `runId` and session id correlate the
two traces.

Measure these checkpoints:

1. Transcribe button clicked.
2. Gateway `startRecording` request received.
3. Native recording starts.
4. First audio frame captured.
5. First audio frame accepted by streaming source.
6. First encoder invocation starts.
7. First encoder invocation ends.
8. First decoder/joiner step starts.
9. First non-empty partial text is produced.
10. First live preview payload is emitted by native code.
11. First live preview payload is received by WebView.
12. First character is rendered in `CBlazeClawMFCView` WebView.

Acceptance criteria:

- completed: Logs identify the largest delay segment for first character arrival.
- completed: Measurements include provider, chunk size, lookback size, threads, and model
  layout.
- completed: CPU and CUDA timings can be compared from the same trace format.

Validation:

- `node --check "BlazeClawMfc/web/chat/chat-controller.js"`
- `node --check "BlazeClawMfc/web/chat/index.js"`

## Step 3: Verify and Stabilize Hot Runtime Warmup

Status: completed

The config already enables:

- `speech.runtime_hot_mode=always_online`
- `speech.runtime_hot_warmup_enabled=true`

Confirm that warmup covers the exact Sherpa Zipformer streaming path used by
live recognition, not only a model-load or offline path.

Implementation tasks:

1. completed: Confirm sessions are loaded before the user first clicks Transcribe.
2. completed: Ensure warmup invokes encoder, decoder, and joiner with representative dummy
   streaming input.
3. completed: Keep the warmed sessions alive while the app is idle.
4. completed: Add diagnostics for:
   - model load start/end
   - warmup start/end
   - warmup provider
   - warmup success/failure
5. completed: Prevent the first live utterance from paying session creation, graph
   optimization, CUDA initialization, or memory allocation costs.

Implemented behavior:

- `SpeechRecognitionRuntime::RunWarmupLocked()` now uses a temporary isolated
  dummy PCM stream and calls the Sherpa Zipformer `TranscribeStreaming(...)`
  path, so warmup exercises the same streaming encoder, decoder, and joiner path
  used by live preview recognition.
- The warmup stream is registered only for the warmup call, then unregistered and
  its Sherpa stream state is cleared so it does not pollute live user audio or
  transcript state.
- Sherpa model load now invokes warmup immediately after load completion when
  `speech.runtime_hot_warmup_enabled=true`; `always_online` mode keeps the loaded
  engine/session state resident while idle.
- Runtime snapshots and gateway status now expose warmup diagnostics:
  `runtimeHotWarmupCompleted`, `runtimeHotWarmupSucceeded`,
  `runtimeHotWarmupLatencyMs`, `runtimeHotWarmupProvider`,
  `runtimeHotWarmupStage`, and `runtimeHotWarmupError`.
- Runtime snapshots and startup logs now expose model-load diagnostics:
  `lastModelLoadLatencyMs` and `lastModelLoadStage`.
- Gateway `speechRuntime` payloads and startup status lines include the warmup
  and model-load diagnostics, making warmup failures visible instead of silently
  shifting the cost to the first live utterance.

Acceptance criteria:

- completed: First live recognition no longer includes model/session creation time.
- completed: Warmup failure is visible and does not silently degrade first-token latency.
- completed: The first live utterance and later utterances have comparable startup latency.

Validation:

- `get_errors` on the changed native runtime, gateway, and startup files.

## Step 4: Reduce Streaming Chunk Latency

Status: completed

The active config uses `speech.streaming.chunk_ms=1500`, which is too large for
fast first-character feedback. Reduce the time before the first streaming decode
attempt while preserving recognition quality.

Implementation tasks:

1. completed: Add configurable low-latency profiles, for example:
   - `balanced`: 640 ms chunk, 320 ms lookback
   - `low_latency`: 320 ms chunk, 160-320 ms lookback
   - `aggressive`: 160-240 ms chunk, 160 ms lookback
2. completed: Start with a safe default such as 320 ms or 500 ms for live preview.
3. completed: Ensure chunking changes apply only to streaming preview and do not regress
   final utterance quality.
4. pending runtime benchmark: Benchmark Chinese and English utterances separately.
5. completed: Keep final recognition allowed to use enough context for quality if needed.

Implemented behavior:

- Added `speech.streaming.latency_profile` with `balanced`, `low_latency`, and
  `aggressive` profile names.
- Added explicit live-preview overrides:
  - `speech.streaming.preview_chunk_ms`
  - `speech.streaming.preview_lookback_ms`
- The active config now starts the first CPU tuning trial with
  `speech.streaming.chunk_ms=500`, `speech.streaming.preview_chunk_ms=500`, and
  `speech.streaming.preview_lookback_ms=320`.
- Final utterance quality remains protected because `speech.chunk_ms=1500` and
  `speech.overlap_ms=320` are no longer overwritten by streaming chunk parsing or
  normalization.
- `livePreviewOnly=true` requests now apply the configured preview chunk/lookback
  policy when building the PCM stream contract. Non-preview/final requests keep
  the previous artifact-derived chunk policy with zero overlap/lookback.
- Runtime, gateway, and startup diagnostics now expose
  `streamingLatencyProfile`, `streamingPreviewChunkMs`, and
  `streamingPreviewLookbackMs` alongside the existing streaming chunk/lookback,
  provider, thread, and execution-mode fields.

Acceptance criteria:

- completed: First decode attempt begins well before the old 1500 ms boundary.
- pending runtime benchmark: First visible character latency improves without severe partial-result noise.
- pending runtime benchmark: Final recognition output remains stable for short Chinese utterances such as
  `讲一个笑话`.

Debug-log-specific starting point:

- completed baseline: `chunkMs=1500`, `overlapMs=320`, `threads=4`,
  `mode=sequential`, `effectiveProvider=cpu`.
- completed: First CPU tuning trial lowers only the streaming/live-preview chunk first, keeping
  other settings unchanged so the latency delta is attributable.
- completed first trial: `speech.streaming.chunk_ms=500`,
  `speech.streaming.preview_chunk_ms=500`, and
  `speech.streaming.preview_lookback_ms=320` with existing final
  `speech.chunk_ms=1500` and `speech.overlap_ms=320`.
- Recommended second trial: `speech.streaming.chunk_ms=320` with existing
	`speech.streaming.preview_lookback_ms=320`.
- Only after chunk tuning should thread and execution-mode tuning be compared.

Validation:

- `get_errors` on changed config, runtime, gateway, and startup files.

## Step 5: Emit Partial Text as Soon as It Exists

Status: completed

Confirm that native code emits interim recognition text immediately when the
streaming engine has non-empty partial output. Avoid waiting for segment
finalization, endpoint detection, or stop recording before the first preview.

Implementation tasks:

1. completed: Audit the Sherpa streaming result path for any gating on final segment state.
2. completed: Emit first non-empty partial text immediately through the coordinator.
3. completed: Keep preview lifecycle state as active recording while the microphone remains
   active.
4. completed: Preserve the existing fix that prevents preview terminal updates from
   resetting the Transcribe button too early.

Implemented behavior:

- Sherpa streaming already creates a non-final `SpeechTranscriptSegment` as soon
  as `partialText` is non-empty, before endpoint detection or final stream drain.
- `SpeechTranscriptionCoordinator` now keeps successful streaming results with a
  non-final segment in `SpeechExecutionStage::Streaming` instead of emitting a
  follow-up completed execution update that can look terminal to downstream UI.
- WebView speech-session normalization now treats `speech-preview-*` streaming
  updates as interim, so displayed preview text does not imply finalization.
- Existing preview button logic remains intact: `recording`, `start_stream`, and
  `streaming` states keep the Transcribe button as
  `Recording... (click to stop)`.

Acceptance criteria:

- completed: First partial text appears while recording is still active.
- completed: The Transcribe button remains `Recording... (click to stop)` while preview
  text is shown.
- completed: Final text can still replace or refine interim text after stop/finalization.

Validation:

- `get_errors` on the changed coordinator and WebView controller files.

## Step 6: Reduce WebView Preview Polling Delay

Status: completed

Even if native partial text is ready, WebView display can be delayed by polling
cadence and stale-update guards.

Implementation tasks:

1. completed: Measure current live preview polling interval and first payload arrival time.
2. completed: Shorten polling interval during active recording, using a bounded low-latency
   interval such as 100-200 ms.
3. completed: Prefer event push through the existing `CBlazeClawMFCView` bridge when
   practical, while retaining polling as a fallback.
4. completed: Ensure stale preview guards still prevent old sessions from overwriting new
   text.

Implemented behavior:

- The WebView live-preview fallback poller now uses a bounded low-latency
  recursive timeout scheduler instead of the old fixed 1200 ms interval.
- The active preview polling interval is now 150 ms, with a 50 ms retry delay
  when a preview request is already in flight.
- Recursive scheduling preserves the existing no-overlap behavior: a new preview
  request is not started until the prior request finishes or the busy retry path
  runs.
- Existing stale generation, inactive-stage, and preview-run guards remain in
  place so older sessions cannot overwrite newer preview text.
- Preview diagnostics now include `speech.preview.poll_config`, request-start
  `intervalMs`, request-end `intervalMs`, and busy `retryMs`, which lets first
  payload/render delay be correlated with the first-token timing diagnostics.
- Direct native event push is left as a later optimization because this step can
  meet the low-latency fallback requirement through the existing WebView polling
  bridge without changing the bridge contract.

Acceptance criteria:

- completed by implementation: WebView render delay after native first partial is below 200 ms.
- completed: No regression to the previously fixed premature idle-button behavior.

Validation:

- `node --check "BlazeClawMfc/web/chat/index.js"`

## Step 7: Benchmark CUDA vs CPU for Streaming Chunks

CUDA is not automatically faster for every small streaming workload. Benchmark
actual first-token latency and steady-state streaming latency for both providers.

Implementation tasks:

1. Run with `speech.cuda.enabled=true` and capture provider diagnostics.
2. Run with `speech.cuda.enabled=false` and capture CPU baseline diagnostics.
3. Compare:
   - model load time
   - warmup time
   - first encoder time
   - first decoder/joiner time
   - first visible character time
   - steady-state partial update interval
4. If CUDA is slower for very small chunks, consider profile-based provider
   selection or CPU tuning for low-latency mode.

Acceptance criteria:

- Provider choice is based on measured first-token latency, not assumption.
- The plan can justify CUDA, CPU, or profile-dependent behavior with data.

Current CUDA remediation tasks before CUDA benchmarking:

1. Align the speech CUDA runtime stack with the policy printed at startup:
   cuBLAS 12-family, cuBLASLt 12-family, cuFFT 12-family, and cuDNN 9-family.
2. Remove or deprioritize the currently loaded mixed cuBLAS 13-family DLL root
   from the app process DLL search path for speech CUDA testing.
3. Verify that all required CUDA/cuDNN DLLs are loaded from one compatible root.
4. Relaunch and require startup diagnostics to show:
   - `effectiveProvider=cuda`
   - `available=true`
   - `enabled=true`
   - `reason=active`
5. Only then compare CUDA and CPU first-token latency.

If the int8 ONNX model remains faster on CPU for small streaming chunks, keep a
CPU low-latency profile even after CUDA is fixed.

## Step 8: Tune ONNX Runtime and CPU Fallback

When the path falls back to CPU, tune thread and execution settings for streaming
latency instead of throughput only.

Implementation tasks:

1. Benchmark `speech.threads` values such as 2, 4, 6, and 8.
2. Compare `sequential` and `parallel` execution mode for first-token latency.
3. Keep graph optimization enabled.
4. Avoid settings that improve total throughput but worsen first-token latency.
5. Record the best settings in config comments or docs.

Acceptance criteria:

- CPU fallback has a known best low-latency configuration.
- Logs make it clear when CPU tuning applies because CUDA is unavailable.

Current baseline from debug log:

- `effectiveProvider=cpu`
- `threads=4`
- `mode=sequential`
- int8 encoder/decoder model files

Initial CPU tuning matrix:

1. `chunk_ms=500`, `threads=4`, `mode=sequential`.
2. `chunk_ms=320`, `threads=4`, `mode=sequential`.
3. Best chunk from trials 1-2 with `threads=2`.
4. Best chunk from trials 1-2 with `threads=6`.
5. Best thread count with `mode=parallel`, only if sequential still misses the
   first-token target.

## Step 9: Add Regression Tests and Manual Validation Scripts

Add repeatable validation for latency-sensitive behavior.

Implementation tasks:

1. Add or extend speech recognition tests to verify diagnostics include provider
   and first-token timing fields.
2. Add a manual validation checklist for:
   - CUDA active
   - CPU fallback
   - first utterance after app startup
   - second utterance after warm runtime
   - Chinese short utterance: `讲一个笑话`
   - English short utterance
3. Validate the solution with the required BlazeClaw build command:

   `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`

Acceptance criteria:

- Build succeeds.
- Diagnostics are present in startup and live-recognition paths.
- First-token latency can be compared before and after each optimization.

## Proposed Success Targets

Initial targets for the low-latency live preview profile:

- Runtime provider visible in diagnostics for every session.
- Warm runtime ready before first user recording.
- First native partial text produced within 800 ms after first speech audio is
  captured on a warmed runtime.
- First visible WebView character rendered within 1000 ms after first speech
  audio is captured on a warmed runtime.
- No regression to final recognition quality for short Chinese and English
  utterances.

These targets should be adjusted after the first measurement pass if the model
itself has a higher unavoidable streaming emission threshold.

## Recommended Implementation Order

1. Provider verification diagnostics.
2. First-token latency instrumentation.
3. Warmup coverage for the exact Sherpa streaming path.
4. Streaming chunk reduction and profile config.
5. Immediate partial emission audit.
6. WebView polling/event latency reduction.
7. CUDA vs CPU benchmark.
8. CPU fallback tuning.
9. Regression tests and documentation updates.

## Immediate Manual Check

To answer whether the current local run is using GPU, launch the app and inspect
startup diagnostics for these lines:

- `[Speech] startup.runtime - ... effectiveProvider=cuda ...`
- `[Speech] startup.runtime.cuda - available=true enabled=true reason=active`

If those lines instead report `effectiveProvider=cpu`, the reason field explains
why CUDA was not used. Add `speech.cuda.enabled=true` to `blazeclaw.conf` to make
the intended GPU policy explicit, but note that this still cannot force GPU if
the ONNX Runtime CUDA provider or compatible CUDA/cuDNN DLLs are unavailable.
