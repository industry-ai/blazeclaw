# Sherpa Zipformer realtime streaming STT implementation plan

## Goal

Implement realtime speech recognition for
`sherpa-onnx-streaming-zipformer-bilingual-zh-en` using in-memory streaming
audio from `AudioRingBuffer`, without WAV-file handoff, and wire model-native
VAD behavior into BlazeClaw speech lifecycle.

## Scope update

This plan replaces the previous WAV-file-oriented approach for sherpa path.

- Input boundary for sherpa: `AudioRingBuffer` streaming windows.
- Execution mode for sherpa: realtime incremental decode.
- VAD for sherpa: model-native/engine-native VAD path only.
- Qwen paths remain backward-compatible (existing file-based flow preserved).

## Progress

- Completed: Phase 1 (runtime model-layout detection and routing)
  - Added structural layout probe for:
	- `qwen_decoder_init_step`
	- `sherpa_zipformer_transducer`
  - Added runtime route diagnostics via `runtime.model.layout.*` traces.
  - Added explicit sherpa routing status path:
	- `model_layout_routed_unimplemented`
	- (replaces generic `model_variant_missing` for sherpa model folders)
  - Added startup/capabilities visibility for detected `modelLayout`.

## Current gap and root causes

From startup diagnostics and runtime structure:

1. Current loader assumes Qwen artifact contract
   (`encoder/decoder_init/decoder_step`, tokenizer JSON) and fails with
   sherpa transducer artifacts (`encoder/decoder/joiner`, `tokens.txt`).
2. Current transcription flow is centered around a post-recording WAV artifact,
   not continuous ring-buffer-fed decode.
3. Speech lifecycle currently models queued/transcribing/completed at request
   granularity, not realtime partial-result streaming with speech segments.

## FunASR references to follow

Use `FunASR/` as operational guidance for realtime behavior:

- `FunASR/docs/tutorial/README.md`
  - chunked streaming inference
  - cache carry-over
  - `is_final` flush contract
- `FunASR/runtime/websocket/bin/websocket-server*.cpp`
  - session state per stream
  - incremental partial + final emission
  - connection lifecycle and end-of-stream flush behavior

These references are for behavior and orchestration parity, while BlazeClaw
implementation remains inside current native architecture.

## Implementation strategy

### Phase 1: Runtime model-layout detection and engine routing

Status: completed

Target files:

- `src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
- (new) `src/core/runtime/SpeechRecognition/SpeechModelLayoutProbe.h/.cpp`

Plan:

1. Add structural detector for model layouts under `speech.storageRoot`.
2. Detect at minimum:
   - `qwen_decoder_init_step`
   - `sherpa_zipformer_transducer`
3. Route runtime execution path by detected layout, not by hard-coded model id
   or folder-name checks.
4. Emit explicit startup diagnostics for detected layout and required files.

Outcome:

- sherpa model no longer fails with generic `model_variant_missing`.
- runtime has deterministic layout resolution trace.

---

### Phase 2: Introduce streaming input contract based on AudioRingBuffer

Target files:

- `src/app/AudioRingBuffer.h`
- `src/app/AudioRingBuffer.cpp`
- `src/core/runtime/SpeechRecognition/ISpeechRecognitionRuntime.h`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`

Plan:

1. Add explicit streaming input contract for speech runtime requests:
   - ring-buffer-backed source handle/session id
   - read cursor sequence (`startSequence`/`nextSequence`)
   - chunk window policy (`chunkMs`, overlap/lookback policy)
2. Keep existing WAV-file contract for non-sherpa models as compatibility path.
3. Ensure ring-buffer reads use stable sequence windows and retry semantics
   already provided by `AudioRingBuffer::ReadWindowBySequence(...)`.

Outcome:

- sherpa path receives continuous in-memory audio.
- no file serialization needed for realtime decode.

---

### Phase 3: Implement sherpa realtime streaming engine with model-native VAD

Target files:

- (new) `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.*`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
- `BlazeClawMfc.vcxproj`

Plan:

1. Implement sherpa transducer artifact loader:
   - `encoder*.onnx`, `decoder*.onnx`, `joiner*.onnx`, `tokens.txt`
2. Implement realtime decode loop:
   - pull chunk windows from `AudioRingBuffer` by sequence
   - keep stream/session caches across chunks
   - emit partial hypotheses incrementally
   - flush final hypotheses on stop/end-of-stream
3. Wire model-native VAD gate and segmentation behavior inside sherpa path:
   - start/continue/end segment transitions
   - VAD-driven finalization for each detected speech segment
4. Preserve cancellation and shutdown safety for active stream sessions.

Outcome:

- realtime recognition works during recording (not after recording).
- sherpa-native VAD drives segment boundaries and final outputs.

---

### Phase 4: Coordinator and gateway streaming orchestration

Target files:

- `src/core/SpeechTranscriptionCoordinator.h`
- `src/core/SpeechTranscriptionCoordinator.cpp`
- `src/gateway/GatewayHost.h`
- `src/gateway/GatewayHost.cpp`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

Plan:

1. Add realtime speech session lifecycle in coordinator:
   - `start_stream`
   - `streaming`
   - `segment_finalized`
   - `stopped`
   - `completed`/`failed`/`cancelled`
2. Bind recording pipeline and sherpa decode pipeline to the same live session,
   using ring-buffer sequence cursors.
3. Ensure only one active realtime stream per session id unless explicitly
   designed otherwise.

Outcome:

- end-to-end realtime pipeline exists from capture to incremental ASR output.

---

### Phase 5: WebView lifecycle and partial-result UX wiring

Target files:

- `src/app/BlazeClawMFCView.h`
- `src/app/BlazeClawMFCView.cpp`
- `src/app/EventTransport.h`
- `src/app/EventTransport.cpp`
- `web/chat/chat-events.js`
- `web/chat/chat-controller.js`
- `web/chat/index.js`

Plan:

1. Extend speech lifecycle events for realtime streaming updates:
   - partial hypothesis
   - segment final
   - stream state transitions
2. Keep existing channels backward-compatible and add new fields rather than
   breaking event contract.
3. Update WebView state to render live transcript updates while recording.
4. Keep final transcript injection into chat flow consistent with current
   orchestration path.

Outcome:

- users see live recognition updates while speaking.
- existing chat submission behavior remains consistent on final segment output.

---

### Phase 6: Config and capabilities for realtime sherpa mode

Target files:

- `src/config/ConfigModels.h`
- `src/config/ConfigLoader.cpp`
- `src/config/blazeclaw.conf`
- `BlazeClawMfc/blazeclaw.conf`

Plan:

1. Add explicit flags for sherpa realtime mode and stream tuning:
   - `speech.streaming.enabled`
   - `speech.streaming.chunk_ms`
   - `speech.streaming.lookback_ms` (or equivalent window policy)
2. Advertise capabilities:
   - `audioHandoffMode=pcm_stream`
   - `streamingSupported=true`
   - `modelNativeVad=true` for sherpa layout
3. Keep Qwen defaults unchanged and compatible.

Outcome:

- runtime behavior is configurable and transparent.
- capabilities accurately represent realtime streaming support.

---

### Phase 7: Tests and validation

Target files:

- `tests/SpeechRecognitionOfflineOptimizationTests.cpp`
- (new) `tests/SpeechRecognitionModelLayoutDetectionTests.cpp`
- (new) `tests/SpeechRecognitionRealtimeStreamingTests.cpp`

Plan:

1. Add layout detection tests for Qwen and sherpa folders.
2. Add ring-buffer streaming sequence tests for chunk continuity and wrap-around.
3. Add realtime sherpa session tests:
   - partial emission
   - segment finalization
   - stop/final flush
   - cancellation behavior
4. Validate with project-required build command:

`msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`

Manual verification checklist:

1. Select sherpa model and start microphone capture.
2. Confirm startup/runtime reports sherpa layout + streaming enabled.
3. Speak mixed Chinese/English and observe live partial updates.
4. Verify VAD-driven segment finals appear without stopping recording.
5. Stop recording and verify final flush + clean shutdown.

## Risks and mitigations

1. **Realtime concurrency risk**: recorder-writer and decoder-reader races.
   - Mitigation: keep sequence-based reads only; avoid direct mutable sharing.

2. **Segment jitter risk**: VAD segmentation can over-split/under-split.
   - Mitigation: expose tuning knobs and record diagnostics for segment events.

3. **Regression risk for non-sherpa paths**: shared runtime changes may impact
   Qwen path.
   - Mitigation: model-layout routing with isolated sherpa engine path and
	 explicit regression tests.

## Definition of done

1. Sherpa model loads with transducer layout detection.
2. Audio input for sherpa comes from `AudioRingBuffer` streaming path only.
3. Realtime partial and final speech results are emitted while recording.
4. Sherpa model-native VAD controls segment finalization.
5. Existing Qwen speech flow remains functional.
6. Build and targeted tests pass.
