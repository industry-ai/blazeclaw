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
- Completed: Phase 2 (streaming input contract based on AudioRingBuffer)
  - Added explicit runtime streaming contract types:
	- `SpeechStreamingSource`
	- `SpeechStreamingCursor`
	- `SpeechStreamingChunkPolicy`
	- `SpeechStreamingInputContract`
  - Extended runtime request/state contracts to carry optional
	`streamingInput` alongside existing `audioArtifact`/`audioPath`.
  - Added cursor helper API on `AudioRingBuffer`:
	- `ReadWindowAndAdvance(...)`
  - Wired gateway/coordinator mapping from `audioArtifact.handoffMode=pcm_stream`
	to runtime `streamingInput` population (session/stream identity, cursor,
	chunk policy).
  - Preserved WAV-file path compatibility for non-sherpa/qwen flow.
- Completed: Phase 3 (sherpa realtime streaming engine wiring)
  - Added `SherpaZipformerStreamingEngine` and integrated it into
	`SpeechRecognitionRuntime` load/transcribe routing for
	`sherpa_zipformer_transducer` layout.
  - Added transducer artifact load contract:
	- `encoder*.onnx`
	- `decoder*.onnx`
	- `joiner*.onnx`
	- `tokens.txt`
  - Added `StreamingAudioSourceRegistry` and wired `CVoiceRecorder` to
	register/unregister ring-buffer stream readers.
  - Updated native recording stop path to return actual stream-sequence artifact
	metadata from recorder instead of synthetic placeholders.
  - Added streaming loop with chunked ring reads, cancellation checks, and
	segment transitions in sherpa engine path.
	- Implemented transducer decode path:
	- chunk log-mel feature extraction,
	- encoder/decoder/joiner ONNX inference,
	- greedy token selection and token->text assembly from `tokens.txt`,
	- per-stream decode cache with decoder context and pending sample carry-over.
  - Added partial/final segment contract in streaming result payload:
	- partial segment updates (`segment.final=false`),
	- final segment + transcript commit (`segment.final=true`) on end-of-speech/input-final.
  - Added stream cache cleanup on cancel/final lifecycle.

## Current gap and root causes

From startup diagnostics and runtime structure:

1. Current loader assumes Qwen artifact contract
   (`encoder/decoder_init/decoder_step`, tokenizer JSON) and fails with
   sherpa transducer artifacts (`encoder/decoder/joiner`, `tokens.txt`).
2. Current transcription flow is centered around a post-recording WAV artifact,
   not continuous ring-buffer-fed decode.
3. Speech lifecycle currently models queued/transcribing/completed at request
   granularity, not realtime partial-result streaming with speech segments.

Latest runtime telemetry root cause:

4. A stopped `pcm_stream` final transcription was still classified as a live
   stream request. The Sherpa engine therefore kept the live polling loop budget
   (`maxSpinCount=64`) and stopped after one 10240-sample budget even though the
   request artifact exposed a bounded final `sequenceEnd` around 101569 samples.
   This produced `ok=true`/`completed` with only partial cursor advancement and
   no committed transcript.
5. Dynamic ONNX input dimensions were normalized to `1` during binding capture.
   This could make later frame-limit logic treat dynamic time dimensions as a
   one-frame fixed shape and suppress effective encoder input size.
6. Follow-up telemetry showed full cursor drain and active ONNX inference
   (`sherpaEncoderFrameCount > 0`, `sherpaJoinerCallCount > 0`), but all joiner
   argmax outputs were blank (`sherpaBlankTokenCount == sherpaJoinerCallCount`).
   Local `tokens.txt` confirms `<blk>=0`, `<sos/eos>=1`, and `<unk>=2`, so token
   ID parsing is not the cause. The next fix targets model-contract handling:
   encoder cache inputs such as `cached*` / `processed_lens` must remain state
   tensors rather than being classified as feature-length inputs.
7. The all-blank state persisted with `sherpaLastBestTokenId=0` and a non-blank
   second-best token. The hand-written frontend differed from Kaldi/Sherpa
   defaults: Hann window, no DC removal, no preemphasis, 400-point FFT, and
   `log10`. Sherpa-compatible fbank behavior requires Povey windowing, DC
   removal, 0.97 preemphasis, padded 512-point FFT, and natural log energies.
8. The latest blank-dominant state still needs exact model-contract visibility.
   Load-time diagnostics now emit encoder/decoder/joiner binding names, shapes,
   kinds, normalized cache-state names, output names, and selected main outputs.
   Runtime cache refresh also avoids shape-incompatible state overwrites and
   honors length-like encoder outputs to prevent padded encoder frames from
   being decoded as real frames.
9. After token emission started, the transcript remained low quality and
   repetitive (for example `请讲一个笑话` decoded as unrelated/repeated BPE
   pieces). This indicates the remaining blocker is recognition quality, not
   transport/UI. Root causes in the current implementation are:
	  - frontend parity was approximate until the recognition-quality Phase 2
	 update replaced the active Sherpa path with `knf::OnlineFbank` from the
	 local FunASR `kaldi-native-fbank` reference,
	  - ONNX streaming cache/state binding was heuristic until the
	 recognition-quality Phase 3 update added model-order tensor metadata and
	 deterministic state input/output cache mapping,
   - greedy search emits at most one non-blank token per encoder frame instead
	 of the standard RNN-T inner loop until the recognition-quality Phase 4
	 update added bounded multi-symbol-per-frame greedy search,
	  - token reconstruction concatenated `tokens.txt` pieces directly until the
	 recognition-quality Phase 5 update added BPE-aware token-piece decoding and
	 preserved raw pieces only as diagnostics.
   These issues explain a plausible-but-wrong sequence like
   `个笑笑笑笑傲江湖火花华海黄瓜明日 N`: the model is running, but the
   frontend/decode/tokenizer contract is not Sherpa-equivalent.
10. Recognition-quality Phase 1 is now implemented in
	`VOICE_STREAM_TO_ASR_SHERPA_RECOGNITION_QUALITY_PLAN.md`: the Sherpa path can
	persist reproducible baseline diagnostics for a finite `pcm_stream` request.
	Set `BLAZECLAW_SHERPA_BASELINE_DIR` to write `<runId>.sherpa-baseline.json`
	and optionally set `BLAZECLAW_SHERPA_BASELINE_EXPECTED_TEXT` to the known
	transcript. The JSON captures sample rate, chunk size, stream range, final
	flush state, encoder/fbank frame counts, token IDs, token pieces, and decoded
	text so later frontend/cache/decode fixes can be compared against the same
	fixed clip.
11. Recognition-quality Phase 2 is now implemented: `BlazeClawMfc.vcxproj`
	compiles the local FunASR `kaldi-native-fbank` sources and
	`SherpaZipformerStreamingEngine` uses `knf::OnlineFbank` instead of the
	hand-written FFT/mel frontend. The active frontend now uses 25 ms frames,
	10 ms shift, Povey window, 0.97 preemphasis, DC removal, padded FFT,
	`snip_edges=true`, log-power fbank, and the FunASR-observed `sample * 32768`
	scaling convention before `AcceptWaveform(...)`.
12. Recognition-quality Phase 3 is now implemented: the Sherpa engine records
	exact ONNX input/output metadata, classifies encoder feature/length/state
	tensors, builds a deterministic state-cache mapping by normalized input and
	output names, and refreshes cache tensors only from compatible mapped outputs.
	Length-like encoder outputs are treated as authoritative valid-frame metadata
	so padded encoder frames are not decoded when the model exposes a length
	output.
13. Recognition-quality Phase 4 is now implemented: joiner decoding uses a
	standard bounded RNN-T greedy inner loop. A non-blank token updates decoder
	context and retries the same encoder frame; blank or `<sos/eos>` advances to
	the next frame. Guardrails cap each frame at 8 emitted symbols and each
	utterance at 512 emitted tokens, with telemetry for inner-loop count,
	max-symbol hits, repeated tokens, and multi-symbol frames.
14. Recognition-quality Phase 5 is now implemented: the Sherpa engine discovers
	`bpe.model` and `bpe.vocab`, decodes emitted token IDs through BPE-aware
	token-piece normalization for user-visible text, and keeps raw `tokens.txt`
	pieces only in diagnostics. Debug snapshots now expose BPE artifact presence,
	decoded text, and raw token pieces for baseline comparison.
15. Recognition-quality Phase 6 is now implemented: finite final `pcm_stream`
	requests are diagnosed as bounded input rather than live polling, final drain
	state is exposed through `sherpaFinalDrainComplete`,
	`sherpaFinalRemainingSamples`, and final cursor telemetry, and the final fbank
	flush path is surfaced through `sherpaFinalFbankFlush`. Debug snapshots now
	classify final outcomes as `no_speech_detected`, `no_tokens_emitted`,
	`tokens_emitted_empty_decoded_text`, `final_transcript`,
	`live_stream_not_final`, or `finite_stream_not_drained`.
16. Recognition-quality Phase 7 is now implemented: `tools/compare_sherpa_baseline.py`
	compares BlazeClaw `*.sherpa-baseline.json` diagnostics with optional
	known-good Sherpa/FunASR reference JSON. It reports structured deltas for
	fbank frames, encoder frames, token IDs, token pieces, decoded text, and final
	lifecycle fields. If no reference output is available locally, it records
	`status: reference_missing` while preserving BlazeClaw baseline metrics, so
	debug telemetry should remain enabled until representative clips are compared
	against a real reference decoder.
17. Post-Phase 7 regression telemetry for `请讲一个笑话` proved the stream fully
	drained and flushed but produced only blank joiner best tokens:
	`sherpaDecodedTokenCount=0`, `sherpaBlankTokenCount=216`,
	`sherpaJoinerCallCount=216`, `sherpaSpeechActive=true`, and
	`sherpaFinalOutcome=no_tokens_emitted`. Load-time binding diagnostics exposed
	a real state-cache bug: encoder state inputs were classified as state tensors
	but kept the default `bufferIndex=0`, making every state-cache mapping use
	`cacheIndex=0` and allowing independent Zipformer caches to overwrite one
	another. `BuildTensorBindings(...)` now assigns unique state cache buffer
	indexes by tensor type before deterministic state input/output mapping is
	built.
18. The unique-cache-index fix is necessary but not sufficient: later runtime
	traces confirm distinct cache indexes while the all-blank no-output signature
	still persists. Step 1 of the no-output root-cause plan is now implemented by
	`tools/freeze_sherpa_no_output_baseline.py`, which validates final drain,
	final fbank flush, speech-active state, encoder/joiner activity, zero decoded
	tokens, and blank-token count matching joiner-call count, then optionally
	archives the baseline JSON plus exported Visual Studio debug trace. This
	preserves the Sherpa-only path and avoids any final transcript fallback/rescue
	behavior.
19. Step 2 of the no-output root-cause plan is now implemented: bounded contract
	diagnostics record feature input shape/length, real versus padded frame counts,
	first/last feature-frame stats, encoder output shape and valid frames, decoder
	context/output shape, joiner input/output shapes, and top-5 joiner token
	scores. These fields are exposed through `gateway.speech.debug.snapshot` and
	sampled `[SherpaContract]` TRACE output to locate the all-blank failure stage
	without changing recognition behavior.
20. Step 3 of the no-output root-cause plan is now implemented: each active
	Sherpa stream owns a persistent `SherpaOnlineFbankFrontend` around
	`knf::OnlineFbank`. The engine feeds only newly read audio chunks into the
	frontend, extracts only newly ready frames, performs final
	`InputFinished()` once for finite input, and consumes pending feature frames
	directly instead of translating processed frames back into retained sample
	counts.
21. Step 4 of the no-output root-cause plan is now implemented: encoder feature
	metadata drives chunk assembly. Fixed encoder time dimensions are treated as
	required chunk sizes for normal streaming, pending full chunks are drained in
	order, and final finite input pads only the remaining partial chunk while the
	feature-length tensor reports the real frame count.
22. Step 5 of the no-output root-cause plan is now implemented as a
	diagnostic-only frontend amplitude switch. `BLAZECLAW_SHERPA_FBANK_SAMPLE_SCALING`
	selects between the default/reference `sample * 32768` path and normalized
	float input into `OnlineFbank`. The chosen mode is included in debug snapshots,
	Sherpa contract traces, and persisted baseline JSON alongside feature stats and
	top joiner tokens so A/B runs can compare fbank frame count, feature
	mean/range, token scores, decoded token count, and decoded text without adding
	fallback transcript behavior.
23. Step 6 of the no-output root-cause plan is now implemented: encoder
	state-cache initialization and refresh are driven by mapped model contracts.
	Cache map telemetry records input/output shapes, static element counts, dynamic
	flags, element type, and cache index for every state pair. Runtime cache inputs
	are initialized from resolved output-contract shapes or valid existing cache
	sizes, and output refreshes validate element counts before updating. Any
	unresolved or mismatched cache contract now fails with explicit diagnostics
	instead of being silently skipped.
24. Step 7 of the no-output root-cause plan is now implemented: decoder and
	joiner tensor contracts are validated against exported ONNX metadata during
	streaming inference. Decoder token inputs resolve from the decoder context
	size, decoder output vector slicing is recorded, joiner encoder/decoder input
	shapes preserve the declared rank, and joiner logits slicing records the
	vocabulary axis and top-token scores. Contract mismatches now produce explicit
	Sherpa inference diagnostics, and baseline/debug JSON carries the selected
	decoder/joiner shapes, slices, validated-call count, and last contract error.
25. Step 8 of the no-output root-cause plan is now implemented as an explicit
	non-blank restoration gate. `tools/compare_sherpa_baseline.py` supports
	`--require-step8-pass` with optional gateway `--debug-log` evidence and checks
	decoded-token count, blank-token count versus joiner calls,
	`final_transcript`, non-empty decoded text, `hasSegment=true`, and absence of
	fallback handoff. Sherpa baseline JSON now persists `finalOutcome`,
	`hasSegment`, and `fallbackUsed` after native segment creation, and the tests
	cover both a passing native transcript fixture and the rejected all-blank
	`no_tokens_emitted` signature.

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

Status: completed

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

Status: completed

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

Status: completed

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

Status: completed

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

Implemented notes:

- Native bridge speech lifecycle payload now carries `speechSession.segment` into
	`speech.lifecycle` events when available.
- `speech.transcribe` bridge pre-dispatch lifecycle now differentiates:
	- streaming artifact (`pcm_stream`): `queued` → `start_stream` → `streaming`
	- non-streaming path: `queued` → `transcribing`
- WebView controller normalization now treats realtime lifecycle transitions
	as segment-final states for:
	`segment_finalized`, `stopped`, `completed`, `failed`, `cancelled`.
- WebView speech status rendering now reflects realtime interim/final semantics
	based on lifecycle stage and segment metadata.

---

### Phase 6: Config and capabilities for realtime sherpa mode

Status: completed

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

Implemented notes:

- Added explicit speech streaming config flags in speech config model and loader:
	- `speech.streaming.enabled`
	- `speech.streaming.chunk_ms`
	- `speech.streaming.lookback_ms`
- Preserved compatibility aliases with existing keys:
	- `speech.chunk_ms`
	- `speech.overlap_ms`
	(bidirectional mapping + shared normalization).
- Runtime snapshot and gateway runtime-status now expose streaming config:
	- `streamingEnabled`
	- `streamingChunkMs`
	- `streamingLookbackMs`
- `speech.capabilities.get` now advertises sherpa-aware realtime capability:
	- `audioHandoffMode=pcm_stream` for sherpa layout
	- config-gated `streamingSupported`
	- `modelNativeVad=true` for sherpa layout
	- includes streaming tuning fields for transparency.

---

### Phase 7: Tests and validation

Status: completed

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

Implemented notes:

- Added new model-layout tests in
	`tests/SpeechRecognitionModelLayoutDetectionTests.cpp`:
	- Qwen layout detection (`qwen_decoder_init_step`)
	- Sherpa layout detection (`sherpa_zipformer_transducer`)
	- Incomplete sherpa artifact reporting.
- Added new realtime streaming tests in
	`tests/SpeechRecognitionRealtimeStreamingTests.cpp`:
	- deterministic streaming segment finalization
	- sequence catch-up behavior when cursor lags oldest ring sequence
	- cancellation behavior path.
- Added both new tests to `BlazeClawMfc.Tests/BlazeClawMfc.Tests.vcxproj`
	for compilation and validation coverage.

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
