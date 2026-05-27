# Voice Stream to ASR Step 14: Sherpa Streaming Root-Cause and Fix Plan

## Context and constraint
- Realtime speech recognition must use Sherpa streaming directly.
- No fallback to non-realtime whole-audio transcription is allowed.
- Goal: return real recognized text tokens from streaming audio, not VAD status placeholders.

## Root cause summary (confirmed)

### 1) Current Sherpa engine is VAD-only, not ASR decode
In `BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`, `TranscribeStreaming()` currently:
- reads chunks from `StreamingAudioSourceRegistry`,
- computes frame energy (`ComputeFrameEnergy`),
- marks `speechDetected` based on threshold,
- returns success/latency metadata.

It does **not** perform:
- feature extraction for model input,
- encoder/decoder/joiner ONNX inference,
- transducer token search/decoding,
- token-to-text assembly from `tokens.txt`.

Therefore, it can only detect speech presence and cannot produce transcript text.

### 2) Load path also confirms non-inference behavior
`SherpaZipformerStreamingEngine::Load()` currently only verifies artifact existence and token file non-empty count. It does not initialize ONNX Runtime sessions for encoder/decoder/joiner.

### 3) Runtime route is correct, engine capability is incomplete
`SpeechRecognitionRuntime::Transcribe()` correctly routes Sherpa streaming requests to `sherpaStreamingEngine->TranscribeStreaming(...)`. The failure is not routing; the engine itself is a placeholder implementation.

## FunASR knowledge references used

### A) Online ASR pipeline is chunked inference + decode, not VAD output
`FunASR/runtime/onnxruntime/src/funasrruntime.cpp` shows realtime path:
- `audio->Split(...)` produces online chunks,
- `asr_online_handle->Forward(frame->data, frame->len, frame->is_final)` runs model inference per chunk,
- partial/final text is accumulated and returned.

### B) Online model path keeps state/cache across chunks
`FunASR/runtime/onnxruntime/src/paraformer-online.cpp` demonstrates key realtime principles:
- persistent caches for frontend/model states,
- chunk-based feature extraction,
- online forward with incremental updates,
- explicit finalization behavior.

Even though the model family differs, the operational pattern required for realtime recognition is the same as Sherpa transducer streaming.

## Fix strategy (Sherpa-native, no fallback)

### Phase 1: Replace placeholder with real Sherpa transducer runtime core
1. Extend `SherpaZipformerStreamingEngine` state to include:
   - `Ort::Env`, `Ort::SessionOptions`,
   - encoder/decoder/joiner `Ort::Session` handles,
   - runtime stream state cache (per streamId) for model states and decode context.
2. In `Load()`, initialize ONNX sessions using Sherpa artifacts:
   - `encoder-*.onnx`, `decoder-*.onnx`, `joiner-*.onnx`, `tokens.txt`.
3. Build token table from `tokens.txt` for ID->token decode.

### Phase 2: Implement streaming feature + inference loop
1. Add streaming feature extractor path compatible with the Sherpa zipformer transducer export.
2. For each chunk:
   - read waveform samples by sequence,
   - extract features,
   - run encoder with cached state,
   - run decoder+joiner for token prediction (greedy first, beam later if needed),
   - update stream decode cache.
3. Emit partial text in `result.text` and final text in `sessionState.transcriptText` when segment finalizes.

### Phase 3: Define explicit segment/finalization contract
1. Finalization trigger rules (realtime semantics):
   - VAD end-of-speech and/or input-final flag.
2. Set `sessionState.segment`:
   - partial updates with `final=false`,
   - finalized segment with `final=true` and stable `sequence` progression.
3. Ensure no system/debug marker text can be emitted as transcript content.

### Phase 4: Runtime integration hardening
1. Keep `SpeechRecognitionRuntime` Sherpa branch pure realtime (no fallback).
2. Add guardrails:
   - if Sherpa sessions/caches are unavailable, return structured error.
3. Add telemetry:
   - chunk count, decoded token count, partial/final text length,
   - per-stage latency (feature/encoder/decode),
   - stream cache lifecycle events.

### Phase 5: Test and validation plan
1. Unit tests in `SpeechRecognitionRealtimeStreamingTests.cpp`:
   - replace VAD-placeholder expectations with real transcript assertions.
2. Add deterministic mock inputs for:
   - partial decoding,
   - final segment emission,
   - cancellation and cache reset.
3. End-to-end manual validation:
   - utterance `讲个笑话` should produce Chinese text transcript,
   - no `[sherpa-streaming] ...` status text,
   - no empty transcript on normal speech.

## File-level implementation plan
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h`
  - add ORT session members, token map, per-stream runtime state.
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
  - replace energy-only logic with full streaming transducer inference loop.
- `src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
  - keep Sherpa route, remove fallback behavior, add telemetry/contract alignment.
- `tests/SpeechRecognitionRealtimeStreamingTests.cpp`
  - update assertions from placeholder/VAD semantics to transcript semantics.

## Risks and mitigations
- Risk: Sherpa exported ONNX input/output signatures vary by model package.
  - Mitigation: runtime introspection of input/output names and shapes before binding.
- Risk: stream-state cache leaks across sessions.
  - Mitigation: explicit cache lifecycle keyed by streamId + final/cancel cleanup.
- Risk: latency regression after full decode is enabled.
  - Mitigation: chunk-size tuning and timing telemetry gates.

## Exit criteria
- Realtime Sherpa path returns non-empty recognized text for valid speech.
- No fallback transcription path is required for realtime results.
- No debug/status marker strings are emitted as user transcript.
- Build and speech streaming tests pass in Debug x64.

## Implementation status update

Status: Phase 1-5 implemented in current codebase revision.

- Phase 1 completed:
  - `SherpaZipformerStreamingEngine` now owns ONNX Runtime env/options and
	encoder/decoder/joiner sessions.
  - `Load()` initializes transducer sessions from `encoder-*.onnx`,
	`decoder-*.onnx`, `joiner-*.onnx` and parses `tokens.txt` into
	token-id table with `<blk>/<sos/eos>/<unk>` handling.
- Phase 2 completed:
  - Realtime streaming loop reads chunked sequence windows from
	`StreamingAudioSourceRegistry`.
  - Added in-engine log-mel frontend (`BuildLogMel`) and greedy transducer path:
	encoder -> decoder(context) -> joiner(argmax token).
  - Per-stream cache now persists decoder context, emitted tokens, pending samples,
	chunk counters and partial text.
- Phase 3 completed:
  - Segment contract implemented with partial/final semantics:
	- partial segment (`final=false`) while stream is active,
	- final segment (`final=true`) on VAD-style silence end or input-final.
  - `sessionState.segment` and `sessionState.transcriptText` are populated from
	decoded token text only.
  - Placeholder/system status text is not emitted as transcript payload.
- Phase 4 completed:
  - `SpeechRecognitionRuntime` Sherpa path remains realtime-only (no fallback).
  - Guardrails return structured runtime-unavailable errors when sessions are not
	available.
  - Stream cache lifecycle now includes cancellation/final cleanup via
	`ClearStreamState(streamId)`.
- Phase 5 completed:
  - `SpeechRecognitionRealtimeStreamingTests.cpp` updated away from VAD-placeholder
	assumptions and aligned with transcript-capable streaming semantics.
  - Build and targeted realtime test tag run succeeded:
	- `msbuild "BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
	- `BlazeClawMfc.Tests.exe "[speech][streaming][realtime]"`
